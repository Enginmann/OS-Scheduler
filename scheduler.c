#include "headers.h"
#include "queue.h"
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>

struct processData
{
    int arrivaltime;
    int priority;
    int runningtime;
    int remainingtime;
    int id;
};

struct msgbuff
{
    long mtype;
    struct processData p;
};

// Global PCB and state variables
PCB pcbs[100];
int finished_processes = 0;
int number_of_processes;
int msg_id;
Queue *ready_queue;
int pcb_count = 0;
PCB *current = NULL;

PCB createPCB(struct processData p)
{
    PCB pcb;
    pcb.id = p.id;
    pcb.pid = 0;
    pcb.arrival = p.arrivaltime;
    pcb.runtime = p.runningtime;
    pcb.remaining = p.runningtime;
    pcb.priority = p.priority;
    return pcb;
}

PCB *getPCB(int id)
{
    for (int i = 0; i < pcb_count; i++)
    {
        if (pcbs[i].id == id)
        {
            return &pcbs[i];
        }
    }
    return NULL;
}

void context_switch(FILE *pFile, PCB *old, PCB *new)
{
    if (old != NULL && old->remaining > 0)
    {
        int waiting_time = getClk() - old->arrival - (old->runtime - old->remaining);
        fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tstopped\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\n", getClk(), old->id, old->arrival, old->runtime, old->remaining, waiting_time);
        kill(old->pid, SIGSTOP);
        printf("[TIME %d] CONTEXT SWITCH (1 sec)\n", getClk());
        sleep(1);
    }
    else if(old != NULL)
    {
        printf("[TIME %d] CONTEXT SWITCH (1 sec)\n", getClk());
        sleep(1);
    }
    if (new->pid == 0)
    {
        int waiting_time = getClk() - new->arrival - (new->runtime - new->remaining);
        fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tstarted\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\n", getClk(), new->id, new->arrival, new->runtime, new->remaining, waiting_time);
        int pid = fork();
        if (pid == 0)
        {
            char remaining_time[10];
            sprintf(remaining_time, "%d", new->remaining);
            execl("./process.out", "process.out", remaining_time, NULL);
        }
        else
        {
            new->pid = pid;
        }
    }
    else
    {
        int waiting_time = getClk() - new->arrival - (new->runtime - new->remaining);
        fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tresumed\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\n", getClk(), new->id, new->arrival, new->runtime, new->remaining, waiting_time);
        kill(new->pid, SIGCONT);
    }
}

void HPF(FILE *pFile)
{
    struct msgbuff message;
    ready_queue = createQueue();
    while (finished_processes < number_of_processes)
    {
        while (msgrcv(msg_id, &message, sizeof(struct processData), 1, IPC_NOWAIT) != -1)
        {
            printf("[TIME %d] Received P%d (arr=%d, run=%d, pri=%d)\n",
                   getClk(),
                   message.p.id,
                   message.p.arrivaltime,
                   message.p.runningtime,
                   message.p.priority);
            PCB pcb = createPCB(message.p);
            if (pcb.runtime == 0)
            {
                printf("[TIME %d] P%d finished immediately\n", getClk(), pcb.id);
                finished_processes++;
                int waiting_time = getClk() - current->arrival - current->runtime;
                int TA = getClk() - current->arrival;
                float WTA = round(((float)TA / current->runtime) * 100) / 100;
                current->waiting_time = waiting_time;
                current->WTA = WTA;
                fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tfinished\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\tTA\t%d\tWTA\t%.2f\n", getClk(), current->id, current->arrival, current->runtime, current->remaining, waiting_time, TA, WTA);
                continue;
            }
            pcbs[pcb_count++] = pcb;
            enqueuePri(ready_queue, pcb.id, pcb.priority);
            printf("[TIME %d] Enqueue P%d\n", getClk(), pcb.id);
            printQueue(ready_queue);
            if (current != NULL && pcb.priority < current->priority)
            {
                printf("[TIME %d] Preemption: P%d replaced by P%d\n",
                       getClk(),
                       current->id,
                       pcb.id);
                enqueuePri(ready_queue, current->id, current->priority);
                int id = dequeue(ready_queue);
                PCB *next = getPCB(id);
                context_switch(pFile, current, next);
                current = next;
            }
        }
        if (current == NULL && !isEmpty(ready_queue))
        {
            int id = dequeue(ready_queue);
            PCB *next = getPCB(id);
            printf("[TIME %d] Pick P%d from queue\n", getClk(), id);
            context_switch(pFile, NULL, next);
            current = next;
        }
        if (current != NULL)
        {
            printf("[TIME %d] Running P%d (remaining=%d)\n",
                   getClk(),
                   current->id,
                   current->remaining);
            sleep(1);
            current->remaining--;
            if (current->remaining == 0)
            {
                PCB *old = current;
                waitpid(current->pid, NULL, 0);
                finished_processes++;
                int waiting_time = getClk() - current->arrival - current->runtime;
                int TA = getClk() - current->arrival;
                float WTA = round(((float)TA / current->runtime) * 100) / 100;
                current->waiting_time = waiting_time;
                current->WTA = WTA;
                fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tfinished\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\tTA\t%d\tWTA\t%.2f\n", getClk(), current->id, current->arrival, current->runtime, current->remaining, waiting_time, TA, WTA);

                current = NULL;
                if (!isEmpty(ready_queue))
                {
                    int id = dequeue(ready_queue);
                    PCB *next = getPCB(id);
                    context_switch(pFile, old, next);
                    current = next;
                }
            }
        }
    }
    freeQueue(ready_queue);
}

////////////////////////////////////////////////////////////
// ROUND ROBIN ALGORITHM
////////////////////////////////////////////////////////////

void RR(FILE *pFile, int quantum)
{
    struct msgbuff message;
    ready_queue = createQueue();
    int quantum_counter = 0;

    current = NULL;

    while (finished_processes < number_of_processes)
    {
        // ===== RECEIVE =====
        while (msgrcv(msg_id, &message, sizeof(struct processData), 2, IPC_NOWAIT) != -1)
        {
            printf("[TIME %d] Received P%d (run=%d)\n",
                   getClk(), message.p.id, message.p.runningtime);

            PCB pcb = createPCB(message.p);

            // handle runtime 0
            if (pcb.runtime == 0)
            {
                printf("[TIME %d] P%d finished immediately\n", getClk(), pcb.id);
                finished_processes++;
                int waiting_time = getClk() - current->arrival - current->runtime;
                int TA = getClk() - current->arrival;
                float WTA = round(((float)TA / current->runtime) * 100) / 100;
                current->waiting_time = waiting_time;
                current->WTA = WTA;
                fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tfinished\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\tTA\t%d\tWTA\t%.2f\n", getClk(), current->id, current->arrival, current->runtime, current->remaining, waiting_time, TA, WTA);
                continue;
            }

            pcbs[pcb_count++] = pcb;
            enqueue(ready_queue, pcb.id);
        }

        // ===== PICK =====
        if (current == NULL && !isEmpty(ready_queue))
        {
            int id = dequeue(ready_queue);
            PCB *next = getPCB(id);

            printf("[TIME %d] Pick P%d\n", getClk(), id);

            context_switch(pFile, NULL, next);
            current = next;
            quantum_counter = 0;
        }

        // ===== RUN =====
        if (current != NULL)
        {
            printf("[TIME %d] Running P%d (remaining=%d, q=%d/%d)\n",
                   getClk(),
                   current->id,
                   current->remaining,
                   quantum_counter,
                   quantum);

            sleep(1);
            current->remaining--;
            quantum_counter++;

            // ===== FINISH =====
            if (current->remaining == 0)
            {
                PCB *old = current;
                printf("[TIME %d] FINISH P%d\n",
                       getClk(), current->id);

                waitpid(current->pid, NULL, 0);

                finished_processes++;
                int waiting_time = getClk() - current->arrival - current->runtime;
                int TA = getClk() - current->arrival;
                float WTA = round(((float)TA / current->runtime) * 100) / 100;
                current->waiting_time = waiting_time;
                current->WTA = WTA;
                fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tfinished\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\tTA\t%d\tWTA\t%.2f\n", getClk(), current->id, current->arrival, current->runtime, current->remaining, waiting_time, TA, WTA);
                current = NULL;
                quantum_counter = 0;
                if (!isEmpty(ready_queue))
                {
                    int id = dequeue(ready_queue);
                    PCB *next = getPCB(id);
                    context_switch(pFile, old, next);
                    current = next;
                }
            }

            // ===== QUANTUM EXPIRE =====
            else if (quantum_counter == quantum)
            {
                // 🔥 Only switch if there is another process
                if (!isEmpty(ready_queue))
                {
                    printf("[TIME %d] Quantum expired for P%d\n",
                           getClk(), current->id);

                    enqueue(ready_queue, current->id);

                    PCB *old = current;
                    int id = dequeue(ready_queue);
                    PCB *next = getPCB(id);

                    quantum_counter = 0;

                    context_switch(pFile, old, next);
                    current = next;
                }
                else
                {
                    // 🔥 NO SWITCH — just reset quantum
                    printf("[TIME %d] Quantum reset (only process)\n", getClk());

                    quantum_counter = 0;
                }
            }
        }
    }

    freeQueue(ready_queue);
}

////////////////////////////////////////////////////////////
// MAIN
////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
    initClk();

    if (argc < 4)
    {
        printf("Usage: ./scheduler.out <num_proc> <algo> <quantum>\n");
        destroyClk(true);
        return -1;
    }

    number_of_processes = atoi(argv[1]);
    int algo = atoi(argv[2]);
    int quantum = atoi(argv[3]);

    msg_id = msgget(MSGKEY, IPC_CREAT | 0666);

    FILE *pFile;
    pFile = fopen("schedulerLog.txt", "w");
    fprintf(pFile, "#At\ttime\tx\tprocess\ty\tstate\tarr\tw\ttotal\tz\tremain\ty\twait\tk\n");

    // FIX: removed the SIGCHLD handler — it was racing with our accounting logic.
    // Child cleanup is now done explicitly with waitpid() when remaining hits 0.
    if (algo == 1)
    {
        // HPF
        HPF(pFile);
    }
    else if (algo == 2)
    {
        // RR
        RR(pFile, quantum);
    }
    else
    {
        printf("Algorithm %d not implemented in this snippet.\n", algo);
    }
    fclose(pFile);
    int total_waiting_time = 0;
    float total_WTA_time = 0;
    int total_runtime = 0;
    float std_WTA = 0;
    for (int i = 0; i < number_of_processes; i++)
    {
        total_waiting_time += pcbs[i].waiting_time;
        total_WTA_time += pcbs[i].WTA;
        total_runtime += pcbs[i].runtime;
    }
    float avg_waiting_time = round(((float)total_waiting_time / number_of_processes) * 100) / 100;
    float avg_WTA_time = round(((float)total_WTA_time / number_of_processes) * 100) / 100;
    float cpu_utilization = round(((float)total_runtime / getClk()) * 10000) / 100;
    for (int i = 0; i < number_of_processes; i++)
    {
        std_WTA += pow(pcbs[i].WTA - avg_WTA_time, 2);
    }
    std_WTA = round((sqrt(std_WTA / number_of_processes)) * 100) / 100;
    pFile = fopen("schedulerPerf.txt", "w");
    fprintf(pFile, "CPU utilization = %.2f%%\n", cpu_utilization);
    fprintf(pFile, "Avg WTA = %.2f\n", avg_WTA_time);
    fprintf(pFile, "Avg Waiting = %.2f\n", avg_waiting_time);
    fprintf(pFile, "Std WTA = %.2f\n", std_WTA);
    fclose(pFile);
    printf("All processes finished. Cleaning up...\n");
    destroyClk(true);
    return 0;
}