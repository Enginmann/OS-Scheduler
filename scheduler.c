#define HEADERS_IMPLEMENTATION
#include "headers.h"
#include "queue.h"
#include "MMU.h"
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
    int base;
    int limit;
};

struct msgbuff
{
    long mtype;
    struct processData p;
    int req_count;                   // 🔥 number of requests
    struct MemRequest requests[100]; // 🔥 requests of THIS process
};

// sharedData is defined in headers.h

PCB pcbs[100];
int finished_processes = 0;
int number_of_processes = 0;
int expected_processes = 0;
int msg_id;
Queue *ready_queue;
int pcb_count = 0;
PCB *current = NULL;
int assigned_cpu[100];
int finish_time[100];
sharedData *shared;
int shmid;
FILE *memFile;
Queue *blocked_queue;
int req_index = 0;
int req_count = 0;
struct MemRequest requests[1000];

// PCB createPCB(struct processData p)
// {
//     PCB pcb;
//     pcb.id = p.id;
//     pcb.pid = 0; // not started yet
//     pcb.arrival = p.arrivaltime;
//     pcb.runtime = p.runningtime;
//     pcb.remaining = p.runningtime;
//     pcb.priority = p.priority;
//     // Phase 2 additions
//     pcb.cpu_time = 0;
//     pcb.blocked_until = 0;
//     pcb.base = pcb.id * 100;
//     init_page_table(&pcb.page_table);
//     return pcb;
// }
PCB createPCB(struct processData p)
{
    PCB pcb;

    pcb.id = p.id;
    pcb.pid = 0;

    pcb.arrival = p.arrivaltime;
    pcb.runtime = p.runningtime;
    pcb.remaining = p.runningtime;
    pcb.priority = p.priority;
    pcb.cpu_time = 0;
    pcb.blocked_until = 0;
    pcb.base = p.base;
    pcb.limit = p.limit;
    pcb.waiting_time = 0;
    pcb.WTA = 0;
    init_page_table(&pcb.page_table);
    pcb.req_index = 0;
    pcb.req_count = 0;

    pcb.has_pending_page = 0;
    pcb.pending_page = -1;
    pcb.pending_frame = -1;
    pcb.pending_mode = 'r';

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
/*
void context_switch(FILE *pFile, FILE *pFile2, PCB *old, PCB *new, int number_of_cpu)
{
    if (old != NULL && old->remaining > 0)
    {
        int waiting_time = getClk() - old->arrival - (old->runtime - old->remaining);
        fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tstopped\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\n", getClk(), old->id, old->arrival, old->runtime, old->remaining, waiting_time);
        kill(old->pid, SIGSTOP);
        printf("[TIME %d] CONTEXT SWITCH (1 sec)\n", getClk());
        sleep(1);
    }
    else if (old != NULL)
    {
        printf("[TIME %d] CONTEXT SWITCH (1 sec)\n", getClk());
        sleep(1);
    }
    if (new->pid == 0)
    {
        printf("[TIME %d] START P%d\n", getClk(), new->id);
        if (pFile2 == NULL)
        {
            int waiting_time = getClk() - new->arrival - (new->runtime - new->remaining);
            fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tstarted\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\n", getClk(), new->id, new->arrival, new->runtime, new->remaining, waiting_time);
        }
        else
        {
            int waiting_time = getClk() - new->arrival - (new->runtime - new->remaining);
            if (number_of_cpu == 1)
            {
                fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tstarted\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\n", getClk(), new->id, new->arrival, new->runtime, new->remaining, waiting_time);
            }
            else
            {
                fprintf(pFile2, "At\ttime\t%d\tprocess\t%d\tstarted\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\n", getClk(), new->id, new->arrival, new->runtime, new->remaining, waiting_time);
            }
        }
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
*/
void context_switch(FILE *pFile, FILE *pFile2, PCB *old, PCB *new, int cpu)
{
    // ===== STOP OLD =====
    if (old != NULL && old->remaining > 0)
    {
        int waiting_time =
            getClk() - old->arrival - (old->runtime - old->remaining);

        fprintf(pFile,
                "At\ttime\t%d\tprocess\t%d\tstopped\tarr\t%d\t"
                "total\t%d\tremain\t%d\twait\t%d\n",
                getClk(), old->id, old->arrival,
                old->runtime, old->remaining, waiting_time);

        kill(old->pid, SIGSTOP);
        sleep(1); // context switch cost
    }
    else if (old != NULL)
    {
        sleep(1);
    }

    // ===== START / RESUME NEW =====
    int waiting_time =
        getClk() - new->arrival - (new->runtime - new->remaining);

    if (new->pid == 0)
    {
        fprintf(pFile,
                "At\ttime\t%d\tprocess\t%d\tstarted\tarr\t%d\t"
                "total\t%d\tremain\t%d\twait\t%d\n",
                getClk(), new->id, new->arrival,
                new->runtime, new->remaining, waiting_time);

        int pid = fork();

        if (pid == 0)
        {
            char rem[10];
            sprintf(rem, "%d", new->remaining);
            execl("./process.out", "process.out", rem, NULL);
        }
        else
        {
            new->pid = pid;
        }
    }
    else
    {
        fprintf(pFile,
                "At\ttime\t%d\tprocess\t%d\tresumed\tarr\t%d\t"
                "total\t%d\tremain\t%d\twait\t%d\n",
                getClk(), new->id, new->arrival,
                new->runtime, new->remaining, waiting_time);

        kill(new->pid, SIGCONT);
    }
}
/*
int access_memory(PCB *p, FILE *memFile)
{
    if (req_index >= req_count)
        return 1;

    if (requests[req_index].time != p->cpu_time)
        return 1;

    int va = requests[req_index].address;
    char mode = requests[req_index].mode;

    int page = va / PAGE_SIZE;

    PageTableEntry *entry = &p->page_table.pages[page];

    printf("[MMU] P%d requests VA=%d (page %d)\n", p->id, va, page);

    // HIT
    if (entry->valid)
    {
        printf("[MMU] HIT in frame %d\n", entry->frame_number);

        memory[entry->frame_number].R = 1;
        if (mode == 'w')
            memory[entry->frame_number].M = 1;

        req_index++;
        return 1;
    }

    // PAGE FAULT
    printf("[MMU] PAGE FAULT\n");

    fprintf(memFile, "PageFault upon VA %d from process %d\n", va, p->id);

    int frame = allocateFrame();

    if (frame == -1)
    {
        frame = nru_replace();

        if (memory[frame].M)
        {
            fprintf(memFile, "Swapping out page %d to disk\n", frame);
            sleep(10);
        }
    }
    else
    {
        fprintf(memFile, "Free Physical page %d allocated\n", frame);
    }

    sleep(10);

    memory[frame].occupied = 1;
    memory[frame].process_id = p->id;
    memory[frame].page_number = page;
    memory[frame].R = 1;
    memory[frame].M = (mode == 'w');

    entry->valid = 1;
    entry->frame_number = frame;

    fprintf(memFile,
            "At time %d disk address %d for process %d is loaded into memory page %d\n",
            getClk(), p->base + page, p->id, frame);

    req_index++;
    return 0;
}
*/
int access_memory(PCB *p, FILE *memFile)
{
    if (p->req_index >= p->req_count)
        return 1;

    if (p->requests[p->req_index].time != p->cpu_time)
        return 1;

    int va = p->requests[p->req_index].address;
    char mode = p->requests[p->req_index].mode;

    int page = va / PAGE_SIZE;

    PageTableEntry *entry = &p->page_table.pages[page];

    printf("[MMU] P%d requests VA=%d\n", p->id, va);

    // HIT
    if (entry->valid)
    {
        sleep(1); // memory access
        p->req_index++;
        return 1;
    }

    // PAGE FAULT
    fprintf(memFile, "PageFault from P%d at VA %d\n", p->id, va);

    int frame = allocateFrame();

    if (frame == -1)
    {
        frame = selectVictimNRU();

        if (memory[frame].M)
        {
            sleep(10); // swap out
        }
    }

    sleep(10); // load from disk

    entry->valid = 1;
    entry->frame_number = frame;

    p->req_index++;

    return 0; // BLOCK
}

// void check_blocked()
// {
//     QNode *cur = blocked_queue->front;
//     QNode *next;
//     while (cur)
//     {
//         next = cur->next; // save next BEFORE removal
//         PCB *p = getPCB(cur->id);
//         if (getClk() >= p->blocked_until)
//         {
//             printf("[TIME %d] UNBLOCK P%d\n", getClk(), p->id);

//             enqueue(ready_queue, p->id);
//             removeFromQueue(blocked_queue, p->id);
//         }
//         cur = cur->next;
//     }
// }
void check_blocked(FILE *pFile)
{
    QNode *cur = blocked_queue->front;
    QNode *next;

    while (cur)
    {
        next = cur->next;

        PCB *p = getPCB(cur->id);

        if (getClk() >= p->blocked_until)
        {
            printf("[TIME %d] UNBLOCK P%d\n", getClk(), p->id);

            fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tresumed\n",
                    getClk(), p->id);

            enqueue(ready_queue, p->id);
            removeFromQueue(blocked_queue, p->id);
        }

        cur = next;
    }
}

void HPF(FILE *pFile)
{
    struct msgbuff message;
    ready_queue = createQueue();
    shmid = shmget(SHKEY + 10, sizeof(struct sharedData), 0666);
    while (shmid == -1)
    {
        sleep(1);
        shmid = shmget(SHKEY + 10, sizeof(struct sharedData), 0666);
    }
    shared = (struct sharedData *)shmat(shmid, NULL, 0);
    while (finished_processes < expected_processes)
    {
        while (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), 1, IPC_NOWAIT) != -1)
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
            // preemption check
            if (current != NULL && pcb.priority < current->priority)
            {
                printf("[TIME %d] Preemption: P%d replaced by P%d\n",
                       getClk(),
                       current->id,
                       pcb.id);
                enqueuePri(ready_queue, current->id, current->priority);
                int id = dequeue(ready_queue);
                PCB *next = getPCB(id);
                context_switch(pFile, NULL, current, next, 1);
                current = next;
            }
        }
        if (current == NULL && !isEmpty(ready_queue))
        {
            int id = dequeue(ready_queue);
            PCB *next = getPCB(id);
            printf("[TIME %d] Pick P%d from queue\n", getClk(), id);
            context_switch(pFile, NULL, NULL, next, 1);
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
                while (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), 2, IPC_NOWAIT) != -1)
                {
                    printf("[TIME %d] Received P%d (run=%d)\n",
                           getClk(),
                           message.p.id,
                           message.p.runningtime);

                    PCB pcb = createPCB(message.p);
                    if (pcb.runtime == 0)
                    {
                        printf("[TIME %d] P%d finished immediately\n",
                               getClk(),
                               pcb.id);

                        finished_processes++;
                        continue;
                    }

                    pcbs[pcb_count++] = pcb;
                    enqueuePri(ready_queue, pcb.id, pcb.priority);
                }
                if (!isEmpty(ready_queue))
                {
                    int id = dequeue(ready_queue);
                    PCB *next = getPCB(id);
                    context_switch(pFile, NULL, old, next, 1);
                    current = next;
                }
            }
        }
    }
    freeQueue(ready_queue);
}
/*
void RR(FILE *pFile, int quantum, int k)
{
    struct msgbuff message;
    ready_queue = createQueue();
    int quantum_counter = 0;
    current = NULL;
    shmid = shmget(SHKEY + 10, sizeof(struct sharedData), 0666);
    while (shmid == -1)
    {
        sleep(1);
        shmid = shmget(SHKEY + 10, sizeof(struct sharedData), 0666);
    }
    shared = (struct sharedData *)shmat(shmid, NULL, 0);
    memFile = fopen("memory.log", "w");
    if (!memFile)
    {
        perror("fopen(memory.log)");
        return;
    }
    setvbuf(memFile, NULL, _IOLBF, 0);

    fprintf(memFile, "# PageFault upon VA \"xx\" from process \"yy\"\n\n");
    fprintf(memFile, "# Free Physical page \"ZZ\" allocated\n\n");
    fprintf(memFile, "# Swapping out page \"ZZ\" to disk\n\n");
    fprintf(memFile, "# At time \"i\" disk address \"x\" for process \"y\" is loaded into memory page \"ZZ\".\n\n");
    fflush(memFile);
    while (finished_processes < expected_processes)
    {
        check_blocked(pFile);
        while (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), 2, IPC_NOWAIT) != -1)
        {
            printf("[TIME %d] Received P%d (run=%d)\n",
                   getClk(), message.p.id, message.p.runningtime);

            PCB pcb = createPCB(message.p);
            number_of_processes++;
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

        if (current == NULL && !isEmpty(ready_queue))
        {
            int id = dequeue(ready_queue);
            PCB *next = getPCB(id);

            printf("[TIME %d] Pick P%d\n", getClk(), id);

            context_switch(pFile, NULL, NULL, next, 1);
            current = next;
            quantum_counter = 0;
        }
        // if (current != NULL)
        // {
        //     printf("[TIME %d] Running P%d (remaining=%d, q=%d/%d)\n",
        //            getClk(),
        //            current->id,
        //            current->remaining,
        //            quantum_counter,
        //            quantum);

        //     sleep(1);
        //     current->remaining--;
        //     quantum_counter++;
        //     if (current->remaining == 0)
        //     {
        //         PCB *old = current;
        //         printf("[TIME %d] FINISH P%d\n",
        //                getClk(), current->id);

        //         waitpid(current->pid, NULL, 0);
        //         finished_processes++;
        //         int waiting_time = getClk() - current->arrival - current->runtime;
        //         int TA = getClk() - current->arrival;
        //         float WTA = round(((float)TA / current->runtime) * 100) / 100;
        //         current->waiting_time = waiting_time;
        //         current->WTA = WTA;

        //         fprintf(pFile,
        //                 "At\ttime\t%d\tprocess\t%d\tfinished\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\tTA\t%d\tWTA\t%.2f\n",
        //                 getClk(),
        //                 current->id,
        //                 current->arrival,
        //                 current->runtime,
        //                 current->remaining,
        //                 waiting_time,
        //                 TA,
        //                 WTA);

        //         current = NULL;
        //         quantum_counter = 0;
        //         while (msgrcv(msg_id, &message, sizeof(struct processData), 2, IPC_NOWAIT) != -1)
        //         {
        //             printf("[TIME %d] Received P%d (run=%d)\n",
        //                    getClk(),
        //                    message.p.id,
        //                    message.p.runningtime);

        //             PCB pcb = createPCB(message.p);
        //             number_of_processes++;
        //             if (pcb.runtime == 0)
        //             {
        //                 printf("[TIME %d] P%d finished immediately\n",
        //                        getClk(),
        //                        pcb.id);

        //                 finished_processes++;
        //                 continue;
        //             }

        //             pcbs[pcb_count++] = pcb;
        //             enqueue(ready_queue, pcb.id);
        //         }
        //         if (!isEmpty(ready_queue))
        //         {
        //             int id = dequeue(ready_queue);
        //             PCB *next = getPCB(id);

        //             context_switch(pFile, NULL, old, next, 1);

        //             current = next;
        //         }
        //     }

        //     else if (quantum_counter == quantum)
        //     {
        //         while (msgrcv(msg_id, &message, sizeof(struct processData), 2, IPC_NOWAIT) != -1)
        //         {
        //             printf("[TIME %d] Received P%d (run=%d)\n",
        //                    getClk(), message.p.id, message.p.runningtime);

        //             PCB pcb = createPCB(message.p);
        //             number_of_processes++;
        //             if (pcb.runtime == 0)
        //             {
        //                 printf("[TIME %d] P%d finished immediately\n", getClk(), pcb.id);
        //                 finished_processes++;
        //                 continue;
        //             }

        //             pcbs[pcb_count++] = pcb;
        //             enqueue(ready_queue, pcb.id);
        //         }

        //         if (!isEmpty(ready_queue))
        //         {
        //             printf("[TIME %d] Quantum expired for P%d\n",
        //                    getClk(), current->id);

        //             enqueue(ready_queue, current->id);
        //             PCB *old = current;
        //             int id = dequeue(ready_queue);
        //             PCB *next = getPCB(id);
        //             context_switch(pFile, NULL, old, next, 1);

        //             current = next;
        //         }
        //         else
        //         {
        //             printf("No other processes, continue\n");
        //         }
        //         quantum_counter = 0;
        //     }
        // }
        if (current != NULL)
        {
            printf("[TIME %d] Running P%d\n", getClk(), current->id);

            current->cpu_time++;

            // MEMORY ACCESS
            if (!access_memory(current, memFile))
            {
                current->blocked_until = getClk() + 10;

                printf("[TIME %d] BLOCK P%d until %d\n", getClk(), current->id, current->blocked_until);

                enqueue(blocked_queue, current->id);
                current = NULL;
                continue;
            }

            sleep(1);
            current->remaining--;

            if (current->remaining == 0)
            {
                printf("[TIME %d] FINISH P%d\n", getClk(), current->id);
                finished_processes++;
                current = NULL;
            }
        }
    }
    fclose(memFile);
    freeQueue(ready_queue);
}
*/
void RR(FILE *pFile, int quantum, int k)
{
    struct msgbuff message;

    ready_queue = createQueue();
    blocked_queue = createQueue();

    int quantum_counter = 0;
    current = NULL;

    shmid = shmget(SHKEY + 10, sizeof(sharedData), 0666);
    while (shmid == -1)
    {
        sleep(1);
        shmid = shmget(SHKEY + 10, sizeof(sharedData), 0666);
    }
    shared = (sharedData *)shmat(shmid, NULL, 0);

    memFile = fopen("memory.log", "w");
    if (!memFile)
    {
        perror("fopen(memory.log)");
        return;
    }
    setvbuf(memFile, NULL, _IOLBF, 0);

    fprintf(memFile, "# PageFault upon VA \"xx\" from process \"yy\"\n\n");
    fprintf(memFile, "# Free Physical page \"ZZ\" allocated\n\n");
    fprintf(memFile, "# Swapping out page \"ZZ\" to disk\n\n");
    fprintf(memFile, "# At time \"i\" disk address \"x\" for process \"y\" is loaded into memory page \"ZZ\".\n\n");
    fflush(memFile);

    initMemory();

    int mem_accesses = 0;

    while (1)
    {
        // =========================
        // UNBLOCK PROCESSES
        // =========================
        QNode *cur = blocked_queue->front;
        QNode *next;

        while (cur)
        {
            next = cur->next;
            PCB *p = getPCB(cur->id);

            if (getClk() >= p->blocked_until)
            {
                printf("[TIME %d] UNBLOCK P%d\n", getClk(), p->id);

                if (p->has_pending_page)
                {
                    swapIn(p, p->pending_page, p->pending_frame, p->pending_mode, memFile);
                    p->has_pending_page = 0;
                }

                fprintf(pFile,
                        "At\ttime\t%d\tprocess\t%d\tresumed\n",
                        getClk(), p->id);

                enqueue(ready_queue, p->id);
                removeFromQueue(blocked_queue, p->id);
            }
            cur = next;
        }

        // =========================
        // RECEIVE NEW PROCESSES
        // =========================
        while (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), 2, IPC_NOWAIT) != -1)
        {
            PCB pcb = createPCB(message.p);

            pcb.req_count = message.req_count;
            pcb.req_index = 0;

            for (int i = 0; i < pcb.req_count; i++)
            {
                pcb.requests[i] = message.requests[i];
            }

            // Create per-process page table frame
            createPageTable(&pcb, memFile);

            pcbs[pcb_count++] = pcb;
            enqueue(ready_queue, pcb.id);
        }

        // =========================
        // TERMINATION CHECK
        // =========================
        if (shared->is_finished && current == NULL && isEmpty(ready_queue) && isEmpty(blocked_queue))
            break;

        // =========================
        // PICK PROCESS
        // =========================
        if (current == NULL && !isEmpty(ready_queue))
        {
            int id = dequeue(ready_queue);
            PCB *next = getPCB(id);

            context_switch(pFile, NULL, NULL, next, 1);
            current = next;
            quantum_counter = 0;
        }

        // =========================
        // EXECUTION
        // =========================
        if (current != NULL)
        {
            printf("[TIME %d] Running P%d\n", getClk(), current->id);

            current->cpu_time++;

            // =========================
            // MEMORY ACCESS
            // =========================
            if (current->req_index < current->req_count && current->requests[current->req_index].time == current->cpu_time)
            {
                int va = current->requests[current->req_index].address;
                char mode = current->requests[current->req_index].mode;

                int frame = -1;
                int hit = handleMemoryRequest(current, va, mode, memFile, &frame);
                mem_accesses++;
                if (k > 0 && (mem_accesses % k) == 0)
                    clear_R_bits();

                if (hit)
                {
                    // Simulated memory access takes 1 second; child should not run during it
                    kill(current->pid, SIGSTOP);
                    sleep(1);
                    kill(current->pid, SIGCONT);
                    current->req_index++;
                }
                else
                {
                    // Page fault: block for 10 seconds and finish swapIn on UNBLOCK
                    kill(current->pid, SIGSTOP);

                    int page = va / PAGE_SIZE;
                    current->has_pending_page = 1;
                    current->pending_page = page;
                    current->pending_frame = frame;
                    current->pending_mode = mode;

                    current->req_index++; // consume this request

                    current->blocked_until = getClk() + 10;

                    fprintf(pFile,
                            "At\ttime\t%d\tprocess\t%d\tblocked\n",
                            getClk(), current->id);

                    enqueue(blocked_queue, current->id);
                    current = NULL;

                    // schedule another immediately
                    if (!isEmpty(ready_queue))
                    {
                        int id = dequeue(ready_queue);
                        PCB *next = getPCB(id);

                        // Context switch overhead (switching from one process to another)
                        sleep(1);

                        context_switch(pFile, NULL, NULL, next, 1);
                        current = next;
                    }

                    continue;
                }
            }

            // =========================
            // CPU EXECUTION (1 sec)
            // =========================
            sleep(1);
            current->remaining--;
            quantum_counter++;

            // =========================
            // FINISH
            // =========================
            if (current->remaining == 0)
            {
                printf("[TIME %d] FINISH P%d\n",
                       getClk(), current->id);

                freeProcessMemory(current);

                waitpid(current->pid, NULL, 0);

                PCB *old = current;

                int TA = getClk() - current->arrival;
                int waiting_time = TA - current->runtime;
                float WTA = (float)TA / current->runtime;

                current->waiting_time = waiting_time;
                current->WTA = WTA;

                fprintf(pFile,
                        "At\ttime\t%d\tprocess\t%d\tfinished\tarr\t%d\t"
                        "total\t%d\tremain\t0\twait\t%d\tTA\t%d\tWTA\t%.2f\n",
                        getClk(),
                        current->id,
                        current->arrival,
                        current->runtime,
                        waiting_time,
                        TA,
                        WTA);

                finished_processes++;
                current = NULL;
                quantum_counter = 0;

                // Immediately dispatch next ready process with context switch overhead
                if (!isEmpty(ready_queue))
                {
                    int id = dequeue(ready_queue);
                    PCB *next = getPCB(id);

                    // Context switch overhead (switching from one process to another)
                    sleep(1);

                    context_switch(pFile, NULL, NULL, next, 1);
                    current = next;
                }
            }

            // =========================
            // QUANTUM EXPIRE
            // =========================
            else if (quantum_counter == quantum)
            {
                printf("[TIME %d] Quantum expired for P%d\n",
                       getClk(), current->id);

                enqueue(ready_queue, current->id);

                PCB *old = current;
                current = NULL;

                if (!isEmpty(ready_queue))
                {
                    int id = dequeue(ready_queue);
                    PCB *next = getPCB(id);
                    context_switch(pFile, NULL, old, next, 1);
                    current = next;
                }

                quantum_counter = 0;
            }
        }
    }

    fclose(memFile);
    freeQueue(ready_queue);
    freeQueue(blocked_queue);
}

int totalRunTime(Queue *q, PCB *cpu_current)
{
    int total = cpu_current ? cpu_current->remaining : 0;
    QNode *cur = q->front;
    while (cur)
    {
        PCB *pcb = getPCB(cur->id);
        total += pcb->remaining;
        cur = cur->next;
    }
    return total;
}

void twoCPUWithFCFS(FILE *pFile, FILE *pFile2, int N, int M)
{
    Queue *cpu1 = createQueue();
    Queue *cpu2 = createQueue();
    PCB *cpu1_current = NULL;
    PCB *cpu2_current = NULL;
    bool switch1 = false;
    bool switch2 = false;
    int returnFromSwitch1 = 0;
    int returnFromSwitch2 = 0;
    struct msgbuff message;
    int last_clk = 0;
    shmid = shmget(SHKEY + 10, sizeof(struct sharedData), 0666);
    while (shmid == -1)
    {
        sleep(1);
        shmid = shmget(SHKEY + 10, sizeof(struct sharedData), 0666);
    }
    shared = (struct sharedData *)shmat(shmid, NULL, 0);
    while (finished_processes < expected_processes)
    {
        while (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), 3, IPC_NOWAIT) != -1)
        {
            printf("[TIME %d] Received P%d (run=%d)\n",
                   getClk(), message.p.id, message.p.runningtime);
            PCB pcb = createPCB(message.p);
            pcbs[pcb_count++] = pcb;
            int size_1 = getSize(cpu1);
            int size_2 = getSize(cpu2);
            if (size_1 <= size_2)
            {
                enqueue(cpu1, pcb.id);
                assigned_cpu[pcb.id] = 1;
                printf("[TIME %d] Enqueue P%d to CPU 1\n", getClk(), pcb.id);
            }
            else
            {
                enqueue(cpu2, pcb.id);
                assigned_cpu[pcb.id] = 2;
                printf("[TIME %d] Enqueue P%d to CPU 2\n", getClk(), pcb.id);
            }
            printQueue(cpu1);
            printQueue(cpu2);
        }
        if (getClk() - last_clk >= N)
        {
            printf("[TIME %d] Checking load balance...\n", getClk());
            int total1 = totalRunTime(cpu1, cpu1_current);
            int total2 = totalRunTime(cpu2, cpu2_current);
            printf("CPU1 total=%d, CPU2 total=%d\n", total1, total2);
            while (abs(total1 - total2) > M)
            {
                printf("[TIME %d] STEAL START (3 sec)\n", getClk());
                if (total1 > total2 && !isEmpty(cpu1))
                {
                    int id = dequeueLast(cpu1);
                    printf("[TIME %d] Moving P%d from CPU 1 to CPU 2\n", getClk(), id);
                    fprintf(pFile, "At\ttime\t%d\tprocess\t%d\twas\tstolen\n", getClk(), id);
                    enqueue(cpu2, id);
                    assigned_cpu[id] = 2;
                    sleep(3);
                }
                else if (total2 > total1 && !isEmpty(cpu2))
                {
                    int id = dequeueLast(cpu2);
                    printf("[TIME %d] Moving P%d from CPU 2 to CPU 1\n", getClk(), id);
                    fprintf(pFile2, "At\ttime\t%d\tprocess\t%d\twas\tstolen\n", getClk(), id);
                    enqueue(cpu1, id);
                    assigned_cpu[id] = 1;
                    sleep(3);
                }
                else
                {
                    printf("[TIME %d] No steal possible\n", getClk());
                    break;
                }
                total1 = totalRunTime(cpu1, cpu1_current);
                total2 = totalRunTime(cpu2, cpu2_current);
                printf("CPU1 total=%d, CPU2 total=%d\n", total1, total2);
            }
            last_clk = getClk();
        }
        if (cpu1_current == NULL && !isEmpty(cpu1))
        {
            int id = dequeue(cpu1);
            printf("[TIME %d] CPU1 Pick P%d\n", getClk(), id);
            PCB *next = getPCB(id);
            context_switch(pFile, pFile2, NULL, next, 1);
            cpu1_current = next;
        }
        if (cpu1_current != NULL)
        {
            printf("[TIME %d] CPU1 Running P%d (rem=%d)\n",
                   getClk(), cpu1_current->id, cpu1_current->remaining);
            cpu1_current->remaining--;
            if (cpu1_current->remaining < 0)
            {
                cpu1_current->remaining = 0;
                PCB *old = cpu1_current;
                waitpid(cpu1_current->pid, NULL, 0);
                switch1 = true;
                returnFromSwitch1 = getClk() + 1;
                finish_time[cpu1_current->id] = getClk();
                finished_processes++;
                printf("finished processes: %d\n", finished_processes);
                int waiting_time = getClk() - cpu1_current->arrival - cpu1_current->runtime;
                int TA = getClk() - cpu1_current->arrival;
                float WTA = round(((float)TA / cpu1_current->runtime) * 100) / 100;
                cpu1_current->waiting_time = waiting_time;
                cpu1_current->WTA = WTA;
                fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tfinished\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\tTA\t%d\tWTA\t%.2f\n", getClk(), cpu1_current->id, cpu1_current->arrival, cpu1_current->runtime, cpu1_current->remaining, waiting_time, TA, WTA);
                printf("[TIME %d] CPU1 FINISH P%d\n", getClk(), cpu1_current->id);
                cpu1_current = NULL;
                if (!isEmpty(cpu1))
                {
                    if (!switch1)
                    {
                        int id = dequeue(cpu1);
                        PCB *next = getPCB(id);
                        int waiting_time = getClk() - next->arrival - (next->runtime - next->remaining);
                        fprintf(pFile, "At\ttime\t%d\tprocess\t%d\tstarted\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\n", getClk(), next->id, next->arrival, next->runtime, next->remaining, waiting_time);
                        int pid = fork();
                        if (pid == 0)
                        {
                            char remaining_time[10];
                            sprintf(remaining_time, "%d", next->remaining);
                            execl("./process.out", "process.out", remaining_time, NULL);
                        }
                        cpu1_current = next;
                    }
                    else if (returnFromSwitch1 <= getClk())
                    {
                        switch1 = false;
                    }
                }
            }
        }
        if (cpu2_current == NULL && !isEmpty(cpu2))
        {
            int id = dequeue(cpu2);
            printf("[TIME %d] CPU2 Pick P%d\n", getClk(), id);
            PCB *next = getPCB(id);
            context_switch(pFile, pFile2, NULL, next, 2);
            cpu2_current = next;
        }
        if (cpu2_current != NULL)
        {
            printf("[TIME %d] CPU2 Running P%d (rem=%d)\n",
                   getClk(), cpu2_current->id, cpu2_current->remaining);
            cpu2_current->remaining--;
            if (cpu2_current->remaining < 0)
            {
                cpu2_current->remaining = 0;
                PCB *old = cpu2_current;
                waitpid(cpu2_current->pid, NULL, 0);
                switch2 = true;
                returnFromSwitch2 = getClk() + 1;
                finish_time[cpu2_current->id] = getClk();
                finished_processes++;
                printf("finished processes: %d\n", finished_processes);
                int waiting_time = getClk() - cpu2_current->arrival - cpu2_current->runtime;
                int TA = getClk() - cpu2_current->arrival;
                float WTA = round(((float)TA / cpu2_current->runtime) * 100) / 100;
                cpu2_current->waiting_time = waiting_time;
                cpu2_current->WTA = WTA;
                fprintf(pFile2, "At\ttime\t%d\tprocess\t%d\tfinished\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\tTA\t%d\tWTA\t%.2f\n", getClk(), cpu2_current->id, cpu2_current->arrival, cpu2_current->runtime, cpu2_current->remaining, waiting_time, TA, WTA);
                printf("[TIME %d] CPU2 FINISH P%d\n", getClk(), cpu2_current->id);
                cpu2_current = NULL;
                if (!isEmpty(cpu2))
                {
                    if (!switch2)
                    {
                        int id = dequeue(cpu2);
                        PCB *next = getPCB(id);
                        int waiting_time = getClk() - next->arrival - (next->runtime - next->remaining);
                        fprintf(pFile2, "At\ttime\t%d\tprocess\t%d\tstarted\tarr\t%d\ttotal\t%d\tremain\t%d\twait\t%d\n", getClk(), next->id, next->arrival, next->runtime, next->remaining, waiting_time);
                        int pid = fork();
                        if (pid == 0)
                        {
                            char remaining_time[10];
                            sprintf(remaining_time, "%d", next->remaining);
                            execl("./process.out", "process.out", remaining_time, NULL);
                        }
                        cpu2_current = next;
                    }
                    else if (returnFromSwitch2 <= getClk())
                    {
                        switch2 = false;
                    }
                }
            }
        }
        sleep(1);
    }
    freeQueue(cpu1);
    freeQueue(cpu2);
}

void handler(int signum)
{
    printf("Received SIGINT, exiting...\n");
    if (shared && shared != (void *)-1)
        shmdt(shared);
    exit(0);
}

int main(int argc, char *argv[])
{
    initClk();

    if (argc < 7)
    {
        printf("Usage: ./scheduler.out <num_proc> <algo> <quantum> <k> <N> <M>\n");
        destroyClk(false);
        return -1;
    }
    signal(SIGINT, handler);
    number_of_processes = atoi(argv[1]);
    expected_processes = number_of_processes;
    int algo = atoi(argv[2]);
    int quantum = atoi(argv[3]);
    int k = atoi(argv[4]);
    int N = atoi(argv[5]);
    int M = atoi(argv[6]);

    msg_id = msgget(MSGKEY, IPC_CREAT | 0666);

    if (algo == 1 || algo == 2)
    {
        FILE *pFile;
        pFile = fopen("scheduler.log", "w");
        fprintf(pFile, "#At\ttime\tx\tprocess\ty\tstate\tarr\tw\ttotal\tz\tremain\ty\twait\tk\n");
        if (algo == 1)
        {
            // HPF
            HPF(pFile);
        }
        else if (algo == 2)
        {
            // RR
            RR(pFile, quantum, k);
        }
        fclose(pFile);
        int total_waiting_time = 0;
        float total_WTA_time = 0;
        int total_runtime = 0;
        float std_WTA = 0;
        for (int i = 0; i < expected_processes; i++)
        {
            total_waiting_time += pcbs[i].waiting_time;
            total_WTA_time += pcbs[i].WTA;
            total_runtime += pcbs[i].runtime;
        }
        float avg_waiting_time = round(((float)total_waiting_time / expected_processes) * 100) / 100;
        float avg_WTA_time = round(((float)total_WTA_time / expected_processes) * 100) / 100;
        float cpu_utilization = round(((float)total_runtime / getClk()) * 10000) / 100;
        for (int i = 0; i < expected_processes; i++)
        {
            std_WTA += pow(pcbs[i].WTA - avg_WTA_time, 2);
        }
        std_WTA = round((sqrt(std_WTA / expected_processes)) * 100) / 100;
        pFile = fopen("scheduler.perf", "w");
        fprintf(pFile, "CPU utilization = %.2f%%\n", cpu_utilization);
        fprintf(pFile, "Avg WTA = %.2f\n", avg_WTA_time);
        fprintf(pFile, "Avg Waiting = %.2f\n", avg_waiting_time);
        fprintf(pFile, "Std WTA = %.2f\n", std_WTA);
        fclose(pFile);
    }
    else if (algo == 3)
    {
        FILE *pFile1;
        pFile1 = fopen("scheduler_1.log", "w");
        fprintf(pFile1, "#At\ttime\tx\tprocess\ty\tstate\tarr\tw\ttotal\tz\tremain\ty\twait\tk\n");
        FILE *pFile2;
        pFile2 = fopen("scheduler_2.log", "w");
        fprintf(pFile2, "#At\ttime\tx\tprocess\ty\tstate\tarr\tw\ttotal\tz\tremain\ty\twait\tk\n");
        // 2cpu + FCFS
        twoCPUWithFCFS(pFile1, pFile2, N, M);
        fclose(pFile1);
        fclose(pFile2);

        int cpu1_count = 0, cpu2_count = 0;
        int cpu1_runtime = 0, cpu2_runtime = 0;
        float cpu1_wta_sum = 0, cpu2_wta_sum = 0;
        float cpu1_wait_sum = 0, cpu2_wait_sum = 0;
        int last1 = 0, last2 = 0;
        for (int i = 0; i < expected_processes; i++)
        {
            int id = pcbs[i].id;

            if (assigned_cpu[id] == 1)
            {
                cpu1_count++;
                cpu1_runtime += pcbs[i].runtime;
                cpu1_wta_sum += pcbs[i].WTA;
                cpu1_wait_sum += pcbs[i].waiting_time;
                if (finish_time[id] > last1)
                {
                    last1 = finish_time[id];
                }
            }
            else
            {
                cpu2_count++;
                cpu2_runtime += pcbs[i].runtime;
                cpu2_wta_sum += pcbs[i].WTA;
                cpu2_wait_sum += pcbs[i].waiting_time;
                if (finish_time[id] > last2)
                {
                    last2 = finish_time[id];
                }
            }
        }
        float cpu1_utilization = (last1 ? (round(((float)cpu1_runtime / last1) * 10000) / 100) : 0);
        float cpu2_utilization = (last2 ? (round(((float)cpu2_runtime / last2) * 10000) / 100) : 0);
        float cpu1_avg_wta = (cpu1_count ? (round(((float)cpu1_wta_sum / cpu1_count) * 100) / 100) : 0);
        float cpu2_avg_wta = (cpu2_count ? (round(((float)cpu2_wta_sum / cpu2_count) * 100) / 100) : 0);
        float cpu1_avg_wait = (cpu1_count ? (round(((float)cpu1_wait_sum / cpu1_count) * 100) / 100) : 0);
        float cpu2_avg_wait = (cpu2_count ? (round(((float)cpu2_wait_sum / cpu2_count) * 100) / 100) : 0);
        float cpu1_std_wta = 0, cpu2_std_wta = 0;
        for (int i = 0; i < expected_processes; i++)
        {
            int id = pcbs[i].id;
            if (assigned_cpu[id] == 1)
            {
                cpu1_std_wta += pow(pcbs[i].WTA - cpu1_avg_wta, 2);
            }
            else
            {
                cpu2_std_wta += pow(pcbs[i].WTA - cpu2_avg_wta, 2);
            }
        }
        cpu1_std_wta = (cpu1_count ? (round((sqrt(cpu1_std_wta / cpu1_count)) * 100) / 100) : 0);
        cpu2_std_wta = (cpu2_count ? (round((sqrt(cpu2_std_wta / cpu2_count)) * 100) / 100) : 0);
        pFile1 = fopen("scheduler_1.perf", "w");
        pFile2 = fopen("scheduler_2.perf", "w");
        fprintf(pFile1, "CPU utilization = %.2f%%\n", cpu1_utilization);
        fprintf(pFile1, "Avg WTA = %.2f\n", cpu1_avg_wta);
        fprintf(pFile1, "Avg Waiting = %.2f\n", cpu1_avg_wait);
        fprintf(pFile1, "Std WTA = %.2f\n", cpu1_std_wta);
        fprintf(pFile2, "CPU utilization = %.2f%%\n", cpu2_utilization);
        fprintf(pFile2, "Avg WTA = %.2f\n", cpu2_avg_wta);
        fprintf(pFile2, "Avg Waiting = %.2f\n", cpu2_avg_wait);
        fprintf(pFile2, "Std WTA = %.2f\n", cpu2_std_wta);
        fclose(pFile1);
        fclose(pFile2);
    }
    else
    {
        printf("Algorithm %d not implemented in this snippet.\n", algo);
    }

    printf("All processes finished. Cleaning up...\n");
    if (shared && shared != (void *)-1)
        shmdt(shared);
    destroyClk(false);
    return 0;
}
