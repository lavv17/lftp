# Directly out of glib.  We don't do copy-by-value and other really
# pessimistic tests, since just about all systems will have one of these.
# Add them if needed.

AC_DEFUN([lftp_VA_COPY],
[
   AC_CACHE_CHECK([for an implementation of va_copy()],lftp_cv_va_copy,[
      AC_RUN_IFELSE([AC_LANG_SOURCE([[
      #include <stdarg.h>
      #include <stdlib.h>
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
      }]])],[lftp_cv_va_copy=yes],[lftp_cv_va_copy=no],[lftp_cv_va_copy=yes])
   ])
   if test x$lftp_cv_va_copy != xyes; then
      AC_CACHE_CHECK([for an implementation of __va_copy()],lftp_cv___va_copy,[
	 AC_RUN_IFELSE([AC_LANG_SOURCE([[
	 #include <stdarg.h>
         #include <stdlib.h>
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
	 }]])],[lftp_cv___va_copy=yes],[lftp_cv___va_copy=no],[lftp_cv___va_copy=no])
      ])
   fi

   if test "x$lftp_cv_va_copy" = "xyes"; then
	   va_copy_func=va_copy
   elif test "x$lftp_cv___va_copy" = "xyes"; then
	   va_copy_func=__va_copy
   fi

   if test -n "$va_copy_func"; then
       AC_DEFINE_UNQUOTED(VA_COPY,$va_copy_func,[A 'va_copy' style function])

   else

      AC_CACHE_CHECK([whether va_lists can be copied by value],lftp_cv_va_val_copy,[
	   AC_RUN_IFELSE([AC_LANG_SOURCE([[
	   #include <stdarg.h>
	   #include <string.h>
           #include <stdlib.h>
	   void f (int i, ...) {
	   va_list args1, args2;
	   va_start (args1, i);

	   memmove(&args2, &args1, sizeof(args1));
	   if (va_arg (args2, int) != 42 || va_arg (args1, int) != 42)
	     exit (1);
	   va_end (args1); va_end (args2);
	   }
	   int main() {
	     f (0, 42);
	     return 0;
	   }]])],[lftp_cv_va_val_copy=yes],[lftp_cv_va_val_copy=no],[lftp_cv_va_val_copy=no])
      ])

      if test x$lftp_cv_va_val_copy = xyes; then
	 AC_DEFINE(VA_VAL_COPY,1,[Define to 1 if va_lists can be copied by value])
      else
	 AC_CACHE_CHECK([whether va_lists can be copied by pointer],lftp_cv_va_ptr_copy,[
	      AC_RUN_IFELSE([AC_LANG_SOURCE([[
	      #include <stdarg.h>
              #include <stdlib.h>
	      void f (int i, ...) {
	      va_list args1, args2;
	      va_start (args1, i);

	      *args2 = *args1;
	      if (va_arg (args2, int) != 42 || va_arg (args1, int) != 42)
		exit (1);
	      va_end (args1); va_end (args2);
	      }
	      int main() {
		f (0, 42);
		return 0;
	      }]])],[lftp_cv_va_ptr_copy=yes],[lftp_cv_va_ptr_copy=no],[lftp_cv_va_ptr_copy=no])
	 ])

	 if test x$lftp_cv_va_ptr_copy = xyes; then
	    AC_DEFINE(VA_PTR_COPY,1,[Define to 1 if va_lists can be copied by pointer])
	 fi
      fi
   fi

   if test x$lftp_cv_va_val_copy != xyes -a x$lftp_cv_va_ptr_copy != xyes -a \
           x$lftp_cv_va_copy != xyes -a x$lftp_cv___va_copy != xyes; then
	   AC_MSG_ERROR(Can't find a way to va_copy.)
   fi

   dnl ZZZ for autoheader sorting.
   AH_VERBATIM([_VA_ZZZ_COPY],
[#if !defined (VA_COPY)
#  if defined (VA_PTR_COPY)
#    define VA_COPY(ap1, ap2)   (*(ap1) = *(ap2))
#  elif defined (VA_VAL_COPY)
#    include <string.h>
#    define VA_COPY(to,from) (memcpy(&(to),&(from),sizeof((to))))
#  endif
#endif /* !VA_COPY */])
])
