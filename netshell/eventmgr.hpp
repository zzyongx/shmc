#ifndef _EVENTMGR_HPP_
#define _EVENTMGR_HPP_

#include <sys/epoll.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

/* Every *Conn class should inherit from AbstractConn and
 * implement driverManchine. */
class AbstractConn {
public:
	AbstractConn(int fd) : fd_(fd) {}
	virtual ~AbstractConn() { if (fd_ >= 0) close(fd_); };
	virtual void driverMachine(int flags) = 0;
	virtual void timer() { /* do nothing */ }
	int fd(int afd = -1) { 
		if (afd != -1) fd_ = afd;
		return fd_;
	}

protected:
	int fd_;
};

/* every 500 millisecond, will call timerHanler once
 */
typedef void (*TimerHandler)(void *);

class EventMgr {
public:
	EventMgr(int nevents = 1024, TimerHandler timerHandler = 0, void *arg = 0) {
		nevents_ = nevents;
		efd_ = epoll_create(nevents);
		if (efd_ == -1) throw errno;
		stop_ = false;

		timerHandler_ = timerHandler;
		handlerArg_ = arg;
	}
	~EventMgr() {
		close(efd_);
	}

	void setTimerHandler(TimerHandler timerHandler) {
		timerHandler_ = timerHandler;
	}
	void setTimerHandlerArg(void *arg) {
		handlerArg_ = arg;
	}

	bool addEvent(AbstractConn *c, int ev) {
		struct epoll_event event;
		event.events = ev;
		event.data.ptr = c;
		int rc = epoll_ctl(efd_, EPOLL_CTL_ADD, c->fd(), &event);
		return rc == 0;
	}
	bool updateEvent(AbstractConn *c, int ev) {
		struct epoll_event event;
		event.events = ev;
		event.data.ptr = c;
		int rc = epoll_ctl(efd_, EPOLL_CTL_MOD, c->fd(), &event);
		return rc == 0;
	}
	bool deleteEvent(AbstractConn *c) {
		struct epoll_event event;
		int rc = epoll_ctl(efd_, EPOLL_CTL_DEL, c->fd(), &event);
		return rc == 0;
	}

	/* infinite loop, you can call stop() in 
	 * Conn::driverMachine or timerHandler
	 */
	bool run() {
		struct epoll_event *events =
			(struct epoll_event *) calloc(nevents_, sizeof(struct epoll_event));

		struct timeval anchor;
		gettimeofday(&anchor, 0);

		stop_ = false;

		int n;
		while (!stop_) {
			n = epoll_wait(efd_, events, nevents_, 500);

			if (n == -1) {
				if (errno == EINTR) continue;
				else break;
			}

			for (int i = 0; i < n; ++i) {
				AbstractConn *c = (AbstractConn *) events[i].data.ptr;
				c->driverMachine(events[i].events);
			}

			if (timerHandler_) {
				struct timeval tv;
				gettimeofday(&tv, 0);  // may bottleneck
				if (n == 0) {
					timerHandler_(handlerArg_);	
				} else {
					if (((tv.tv_sec - anchor.tv_sec)*1000 + (tv.tv_usec - anchor.tv_usec)/1000) > 500) {
						timerHandler_(handlerArg_);
						anchor = tv;
					}
				}
			}
		}

		free(events);
		return stop_ == true;
	}

	bool start() { stop_ = true; return true; };
	bool stop() { stop_ = true; return true; };

private:
	TimerHandler timerHandler_;
	void *handlerArg_;

private:
	int efd_;
	int nevents_;
	volatile bool stop_;
};

#endif
