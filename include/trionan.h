/*************************************************************************
 *
 * $Id$
 *
 * Copyright (C) 2001 Bjorn Reese <breese@users.sourceforge.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE AUTHORS AND
 * CONTRIBUTORS ACCEPT NO RESPONSIBILITY IN ANY CONCEIVABLE MANNER.
 *
 ************************************************************************/

/*
 * Compiler options
 *  Alpha
 *   GCC:  -mieee
 *   DECC: -ieee
 * Linker options
 *  -lm
 */

#ifndef __TRIO_NAN_H__
#define __TRIO_NAN_H__

#ifdef __cplusplus
extern "C" {
#endif

double trio_nan();
double trio_pinf();
double trio_ninf();
int trio_isnan(double number);
int trio_isinf(double number);

#ifdef __cplusplus
}
#endif

#endif /* __TRIO_NAN_H__ */
