#
#  namechange3.sed - give the arguments of the generated clnt_call()s
#  the (caddr_t) casts that some rpcgens leave out (OpenBSD's): newer
#  compilers reject passing typed pointers as caddr_t.  Lines that
#  already cast (Linux, Solaris) do not match.
#
/clnt_call/ {
s/\([(,][ 	]*\)argp\([ 	]*[,)]\)/\1(caddr_t) argp\2/g
s/\([(,][ 	]*\)&clnt_res\([ 	]*[,)]\)/\1(caddr_t) \&clnt_res\2/g
}
