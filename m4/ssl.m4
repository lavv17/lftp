AC_DEFUN([LFTP_SSL_CHECK],
[AC_MSG_CHECKING([for ssl library])
AC_CACHE_VAL(lftp_cv_ssl,
[
	found_loc=none;
	lftp_cv_ssl=none;
	for loc in $ssl_loc default /usr/local/ssl; do
		old_LIBS="$LIBS"
		old_LDFLAGS="$LDFLAGS"
		old_CPPFLAGS="$CPPFLAGS"
		LIBS="$LIBS -lssl -lcrypto"
		if test $loc != default; then
			LDFLAGS="$LDFLAGS -L$loc/lib"
			CPPFLAGS="$CPPFLAGS -I$loc/include"
		fi
		AC_TRY_LINK(
			[#include <openssl/ssl.h>
			 #include <openssl/rand.h>],
			[static SSL_CTX *ctx; SSL_new(ctx); RAND_status(); RAND_egd("file")],
			[found_loc="$loc"])
		LIBS="$old_LIBS"
		LDFLAGS="$old_LDFLAGS"
		CPPFLAGS="$old_CPPFLAGS"
		if test $found_loc != "none"; then
			lftp_cv_ssl="SSL_LIBS=\"-lssl -lcrypto\""
			if test $found_loc != default; then
				lftp_cv_ssl="$lftp_cv_ssl SSL_LDFLAGS=\"-L$found_loc/lib -R$found_loc/lib\""
				lftp_cv_ssl="$lftp_cv_ssl SSL_CPPFLAGS=-I$found_loc/include"
			fi
			break;
		fi
	done
])
if test "$lftp_cv_ssl" != none; then
	eval $lftp_cv_ssl
	AC_SUBST(SSL_LIBS)
	AC_SUBST(SSL_LDFLAGS)
	AC_SUBST(SSL_CPPFLAGS)
	AC_DEFINE(USE_SSL, 1, [define if you are using ssl])
	AC_MSG_RESULT($lftp_cv_ssl)
else
	AC_MSG_RESULT(none found)
fi
])
