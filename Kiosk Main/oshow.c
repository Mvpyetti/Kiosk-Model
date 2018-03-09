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
#include <stdbool.h>

#include <pthread.h>

#include "oshow.h"

#define handle_error(msg) \
        do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error_en(en, msg) \
        do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)


// there are 10 kiosks represented as named pipe kiosk[0-9].
// opening named pipes. 
int open_kiosk(int n, int* fd_client, int* fd_server)
{
    int fd_c, fd_s;
    char name[16];
    
    if (n < 0 || n > 9) handle_error_en(ERANGE, "kiosk number");

    sprintf(name, "kiosk-client%d", n);
    fd_c = open(name, O_RDONLY);    // this will block until other end opens
    if (fd_c == -1) handle_error("open (read)  failed");
    sprintf(name, "kiosk-server%d", n);
    fd_s = open(name, O_WRONLY);
    if (fd_s == -1) handle_error("open (write) failed");

    *fd_client = fd_c;
    *fd_server = fd_s;

    return 0;
}

struct thread_info {
    int id;
    int fd_client;
    int fd_server;
};

#define TOTAL_FLOOR (100)
#define TOTAL_MEZZANINE (80)
#define TOTAL_BALCONY (120)
#define TOTAL_BOX (30)

#define FLO (0)
#define MEZ (1)
#define BAL (2)
#define BOX (3)
#define NUM_AREAS (4)

// our shared object
struct _tickets {
    int total[NUM_AREAS];
    int price[NUM_AREAS];
    bool soldOut[NUM_AREAS];
    int possibleBuy[NUM_AREAS];
    int seqNum;
    pthread_mutex_t seqLock;
} tickets;

pthread_mutex_t lock[NUM_AREAS];
pthread_cond_t cond[NUM_AREAS];

void tickets_init(void)
{
    memset(&tickets, 0, sizeof(tickets));
    tickets.total[FLO] = TOTAL_FLOOR;
    tickets.total[MEZ] = TOTAL_MEZZANINE;
    tickets.total[BAL] = TOTAL_BALCONY;
    tickets.total[BOX] = TOTAL_BOX;

    tickets.seqNum = 1000;

    tickets.price[FLO] = 150;
    tickets.price[MEZ] = 130;
    tickets.price[BAL] = 90;
    tickets.price[BOX] = 110;


    tickets.soldOut[FLO] = false;
    tickets.soldOut[MEZ] = false;
    tickets.soldOut[BAL] = false;
    tickets.soldOut[BOX] = false;

    tickets.possibleBuy[FLO] = 0;
    tickets.possibleBuy[MEZ] = 0;
    tickets.possibleBuy[BAL] = 0;
    tickets.possibleBuy[BOX] = 0;

    pthread_mutex_init(&lock[FLO], NULL);    
    pthread_mutex_init(&lock[MEZ], NULL);    
    pthread_mutex_init(&lock[BAL], NULL);    
    pthread_mutex_init(&lock[BOX], NULL);

    pthread_mutex_init(&tickets.seqLock, NULL);

    pthread_cond_init(&cond[FLO], NULL);
    pthread_cond_init(&cond[MEZ], NULL);
    pthread_cond_init(&cond[BAL], NULL);
    pthread_cond_init(&cond[BOX], NULL);
}
/*
 * read into buf a 4-byte message from pipe
 */
ssize_t read_msg(struct thread_info* ti, int32_t* buf) {
    ssize_t n;
    n = read(ti->fd_client, buf, 4);
    if (n == 0) {
	return n;   // pipe closed
    }
    if (n != 4) handle_error("message (read) failed");
    return n;
}

/*
 * write to pipe 4-byte message val
 */
ssize_t write_msg(struct thread_info* ti, int32_t val) {
    ssize_t n;
    n = write(ti->fd_server, &val, 4);
    if (n != 4) handle_error("message (write) failed");
    return n;
}


/*
 * main loop exit condition check
 * returns 1 if all tickets are sold out and all pipe closed.
 */
int check_all_sold_out(void) {

    if(tickets.soldOut[FLO] && tickets.soldOut[MEZ] && tickets.soldOut[BAL] && tickets.soldOut[BOX]) 
        return 1;
    else
        return 0;
}

void* kiosk_main(void* arg)
{
    struct thread_info* ti = (struct thread_info*)arg;
    int res;
    int msg;
    int decision;
    int localSeq;

    //Returns the fd of client and server for this specific thread
    res = open_kiosk(ti->id, &ti->fd_client, &ti->fd_server);
    if (res) handle_error("open_kiosk failed");
    
    while (1) {
	   msg = 0;
	   if (read_msg(ti, &msg) == 0) {
	    if (!check_all_sold_out()) {
		printf("(%d) operation failure.\n", ti->id);
	    }
	    break;
	}
	switch (msg) {
	   case QUOTE_ME_FLOOR:
	   case QUOTE_ME_MEZZANINE:
	   case QUOTE_ME_BALCONY:
	   case QUOTE_ME_BOX:
	{
        
        //Obtain a unique sequence code by incrementing sequence.
        //NOTE sequence is a global variabled shared by threads so we must 
        //include another lock to insure no threads have the same sequence #
        pthread_mutex_lock(&tickets.seqLock);
        localSeq = tickets.seqNum;
        tickets.seqNum++;
        pthread_mutex_unlock(&tickets.seqLock);

        //Create a critical section for checking the total- possibleBuy. 
        //We only want to keep the kiosk waiting if its possible that the ticket will be sold out and if there
        //are tickets even available. If neither of these conditions are met, then just continue with the sale.
        //NOTE: Four seperate locks were created for efficiency purposes. Each ticket sale is independant from one another.
        pthread_mutex_lock(&lock[msg-1]);
        while (tickets.total[msg-1] - tickets.possibleBuy[msg-1] <= 0 && tickets.soldOut[msg-1] == false) 
            pthread_cond_wait(&cond[msg-1], &lock[msg-1]);
        
        //Since the customer is not waiting, account for the fact that he may possibly buy the ticket. 
        tickets.possibleBuy[msg-1]++;
        pthread_mutex_unlock(&lock[msg-1]);

        //Make sure there are enough tickets, before quoting the customer
        if(tickets.total[msg-1] > 0) {

            //Send back a response to the customer with the quote.
            write_msg(ti, tickets.price[msg-1]);

            //Receive a response of whether the customer wants to buy or not.
            read_msg(ti, &decision);

            //If the customer wants to buy, then return the sequence number and reduce the amount of tickets by 1
            if(decision == I_ACCEPT) {
                tickets.total[msg-1]--;
                write_msg(ti, localSeq) ;

                //If there are no more tickets, then we need to say that it is soldout so that the waiting thread doesnt wait again for availability.
                if(tickets.total[msg-1] == 0) {
                    tickets.soldOut[msg-1] = true;
                }
            }
            else if(decision == I_REJECT) {
                //if the customer does not want to buy then write back a reject acknowledged
                write_msg(ti, REJECT_ACKNOWLEDGED);
            }
            
        }

        else {
            //Make sure we change the state of the ticket to sold out, so waiting threads do not wait for availability. 
            tickets.soldOut[msg-1] = true;
            write_msg(ti, TICKET_SOLD_OUT);
        }

        //The customer no longer has the possibility of buying or not since the transaction has finished, so take the customer out of the 
        //list of people who may currently buy.
        tickets.possibleBuy[msg-1]--;

        //Since the transaction is over, let other kiosks know that this transaction has finished.
        pthread_cond_signal(&cond[msg-1]);

	}
	break;
	default:
	    fprintf(stderr, "(%d) incorrect message seq.\n", ti->id);
	    break;
	}
    }

    return ti;
}

#define NTHREADS (10)
int main(int argc, char** argv)
{
    int s, i;
    void* res;

    pthread_t thread[NTHREADS];

    //Amount of tickets and prices are initiated to constants
    tickets_init();

    //For each thread 
    for (i = 0; i < NTHREADS; i++) {
	   struct thread_info* ti = malloc(sizeof(*ti));
	   if (!ti) 
        handle_error_en(ENOMEM, "malloc failed");
	   ti->id = i;
	   s = pthread_create(&thread[i], NULL, &kiosk_main, ti);
	   if (s) 
        handle_error_en(s, "pthread_create: ");
    }

    for (i = 0; i < NTHREADS; i++) {
	s = pthread_join(thread[i], &res);
	if (s) perror("pthread_join:");
	printf("thread %d joined\n", i);
    }
    
    printf("main done\n");
    return 0;
}
