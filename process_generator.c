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

void clearResources(int);

int main(int argc, char * argv[])
{
    signal(SIGINT, clearResources);
    // TODO Initialization
    // 1. Read the input files.
    FILE *file = fopen("processes.txt", "r");
    if (!file)
    {
        perror("Error opening file");
        return 1;
    }

    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), file))
    {
        
        if (line[0] == '#' || line[0] == '\n')
            continue;

        count++;
    }
    rewind(file);
    struct processData p[count];
    int i=0;
    while (fgets(line, sizeof(line), file))
    {
        
        if (line[0] == '#' || line[0] == '\n')
            continue;

        int id, arrival, runtime, priority;

        sscanf(line, "%d\t%d\t%d\t%d", &id, &arrival, &runtime, &priority);

        p[i].id= id; 
        p[i].arrivaltime= arrival;
        p[i].runningtime= runtime;
        p[i].priority= priority;
        i++;
    }

    fclose(file);
    for(int i=0;i<count;i++)
    {
        printf("%d %d %d %d\n",p[i].id,p[i].arrivaltime,p[i].runningtime,p[i].priority);
    }

    // 2. Ask the user for the chosen scheduling algorithm and its parameters, if there are any.
    printf("Choose a scheduling algorithm:\n");
    printf("1. Preemptive Highest Priority First (HPF)\n");
    printf("2. Round Robin (RR)\n");
    printf("3. 2-CPUs (FCFS)\n");
    int choice;
    scanf("%d", &choice);
    char algo_char[10], q_char[10], n_char[10], m_char[10];
    sprintf(algo_char, "%d", choice);
    sprintf(q_char, "0");
    sprintf(n_char, "0");
    sprintf(m_char, "0");
    if (choice == 2) 
    {
        printf("Enter quantum: ");
        scanf("%s", q_char); 
    } 
    else if (choice == 3)
    {
        printf("Enter N and M: ");
        scanf("%s %s", n_char, m_char);
    }
    // 3. Initiate and create the scheduler and clock processes.
    // 4. Use this function after creating the clock process to initialize clock
    int clk_pid = fork();
    if (clk_pid == 0) 
    {
        execl("./clk.out", "clk.out", NULL);
    }
    int sched_pid = fork();
    if (sched_pid == 0)  
    {
        execl("./scheduler.out", "scheduler.out", algo_char, q_char, n_char, m_char, NULL);
    }
    initClk();
    // To get time use this
    int x = getClk();
    printf("current time is %d\n", x);
    int msg_id = msgget(MSGKEY, IPC_CREAT | 0666);
    struct msgbuff message;
    for(int i=0;i<count;i++)
    {
        while(getClk()<p[i].arrivaltime);
        message.mtype = choice;
        message.p = p[i];
        msgsnd(msg_id, &message, sizeof(struct processData), !IPC_NOWAIT);
        printf("Sent P%d at %d\n", p[i].id, getClk());
    }
    // TODO Generation Main Loop
    // 5. Create a data structure for processes and provide it with its parameters.
    // 6. Send the information to the scheduler at the appropriate time.
    // 7. Clear clock resources
    destroyClk(true);
}

void clearResources(int signum)
{
    msgctl(MSGKEY, IPC_RMID, NULL);
    printf("Process Generator terminating!\n");
}
