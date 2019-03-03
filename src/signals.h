#ifndef SIGNALS_H
#define SIGNALS_H

#include <time.h>

int signal_setup(int signum, void (*handler)(int signo));
int signal_block_all();
int signal_allow_all();
int signal_block(int signo);
int signal_allow(int signo);

timer_t timer_createThreadTimer(void (*handler)());
timer_t timer_createSignalTimer(int signo);
int timer_destroy(timer_t timer);

int timer_startTimer(timer_t timer, unsigned long ms);
int timer_startInterval(timer_t timer, unsigned long ms);
int timer_stop(timer_t timer);

#endif
