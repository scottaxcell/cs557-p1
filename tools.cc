/*
* tools.cc      : Implements a few utility functions
* authors       : Fabio Silva
*
* Copyright (C) 2000-2001 by the Unversity of Southern California
* $Id: tools.cc,v 1.3 2002/02/17 08:17:26 johnh Exp $
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

#include "tools.hh"

void 
getTime(struct timeval * tv)
{
	gettimeofday(tv, NULL);
}

void 
setSeed(struct timeval * tv)
{
	srand(tv->tv_usec);
}

int 
getRand()
{
	return (rand());
}
