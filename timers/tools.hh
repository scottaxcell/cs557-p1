/*
* tools.hh      : Other utility functions
* authors       : Fabio Silva
*
* Copyright (C) 2000-2001 by the Unversity of Southern California
* $Id: tools.hh,v 1.4 2002/03/02 00:18:43 johnh Exp $
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License,
* version 2, as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
*
*/
/*
* tools.hh contains OS abstractions to make it easy to use
* in different environments (i.e. in simulations,
* where time is virtualized, and in embeddedd apps where
* error logging happens in some non-trivial way).
*/
#ifndef _TOOLS_H_
#define _TOOLS_H_
#include <sys/time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

void setSeed(struct timeval * tv);
void getTime(struct timeval * tv);
int getRand();

#ifdef __cplusplus
};
#endif

#endif				/* !_TOOLS_H_ */
