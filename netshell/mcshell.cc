/* vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <token.h>
#include <mcshell.h>

#ifdef LOG_ERROR
# define log_error(eno, fmt, args...) \
	printf("%d:%s "fmt"\n", eno, eno == 0 ? "" : strerror(eno), ##args)
#else
# define log_error(eno, fmt, args...)
#endif

#define MAX_TOKENS 7
#define CMD_TOKEN  0
#define KEY_TOKEN  1
#define FILE_TOKEN 1
#define NVAL_TOKEN 4
#define FLAG_TOKEN 2

class McConn : public AbstractConn {
public:
	enum ConnState { Listening, Read, NRead, Write, Close };

	McConn(int fd, shmc_t *shmc, EventMgr *em, ConnState state, stats_t *stats);
	~McConn();
	void driverMachine(int flags);

private:
	enum DmState { DmStop, DmGoOn };
	enum CmdType { Set, Add, Replace, Prepend, Append };

	static const char *stateTxt(ConnState state);

	void doGet();
	void doIncr();
	void doDecr();
	void doDelete();
	void doStats();
	void doDump();
	void doLoad();
	void doSet();
	void doAdd();
	void doReplace();
	void doPrepend();
	void doAppend();

	void outString(const char *fmt, ...);

	DmState onListening();
	DmState onRead();
	DmState onNRead();
	DmState onWrite();
	DmState onClose();

private:
	shmc_t *shmc_;
	EventMgr *em_;
	ConnState state_;
	stats_t *stats_;

private:
	char *reqHeader_;
	const size_t reqHeaderSize_;
	size_t reqHeaderBytes_;

	char *reqBody_;
	size_t reqBodySize_;
	size_t reqBodyBytes_;
	size_t reqBodyCapability_;

	char *resHeader_;;
	size_t resHeaderSize_;
	size_t resHeaderBytes_;

	char *resBody_;
	size_t resBodySize_;
	size_t resBodyBytes_;

	size_t resTailBytes_;

	CmdType ctype_;
	uint32_t flags_;
	token_t tokens_[MAX_TOKENS];
	size_t ntokens_;
};

const char *McConn::stateTxt(ConnState state)
{
	const char *txt = "unknow state";
	switch (state) {
		case Listening: txt = "listening"; break;
		case Read:      txt = "reading";   break;
		case NRead:     txt = "nreading";  break;
		case Write:     txt = "writing";   break;
		case Close:     txt = "close";     break;
	}
	return txt;
}

#define REQ_HEADER_SIZE 312
#define RES_HEADER_SIZE 312

McConn::McConn(int fd, shmc_t *shmc, EventMgr *em, ConnState state, stats_t *stats)
	: AbstractConn(fd), shmc_(shmc), em_(em), state_(state), stats_(stats),
	  reqHeaderSize_(REQ_HEADER_SIZE)
{
	reqHeader_ = new char[reqHeaderSize_];
	reqHeaderBytes_ = 0;

	reqBody_ = 0;
	reqBodySize_ = reqBodyBytes_ = 0;
	reqBodyCapability_ = 0;

	resHeader_ = new char[RES_HEADER_SIZE];
	resHeaderSize_ = resHeaderBytes_ = 0;

	resBody_ = 0;
	resBodySize_ = resBodyBytes_ = 0;

	resTailBytes_ = 0;
}

McConn::~McConn()
{
	delete reqHeader_;
	delete resHeader_;

	if (reqBody_) free(reqBody_);
	if (resBody_) free(resBody_);
}

McConn::DmState McConn::onListening()
{
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	
	int fd = accept(fd_, (struct sockaddr *) &addr, &addrlen);
	if (fd == -1) {
		log_error(errno, "accept() failed");
		return DmStop;
	}

	int flags;
	if ((flags = fcntl(fd, F_GETFL, 0)) < 0 ||
		fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		log_error(errno, "fcntl() failed");
		return DmStop;
	}

	McConn *c = new McConn(fd, shmc_, em_, Read, stats_);
	if (!em_->addEvent(c, EPOLLIN)) {
		log_error(errno, "#%p addEvent(IN) failed", (void *) c);
		delete c;
	} else {
		log_error(0, "#%p addEvent(IN)", (void *) c);
	}
	return DmStop;
}

void McConn::outString(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	resHeaderSize_ = vsnprintf(resHeader_, RES_HEADER_SIZE, fmt, ap);
	va_end(ap);
}

void McConn::doGet()
{
	char    *val;
	size_t   nval;
	uint32_t flags;

	stats_->get_cnts++;

	SHMC_RC rc = shmc_get(shmc_, tokens_[KEY_TOKEN].value, tokens_[KEY_TOKEN].length, &val, &nval, &flags);
	if (rc == SHMC_OK) {
		outString("VALUE %s %"PRIu32" %d\r\n", tokens_[KEY_TOKEN].value, flags, (int) nval);	
		resBody_ = val;
		resBodySize_ = nval;
	} else if (rc == SHMC_NOTFOUND) {
		stats_->get_misses++;
		outString("END\r\n");
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));
	}
}

void McConn::doIncr()
{
	uint64_t newVal;
	uint64_t val = strtoull(tokens_[KEY_TOKEN+1].value, 0, 10);

	stats_->incr_cnts++;

	SHMC_RC rc = shmc_incr(shmc_, tokens_[KEY_TOKEN].value, tokens_[KEY_TOKEN].length, val, &newVal, 0);
	if (rc == SHMC_OK) {
		outString("%"PRIu64"\r\n", newVal);
	} else if (rc == SHMC_NOTFOUND) {
		stats_->incr_misses++;
		outString("NOT_FOUND\r\n");
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));	
	}
}

void McConn::doDecr()
{
	uint64_t newVal;
	uint64_t val = strtoull(tokens_[KEY_TOKEN+1].value, 0, 10);

	stats_->decr_cnts++;

	SHMC_RC rc = shmc_decr(shmc_, tokens_[KEY_TOKEN].value, tokens_[KEY_TOKEN].length, val, &newVal, 0);
	if (rc == SHMC_OK) {
		outString("%"PRIu64"\r\n", newVal);	
	} else if (rc == SHMC_NOTFOUND) {
		stats_->decr_misses++;
		outString("NOT_FOUND\r\n");
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));	
	}
}

void McConn::doDelete()
{
	stats_->del_cnts++;

	SHMC_RC rc = shmc_del(shmc_, tokens_[KEY_TOKEN].value, tokens_[KEY_TOKEN].length);
	if (rc == SHMC_OK) {
		outString("DELETED\r\n");
	} else if (rc == SHMC_NOTFOUND) {
		stats_->del_misses++;
		outString("NOT_FOUND\r\n");	
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));
	}
}

void McConn::doStats()
{
	/* uint64 18446744073709551615, length 20
	 * 1024 is enough
	 */
	const size_t STATS_SIZE = 1024;
	resBody_ = (char *) malloc(STATS_SIZE);
	if (!resBody_) {
		outString("SERVER_ERROR out of memory\r\n");	
		return;
	}

	resBodySize_ = 0;
	int n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT cmd_get %"PRIu64"\r\n", stats_->get_cnts);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT cmd_set %"PRIu64"\r\n", stats_->set_cnts);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT cmd_del %"PRIu64"\r\n", stats_->del_cnts);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT cmd_incr %"PRIu64"\r\n", stats_->incr_cnts);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT cmd_decr %"PRIu64"\r\n", stats_->decr_cnts);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT get_misses %"PRIu64"\r\n", stats_->get_misses);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT del_misses %"PRIu64"\r\n", stats_->del_misses);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT incr_misses %"PRIu64"\r\n", stats_->incr_misses);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT decr_misses %"PRIu64"\r\n", stats_->decr_misses);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT err_cnts %"PRIu64"\r\n", stats_->err_cnts);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT nbuckets %d\r\n", shmc_->attr->nbuckets);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT item_min %d\r\n", (int) shmc_->attr->item_size_min);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT item_max %d\r\n", (int) shmc_->attr->item_size_max);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT item_factor %.2f\r\n", shmc_->attr->item_size_factor);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT evict_free %d\r\n", shmc_->attr->evict_to_free);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT default_counter %d\r\n", shmc_->attr->default_counter);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT use_flock %d\r\n", shmc_->attr->use_flock);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT bytes %lu\r\n", (unsigned long) shmc_->attr->mem_used);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT limit_maxbytes %lu\r\n", (unsigned long) shmc_->attr->mem_limit);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT total_items %lu\r\n", (unsigned long) shmc_->attr->nitems);
	resBodySize_ += n;

	n = snprintf(resBody_ + resBodySize_, STATS_SIZE - resBodySize_,
			"STAT max_depth %d", shmc_->attr->max_depth);
	resBodySize_ += n;
}

void McConn::doDump()
{
	SHMC_RC rc = shmc_dump(shmc_, tokens_[FILE_TOKEN].value);
	if (rc == SHMC_OK) {
		outString("DUMPED\r\n");
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));
	}
}

void McConn::doLoad()
{
	SHMC_RC rc = shmc_load(shmc_, tokens_[FILE_TOKEN].value);
	if (rc == SHMC_OK) {
		outString("LOADED\r\n");
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));
	}
}

McConn::DmState McConn::onRead()
{
	ssize_t nn;
	while ((nn = recv(fd_, reqHeader_ + reqHeaderBytes_, reqHeaderSize_ - reqHeaderBytes_, 0)) > 0) {
		reqHeaderBytes_ += nn;
		if (reqHeaderBytes_ == reqHeaderSize_) break;
	}

	if (nn == 0) {
		log_error(0, "#%p closed", (void *) this);
		state_ = Close;	
		return DmGoOn;
	} else if (nn < 0) {
		if (errno != EAGAIN) {
			log_error(errno, "#%p recv failed", (void *) this);
			state_ = Close;
			return DmGoOn;
		}
	}

	char *end = (char *) memchr(reqHeader_, '\n', reqHeaderBytes_);
	if (!end) {
		if (reqHeaderBytes_ == reqHeaderSize_) {
			outString("ERROR request header too long\r\n");
			if (em_->updateEvent(this, EPOLLOUT)) {
				state_ = Write;
			} else {
				state_ = Close;
			}
			reqHeaderBytes_ = 0;
			return DmGoOn;
		} else {
			return DmStop;	
		}
	}

	char *count = end + 1;

	if (end - reqHeader_ > 1 && *(end - 1) == '\r') {
		end--;
	}
	*end = '\0';

	bool stop = false;
	ntokens_ = tokenize(reqHeader_, tokens_, MAX_TOKENS);

	/* get key
	 * set/add/replace/prepend/append key flags exptime bytes
	 * incr/decr key value
	 * delete key
	 * quit
	 */
	if (ntokens_ == 3 && strcmp("get", tokens_[CMD_TOKEN].value) == 0) {
		stop = true;
		doGet();
	} else if (ntokens_ == 6 && strcmp("set", tokens_[CMD_TOKEN].value) == 0) {
		ctype_ = Set;
	} else if (ntokens_ == 6 && strcmp("add", tokens_[CMD_TOKEN].value) == 0) {
		ctype_ = Add;
	} else if (ntokens_ == 6 && strcmp("replace", tokens_[CMD_TOKEN].value) == 0) {
		ctype_ = Replace;
	} else if (ntokens_ == 6 && strcmp("prepend", tokens_[CMD_TOKEN].value) == 0) {
		ctype_ = Prepend;
	} else if (ntokens_ == 6 && strcmp("append", tokens_[CMD_TOKEN].value) == 0) {
		ctype_ = Append;
	} else if (ntokens_ == 4 && strcmp("incr", tokens_[CMD_TOKEN].value) == 0) {
		stop = true;
		doIncr();
	} else if (ntokens_ == 4 && strcmp("decr", tokens_[CMD_TOKEN].value) == 0) {
		stop = true;
		doDecr();
	} else if (ntokens_ == 3 && strcmp("delete", tokens_[CMD_TOKEN].value) == 0) {
		stop = true;
		doDelete();
	} else if (ntokens_ == 2 && strcmp("stats", tokens_[CMD_TOKEN].value) == 0) {
		stop = true;
		doStats();
	} else if (ntokens_ == 3 && strcmp("dump", tokens_[CMD_TOKEN].value) == 0) {
		stop = true;
		doDump();
	} else if (ntokens_ == 3 && strcmp("load", tokens_[CMD_TOKEN].value) == 0) {
		stop = true;
		doLoad();
	} else if (ntokens_ == 2 && strcmp("quit", tokens_[CMD_TOKEN].value) == 0) {
		state_ = Close;
		return DmGoOn;
	} else {
		stop = true;
		outString("CLIENT_ERROR unknow command\r\n");
	}

	if (!stop) {
		size_t nval = strtoul(tokens_[NVAL_TOKEN].value, 0, 10);

		if (reqBodyCapability_ < nval + 2) {
			if (reqBody_) free(reqBody_);
			reqBodyCapability_ = nval + 2;
			reqBody_ = (char *) malloc(reqBodyCapability_);
		}

		reqBodySize_ = nval + 2;
		if (reqHeaderBytes_ - (count - reqHeader_) > 0) {
			reqBodyBytes_ = reqHeaderBytes_ - (count - reqHeader_);
			if (reqBodyBytes_ > reqBodySize_) reqBodyBytes_ = reqBodySize_;
			memcpy(reqBody_, count, reqBodyBytes_);
		}

		state_ = NRead;
		if (reqBodyBytes_ == reqBodySize_) {
			return DmGoOn;
		} else {
			if (nn < 0 && errno == EAGAIN) {
				return DmStop;
			} else {
				return DmGoOn;
			}
		}
	} else {
		reqHeaderBytes_ = 0;
		if (em_->updateEvent(this, EPOLLOUT)) {
			log_error(0, "#%p updateEvent(OUT)", (void *) this);
			state_ = Write;	
		} else {
			log_error(errno, "#%p updateEvent(OUT) failed", (void *) this);
			state_ = Close;
		}
		return DmGoOn;
	}
}

void McConn::doSet()
{
	stats_->set_cnts++;

	SHMC_RC rc = shmc_set(shmc_, tokens_[KEY_TOKEN].value, tokens_[KEY_TOKEN].length,
			reqBody_, reqBodySize_ - 2, flags_);
	if (rc == SHMC_OK) {
		outString("STORED\r\n");	
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));	
	}
}

void McConn::doAdd()
{
	stats_->set_cnts++;

	SHMC_RC rc = shmc_add(shmc_, tokens_[KEY_TOKEN].value, tokens_[KEY_TOKEN].length,
			reqBody_, reqBodySize_ - 2, flags_);
	if (rc == SHMC_OK) {
		outString("STORED\r\n");	
	} else if (rc == SHMC_EXIST) {
		outString("EXISTS\r\n");
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));	
	}
}

void McConn::doReplace()
{
	stats_->set_cnts++;

	SHMC_RC rc = shmc_replace(shmc_, tokens_[KEY_TOKEN].value, tokens_[KEY_TOKEN].length,
			reqBody_, reqBodySize_ - 2, flags_);
	if (rc == SHMC_OK) {
		outString("STORED\r\n");
	} else if (rc == SHMC_NOTFOUND) {
		outString("NOT_FOUND\r\n");
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));
	}
}

void McConn::doPrepend()
{
	stats_->set_cnts++;

	SHMC_RC rc = shmc_prepend(shmc_, tokens_[KEY_TOKEN].value, tokens_[KEY_TOKEN].length,
			reqBody_, reqBodySize_ - 2, flags_);
	if (rc == SHMC_OK) {
		outString("STORED\r\n");
	} else if (rc == SHMC_NOTFOUND) {
		outString("NOT_FOUND\r\n");
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));
	}
}

void McConn::doAppend()
{
	stats_->set_cnts++;

	SHMC_RC rc = shmc_append(shmc_, tokens_[KEY_TOKEN].value, tokens_[KEY_TOKEN].length,
			reqBody_, reqBodySize_ - 2, flags_);
	if (rc == SHMC_OK) {
		outString("STORED\r\n");
	} else if (rc == SHMC_NOTFOUND) {
		outString("NOT_FOUND\r\n");	
	} else {
		stats_->err_cnts++;
		outString("SERVER_ERROR %s\r\n", shmc_error(rc));
	}
}

McConn::DmState McConn::onNRead()
{
	if (reqBodyBytes_ != reqBodySize_) {
		ssize_t nn;
		while ((nn = recv(fd_, reqBody_ + reqBodyBytes_, reqBodySize_ - reqBodyBytes_, 0)) > 0) {
			reqBodyBytes_ += nn;
			if (reqBodyBytes_ == reqBodySize_) break;
		}

		if (nn == 0) {
			log_error(0, "#%p closed", (void *) this);
			state_ = Close;	
			return DmGoOn;
		} else if (nn < 0) {
			if (errno == EAGAIN) {
				return DmStop;
			} else {
				log_error(errno, "#%p recv() failed", (void *) this);
				state_ = Close;
				return DmGoOn;	
			}
		}
	}

	flags_ = strtoul(tokens_[FLAG_TOKEN].value, 0, 10);

	switch (ctype_) {
		case Set:     doSet();     break;
		case Add:     doAdd();     break;
		case Replace: doReplace(); break;
		case Prepend: doPrepend(); break;
		case Append:  doAppend();  break;
	}

	reqHeaderBytes_ = 0;
	reqBodyBytes_ = 0;

	if (em_->updateEvent(this, EPOLLOUT)) {
		log_error(0, "#%p updateEvent(OUT)", (void *) this);
		state_ = Write;
	} else {
		state_ = Close;
	}
	return DmGoOn;
}

#define IOV_CNT 3

McConn::DmState McConn::onWrite()
{
	size_t niov;
	struct iovec iov[IOV_CNT];
	iov[0].iov_base = resHeader_ + resHeaderBytes_;
	iov[0].iov_len  = resHeaderSize_ - resHeaderBytes_;
	if (resBodySize_) {
		niov = 3;
		iov[1].iov_base	= resBody_ + resBodyBytes_;
		iov[1].iov_len  = resBodySize_ - resBodyBytes_;
		iov[2].iov_base = (char *) "\r\nEND\r\n" + resTailBytes_;
		iov[2].iov_len  = 7 - resTailBytes_;
	} else {
		niov = 1;
	}

	size_t nsucc;
	for ( ;; ) {
		nsucc = 0;
		ssize_t nn = writev(fd_, iov, niov);
		for (size_t i = 0; i < niov && nn > 0; ++i) {
			if (iov[i].iov_len >= (size_t) nn) {
				iov[i].iov_base = (char *) iov[i].iov_base + nn;
				iov[i].iov_len -= nn;
				nn = 0;
			} else {
				nn -= iov[i].iov_len;
				iov[i].iov_len  = 0;	
			}
			if (iov[i].iov_len == 0) nsucc++;
		}
		if (nsucc == niov || nn < 0) {
			break;
		}
	}

	if (nsucc == niov) {
		if (resBodySize_) {
			free(resBody_);	
			resBody_ = 0;
			resBodySize_ = resBodyBytes_ = 0;
			resTailBytes_ = 0;
		}
		if (em_->updateEvent(this, EPOLLIN)) {
			log_error(0, "#%p updateEvent(IN)", (void *) this);
			state_ = Read;
			return DmStop;
		} else {
			state_ = Close;
			return DmGoOn;
		}
	} else {
		resHeaderBytes_ = resHeaderSize_ - iov[0].iov_len;
		if (resBodySize_) {
			resBodyBytes_ = resBodySize_ - iov[1].iov_len;
			resTailBytes_ = 7 - iov[2].iov_len;
		}
		if (errno == -1) {
			return DmStop;	
		} else {
			state_ = Close;
			return DmGoOn;	
		}
	}
}

McConn::DmState McConn::onClose()
{
	delete this;
	return DmStop;
}

void McConn::driverMachine(int /* flags */)
{
	DmState dmState = DmGoOn;
	while (dmState == DmGoOn) {
		log_error(0, "#%p state %s", this, stateTxt(state_));
		switch (state_) {
			case Listening: dmState = onListening(); break;
			case Read: dmState = onRead(); break;
			case NRead: dmState = onNRead(); break;
			case Write: dmState = onWrite(); break;
			case Close: dmState = onClose(); break;
		}
	}
}

McShell::McShell(shmc_t *shmc, int port, const char *inter) : shmc_(shmc)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) throw errno;

	int flags;
	if ((flags = fcntl(fd, F_GETFL, 0)) < 0 ||
		(fcntl(fd, F_SETFL, flags | O_NONBLOCK)) < 0) {
		close(fd);
		throw errno;	
	}

	flags = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags)) < 0) {
		close(fd);
		throw errno;	
	}
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));

	struct sockaddr_in addr;

	memset(&addr, 0x00, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(port);
    if (inter) {
        inet_aton(inter, &addr.sin_addr);
    } else {
	    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

	if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		close(fd);
		throw errno;	
	}

	if (listen(fd, 1024) < 0) {
		close(fd);
		throw errno;	
	}

	memset(&stats_, 0x00, sizeof(stats_));

	em_ = new EventMgr(1024);

	McConn *c = new McConn(fd, shmc_, em_, McConn::Listening, &stats_);
	if (!em_->addEvent(c, EPOLLIN | EPOLLOUT)) {
		close(fd);
		delete em_;
		throw errno;
	}
}

bool McShell::run()
{
	return em_->run();
}

void McShell::stop()
{
	em_->stop();
}
