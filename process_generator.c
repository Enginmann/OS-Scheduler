#define HEADERS_IMPLEMENTATION
#include "headers.h"

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

struct MemRequest requests[100];
int req_count = 0;
int req_index = 0;

static int parse_address(const char *addr)
{
    if (!addr)
        return 0;

    
    for (const char *p = addr; *p; ++p)
    {
        if (*p != '0' && *p != '1')
            return (int)strtol(addr, NULL, 0);
    }
    return (int)strtol(addr, NULL, 2);
}


void load_requests(int pid)
{
    char filename[20];
    sprintf(filename, "requests_%d.txt", pid);

    FILE *f = fopen(filename, "r");
    req_count = 0;

    char line[100];
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#')
            continue;

        int t;
        char addr[20], mode;

        sscanf(line, "%d %s %c", &t, addr, &mode);

        requests[req_count].time = t;
        requests[req_count].address = parse_address(addr);
        requests[req_count].mode = mode;
        snprintf(requests[req_count].address_str, sizeof(requests[req_count].address_str), "%s", addr);

        req_count++;
    }

    fclose(f);
}

void clearResources(int);
int msg_id;
sharedData *shared;
int shmid;
static int clk_pid_global = -1;

int main(int argc, char *argv[])
{
    signal(SIGINT, clearResources);

    
    int old_clock = shmget(SHKEY, 4, 0444);
    if (old_clock != -1)
        shmctl(old_clock, IPC_RMID, NULL);

    int old_q = msgget(MSGKEY, 0666);
    if (old_q != -1)
        msgctl(old_q, IPC_RMID, NULL);

    shmid = shmget(SHKEY + 10, sizeof(struct sharedData), IPC_CREAT | 0666);
    if (shmid == -1)
    {
        perror("shmget(SHKEY+10) failed");
        return 1;
    }
    shared = (struct sharedData *)shmat(shmid, NULL, 0);
    if (shared == (void *)-1)
    {
        perror("shmat(SHKEY+10) failed");
        return 1;
    }
    shared->is_finished = false;

    FILE *file = fopen("processes.txt", "r");
    if (!file)
    {
        perror("Error opening file");
        shmdt(shared);
        return 1;
    }

    char line[256];
    int number_of_processes = 0;
    while (fgets(line, sizeof(line), file))
    {

        if (line[0] == '#' || line[0] == '\n')
            continue;

        number_of_processes++;
    }
    rewind(file);
    struct processData p[number_of_processes];
    int i = 0;
    while (fgets(line, sizeof(line), file))
    {

        if (line[0] == '#' || line[0] == '\n')
            continue;

        int id, arrival, runtime, priority, base, limit;

        sscanf(line, "%d\t%d\t%d\t%d\t%d\t%d", &id, &arrival, &runtime, &priority, &base, &limit);

        p[i].id = id;
        p[i].arrivaltime = arrival;
        p[i].runningtime = runtime;
        p[i].remainingtime = runtime;
        p[i].priority = priority;
        p[i].base = base;
        p[i].limit = limit;
        i++;
    }

    fclose(file);
    for (int i = 0; i < number_of_processes; i++)
    {
        load_requests(p[i].id);
    }
    printf("Choose a scheduling algorithm:\n");
    printf("1. Preemptive Highest Priority First (HPF)\n");
    printf("2. Round Robin (RR)\n");
    printf("3. 2-CPUs (FCFS)\n");
    int choice;
    scanf("%d", &choice);
    char count_of_processes[10], algo_char[10], q_char[10], k_char[10], n_char[10], m_char[10];
    sprintf(algo_char, "%d", choice);
    sprintf(q_char, "0");
    sprintf(k_char, "0");
    sprintf(n_char, "0");
    sprintf(m_char, "0");
    sprintf(count_of_processes, "%d", number_of_processes);
    if (choice == 2)
    {
        printf("Enter quantum q and k: ");
        scanf("%s %s", q_char, k_char);
    }
    else if (choice == 3)
    {
        printf("Enter N and M: ");
        scanf("%s %s", n_char, m_char);
    }
    int sched_pid = fork();
    if (sched_pid == 0)
    {
        execl("./scheduler.out", "scheduler.out", count_of_processes, algo_char, q_char, k_char, n_char, m_char, NULL);
        exit(0);
    }
    else
    {
        int clk_pid = fork();
        if (clk_pid == 0)
        {
            execl("./clk.out", "clk.out", NULL);
        }
        else
        {
            clk_pid_global = clk_pid;
            
            initClk();
            int x = getClk();
            printf("current time is %d\n", x);
            msg_id = msgget(MSGKEY, IPC_CREAT | 0666);
            struct msgbuff message;
            for (int i = 0; i < number_of_processes; i++)
            {
                
                while (getClk() < p[i].arrivaltime);

                struct msgbuff message;
                message.mtype = choice;
                message.p = p[i];

                
                load_requests(p[i].id);

                message.req_count = req_count;

                for (int j = 0; j < req_count; j++)
                {
                    message.requests[j] = requests[j];
                }

                msgsnd(msg_id, &message, sizeof(message) - sizeof(long), !IPC_NOWAIT);

                printf("Sent P%d with %d requests\n", p[i].id, req_count);
            }
            shared->is_finished = true;
            int stat_loc;
            waitpid(sched_pid, &stat_loc, 0);
            if (clk_pid_global > 0)
                kill(clk_pid_global, SIGINT);
            shmdt(shared);
            destroyClk(true);
        }
    }
}

void clearResources(int signum)
{
    msgctl(msg_id, IPC_RMID, NULL);
    shmctl(shmid, IPC_RMID, NULL);
    printf("Process Generator terminating!\n");
    exit(0);
}
