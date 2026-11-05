#!/usr/bin/env bash
# Install and check the built package in a fresh Arch container.
set -Eeuo pipefail
stage="${1:?stage required}"
artifact="$GITHUB_WORKSPACE/artifact"
scratch="$RUNNER_TEMP/cde-arch-install"
expected="${CDE_EXPECTED_SHA:?}"
mkdir -p "$scratch"
# Check both the verification command and its logger.
case "$stage" in
  verify)
    exec bash "$GITHUB_WORKSPACE/.github/packaging/archlinux/ci-log.sh" \
      "$scratch/verification.txt" bash "$0" verify-raw
    ;;
  motif)
    exec bash "$GITHUB_WORKSPACE/.github/packaging/archlinux/ci-log.sh" \
      "$scratch/motif.txt" bash "$0" motif-raw
    ;;
esac
case "$stage" in
  inspect)
    (cd "$artifact"; sha256sum --check SHA256SUMS)
    test "$(cat "$artifact/expected-sha")" = "$expected"
    test "$(cat "$artifact/verified-source-sha")" = "$expected"
    test "$(cat "$artifact/source-repository")" = "$CDE_SOURCE_REPOSITORY"
    git config --global --add safe.directory "$GITHUB_WORKSPACE"
    tree="$(git -C "$GITHUB_WORKSPACE" rev-parse 'HEAD^{tree}')"
    test "$(git -C "$GITHUB_WORKSPACE" rev-parse HEAD)" = "$expected"
    test "$(cat "$artifact/expected-tree")" = "$tree"
    test "$(cat "$artifact/verified-source-tree")" = "$tree"
    [[ "$(cat "$artifact/aur-revision")" =~ ^[a-f0-9]{40}$ ]]
    test "$(git hash-object -t commit "$artifact/aur-commit")" = "$(cat "$artifact/aur-revision")"
    grep -Fxq "tree $(cat "$artifact/aur-tree")" "$artifact/aur-commit"
    mkdir "$scratch/aur-original"
    tar -xf "$artifact/aur-tree.tar" -C "$scratch/aur-original"
    (cd "$scratch/aur-original"; sha256sum --check "$artifact/aur-files.sha256")
    (cd "$scratch/aur-original"; sha256sum --check "$artifact/PKGBUILD.sha256")
    cmp "$artifact/original-PKGBUILD" "$scratch/aur-original/PKGBUILD"
    # Check the archived AUR tree, including file modes.
    git -C "$scratch/aur-original" init
    git -C "$scratch/aur-original" add --all
    test "$(git -C "$scratch/aur-original" write-tree)" = "$(cat "$artifact/aur-tree")"
    shopt -s nullglob
    archives=("$artifact"/*.pkg.tar.zst)
    (( ${#archives[@]} > 0 ))
    count=0
    for p in "${archives[@]}"; do
      pacman -Qip "$p"
      bsdtar -tf "$p" > "$scratch/${p##*/}.filelist"
      bsdtar -xOf "$p" .PKGINFO > "$scratch/${p##*/}.PKGINFO"
      bsdtar -xOf "$p" .BUILDINFO > "$scratch/${p##*/}.BUILDINFO"
      cmp "$scratch/${p##*/}.PKGINFO" "$p.PKGINFO"
      cmp "$scratch/${p##*/}.BUILDINFO" "$p.BUILDINFO"
      name="$(sed -n 's/^pkgname = //p' "$p.PKGINFO")"
      test "$(sed -n 's/^arch = //p' "$p.PKGINFO")" = x86_64
      case "$name" in
        cdesktopenv-git) printf '%s\n' "$p" > "$scratch/package-path"; ((count+=1));;
        cdesktopenv-git-debug) :;;
        *) echo "Unexpected package: $name" >&2; exit 1;;
      esac
    done
    test "$count" = 1
    (cd "$artifact"; sha256sum --check native-PKGBUILD.sha256)
    python "$GITHUB_WORKSPACE/.github/packaging/archlinux/ci-verify-recipe.py" \
      "$artifact/original-PKGBUILD" "$artifact/native-PKGBUILD"
    grep -Fxq "pkgbuild_sha256sum = $(awk '{print $1}' "$artifact/native-PKGBUILD.sha256")" \
      "$(cat "$scratch/package-path").BUILDINFO"
    : > "$scratch/install-packages"
    if test -d "$artifact/dependencies/mksh"; then
      dep="$artifact/dependencies/mksh"
      test "$(git hash-object -t commit "$dep/aur-commit")" = "$(cat "$dep/aur-revision")"
      grep -Fxq "tree $(cat "$dep/aur-tree")" "$dep/aur-commit"
      mkdir "$scratch/mksh-original"
      tar -xf "$dep/aur-tree.tar" -C "$scratch/mksh-original"
      (cd "$scratch/mksh-original"; sha256sum --check "$dep/aur-files.sha256")
      git -C "$scratch/mksh-original" init
      git -C "$scratch/mksh-original" add --all
      test "$(git -C "$scratch/mksh-original" write-tree)" = "$(cat "$dep/aur-tree")"
      dep_count=0
      for p in "$dep"/*.pkg.tar.zst; do
        name="$(bsdtar -xOf "$p" .PKGINFO | sed -n 's/^pkgname = //p')"
        if test "$name" = mksh; then
          printf '%s\n' "$p" >> "$scratch/install-packages"
          ((dep_count+=1))
        else
          test "$name" = mksh-debug
        fi
      done
      test "$dep_count" = 1
    fi
    cat "$scratch/package-path" >> "$scratch/install-packages"
    test -s "$artifact/provenance.txt"
    ;;
  install)
    mapfile -t packages < "$scratch/install-packages"
    set +e
    pacman -U --noconfirm -- "${packages[@]}" 2>&1 | tee "$RUNNER_TEMP/package-install.log"
    statuses=("${PIPESTATUS[@]}")
    set -e
    printf '%s\n' "${statuses[0]}" > "$scratch/install-exit-status"
    test "${statuses[1]}" = 0
    exit "${statuses[0]}"
    ;;
  verify-raw)
    test "$(cat "$scratch/install-exit-status")" = 0
    pacman -Dk
    pacman -Qkk cdesktopenv-git
    pacman -Q cdesktopenv-git
    pacman -Ql cdesktopenv-git > "$scratch/installed-file-list.txt"
    pacman -Qlq cdesktopenv-git > "$scratch/installed-paths.txt"
    pacman -Qip "$(cat "$scratch/package-path")" > "$scratch/package-info.txt"
    failed=0
    elf_count=0
    while IFS= read -r path; do
      test -n "$path" || continue
      if test -L "$path" && ! test -e "$path"; then
        printf 'Broken package symlink: %s -> %s\n' "$path" "$(readlink "$path")"
        failed=1
      fi
      test -f "$path" || continue
      if file -b "$path" | grep '^ELF ' >/dev/null; then
        ((elf_count+=1))
        echo "=== $path ==="
        dynamic="$(readelf -d "$path")"
        printf '%s\n' "$dynamic"
        if grep -E '(RPATH|RUNPATH)' <<<"$dynamic" |
          grep -E '(/tmp/|/__w/|/home/|/src/|cde-arch-ci|github/workspace)'; then
          echo "Build directory RPATH: $path"
          failed=1
        fi
        # Static ELF files have no shared-library dependencies.
        if grep -q '(NEEDED)' <<<"$dynamic"; then
          if ! linked="$(ldd "$path" 2>&1)"; then failed=1; fi
          printf '%s\n' "$linked"
          if grep -q 'not found' <<<"$linked"; then failed=1; fi
        fi
      fi
    done < "$scratch/installed-paths.txt"
    test "$elf_count" -gt 0
    printf 'Installed ELF files checked: %d\n' "$elf_count"
    exit "$failed"
    ;;
  motif-raw)
    python "$GITHUB_WORKSPACE/.github/packaging/archlinux/ci-motif-provenance.py" \
      --phase install --output-dir "$scratch" \
      --aur-revision "$(cat "$artifact/aur-revision")" \
      --build-report "$artifact/motif-build.json"
    ;;
  namcap)
    set +e
    namcap "$(cat "$scratch/package-path")" > "$scratch/namcap.txt" 2>&1
    status=$?
    set -e
    printf '%s\n' "$status" > "$scratch/namcap-exit-status"
    awk '
      /^[^[:space:]]+ E: / { errors++ }
      /^[^[:space:]]+ W: / { warnings++ }
      END { printf "Namcap: %d errors, %d warnings (advisory)\n", errors, warnings }
    ' "$scratch/namcap.txt" > "$scratch/namcap-summary.txt"
    cat "$scratch/namcap-summary.txt"
    printf 'Namcap exit status: %s\n' "$status"
    printf 'First 12 lines; full output: namcap.txt\n'
    head -n 12 "$scratch/namcap.txt"
    {
      printf '### Namcap (advisory)\n\n'
      cat "$scratch/namcap-summary.txt"
      printf '\nExit status: %s (advisory).\n' "$status"
    } >> "$GITHUB_STEP_SUMMARY"
    exit "$status"
    ;;
  diagnostics)
    bundle="$RUNNER_TEMP/ci-diagnostics/install-test"
    mkdir -p "$bundle"
    for f in "$RUNNER_TEMP/package-install.log" "$scratch"/{*.txt,*.json,*.md,*.PKGINFO,*.BUILDINFO,*.filelist,install-exit-status,namcap-exit-status,package-path}; do
      if test -f "$f"; then cp "$f" "$bundle/${f##*/}"; fi
    done
    if pacman -Q cdesktopenv-git >/dev/null 2>&1; then
      pacman -Qlq cdesktopenv-git > "$bundle/configuration-paths.txt"
      {
        while IFS= read -r path; do
          case "$path" in
            */pam.d/*|*/systemd/system/*|*/share/xsessions/*)
              if test -f "$path"; then
                printf '\n=== %s ===\n' "$path"
                pacman -Qo "$path"
                cat "$path"
              fi
              ;;
          esac
        done < "$bundle/configuration-paths.txt"
      } > "$bundle/installed-configuration.txt"
    fi
    {
      printf 'Image: archlinux:base\n'
      cat /etc/os-release
      uname -sm
      pacman -Q | sort
      printf 'Source SHA: %s\nSource repository: %s\n' "$expected" "$CDE_SOURCE_REPOSITORY"
      if test -f "$artifact/aur-revision"; then
        printf 'AUR revision: '; cat "$artifact/aur-revision"
        cat "$artifact/PKGBUILD.sha256"
      fi
    } > "$bundle/environment.txt"
    ;;
  *) echo "Unknown stage: $stage" >&2; exit 2;;
esac
