/*
 * Copyright 2013-16 Board of Trustees of Stanford University
 * Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * trap.c - handles system calls, page faults, and other traps
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <asm/prctl.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <utime.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sched.h>
#include <linux/sched.h>
#include <sys/mman.h>
#include <pthread.h>
#include <err.h>
#include <linux/futex.h>
#include <linux/unistd.h>

#include "sandbox.h"
#include <cpu-x86.h>

#include "ix/kstats.h"

struct thread_arg {
	pthread_cond_t	ta_cnd;
	pthread_mutex_t	ta_mtx;
	pid_t		ta_tid;
	struct dune_tf	*ta_tf;
	bool		dune_started;
};

int exec_execev(const char *filename, char *const argv[], char *const envp[]);

static pthread_mutex_t _syscall_mtx;

int check_extent(const void *ptr, size_t len)
{
	if (!mem_ref_is_safe(ptr, len)) {
		printf("sandbox: mem ref with addr %p, len %lx is out of range\n",
		       ptr, len);
		return -EFAULT;
	}

	return 0;
}

int check_string(const void *ptr)
{
	void *pos;
	size_t maxlen;

	if ((uintptr_t) ptr < MEM_IX_BASE_ADDR)
		maxlen = MEM_IX_BASE_ADDR - ((uintptr_t) ptr);
	else if ((uintptr_t) ptr >= MEM_USER_DIRECT_BASE_ADDR) {
		if ((uintptr_t) ptr >= MEM_USER_DIRECT_END_ADDR)
			goto fault;
		maxlen = MEM_USER_DIRECT_END_ADDR - ((uintptr_t) ptr);
	} else
		goto fault;

	pos = memchr(ptr, 0, maxlen);
	if (!pos)
		goto fault;

	return 0;

fault:
	printf("str ref addr %p is out of range\n", ptr);
	return -EFAULT;
}

void do_enter_thread(struct dune_tf *tf)
{
	long rc;

	rc = dune_jump_to_user(tf);

	syscall(SYS_exit, rc);
}

void *pthread_entry(void *arg)
{
	struct thread_arg *a = arg;
	struct dune_tf *tf = a->ta_tf;
	struct dune_tf child_tf;
	int *tidp = NULL;
	pid_t tid;
	int flags = ARG0(tf);

	if (!a->dune_started)
		dune_enter();

	tid = syscall(SYS_gettid);

	/* XXX validate */
	/* set up tls */
	if (flags & CLONE_SETTLS)
		dune_set_user_fs(ARG4(tf));

	if (flags & CLONE_PARENT_SETTID) {
		tidp = (int *) ARG2(tf);
		*tidp = tid;
	}

	if (flags & CLONE_CHILD_CLEARTID) {
		tidp = (int *) ARG3(tf);
		syscall(SYS_set_tid_address, tidp);
	}

	/* enter thread */
	memcpy(&child_tf, tf, sizeof(child_tf));
	child_tf.rip = tf->rip;
	child_tf.rax = 0;
	child_tf.rsp = ARG1(tf);

	/* tell parent tid */
	pthread_mutex_lock(&a->ta_mtx);
	a->ta_tid = tid;
	pthread_mutex_unlock(&a->ta_mtx);
	pthread_cond_signal(&a->ta_cnd);

	do_enter_thread(&child_tf);

	return NULL;
}

extern bool sys_spawn_cores;
extern int init_do_spawn(void *arg);

static long dune_pthread_create(struct dune_tf *tf)
{
	pthread_t pt;
	struct thread_arg arg;

	arg.ta_tf  = tf;
	arg.ta_tid = 0;

	if (pthread_cond_init(&arg.ta_cnd, NULL))
		return -1;

	if (pthread_mutex_init(&arg.ta_mtx, NULL))
		return -1;

	if (sys_spawn_cores) {
		arg.dune_started = true;
		if (init_do_spawn(&arg))
			return -1;
	} else {
		arg.dune_started = false;
		if (pthread_create(&pt, NULL, pthread_entry, &arg))
			return -1;
	}

	pthread_mutex_lock(&arg.ta_mtx);
	if (arg.ta_tid == 0)
		pthread_cond_wait(&arg.ta_cnd, &arg.ta_mtx);
	pthread_mutex_unlock(&arg.ta_mtx);

	pthread_mutex_destroy(&arg.ta_mtx);
	pthread_cond_destroy(&arg.ta_cnd);

	return arg.ta_tid;
}

static long dune_clone(struct dune_tf *tf)
{
	unsigned long fs;
	int rc;
	//unsigned long pc;

	//rdmsrl(MSR_GS_BASE, pc);

	if (ARG1(tf) != 0)
		return dune_pthread_create(tf);

	fs = dune_get_user_fs();

	rc = syscall(SYS_clone, ARG0(tf), ARG1(tf), ARG2(tf), ARG3(tf),
		     ARG4(tf));

	if (rc < 0)
		return -errno;

	if (rc == 0) {
		dune_enter();
		dune_set_user_fs(fs);
	}

	return rc;
}

static int check_iovec(struct iovec *iov, int num)
{
	if (check_extent(iov, sizeof(*iov) * num))
		return -EFAULT;

	while (num--) {
		if (check_extent(iov->iov_base, iov->iov_len))
			return -EFAULT;

		iov++;
	}

	return 0;
}

static int syscall_check_params(struct dune_tf *tf)
{
	void *ptr = NULL;
	uint64_t len = 0;
	char *str = NULL;
	int err = 0;

	switch (tf->rax) {
	case SYS_uname:
		ptr = (void *) ARG0(tf);
		len = sizeof(struct utsname);
		break;

	case SYS_arch_prctl:
		if (ARG0(tf) == ARCH_GET_FS) {
			ptr = (void *) ARG1(tf);
			len = sizeof(unsigned long);
		}
		break;

	case SYS_access:
	case SYS_open:
	case SYS_unlink:
		str = (char *) ARG0(tf);
		break;

	case SYS_openat:
		str = (char *) ARG1(tf);
		break;

	case SYS_getdents:
	case SYS_read:
	case SYS_write:
	case SYS_connect:
	case SYS_bind:
		ptr = (void *) ARG1(tf);
		len = ARG2(tf);
		break;

	case SYS_writev:
	case SYS_readv:
		err = check_iovec((struct iovec *) ARG1(tf), ARG2(tf));
		break;

	case SYS_stat:
	case SYS_lstat:
		str = (char *) ARG0(tf);
	case SYS_fstat:
		ptr = (void *) ARG1(tf);
		len = sizeof(struct stat);
		break;

	case SYS_statfs:
		str = (char *) ARG0(tf);
		ptr = (void *) ARG1(tf);
		len = sizeof(struct statfs);
		break;

	case SYS_time:
		ptr = (void *) ARG0(tf);
		len = sizeof(time_t);
		break;

	case SYS_epoll_ctl:
		ptr = (void *) ARG3(tf);
		len = sizeof(struct epoll_event);
		break;

	case SYS_epoll_wait:
		ptr = (void *) ARG1(tf);
		len = ARG2(tf) * sizeof(struct epoll_event);
		break;

	case SYS_setsockopt:
		ptr = (void *) ARG3(tf);
		len = ARG4(tf);
		break;

	case SYS_accept:
		if (ARG2(tf)) {
			socklen_t *l = (socklen_t *) ARG2(tf);

			if (check_extent(l, sizeof(*l)))
				err = -EFAULT;
			else {
				len = *l;
				ptr = (void *) ARG1(tf);
				assert(ptr);
			}
		}
		break;

	case SYS_fcntl:
		switch (ARG1(tf)) {
		case F_DUPFD:
		case F_DUPFD_CLOEXEC:
		case F_GETFD:
		case F_SETFD:
		case F_GETFL:
		case F_SETFL:
		case F_GETOWN:
		case F_SETOWN:
			break;

		case F_SETLKW:
		case F_GETLK:
		case F_SETLK:
			ptr = (void *) ARG2(tf);
			len = sizeof(struct flock);
			break;

		default:
			err = -EFAULT;
			break;
		}
		break;

	case SYS_ioctl:
		/* XXX unreliable */
		if (_IOC_DIR(ARG1(tf))) {
			ptr = (void *) ARG2(tf);
			len = _IOC_SIZE(ARG1(tf));
		}
		break;

	case SYS_setgroups:
		ptr = (void *) ARG1(tf);
		len = sizeof(gid_t) * ARG0(tf);
		break;

	case SYS_rt_sigaction:
		ptr = (void *) ARG1(tf);
		len = sizeof(struct sigaction);

		if (ARG2(tf)) {
			if (check_extent((void *) ARG2(tf), len)) {
				tf->rax = -EFAULT;
				return -1;
			}
		}
		break;
	case SYS_sigaltstack:
	case SYS_signalfd:
	case SYS_signalfd4:
		ptr = (void *) ARG1(tf);
		len = sizeof(sigset_t);
		break;
	case SYS_rt_sigpending:
		ptr = (void *) ARG0(tf);
		len = sizeof(sigset_t);
		break;
	case SYS_rt_sigprocmask:
		if (ARG1(tf)) {
			ptr = (void *) ARG1(tf);
			len = sizeof(sigset_t);
		}
		if (ARG2(tf)) {
			if (check_extent((void *) ARG2(tf), sizeof(sigset_t))) {
				tf->rax = -EFAULT;
				return -1;
			}
		}
		break;
	case SYS_rt_sigreturn:
		break;
	case SYS_rt_sigsuspend:
		ptr = (void *) ARG0(tf);
		len = sizeof(sigset_t);
		break;
	case SYS_rt_sigqueueinfo:
		ptr = (void *) ARG1(tf);
		len = sizeof(siginfo_t);
		break;
	case SYS_rt_sigtimedwait:
		if (check_extent((void *) ARG0(tf), sizeof(siginfo_t))) {
			tf->rax = -EFAULT;
			return -1;
		}
		if (check_extent((void *) ARG1(tf), sizeof(siginfo_t))) {
			tf->rax = -EFAULT;
			return -1;
		}
		if (ARG2(tf)) {
			if (check_extent((void *) ARG2(tf), sizeof(struct timespec))) {
				tf->rax = -EFAULT;
				return -1;
			}
		}
		break;

	/* umm_ checks for correctness */
	case SYS_brk:
		break;

	case SYS_mprotect:
	case SYS_munmap:
	case SYS_mmap:
//		ptr = (void*) ARG0(tf);
//		len = ARG1(tf);
		break;

	case SYS_getcwd:
		ptr = (void *) ARG0(tf);
		len = ARG1(tf);
		break;

	case SYS_getrlimit:
		ptr = (void *) ARG1(tf);
		len = sizeof(struct rlimit);
		break;

	case SYS_sendfile:
		ptr = (void *) ARG2(tf);
		len = sizeof(off_t);
		break;

	case SYS_getuid:
	case SYS_setuid:
	case SYS_getgid:
	case SYS_setgid:
	case SYS_getpid:
	case SYS_epoll_create:
	case SYS_dup2:
	case SYS_socket:
	case SYS_shutdown:
	case SYS_listen:
	case SYS_lseek:
		break;

	/* XXX - doesn't belong here */
	case SYS_close:
		if (ARG0(tf) < 3) {
			tf->rax = 0;
			return -1;
		}
		break;

	case SYS_execve: {
		char *p = (char *)ARG0(tf);

		if (check_string(p)) {
			err = -EFAULT;
			break;
		}

		// XXX: Check arrays

		break;
	}

	default:
#if 0
	{
		static FILE *_out;

		if (!_out)
			_out = fopen("/tmp/syscall.log", "w");

		fprintf(_out, "Syscall %d\n", tf->rax);
		fflush(_out);
	}
#endif
	break;
	}

	if (ptr != NULL && len != 0 && check_extent(ptr, len))
		err = -EFAULT;

	if (str != NULL && check_string(str))
		err = -EFAULT;

	if (err) {
		tf->rax = err;
		return -1;
	}

	return 0;
}

static void syscall_do_foreal(struct dune_tf *tf)
{
	switch (tf->rax) {
	case SYS_arch_prctl:
		switch (ARG0(tf)) {
		case ARCH_GET_FS:
			*((unsigned long *) ARG1(tf)) = dune_get_user_fs();
			tf->rax = 0;
			break;

		case ARCH_SET_FS:
			dune_set_user_fs(ARG1(tf));
			tf->rax = 0;
			break;

		default:
			tf->rax = -EINVAL;
			break;
		}
		break;

	case SYS_brk:
		tf->rax = umm_brk((unsigned long) ARG0(tf));
		break;

	case SYS_mmap:
		tf->rax = (unsigned long) umm_mmap((void *) ARG0(tf),
						   (size_t) ARG1(tf), (int) ARG2(tf), (int) ARG3(tf),
						   (int) ARG4(tf), (off_t) ARG5(tf));
		break;

	case SYS_mprotect:
		tf->rax = umm_mprotect((void *) ARG0(tf),
				       (size_t) ARG1(tf),
				       ARG2(tf));
		break;

	case SYS_munmap:
		tf->rax = umm_munmap((void *) ARG0(tf), (size_t) ARG1(tf));
		break;

	case SYS_mremap:
		tf->rax = (unsigned long) umm_mremap((void *) ARG0(tf),
						     (size_t) ARG1(tf), (size_t) ARG2(tf),
						     (int) ARG3(tf), (void *) ARG4(tf));
		break;

	case SYS_shmat:
		tf->rax = (unsigned long) umm_shmat((int) ARG0(tf),
						    (void *) ARG1(tf), (int) ARG2(tf));
		break;

	case SYS_clone:
		tf->rax = dune_clone(tf);
		break;

	case SYS_execve:
		tf->rax = exec_execev((const char *)ARG0(tf),
				      (char **const)ARG1(tf),
				      (char **const)ARG2(tf));
		break;


	/* ignore signals for now */
	case SYS_rt_sigaction:
	case SYS_rt_sigprocmask:
		tf->rax = 0;
		break;

	case SYS_sigaltstack:
	case SYS_signalfd:
	case SYS_signalfd4:
	case SYS_rt_sigpending:
	case SYS_rt_sigreturn:
	case SYS_rt_sigsuspend:
	case SYS_rt_sigqueueinfo:
	case SYS_rt_sigtimedwait:
		tf->rax = -ENOSYS;
		break;

	case SYS_exit_group:
	case SYS_exit:
		dune_ret_from_user(ARG0(tf));
		break;

	default:
		dune_passthrough_syscall(tf);
		break;
	}
}

static void syscall_do(struct dune_tf *tf)
{
	int need_lock = 0;

	switch (tf->rax) {
	case SYS_mmap:
	case SYS_mprotect:
	case SYS_munmap:
		need_lock = 1;
		break;
	}

	if (need_lock && pthread_mutex_lock(&_syscall_mtx))
		err(1, "pthread_mutex_lock()");

	syscall_do_foreal(tf);

	if (need_lock && pthread_mutex_unlock(&_syscall_mtx))
		err(1, "pthread_mutex_unlock()");
}

#define SYSCALL_START	0x100000
extern void do_syscall(struct dune_tf *tf, uint64_t sysnr);

static void syscall_handler(struct dune_tf *tf)
{
//	printf("Syscall No. %d\n", tf->rax);

	if (tf->rax >= SYSCALL_START)
		return do_syscall(tf, tf->rax - SYSCALL_START);

	if (syscall_check_params(tf) == -1)
		return;

	KSTATS_POP(NULL);
	KSTATS_PUSH(posix_syscall, NULL);
	syscall_do(tf);
	KSTATS_POP(NULL);
	KSTATS_PUSH(user, NULL);
}

int trap_init(void)
{
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);

	if (pthread_mutex_init(&_syscall_mtx, &attr))
		return -1;

	dune_register_syscall_handler(&syscall_handler);

	return 0;
}
