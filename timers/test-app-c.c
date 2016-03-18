/*
* test-app.cc    : Test Application
* author         : Aleifya Hussain 
*
* Copyright (C) 2000-2001 by the Unversity of Southern California
* $Id: test-app.cc,v 1.8 2002/02/19 16:03:36 hussain Exp $
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


#include "timers-c.h"
#include "string.h"

/* 
 * This application demonstrates using the timer package
 * by using two timers continuously.
 */

/*
 * This function can be use to process the timer. 
* The return value from expire is used 
* to indicate what should be done with the timer
*  = 0   Add timer again to queue
*  > 0   Set new_timeout as timeout value for timer 
*  < 0   Discard timer 
*/  
struct mystruct
{
  int p;
  char c[256];
};

//int ProcessTimer(int p, char* c)
int ProcessTimer(struct mystruct *s)
{
	struct timeval tv;

	getTime(&tv);
	fprintf(stderr, "Timer %d has expired! Time = %d:%d\n",
		s->p, (int)tv.tv_sec,(int)tv.tv_usec);
  fprintf(stderr, "%s\n", s->c);
	fflush(NULL);
	return -1;
}


void start()
{
	struct timeval tmv;
	int status;

	/* Change while condition to reflect what is required for Project 1
	   ex: Routing table stabalization.  */
	while (1) {
		Timers_NextTimerTime(&tmv);
		if (tmv.tv_sec == 0 && tmv.tv_usec == 0) {
		  /* The timer at the head on the queue has expired  */
		        Timers_ExecuteNextTimer();
			continue;
		}
		if (tmv.tv_sec == MAXVALUE && tmv.tv_usec == 0){
		  /* There are no timers in the event queue */
		        break;
		}
		  
		/* The select call here will wait for tv seconds before expiring 
		 * You need to  modifiy it to listen to multiple sockets and add code for 
		 * packet processing. Refer to the select man pages or "Unix Network 
		 * Programming" by R. Stevens Pg 156.
		 */
		status = select(0, NULL, NULL, NULL, &tmv);
		
		if (status < 0){
		  /* This should not happen */
			fprintf(stderr, "Select returned %d\n", status);
		}else{
			if (status == 0){
				/* Timer expired, Hence process it  */
			        Timers_ExecuteNextTimer();
				/* Execute all timers that have expired.*/
				Timers_NextTimerTime(&tmv);
				while(tmv.tv_sec == 0 && tmv.tv_usec == 0){
				  /* Timer at the head of the queue has expired  */
				        Timers_ExecuteNextTimer();
					Timers_NextTimerTime(&tmv);
					
				}
			}
			if (status > 0){
				/* The socket has received data.
				   Perform packet processing. */
		    
			}
		}
	}
	return;
}


int main()
{
	/* Initialize the Timer test application  by
	 * adding timers initially. 
	 *     -Add a timer for each packet sent.
	 *     -Set the timeout value appropriately 
	 */
        
        int (*fp)();   /* Define a pointer to the function */

	fp = ProcessTimer;  /* Gets the address of ProcessTimer */ 
  
  struct mystruct *s = malloc(sizeof(struct mystruct));
  s->p = 42;
  strncpy(s->c, "I'm a char array man!", sizeof(s->c));
  printf("%s\n", s->c);
	//Timers_AddTimer(3000,fp,(int*)1);
	//Timers_AddTimer(10000,fp,(int*)2);
	Timers_AddTimer(100,fp, (struct mystruct*)s);
	start();

  free(s);
  return 0;
}
