#include "headers.h"

struct processData
{
    int arrivaltime;
    int priority;
    int runningtime;
    int id;
};

struct msgbuff
{
    long mtype;
    struct processData p;
};

int finished_processes = 0;
int number_of_processes;

void HPF();
void RR(int quantum);
void FCFS_2CPU(int N, int M);

int main(int argc, char *argv[])
{
    initClk();
    // TODO implement the scheduler :)
    // upon termination release the clock resources.
    number_of_processes = atoi(argv[1]);
    int algo = atoi(argv[2]);
    int msg_id = msgget(MSGKEY, IPC_CREAT | 0666);
    struct msgbuff message;
    msgrcv(msg_id, &message, sizeof(struct processData), algo, !IPC_NOWAIT);
    if (message.mtype == 1)
    {
        // HPF
        HPF();
    }
    else if (message.mtype == 2)
    {
        // RR
        int quantum = atoi(argv[3]);
        RR(quantum);
    }
    else if (message.mtype == 3)
    {
        // 2CPU FCFS
        int N = atoi(argv[4]);
        int M = atoi(argv[5]);
        FCFS_2CPU(N, M);
    }

    exit(0);
    destroyClk(true);
}

void HPF()
{
    return;
}

void RR(int quantum)
{
    return;
}

void FCFS_2CPU(int N, int M)
{
    return;
}

