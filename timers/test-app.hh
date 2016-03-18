/*
* test-app.hh    : Test App Include File
* author         : Fabio Silva and Chalermek Intanagonwiwat
*
* Copyright (C) 2000-2001 by the Unversity of Southern California
* $Id: test-app.hh,v 1.6 2002/02/18 00:05:37 hussain Exp $
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

#ifndef _TEST_APP_H_
#define _TEST_APP_H_

#include "timers.hh"

class TestApp;

/* In TestTimer1 and TestTimer2, we are using a varible p_ to demonstrate 
 * the how to associate variable in the event queue. 
 * p_ can be replace by any other type of varible/structure 
 * ex: a LSA structure 
 */
class TestTimer1: public TimerCallback {
public:
        TestTimer1(TestApp *app, int p) : app_(app),p_(p)
		{};
        ~TestTimer1() {};
	TestApp *app_;
        int p_;
	int Expire();
};

class TestTimer2: public TimerCallback {
public:
	TestTimer2(int p):p_(p) {};
        ~TestTimer2() {};
        int p_;
	int Expire();
};

class TestApp{
public:
	TestApp();
	void start();

  	int ProcessTimer(int p);


protected:
	Timers *timersManager_;
};

#endif /* _TEST_APP_H_ */






