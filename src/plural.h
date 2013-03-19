/*
 * plural word form chooser for i18n
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This file is in public domain.
 */

#ifndef PLURAL_H
#define PLURAL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FORMAT_ARG
# ifdef __GNUC__
#  define FORMAT_ARG(n) __attribute__((format_arg(n)))
# else
#  define FORMAT_ARG(n)
# endif
#endif

const char *plural(const char *format,...) FORMAT_ARG(1);

#ifdef __cplusplus
}
#endif

#endif /* PLURAL_H */
