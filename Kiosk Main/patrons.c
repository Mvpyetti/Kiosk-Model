/*  
 *  Copyright (c) 2017, Jisoo Yang
 *
 *  This is written as part of CS 370 project
 *  
 *  Writen by Jisoo Yang <jisoo.yang@unlv.edu>  
 */

#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "oshow.h"

#define handle_error(msg) \
        do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error_en(en, msg) \
        do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)


#define TOTAL_FLOOR (100)
#define TOTAL_MEZZANINE (80)
#define TOTAL_BALCONY (120)
#define TOTAL_BOX (30)

#define FLO (0)
#define MEZ (1)
#define BAL (2)
#define BOX (3)
#define NUM_AREAS (4)

struct thread_info {
    int id;
    int fd_client;
    int fd_server;
    int roll;
    int flip;
    int sold_out[NUM_AREAS];
    int bought[NUM_AREAS];
};

// patron implementation
// there are 10 kiosks represented as named pipe 
// opening named pipes. 
int get_kiosk(int n, int* fd_client, int* fd_server)
{
    int fd_c, fd_s;
    char name[16];
    
    if (n < 0 || n > 9) handle_error_en(ERANGE, "kiosk number");

    sprintf(name, "kiosk-client%d", n);
    fd_c = open(name, O_WRONLY);
    if (fd_c == -1) handle_error("open (write)  failed");
    sprintf(name, "kiosk-server%d", n);
    fd_s = open(name, O_RDONLY);
    if (fd_s == -1) handle_error("open (read) failed");

    *fd_client = fd_c;
    *fd_server = fd_s;

    return 0;
}

ssize_t read_msg(struct thread_info* ti, int32_t* buf) {
    ssize_t n;
    n = read(ti->fd_server, buf, 4);
    if (n == 0) {
	fprintf(stderr, "pipe closed??\n");
	return n;
    }
    if (n != 4) handle_error("message (read) failed");
    return n;
}

ssize_t write_msg(struct thread_info* ti, int32_t val) {
    ssize_t n;
    n = write(ti->fd_client, &val, 4);
    if (n != 4) handle_error("message (write) failed");
    return n;
}

static inline int roll(struct thread_info* ti) {
    return ((ti->roll++)*31 + ti->id) % NUM_AREAS;
}
static inline int flip(struct thread_info* ti) {
    return ((ti->flip++)*17 + ti->id) % 2;
}
int all_sold_out(struct thread_info* ti) {
    return (ti->sold_out[FLO] && ti->sold_out[MEZ] 
	    && ti->sold_out[BAL] && ti->sold_out[BOX]);
}

void* patron_main(void* arg)
{
    struct thread_info* ti = (struct thread_info*)arg;
    int res, type;
    int msg, decision, seqno;

    res = get_kiosk(ti->id, &ti->fd_client, &ti->fd_server);
    if (res) handle_error("open_kiosk failed");

    while (1) {
	type = roll(ti);
	if (ti->sold_out[type]) continue;
	write_msg(ti, type+1);
	read_msg(ti, &msg);
	if (msg == TICKET_SOLD_OUT) {
	    ti->sold_out[type] = 1;
	    if (all_sold_out(ti)) break;
	    continue;
	}
	usleep(100);
	decision = flip(ti);
	if (decision == 0) { // accept
	    write_msg(ti, I_ACCEPT);
	    read_msg(ti, &seqno);
	    ti->bought[type]++;
	} else {
	    write_msg(ti, I_REJECT);
	    read_msg(ti, &seqno);
	    if (seqno != REJECT_ACKNOWLEDGED) {
		fprintf(stderr, "(%d) invalid sequence\n", ti->id);
		break;
	    }
	}

	// (thread id) [ticket category (0-3), ticket sequence #]
	printf("(%d) [%d, %d]\n", ti->id, type, seqno);
    }

    return ti;
}


#define NTHREADS (10)
int main(int argc, char** argv)
{
    int s, i;
    struct thread_info* ti;

    int ticket_sum[NUM_AREAS];

    pthread_t thread[NTHREADS];

    for (i = 0; i < NTHREADS; i++) {
	ti = malloc(sizeof(*ti));
	if (!ti) handle_error_en(ENOMEM, "malloc failed");
	ti->id = i;
	s = pthread_create(&thread[i], NULL, &patron_main, ti);
	if (s) handle_error_en(s, "pthread_create: ");
    }

    memset(ticket_sum, 0, sizeof(ticket_sum));
    for (i = 0; i < NTHREADS; i++) {
	s = pthread_join(thread[i], (void**)&ti);
	ticket_sum[0] += ti->bought[0];
	ticket_sum[1] += ti->bought[1];
	ticket_sum[2] += ti->bought[2];
	ticket_sum[3] += ti->bought[3];
    }
    
    for (i = 0; i < NUM_AREAS; i++) {
	printf("tickets bought[%d] = %d\n", i, ticket_sum[i]);
    }
    return 0;
}
