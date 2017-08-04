AC_DEFUN([LFTP_OPENSSL_CHECK],
[AC_MSG_CHECKING([for openssl library])
AC_CACHE_VAL(lftp_cv_openssl,
[
	found_loc=none;
	lftp_cv_openssl=none;
	for loc in $openssl_loc default /usr/local/ssl; do
		old_LIBS="$LIBS"
		old_LDFLAGS="$LDFLAGS"
		old_CPPFLAGS="$CPPFLAGS"
		LIBS="$LIBS -lssl -lcrypto"
		if test $loc != default; then
			LDFLAGS="$LDFLAGS -L$loc/lib"
			CPPFLAGS="$CPPFLAGS -I$loc/include"
		fi
		AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <openssl/ssl.h>
			 #include <openssl/rand.h>]], [[static SSL_CTX *ctx; SSL_new(ctx); RAND_status()]])],[found_loc="$loc"],[])
		LIBS="$old_LIBS"
		LDFLAGS="$old_LDFLAGS"
		CPPFLAGS="$old_CPPFLAGS"
		if test $found_loc != "none"; then
			lftp_cv_openssl="OPENSSL_LIBS=\"-lssl -lcrypto\""
			if test $found_loc != default; then
				r=""; test "$enable_rpath" = yes -a "$found_loc" != /usr && r=" -R$found_loc/lib"
				lftp_cv_openssl="$lftp_cv_openssl OPENSSL_LDFLAGS=\"-L$found_loc/lib$r\""
				lftp_cv_openssl="$lftp_cv_openssl OPENSSL_CPPFLAGS=-I$found_loc/include"
			fi
			break;
		fi
	done
])
if test "$lftp_cv_openssl" != none; then
	eval $lftp_cv_openssl
	AC_SUBST(OPENSSL_LIBS)
	AC_SUBST(OPENSSL_LDFLAGS)
	AC_SUBST(OPENSSL_CPPFLAGS)
	AC_DEFINE(USE_OPENSSL, 1, [define if you are using openssl])
	AC_MSG_RESULT($lftp_cv_openssl)
else
	AC_MSG_RESULT(none found)
fi
])
