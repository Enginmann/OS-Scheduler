#ifndef HEADERS_H
#define HEADERS_H
#include <stdio.h>      //if you don't use scanf/printf change this include
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>


typedef short bool;
#define true 1
#define false 0

#define SHKEY 300
#define MSGKEY 100

///==============================
//don't mess with this variable//
#ifdef HEADERS_IMPLEMENTATION
int *shmaddr = NULL;
#else
extern int *shmaddr;
#endif
//===============================

typedef struct MemRequest
{
    int time;
    int address;
    char mode;
} MemRequest;

typedef struct PageTableEntry
{
    int frame_number;
    int valid;
    int R;
    int M;
} PageTableEntry;

typedef struct PageTable
{
    PageTableEntry pages[1024];
    int page_table_frame;
} PageTable;

typedef struct PCB
{
    int id;
    int pid;
    int arrival;
    int runtime;
    int remaining;
    int priority;
    int waiting_time;
    float WTA;
    int base;
    int limit;
    int cpu_time; // used for request timing
    int blocked_until;
    PageTable page_table;
    struct MemRequest requests[100];
    int req_index;
    int req_count;
} PCB;

int getClk(void);
void initClk(void);
void destroyClk(bool terminateAll);


/*
 * All process call this function at the beginning to establish communication between them and the clock module.
 * Again, remember that the clock is only emulation!
*/
#ifdef HEADERS_IMPLEMENTATION

int getClk(void)
{
    return *shmaddr;
}

/*
 * All process call this function at the beginning to establish communication between them and the clock module.
 * Again, remember that the clock is only emulation!
*/
void initClk(void)
{
    int shmid = shmget(SHKEY, 4, 0444);
    while ((int)shmid == -1)
    {
        //Make sure that the clock exists
        printf("Wait! The clock not initialized yet!\n");
        sleep(1);
        shmid = shmget(SHKEY, 4, 0444);
    }
    shmaddr = (int *) shmat(shmid, (void *)0, 0);
}


/*
 * All process call this function at the end to release the communication
 * resources between them and the clock module.
 * Again, Remember that the clock is only emulation!
 * Input: terminateAll: a flag to indicate whether that this is the end of simulation.
 *                      It terminates the whole system and releases resources.
*/

void destroyClk(bool terminateAll)
{
    shmdt(shmaddr);
    if (terminateAll)
    {
        killpg(getpgrp(), SIGINT);
    }
}

#endif
#endif