# Directly out of glib.  We don't do copy-by-value and other really
# pessimistic tests, since just about all systems will have one of these.
# Add them if needed.

AC_DEFUN(lftp_VA_COPY,
[
   AC_CACHE_CHECK([for an implementation of va_copy()],lftp_cv_va_copy,[
      AC_TRY_RUN([
      #include <stdarg.h>
      void f (int i, ...) {
	 va_list args1, args2;
	 va_start (args1, i);
	 va_copy (args2, args1);
	 if (va_arg (args2, int) != 42 || va_arg (args1, int) != 42)
	    exit (1);
	 va_end (args1); va_end (args2);
      }
      int main() {
	 f (0, 42);
	 return 0;
      }],
      [lftp_cv_va_copy=yes],
      [lftp_cv_va_copy=no],
      [])
   ])
   AC_CACHE_CHECK([for an implementation of __va_copy()],lftp_cv___va_copy,[
      AC_TRY_RUN([
      #include <stdarg.h>
      void f (int i, ...) {
	 va_list args1, args2;
	 va_start (args1, i);
	 __va_copy (args2, args1);
	 if (va_arg (args2, int) != 42 || va_arg (args1, int) != 42)
	    exit (1);
	 va_end (args1); va_end (args2);
      }
      int main() {
	 f (0, 42);
	 return 0;
      }],
      [lftp_cv___va_copy=yes],
      [lftp_cv___va_copy=no],
      [])
   ])

   if test "x$lftp_cv_va_copy" = "xyes"; then
	   va_copy_func=va_copy
   elif test "x$lftp_cv___va_copy" = "xyes"; then
	   va_copy_func=__va_copy
   fi

   if test -n "$va_copy_func"; then
       AC_DEFINE_UNQUOTED(VA_COPY,$va_copy_func,[A 'va_copy' style function])
   fi
])
