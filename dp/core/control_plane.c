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
 * control_plane.c - control plane implementation
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <ix/control_plane.h>
#include <ix/log.h>

volatile struct cp_shmem *cp_shmem;

DEFINE_PERCPU(volatile struct command_struct *, cp_cmd);

double energy_unit;

int cp_init(void)
{
	int fd, ret;
	void *vaddr;

	fd = shm_open("/ix", O_RDWR | O_CREAT | O_TRUNC, 0660);
	if (fd == -1)
		return 1;

	ret = ftruncate(fd, sizeof(struct cp_shmem));
	if (ret)
		return ret;

	vaddr = mmap(NULL, sizeof(struct cp_shmem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (vaddr == MAP_FAILED)
		return 1;

	cp_shmem = vaddr;

	bzero((void *)cp_shmem, sizeof(struct cp_shmem));
	cp_shmem->cycles_per_us = cycles_per_us;

	return 0;
}

void cp_idle(void)
{
	int fd, ret;
	char buf;

	percpu_get(cp_cmd)->cmd_id = CP_CMD_NOP;
	percpu_get(cp_cmd)->status = CP_STATUS_READY;
	percpu_get(cp_cmd)->cpu_state = CP_CPU_STATE_IDLE;
	fd = open((char *) percpu_get(cp_cmd)->idle.fifo, O_RDONLY);
	if (fd != -1) {
		ret = read(fd, &buf, 1);
		if (ret == -1)
			log_err("read on wakeup pipe returned -1 (errno=%d)\n", errno);
		close(fd);
	}
	percpu_get(cp_cmd)->cpu_state = CP_CPU_STATE_RUNNING;
	/* NOTE: reset timer position */
	timer_init_cpu();
}
