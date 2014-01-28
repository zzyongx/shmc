#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

/* g++ -o tplock_lockf tplock.cc -g -Wall -lpthread -DUSE_LOCKF
 * g++ -o tplock_mutex tplock.cc -g -Wall -lpthread
 */

#define LOOP (500 * 10000)

class ProcLock {
public:
	virtual ~ProcLock() { };
	virtual void lock() = 0;
	virtual void unlock() = 0;
};

class ProcMutexLock : public ProcLock {
public:
	ProcMutexLock() {
		size_t size = sizeof(pthread_mutex_t) + sizeof(pthread_mutexattr_t);
		area_ = mmap(0, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		if (area_ == MAP_FAILED) {
			throw errno;
		}

		mutex_ = (pthread_mutex_t *) area_;
		mutexattr_ = (pthread_mutexattr_t *) ((char *) area_ + sizeof(pthread_mutex_t));
		pthread_mutexattr_init(mutexattr_);
		pthread_mutexattr_setpshared(mutexattr_, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(mutex_, mutexattr_);
	}

	~ProcMutexLock() {
		pthread_mutex_destroy(mutex_);
		pthread_mutexattr_destroy(mutexattr_);
	}

	void lock() {
		pthread_mutex_lock(mutex_);
	}

	void unlock() {
		pthread_mutex_unlock(mutex_);
	}

private:
	void *area_;
	pthread_mutex_t *mutex_;
	pthread_mutexattr_t *mutexattr_;
};

class ProcFileLock : public ProcLock {
public:
	ProcFileLock(const char *name = 0) {
		int fd;
		if (name) {
			fd = open(name, O_RDWR | O_CREAT, 0644);
		} else {
			char tmp[] = "/tmp/plock_XXXXXX";
			fd = mkstemp(tmp);
		}

		if (fd == -1) {
			throw errno;
		}
		fd_ = fd;
	}

	~ProcFileLock() {
		close(fd_);
	}

	void lock() {
		// fcntl_(fd_, F_WRLCK);
		lockf(fd_, F_LOCK, 1);
	}

	void unlock() {
		// fcntl_(fd_, F_UNLCK);
		lockf(fd_, F_ULOCK, 1);
	}

private:
	/*
	void fcntl_(int fd, int type) {
		struct flock lock;
		lock.l_type = type;
		lock.l_start = 0;
		lock.l_whence = SEEK_SET;
		lock.l_len = 0;

		fcntl(fd, F_SETLKW, &lock);
	}
	*/

private:
	int fd_;
};

static void *worker(int *intval)
{
	ProcFileLock lock("/tmp/tplock.lock");
	for (int i = 0; i < LOOP; ++i) {
		lock.lock();
		(*intval)++;
		lock.unlock();
	}
	return 0;
}

int main(int argc, char *argv[])
{
    int *intval = (int *) mmap(0, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (intval == MAP_FAILED) {
        perror("mmap failed");
        return -1;
    }

    *intval = 0;

    pid_t pid;
    if ((pid = fork()) == 0) {
		worker(intval);
        exit(0);
    } else if (pid < 0) {
        perror("fork error"); 
        return -1;
    }

	worker(intval);

    wait(0);
    printf("\nexpected: %d, actual: %d\n", 2 * LOOP, *intval);

    return 0;
}

#if 0

static int intval = 0;

static void *worker(void *)
{
	ProcFileLock lock("/tmp/tplock.lock");
	for (int i = 0; i < LOOP; ++i) {
		lock.lock();
		intval++;
		lock.unlock();
	}
	return 0;
}

int main(int argc, char *argv[])
{
	pthread_t tid;
	if (pthread_create(&tid, 0, worker, 0) != 0) {
		return -1;
	}

	worker(0);
	pthread_join(tid, 0);

    printf("\nexpected: %d, actual: %d\n", 2 * LOOP, intval);
	return 0;
}

#endif

#if 0

int main(int argc, char *argv[])
{
    int *intval = (int *) mmap(0, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (intval == MAP_FAILED) {
        perror("mmap failed");
        return -1;
    }

#ifdef USE_LOCKF
	printf("use lockf\n");
	ProcLock *lock = new ProcFileLock();
#else
	printf("use mutex\n");
	ProcLock *lock = new ProcMutexLock();
#endif

    int i;
    *intval = 0;

    pid_t pid;
    if ((pid = fork()) == 0) {
        for (i = 0; i < LOOP; ++i) {
            if (i % 500000 == 0) fprintf(stderr, "C");
			lock->lock();
			(*intval)++; 
			lock->unlock();
        }
        exit(0);
    } else if (pid < 0) {
        perror("fork error"); 
        return -1;
    }

    for (i = 0; i < LOOP; ++i) {
        if (i % 500000 == 0) fprintf(stderr, "P");
		lock->lock();
		(*intval)++; 
		lock->unlock();
    }

    wait(0);
    printf("\nexpected: %d, actual: %d\n", 2 * LOOP, *intval);

	delete lock;

    return 0;
}

#endif
