#include "headers.h"
#include "queue.h"
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

struct processData {
    int arrivaltime;
    int priority;
    int runningtime;
    int remainingtime;
    int id;
};

struct msgbuff {
    long mtype;
    struct processData p;
};

// Global PCB and state variables
PCB pcbs[100];
int finished_processes = 0;
int number_of_processes;
int current_running = -1;
int msg_id;

////////////////////////////////////////////////////////////
// ROUND ROBIN ALGORITHM
////////////////////////////////////////////////////////////
void RR(int quantum) {
    struct msgbuff message;
    Queue *q = createQueue();
    int quantum_counter = 0;
    int lastClk = -1;  // FIX: start at -1 so time 0 is processed

    printf("Starting Round Robin with Quantum: %d\n", quantum);

    while (finished_processes < number_of_processes) {

        // 1. SYNC WITH CLOCK — wait for a new tick
        int currentClk = getClk();
        if (currentClk == lastClk) {
            usleep(1000);
            continue;
        }
        lastClk = currentClk;

        // 2. RECEIVE NEW PROCESSES for this tick (non-blocking)
        // FIX: moved AFTER clock sync so we don't miss arrivals mid-tick
        int arrived_id = -1;
        while (msgrcv(msg_id, &message, sizeof(struct processData), 0, IPC_NOWAIT) != -1) {
            arrived_id = message.p.id;

            // Handle edge case: runtime 0
            if (message.p.runningtime <= 0) {
                finished_processes++;
                continue;
            }

            int pid = fork();
            if (pid == 0) { // Child Process
                char runtime_str[10];
                sprintf(runtime_str, "%d", message.p.runningtime);
                execl("./process.out", "process.out", runtime_str, NULL);
                exit(0);
            } else { // Parent (Scheduler)
                pcbs[message.p.id].id        = message.p.id;
                pcbs[message.p.id].pid       = pid;
                pcbs[message.p.id].arrival   = message.p.arrivaltime;
                pcbs[message.p.id].runtime   = message.p.runningtime;
                pcbs[message.p.id].remaining = message.p.runningtime;

                // Stop it immediately until it's its turn
                kill(pid, SIGSTOP);
                enqueue(q, message.p.id);
            }
        }

        // 3. EXECUTE LOGIC (The CPU step for this tick)
        int finished_id = -1;

        if (current_running != -1) {
            pcbs[current_running].remaining--;
            quantum_counter++;

            // CHECK IF FINISHED
            if (pcbs[current_running].remaining <= 0) {
                finished_id = current_running;

                // FIX: resume the stopped child so it can run to completion and exit cleanly
                kill(pcbs[current_running].pid, SIGCONT);

                // Wait for it to actually finish
                waitpid(pcbs[current_running].pid, NULL, 0);

                finished_processes++;
                current_running = -1;
                quantum_counter = 0;
            }
            // CHECK IF QUANTUM EXPIRED
            // FIX: was (> quantum), should be (>= quantum)
            else if (quantum_counter >= quantum) {
                kill(pcbs[current_running].pid, SIGSTOP);
                enqueue(q, current_running);
                current_running = -1;
                quantum_counter = 0;
            }
        }

        // 4. FILL CPU IF IDLE
        if (current_running == -1 && !isEmpty(q)) {
            current_running = dequeue(q);
            kill(pcbs[current_running].pid, SIGCONT);
            quantum_counter = 0;
        }

        // 5. PRINT STATE
        printf("\n===== Time %d =====\n", lastClk);
        if (arrived_id  != -1) printf("ARRIVED:  P%d\n", arrived_id);
        if (finished_id != -1) printf("FINISHED: P%d\n", finished_id);

        if (current_running == -1)
            printf("RUNNING: NONE\n");
        else
            printf("RUNNING: P%d (Rem: %d, Q: %d/%d)\n",
                   current_running,
                   pcbs[current_running].remaining,
                   quantum_counter, quantum);

        printf("QUEUE: ");
        QNode *curr = q->front;
        while (curr) {
            printf("P%d ", curr->id);
            curr = curr->next;
        }
        printf("\n====================\n");
    }

    freeQueue(q);
}

////////////////////////////////////////////////////////////
// MAIN
////////////////////////////////////////////////////////////
int main(int argc, char *argv[]) {
    initClk();

    if (argc < 4) {
        printf("Usage: ./scheduler.out <num_proc> <algo> <quantum>\n");
        destroyClk(true);
        return -1;
    }

    number_of_processes = atoi(argv[1]);
    int algo            = atoi(argv[2]);
    int quantum         = atoi(argv[3]);

    msg_id = msgget(MSGKEY, IPC_CREAT | 0666);

    // FIX: removed the SIGCHLD handler — it was racing with our accounting logic.
    // Child cleanup is now done explicitly with waitpid() when remaining hits 0.

    if (algo == 2) { // RR
        RR(quantum);
    } else {
        printf("Algorithm %d not implemented in this snippet.\n", algo);
    }

    printf("All processes finished. Cleaning up...\n");
    destroyClk(true);
    return 0;
}