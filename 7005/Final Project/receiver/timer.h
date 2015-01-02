/*  SOURCE FILE:      timer.h - A header file to arm, disarm, and handle timers
--
--  PROGRAM:          tcp_svr, tcp_clnt, tcp_network
--
--  FUNCTIONS:        Timer
--
--  DATE:             December 2, 2014
--
--  REVISIONS:        (Date and Description)
--
--  DESIGNERS:        Christopher Eng
--
--  PROGRAMMERS:      Christopher Eng
--
--  NOTES:
--  This header file will allow a program to handle itimerspec calculations for TCP.
--  This header file will provide a program with the tools to arm and disarm a timer.
--  This header fille will provide a program with a method to handle timer expiry.
-----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>

#define CLOCKID CLOCK_REALTIME
static int const NSEC_TO_SEC = 1000000000;

timer_t timerid;
struct itimerspec its;
struct itimerspec disarm;
int timeout = 0; // handler method modifies timeout var
// initial timer values
int estimatedRTT;
int devRTT;

// subtract smallsec from bigsec and return by reference to bigsec
void subtractTime(int *result_sec, int *result_nsec, int bigsec, int bignsec, int smallsec, int smallnsec)
{
  // carry over sec from timer value if smallnsec is larger than bignsec
  if (smallsec <= bigsec - 1 && smallnsec > bignsec)
  {
    bigsec--;
    bignsec += NSEC_TO_SEC;
  }
  *result_sec = bigsec - smallsec;
  *result_nsec = bignsec - smallnsec;
}

// sec/nsec is amount of time remaining until timer expiry
void calculateTimeoutInterval(int sec, int nsec)
{
  int timer_sec = (int) its.it_value.tv_sec;
  int timer_nsec = (int) its.it_value.tv_nsec;

  int *result_sec = &timer_sec;
  int *result_nsec = &timer_nsec;
  subtractTime(result_sec, result_nsec, timer_sec, timer_nsec, sec, nsec);

  int sampleRTT = timer_sec * NSEC_TO_SEC + timer_nsec;
  devRTT = (int) (0.75 * devRTT + 0.25 * abs(sampleRTT - estimatedRTT));
  estimatedRTT = (int) (0.875 * estimatedRTT + 0.125 * sampleRTT);

  timer_sec = 0;
  timer_nsec = estimatedRTT;
  int i;
  // add devRTT to avoid integer overflow
  for (i = 0; i < 4; i++)
  {
    timer_nsec += devRTT;
    while (timer_nsec >= NSEC_TO_SEC)
    {
      timer_nsec -= NSEC_TO_SEC;
      timer_sec++;
    }
  }

  printf("Timer value changed from %i s, %i nsec to %i s, %i nsec\n", (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec, timer_sec, timer_nsec);
  its.it_value.tv_sec = timer_sec;
  its.it_value.tv_nsec = timer_nsec;
}

void doubleTimeoutInterval()
{
  int sec = its.it_value.tv_sec;
  int nsec = its.it_value.tv_nsec;
  sec *= 2;
  nsec *= 2;
  if (nsec >= NSEC_TO_SEC)
  {
    nsec -= NSEC_TO_SEC;
    sec++;
  }

  printf("Timer value changed from %i s, %i nsec to %i s, %i nsec\n", (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec, sec, nsec);
  its.it_value.tv_sec = sec;
  its.it_value.tv_nsec = nsec;
}

void armTimer()
{
  timer_settime(timerid, 0, &its, NULL);
}

void disarmTimer()
{
  timer_settime(timerid, 0, &disarm, NULL);
}

// method run when timer runs out
void handler(int sig, siginfo_t *si, void *uc)
{
  if (si->si_value.sival_ptr == &timerid)
  {
    if (timeout == 0)
    {
      timeout = 1;
    }
  }
}
