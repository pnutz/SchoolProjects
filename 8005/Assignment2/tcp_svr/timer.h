/*  SOURCE FILE:      timer.h - A header file to arm, disarm, and handle timers
--
--  PROGRAM:          tcp_svr, tcp_clnt
--
--  FUNCTIONS:        Timer
--
--  DATE:             December 2, 2014
--
--  REVISIONS:        (Date and Description)
--                    February 10, 2015
--                      Modified to remove tcp-packet functionality, focusing on timer.
--                      Added timerinit function to remove main program dependency.
--                      Modified handler function to be an argument during initialization.
--
--  DESIGNERS:        Christopher Eng
--
--  PROGRAMMERS:      Christopher Eng
--
--  NOTES:
--  This header file will run argument handler function when a timeout occurs.
--  This header file will provide a program with the tools to arm and disarm a timer.
--  This header file will provide a program with a method to handle timer expiry.
-----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>

#define SIG SIGUSR1
#define CLOCKID CLOCK_REALTIME
static int const NSEC_TO_SEC = 1000000000;

timer_t timerid;
struct itimerspec its;
struct itimerspec disarm;
int timeout = 0; // handler method modifies timeout var
struct sigevent sev;
struct sigaction sa;

// method run when timer runs out
/*void handler(int sig, siginfo_t *si, void *uc)
{
  if (si->si_value.sival_ptr == &timerid)
  {
    if (timeout == 0)
    {
      timeout = 1;
    }
  }
}*/

// initialize timer to be sec, nsec long
void timerinit(int sec, int nsec, void handler(int, siginfo_t*, void*))
{
  //printf("Establishing handler for signal %d\n", SIG);
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask); // initializes sa_mask signal set, preventing them from being blocked
  sigaction(SIG, &sa, NULL);

  sev.sigev_notify = SIGEV_SIGNAL; // notify when timer ends signal set at sigev_signo
  sev.sigev_signo = SIG; // notification signal
  sev.sigev_value.sival_ptr = &timerid; // pass timer with notification
  if (timer_create(CLOCKID, &sev, &timerid) == -1)
  {
    perror("Cannot create timer");
    exit(1);
  }

  its.it_value.tv_sec = sec; // seconds
  its.it_value.tv_nsec = nsec; // nanoseconds (1 billion in a second)
  // timer period, does not repeat
  its.it_interval.tv_nsec = 0;
  its.it_interval.tv_nsec = 0;

  disarm.it_value.tv_sec = 0;
  disarm.it_value.tv_nsec = 0;
  disarm.it_interval.tv_sec = 0;
  disarm.it_interval.tv_nsec = 0;
}

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

  printf("Timer value changed from %i s, %i nsec to %i s, %i nsec\n", (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec, timer_sec, timer_nsec);
  its.it_value.tv_sec = timer_sec;
  its.it_value.tv_nsec = timer_nsec;
}

void armTimer()
{
  timer_settime(timerid, 0, &its, NULL);
}

void disarmTimer()
{
  timer_settime(timerid, 0, &disarm, NULL);
}
