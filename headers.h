#ifndef HEADERS_H
#define HEADERS_H
#include <stdio.h>      
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



#ifdef HEADERS_IMPLEMENTATION
int *shmaddr = NULL;
#else
extern int *shmaddr;
#endif


typedef struct MemRequest
{
    int time;
    int address;
    char mode;
    char address_str[32];
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
    int cpu_time; 
    int blocked_until;
    PageTable page_table;
    struct MemRequest requests[100];
    int req_index;
    int req_count;

    
    int has_pending_page;
    int pending_page;
    int pending_frame;
    char pending_mode;
} PCB;

typedef struct sharedData
{
    bool is_finished;
} sharedData;

int getClk(void);
void initClk(void);
void destroyClk(bool terminateAll);


#ifdef HEADERS_IMPLEMENTATION

int getClk(void)
{
    return *shmaddr;
}

void initClk(void)
{
    int shmid = shmget(SHKEY, 4, 0444);
    while ((int)shmid == -1)
    {
        
        printf("Wait! The clock not initialized yet!\n");
        sleep(1);
        shmid = shmget(SHKEY, 4, 0444);
    }
    shmaddr = (int *) shmat(shmid, (void *)0, 0);
}


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