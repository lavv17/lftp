AC_DEFUN([LFTP_PTY_CHECK],[
case "$host" in
*-*-linux*)	no_dev_ptmx=1;;
*-*-sco3.2v4*)	no_dev_ptmx=1;;
*-*-sco3.2v5*)	no_dev_ptmx=1;;
esac
if test -z "$no_dev_ptmx" ; then
   AC_CHECK_FILE("/dev/ptmx", [
      AC_DEFINE(HAVE_DEV_PTMX,1,[define if you have /dev/ptmx device])])
fi
AC_CHECK_FILE("/dev/ptc", [
   AC_DEFINE(HAVE_DEV_PTS_AND_PTC,1,[define if you have /dev/ptc device])])
AC_CHECK_HEADERS([util.h sys/stropts.h pty.h])
AC_CHECK_FUNCS([openpty _getpty])
])
