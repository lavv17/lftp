dnl check if c++ compiler can use dynamic initializers for static variables
AC_DEFUN(CXX_DYNAMIC_INITIALIZERS,
[
   AC_LANG_SAVE
   AC_LANG_CPLUSPLUS
   AC_MSG_CHECKING(if c++ compiler can handle dynamic initializers)
   AC_TRY_RUN(
   [
      int f() { return 1; }
      int a=f();
      int main()
      {
	 return(1-a);
      }
   ],
   [cxx_dynamic_init=yes],
   [cxx_dynamic_init=no],
   [cxx_dynamic_init=yes])
   AC_MSG_RESULT($cxx_dynamic_init)
   if test x$cxx_dynamic_init = xno; then
      AC_MSG_ERROR(C++ compiler cannot handle dynamic initializers of static objects)
   fi
   AC_LANG_RESTORE
])
