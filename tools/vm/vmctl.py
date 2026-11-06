#!/usr/bin/env python3
"""Drive the Clanker/CDE test VM built by build-vm.sh (no root needed).

    vmctl.py boot [--ram MB] [--cpus N]  start QEMU in the background (KVM)

    VM_OUT=dir selects another VM built with VM_OUT=dir build-vm.sh
    vmctl.py provision             install CDE + deps inside the VM
    vmctl.py run 'cmd' [-t SECS]   run a root shell command on the serial console
    vmctl.py screenshot FILE.png   capture the VM screen
    vmctl.py type 'text'           type text on the VM keyboard
    vmctl.py key KEY [KEY...]      press keys (QEMU names: ret, tab, alt-f4 ...)
    vmctl.py stop                  power off
"""
import argparse
import os
import random
import re
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get("VM_OUT") or os.path.join(HERE, "out")
SERIAL = os.path.join(OUT, "serial.sock")
MONITOR = os.path.join(OUT, "monitor.sock")
PIDFILE = os.path.join(OUT, "qemu.pid")


def connect(path, timeout=30):
    deadline = time.time() + timeout
    while True:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(path)
            return s
        except OSError:
            if time.time() > deadline:
                raise
            time.sleep(0.5)


def read_until(s, pattern, timeout):
    """Read from s until regex pattern matches; return the text read."""
    buf = ""
    deadline = time.time() + timeout
    rx = re.compile(pattern)
    while time.time() < deadline:
        s.settimeout(max(0.1, min(1.0, deadline - time.time())))
        try:
            data = s.recv(65536)
        except socket.timeout:
            continue
        if not data:
            break
        buf += data.decode("utf-8", "replace")
        if rx.search(buf):
            return buf
    raise TimeoutError("timed out waiting for %r; got tail: %r" % (pattern, buf[-500:]))


def monitor(cmd):
    s = connect(MONITOR)
    read_until(s, r"\(qemu\) ", 10)
    s.sendall((cmd + "\n").encode())
    out = read_until(s, r"\(qemu\) ", 30)
    s.close()
    return out


def boot(ram, cpus=1):
    if os.path.exists(PIDFILE):
        try:
            os.kill(int(open(PIDFILE).read()), 0)
            print("already running")
            return
        except (OSError, ValueError):
            pass
    for p in (SERIAL, MONITOR):
        if os.path.exists(p):
            os.unlink(p)
    cmd = [
        "qemu-system-x86_64", "-enable-kvm", "-cpu", "host",
        "-machine", "q35", "-smp", str(cpus), "-m", str(ram),
        "-drive", "file=%s/disk.qcow2,if=virtio,format=qcow2" % OUT,
        "-drive", "file=%s/payload.tar,if=virtio,format=raw,readonly=on" % OUT,
        "-nic", "user,model=virtio-net-pci",
        "-vga", "std", "-display", "none",
        "-serial", "unix:%s,server=on,wait=off" % SERIAL,
        "-monitor", "unix:%s,server=on,wait=off" % MONITOR,
        "-pidfile", PIDFILE, "-daemonize",
    ]
    subprocess.run(cmd, check=True)
    print("booted with %d MB RAM, %d CPU(s)" % (ram, cpus))


def credential(key):
    with open(os.path.join(OUT, "credentials")) as f:
        for line in f:
            if line.startswith(key + "="):
                return line.split("=", 1)[1].strip()
    raise KeyError(key)


def shell(s, timeout=240):
    """Get a root prompt on the serial console (log in if needed)."""
    def send(data):
        s.settimeout(5.0)
        try:
            s.sendall(data)
        except socket.timeout:
            pass

    send(b"\n")
    deadline = time.time() + timeout
    buf = ""
    last_data = time.time()
    while time.time() < deadline:
        s.settimeout(1.0)
        try:
            data = s.recv(65536)
        except socket.timeout:
            data = b""
        if data:
            last_data = time.time()
            buf = (buf + data.decode("utf-8", "replace"))[-4000:]
        tail = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", buf).rstrip(" ")
        if re.search(r"login: ?$", tail):
            send(b"root\n"); buf = ""
        elif re.search(r"(root password|password again|Password)[^\n]*: ?$", tail):
            # login prompt, or systemd-firstboot setting the root password
            send((credential("rootpw") + "\n").encode()); buf = ""
        elif re.search(r"(root@[^\s#]*:[^\n#]*# ?|(^|[\r\n])# ?)$", tail):
            send(b"stty -echo; dmesg -n 1; export PS1='# ' TERM=dumb\n")
            read_until(s, r"(^|[\r\n])\s*# ?$", 15)
            return
        elif re.search(r"(Press any key[^\n]*|\(empty to skip[^\n]*: ?)$", tail):
            # systemd-firstboot: proceed / skip locale, keymap, timezone...
            send(b"\n"); buf = ""
        elif time.time() - last_data > 8:
            send(b"\n"); last_data = time.time()
    raise TimeoutError("no root prompt on serial console")


def run(command, timeout):
    s = connect(SERIAL)
    shell(s)
    tag = "%08x" % random.getrandbits(32)
    # The marker is split in the typed line so the echo never matches.
    line = '%s; echo "__E""ND_%s__ rc=$?"\n' % (command, tag)
    s.sendall(line.encode())
    out = read_until(s, r"__END_%s__ rc=(\d+)" % tag, timeout)
    s.close()
    m = re.search(r"__END_%s__ rc=(\d+)" % tag, out)
    body = out[: m.start()]
    body = body.replace(line.strip(), "", 1).strip("\r\n")
    print(body.replace("\r", ""))
    return int(m.group(1))


KEYMAP = {" ": "spc", "\n": "ret", "-": "minus", "=": "equal", ".": "dot",
          "/": "slash", ",": "comma", ";": "semicolon", "'": "apostrophe",
          "_": "shift-minus", ":": "shift-semicolon", "!": "shift-1",
          "@": "shift-2", "#": "shift-3", "$": "shift-4", "%": "shift-5",
          "&": "shift-7", "*": "shift-8", "(": "shift-9", ")": "shift-0",
          "+": "shift-equal", "?": "shift-slash", "~": "shift-grave_accent"}


def type_text(text):
    for ch in text:
        if ch.isalnum():
            key = ("shift-" + ch.lower()) if ch.isupper() else ch
        else:
            key = KEYMAP[ch]
        monitor("sendkey " + key)
        time.sleep(0.04)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("boot")
    b.add_argument("--ram", type=int, default=384)
    b.add_argument("--cpus", type=int, default=1)
    sub.add_parser("provision")
    r = sub.add_parser("run")
    r.add_argument("command")
    r.add_argument("-t", "--timeout", type=int, default=120)
    sc = sub.add_parser("screenshot")
    sc.add_argument("file")
    t = sub.add_parser("type")
    t.add_argument("text")
    k = sub.add_parser("key")
    k.add_argument("keys", nargs="+")
    sub.add_parser("stop")
    a = ap.parse_args()

    if a.cmd == "boot":
        boot(a.ram, a.cpus)
    elif a.cmd == "provision":
        sys.exit(run("mkdir -p /root/payload && tar -xf /dev/vdb -C /root/payload"
                     " && sh /root/payload/provision.sh 2>&1 | tail -5", 1800))
    elif a.cmd == "run":
        sys.exit(run(a.command, a.timeout))
    elif a.cmd == "screenshot":
        monitor("screendump %s -f png" % os.path.abspath(a.file))
        print(a.file)
    elif a.cmd == "type":
        type_text(a.text)
    elif a.cmd == "key":
        for key in a.keys:
            monitor("sendkey " + key)
            time.sleep(0.1)
    elif a.cmd == "stop":
        # "quit" is a hard power-off: flush the guest's disk cache first,
        # or files written just before (installs!) come back truncated.
        try:
            run("sync", 60)
        except Exception:
            pass
        try:
            monitor("quit")
        except Exception:
            pass
        try:
            os.unlink(PIDFILE)      # QEMU usually removes it itself
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    main()
