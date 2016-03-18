
/*
* timers-c.cc     : Timer Management C-language API
* authors         : John Heidemann
*
* Copyright (C) 2000-2001 by the Unversity of Southern California
* $Id: timers-c.cc,v 1.1 2002/03/02 00:18:43 johnh Exp $
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

#include <assert.h>
#include <map>

#include "timers.hh"
#include "timers-c.h"



class TimersCCallback : public TimerCallback {
protected:
	TimerCallback_t cb_;
	void *p_;
public:
	TimersCCallback(TimerCallback_t cb, void *p) : cb_(cb), p_(p) {};
	int Expire();
};

static Timers timers;



int
TimersCCallback::Expire()
{
	int res = (*cb_)(p_);
	if (res < 0)
		delete this;
	return res;
}

int
Timers_AddTimer(int timeout, TimerCallback_t cb_fp, void *p)
{
	TimersCCallback *cb = new TimersCCallback(cb_fp, p);
	return timers.AddTimer(timeout, cb);
}

int
Timers_RemoveTimer(int handle)
{
	// xxx: leaks memory
	return (int) timers.RemoveTimer(handle);
}

void
Timers_NextTimerTime(struct timeval *tv)
{
	timers.NextTimerTime(tv);
}

void
Timers_ExecuteNextTimer()
{
	timers.ExecuteNextTimer();
}

