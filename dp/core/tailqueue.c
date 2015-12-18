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
 * tailqueue.c -- measurement of the tail queue of a distribution
 *    used to track the latency in delivery of packets
 *
 *
 */

#include <stdint.h>
#include <math.h>
#include <strings.h>
#include <stdio.h>

#include <ix/tailqueue.h>

#define OVERLAP_FACTOR 2
#define NUM_BUCKETS (10*OVERLAP_FACTOR)
#define NUM_LEVELS 5
#define GRANULARITY_0  (10)  /* 10 microseconds */


typedef struct tailqueue {
	uint32_t count;
	uint64_t min;
	uint64_t max;
	uint32_t gran[NUM_LEVELS][NUM_BUCKETS];
	uint32_t overflow;
} tailqueue;

typedef struct taildistr taildistr;



void tailqueue_addsample(struct tailqueue *tq,
			 uint64_t t_us)
{
	int i;
	uint64_t x;
	uint64_t gran = GRANULARITY_0 * pow(10, NUM_LEVELS - 1);


	x = t_us / gran;
	if (x >= NUM_BUCKETS) {

		tq->overflow++;
	} else {
		for (i = NUM_LEVELS - 1; i >= 0; i--) {
			tq->gran[i][x]++;
			gran = gran / 10;
			x = t_us / gran;
			if (x >= NUM_BUCKETS)
				break;
		}
	}
	if (tq->count == 0) {
		tq->min = t_us;
		tq->max = t_us;
	} else {
		if (tq->min > t_us)
			tq->min = t_us;
		if (tq->max < t_us)
			tq->max = t_us;
	}
	tq->count++;
}



void tailqueue_calcnines(struct tailqueue *tq,
			 struct taildistr *td,
			 int reset)
{

	uint64_t above;
	uint64_t threshold;
	int nines = MAX_NINES;
	int cur_level, cur_bucket;
	bzero(td, sizeof(*td));
	if (tq->count == 0) {
		return;
	}
	td->count = tq->count;
	td->min = tq->min;
	td->max = tq->max;

	/*
	 * start with the max, starting with overflow; go down until you hit the 99.99, 99.9, 99% marks
	 */

	if (tq->overflow) {
		above = tq->overflow;
		for (; nines >= MIN_NINES; nines--) {
			threshold = tq->count / pow(10, nines);
			if (above >= threshold) {
				td->nines[nines] = tq->max;
			} else {
				break;
			}
		}
		cur_level = NUM_LEVELS - 1;
		cur_bucket = NUM_BUCKETS - 1;
	} else {
		uint64_t gran = GRANULARITY_0 * pow(10, NUM_LEVELS - 1);
		cur_bucket = tq->max / gran;
		cur_level = NUM_LEVELS - 1;
		above = 0;
	}

	for (; nines >= MIN_NINES; nines--) {
		threshold = tq->count / pow(10, nines);
		while (cur_level && cur_bucket) {
			if (cur_level && cur_bucket < OVERLAP_FACTOR) {
				cur_level--;
				cur_bucket = NUM_BUCKETS - 1;
			}
			above += tq->gran[cur_level][cur_bucket];
			if (above >= threshold) {
				td->nines[nines] = GRANULARITY_0 * (pow(10, cur_level) + cur_bucket) ;
				//printf("nines=%d above = %lu threshold=%lu level=%d bucket=%d val=%lu\n",
				//       nines,above,threshold,cur_level,cur_bucket,td->nines[nines]);
				break;
			}
			cur_bucket--;
		}
	}


	if (reset) {
		bzero(tq, sizeof(*tq));
	}
}



/*
 * debugging
 */

#ifdef notdef
main(int argc, char **argv)
{
	tailqueue tq;
	taildistr  td;
	int i;

	bzero(&tq, sizeof(tq));
	bzero(&td, sizeof(td));

	int n = atoi(argv[1]);
	printf("exp. distribution with %d samples \n", n);

	for (i = 0; i < n; i++) {
		float u = (random() % 1000000) / 1000000.0;
		float val = -log(u);
		uint64_t x = (uint64_t)(val * 1000000.0 * .5);
		printf("rand %5.3f %5.3f %lu \n", u, val, x);
		tailqueue_addsample(&tq, x);
	}

	for (i = 0; i < NUM_BUCKETS; i++) {
		printf("bucket %3d : ", i);
		int l;
		for (l = 0; l < NUM_LEVELS; l++) {
			printf(" %4d | ", tq.gran[l][i]);
		}
		printf("\n");
	}

	printf("overflow: %d\n", tq.overflow);
	tailqueue_calcnines(&tq, &td, 0);
	printf("count = %u min = %lu  max = %lu \n", n, td.min, td.max);
	for (i = MIN_NINES; i <= MAX_NINES; i++) {
		printf(" %dnines  %lu\n", i, td.nines[i]);
	}
}

#endif

