AC_DEFUN([LE_FUNC_WCWIDTH_REPLACE],[
	AC_ARG_ENABLE([wcwidth-replacement],
[  --enable-wcwidth-replacement		replace system wcwidth function
					(good for solaris)],
		[enable_wcwidth_replecement=$enableval],
		[
	   # try to find out
	   enable_wcwidth_replecement=no
	   AC_TRY_RUN([
		   #include <locale.h>
		   #include <wchar.h>
		   int main() {
			   setlocale(LC_ALL,"en_US.UTF-8");
			   if(wcwidth(0x2022)!=1)
				   return 1;
			   return 0;
		   }
	   ],[],[enable_wcwidth_replecement=yes])
	])
	if test x$enable_wcwidth_replecement = xyes -a x$ac_cv_header_wchar_h = xyes; then
		AC_LIBOBJ([wcwidth])
		AC_LIBOBJ([wcwidth1])
		AC_MSG_RESULT([Replacing wcwidth function...])
	fi
])
