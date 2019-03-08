#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>

#include "signals.h"

int signal_setup(int signo, void (*handler)(int signo)) {
	struct sigaction action;
	
	action.sa_flags = 0;
	sigemptyset(&(action.sa_mask));
	action.sa_handler = handler;

	return sigaction(signo, &action, NULL);
}

int signal_block_all() {
	sigset_t mask;
	sigfillset(&mask);
	return pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

int signal_allow_all() {
	sigset_t mask;
	sigfillset(&mask);
	return pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
}
int signal_block(int signo) {
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, signo);
	return pthread_sigmask(SIG_BLOCK, &mask, NULL);
}
int signal_allow(int signo) {
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, signo);
	return pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
}

static void timerHandler(union sigval target) {
	((void (*)(void))(target.sival_ptr))();
}
timer_t timer_createThreadTimer(void (*handler)()) {
	struct sigevent sevp;
	sevp.sigev_notify = SIGEV_THREAD;
	sevp.sigev_notify_function = timerHandler;	
	sevp.sigev_notify_attributes = NULL;
	sevp.sigev_value.sival_ptr = (void*) handler;

	timer_t timer;

	if (timer_create(CLOCK_BOOTTIME, &sevp, &timer) < 0) {
		timer = NULL;
	}
	return timer;
}
timer_t timer_createSignalTimer(int signo) {
	struct sigevent sevp;
	sevp.sigev_notify = SIGEV_SIGNAL;
	sevp.sigev_signo = signo;
	
	timer_t timer;

	if (timer_create(CLOCK_BOOTTIME, &sevp, &timer) < 0) {
		timer = NULL;
	}
	
	return timer;
}
int timer_destroy(timer_t timer) {
	return timer_delete(timer);
}

int timer_startTimer(timer_t timer, unsigned long ms) {
	struct itimerspec time, old;

	time.it_value.tv_sec = ms / 1000;
	time.it_value.tv_nsec = ((ms % 1000) * 1000000);
	time.it_interval.tv_sec = 0;
	time.it_interval.tv_nsec = 0;

	return timer_settime(timer, 0, &time, &old);
}
int timer_startInterval(timer_t timer, unsigned long ms) {
	struct itimerspec time, old;

	time.it_value.tv_sec = ms / 1000;
	time.it_value.tv_nsec = ((ms % 1000) * 1000000);
	time.it_interval.tv_sec = ms / 1000;
	time.it_interval.tv_nsec = ((ms % 1000) * 1000000);

	return timer_settime(timer, 0, &time, &old);
}
int timer_stop(timer_t timer) {
	struct itimerspec time, old;

	time.it_value.tv_sec = 0;
	time.it_value.tv_nsec = 0;
	time.it_interval.tv_sec = 0;
	time.it_interval.tv_nsec = 0;

	return timer_settime(timer, 0, &time, &old);
}
