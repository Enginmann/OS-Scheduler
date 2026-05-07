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
    int req_count;
    struct MemRequest requests[100];
};

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

static void on_quantum_boundary(int *quantums_elapsed, int k)
{
    (*quantums_elapsed)++;
    if (k > 0 && ((*quantums_elapsed) % k) == 0)
        clear_R_bits();
}

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
    pcb.page_table.page_table_frame = -1;
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
        // Phase 2: allocate page table frame + load first page when process starts.
        // no extra time for these initial allocations.
        if (memFile && new->page_table.page_table_frame < 0)
        {
            createPageTable(new, memFile);
            loadFirstPage(new, memFile);
        }

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

static int drain_rr_arrivals(Queue *ready_queue)
{
    struct msgbuff message;
    int drained = 0;
    while (msgrcv(msg_id, &message, sizeof(message) - sizeof(long), 2, IPC_NOWAIT) != -1)
    {
        PCB pcb = createPCB(message.p);

        pcb.req_count = message.req_count;
        pcb.req_index = 0;

        for (int i = 0; i < pcb.req_count; i++)
            pcb.requests[i] = message.requests[i];

        pcbs[pcb_count++] = pcb;

        // RR: newly arrived processes join the ready queue in FIFO order.
        enqueue(ready_queue, pcb.id);
        drained++;
    }

    return drained;
}

static void drain_rr_arrivals_stable(Queue *ready_queue)
{
    // The generator may send multiple processes for the same clock tick sequentially.
    // This helper waits (without advancing the simulated clock) until the queue stays
    // empty briefly, so we don't schedule before all same-tick arrivals are visible.
    int empty_polls = 0;
    // Require a longer quiet period so we don't miss bursty same-tick sends.
    // 50 polls * 1ms = ~50ms of no arrivals.
    for (int i = 0; i < 1000 && empty_polls < 50; i++)
    {
        int n = drain_rr_arrivals(ready_queue);
        if (n == 0)
        {
            empty_polls++;
            usleep(1000); // 1ms, real-time only
        }
        else
        {
            empty_polls = 0;
        }
    }
}

static void rr_unblock_processes(Queue *blocked_queue, Queue *ready_queue, FILE *memFile)
{
    QNode *cur = blocked_queue->front;
    QNode *next;

    while (cur)
    {
        next = cur->next;
        PCB *p = getPCB(cur->id);

        if (getClk() >= p->blocked_until)
        {
            if (p->has_pending_page)
            {
                swapIn(p, p->pending_page, p->pending_frame, p->pending_mode, memFile);
                p->has_pending_page = 0;
            }

            enqueue(ready_queue, p->id);
            removeFromQueue(blocked_queue, p->id);
        }

        cur = next;
    }
}

static void rr_enqueue_front(Queue *q, int id)
{
    QNode *node = (QNode *)malloc(sizeof(QNode));
    node->id = id;
    node->next = q->front;
    q->front = node;
    if (q->rear == NULL)
        q->rear = node;
    q->size++;
}

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

    initMemory();

    int quantums_elapsed = 0;

    while (1)
    {
        // =========================
        // UNBLOCK PROCESSES
        // =========================
        rr_unblock_processes(blocked_queue, ready_queue, memFile);

        // =========================
        // RECEIVE NEW PROCESSES
        // =========================
        drain_rr_arrivals(ready_queue);

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
            // If CPU is idle, give a brief chance to collect any remaining
            // same-tick arrivals before committing to a dispatch.
            drain_rr_arrivals_stable(ready_queue);
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

            // One loop iteration = exactly ONE tick of CPU time for the running process.
            // That tick is either a memory access (if a request is due) or normal CPU execution.
            int did_memory_tick = 0;

            if (current->req_index < current->req_count &&
                current->requests[current->req_index].time == current->cpu_time)
            {
                int va = current->requests[current->req_index].address;
                char mode = current->requests[current->req_index].mode;
                const char *vaToken = current->requests[current->req_index].address_str;

                // Validate virtual page against the process limit (FAQ: ignore out-of-scope access).
                int req_page = va / PAGE_SIZE;
                if (req_page < 0 || req_page >= current->limit)
                {
                    // The attempt still consumes the 1-tick memory-access slot from runtime,
                    // but produces no memory.log output.
                    sleep(1);
                    current->cpu_time++;
                    current->remaining--;
                    quantum_counter++;
                    did_memory_tick = 1;

                    current->req_index++; // discard invalid request
                }
                else
                {

                    int frame = -1;
                    int hit = handleMemoryRequest(current, va, mode, memFile, &frame, NULL);

                    // Any memory access (hit or miss) consumes 1 tick and counts toward runtime.
                    sleep(1);
                    current->cpu_time++;
                    current->remaining--;
                    quantum_counter++;
                    did_memory_tick = 1;

                    if (hit)
                    {
                        current->req_index++;
                    }
                    else
                    {
                        if (vaToken == NULL || vaToken[0] == '\0')
                            vaToken = "0";

                        // Fault is observed after the 1-tick RAM check (FAQ #11).
                        fprintf(memFile, "PageFault upon VA %s from process %d\n", vaToken, current->id);
                        fflush(memFile);

                        int disk_ticks = 0;
                        frame = handlePageFault(current, va, mode, memFile, &disk_ticks);

                        kill(current->pid, SIGSTOP);

                        int page = va / PAGE_SIZE;
                        current->has_pending_page = 1;
                        current->pending_page = page;
                        current->pending_frame = frame;
                        current->pending_mode = mode;

                        current->req_index++; // consume this request

                        current->blocked_until = getClk() + disk_ticks;

                        enqueue(blocked_queue, current->id);
                        current = NULL;

                        on_quantum_boundary(&quantums_elapsed, k);
                        quantum_counter = 0;

                        // Dispatch another ready process with 1-tick context-switch overhead (FAQ #13).
                        // FAQ #23 ordering: unblocked before newly arrived.
                        rr_unblock_processes(blocked_queue, ready_queue, memFile);
                        drain_rr_arrivals(ready_queue);
                        if (!isEmpty(ready_queue))
                        {
                            int id = dequeue(ready_queue);
                            PCB *next = getPCB(id);

                            sleep(1);
                            context_switch(pFile, NULL, NULL, next, 1);
                            current = next;
                        }

                        continue;
                    }
                }
            }

            if (!did_memory_tick)
            {
                // Normal CPU execution tick
                sleep(1);
                current->cpu_time++;
                current->remaining--;
                quantum_counter++;
            }

            // Tick boundary: enforce FAQ ordering and exact-time handling.
            // Unblocked processes enter ready queue before newly arrived ones.
            rr_unblock_processes(blocked_queue, ready_queue, memFile);
            drain_rr_arrivals(ready_queue);

            // ===== finish/preempt decisions after the tick =====
            if (current->remaining == 0)
            {
                printf("[TIME %d] FINISH P%d\n", getClk(), current->id);

                freeProcessMemory(current);
                waitpid(current->pid, NULL, 0);

                int TA = getClk() - current->arrival;
                int waiting_time = TA - current->runtime;
                float WTA = (float)TA / current->runtime;
                current->waiting_time = waiting_time;
                current->WTA = WTA;

                fprintf(pFile,
                        "At\ttime\t%d\tprocess\t%d\tfinished\tarr\t%d\t"
                        "total\t%d\tremain\t0\twait\t%d\tTA\t%d\tWTA\t%.2f\n",
                        getClk(), current->id, current->arrival, current->runtime,
                        waiting_time, TA, WTA);

                finished_processes++;
                current = NULL;
                quantum_counter = 0;
                on_quantum_boundary(&quantums_elapsed, k);

                rr_unblock_processes(blocked_queue, ready_queue, memFile);
                drain_rr_arrivals(ready_queue);
                if (!isEmpty(ready_queue))
                {
                    int id = dequeue(ready_queue);
                    PCB *next = getPCB(id);
                    sleep(1);
                    context_switch(pFile, NULL, NULL, next, 1);
                    current = next;
                }
                continue;
            }

            if (quantum_counter == quantum)
            {
                printf("[TIME %d] Quantum expired for P%d\n", getClk(), current->id);

                PCB *old = current;
                current = NULL;

                // FAQ #23 + FIFO RR:
                // - If the ready queue already had processes from earlier, keep that FIFO order.
                // - For events at this boundary, order is: preempted, then unblocked, then arrivals.
                // - If the ready queue is empty and there are no boundary events, keep running
                //   without a context switch (FAQ #30).
                int had_ready_before = !isEmpty(ready_queue);

                if (had_ready_before)
                {
                    // Existing ready processes keep their place; preempted joins ahead of boundary events.
                    enqueue(ready_queue, old->id);
                    rr_unblock_processes(blocked_queue, ready_queue, memFile);
                    drain_rr_arrivals(ready_queue);
                }
                else
                {
                    // No one was ready before this boundary; check boundary events first.
                    rr_unblock_processes(blocked_queue, ready_queue, memFile);
                    drain_rr_arrivals(ready_queue);

                    if (isEmpty(ready_queue))
                    {
                        current = old;
                        quantum_counter = 0;
                        on_quantum_boundary(&quantums_elapsed, k);
                        continue;
                    }

                    // Boundary events exist: preempted has priority over them.
                    rr_enqueue_front(ready_queue, old->id);
                }

                if (!isEmpty(ready_queue))
                {
                    int id = dequeue(ready_queue);
                    PCB *next = getPCB(id);
                    context_switch(pFile, NULL, old, next, 1);
                    current = next;
                }

                quantum_counter = 0;
                on_quantum_boundary(&quantums_elapsed, k);
                continue;
            }
        }
    }

    fclose(memFile);
    freeQueue(ready_queue);
    freeQueue(blocked_queue);
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
    destroyClk(true);
    return 0;
}
