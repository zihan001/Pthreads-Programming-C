#define _REENTRANT

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>

#define NRES_TYPES 10
#define NTASKS 25
#define MAXNAME 32
#define MAXLINE 256

// varaible and struct declarations
int monitorFlag = 0;
int monitorTime, NITER, monitor_done, no_of_resources;
pthread_t  tid[NTASKS+1];
pthread_mutex_t create_mutex;

typedef struct Resource {
    char name[MAXNAME];
    int value;
} Resource;

typedef struct Task {
    char taskName[MAXNAME];
    double busyTime;
    double idleTime;
    Resource requiredResources[NRES_TYPES];
    int resourceType;
    int flag;
    int iterations_done;
    double wait_time;
} Task;

Resource resources[NRES_TYPES];
Task tasks[NTASKS];

// ------------------------------
// The FATAL function is due to the authors of the AWK Programming Language.

void FATAL (const char *fmt, ... )
{
    va_list  ap;
    fflush (stdout);
    va_start (ap, fmt);  vfprintf (stderr, fmt, ap);  va_end(ap);
    fflush (NULL); exit(1);
}
// ------------------------------
// functions for initializing, locking and unlocking mutexes

void mutex_init (pthread_mutex_t* mutex)
{
    int rval= pthread_mutex_init(mutex, NULL);
    if (rval) FATAL ("mutex_init: %s\n",strerror(rval));
}    

void mutex_lock (pthread_mutex_t* mutex)
{
    int rval= pthread_mutex_lock(mutex);
    if (rval) FATAL ("mutex_lock: %s\n",strerror(rval));
}    

void mutex_unlock (pthread_mutex_t* mutex)
{
    int rval= pthread_mutex_unlock(mutex);
    if (rval) FATAL ("mutex_unlock: %s\n",strerror(rval));
}
// ------------------------------
// function copied from http://webdocs.cs.ualberta.ca/~cmput379/W23/379only/a4.pdf
/* msleep(): Sleep for the requested number of milliseconds. */
int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

// function which prints the task names. Part of monitor thread
void print_thread_state(int no_of_tasks, int state) {
    for (int i = 0; i < no_of_tasks; i++) {
        if (tasks[i].flag == state) {
            printf("%s ", tasks[i].taskName);
        }
    }
}

// function to print the resources available to the system.
void print_sys_res() {
    printf("\nSystem Resources:\n");
    for (int i = 0; i < no_of_resources; i++) {
        printf("        %s: (maxAvail=   %d, held=   0)\n", resources[i].name, resources[i].value);
    }
    printf("\n");
    return;
}

// function to print information about the tasks.
void print_sys_tasks(int no_of_tasks) {

    const char * state[] = {"WAIT", "RUN", "IDLE"};

    printf("\nSystem Tasks:\n");
    for (int i = 0; i < no_of_tasks; i++) {
        printf("[%d] %s (%s, runTime= %.0f msec, idleTime= %.0f msec):\n", i, tasks[i].taskName, state[tasks[i].flag], tasks[i].busyTime, tasks[i].idleTime);
        printf("        (tid= 0x%lx)\n", tid[i]);
        for (int j = 0; j < tasks[i].resourceType; j++) {
            printf("        %s: (needed=   %d, held=   0)\n", tasks[i].requiredResources[j].name, tasks[i].requiredResources[j].value);
        }
        printf("        (RUN: %d times, WAIT: %.0f msec)\n\n", NITER, tasks[i].wait_time);
    }
    return;
}

int parse_file(char file[]) {
    
    char str[MAXLINE];
    char* token[MAXLINE];
    char WSPACE[]= "\n \t";

    // memset both structs
    memset( &resources, 0, sizeof(resources) );
    memset( &tasks, 0, sizeof(tasks) );

    FILE* inputFile = fopen(file, "r");
    // parsing file
    int k = 0;
    while (fgets(str, MAXLINE, inputFile) != NULL) {
        int i = 0;
        // if first word resource get resources and put in table
        if (!strncmp(str, "resources", 9)) {
            // tokenizes the input line
            token[i] = strtok(str, WSPACE);
            while ( token[i] != NULL ) {
                i++;
                token[i] = strtok(NULL, WSPACE);
            }
            for (int j = 1; j < i; j++) {
                strcpy(resources[j-1].name, strtok(token[j],":"));
                resources[j-1].value = atoi(strtok(NULL, ":"));

                no_of_resources++;
            }
        }
        // if first word task, get task info and put in table
        if (!strncmp(str, "task", 4)) {
            token[i] = strtok(str, WSPACE);
            while ( token[i] != NULL ) {
                i++;
                token[i] = strtok(NULL, WSPACE);
            }
            for (int j = 1; j < i; j++) {
                if (j == 1) {
                    strcpy(tasks[k].taskName, token[j]);
                }
                else if (j == 2) {
                    tasks[k].busyTime = atof(token[j]);
                }
                else if (j == 3) {
                    tasks[k].idleTime = atof(token[j]);
                }
                else if (j > 3) {
                    strcpy(tasks[k].requiredResources[j-4].name, strtok(token[j],":"));
                    tasks[k].requiredResources[j-4].value = atoi(strtok(NULL, ":"));
                    tasks[k].resourceType++;
                }
            }
            k++;
        }
    }
    // return number of tasks
    return k;
}

// function for the task thread. Locks the ressources table while checking, acquiring and giving back resources.
void *task_thread(int arg) {
    clock_t t, u;
    t = clock();

    int tasks_no = arg;
    int iterations = 0;
    // keeps looping till NITER iterations complete
    while (iterations < NITER)
    {
        while (monitorFlag != 0);
        u = clock();
        tasks[tasks_no].flag = 0;
        
        // WAIT and acquire resources
        mutex_lock(&create_mutex);
        int counter = 0;
        for (int i = 0; i < tasks[tasks_no].resourceType; i++) {
            for (int j = 0; j < (sizeof(resources)/sizeof(resources[0])); j++) {
                if ((!strcmp(tasks[tasks_no].requiredResources[i].name, resources[j].name)) && (resources[j].value >= tasks[tasks_no].requiredResources[i].value)) {
                    counter++;
                    break;
                }
            }
        }
        // if all resources available
        if (counter == tasks[tasks_no].resourceType) {
            // acquire resources
            for (int i = 0; i < tasks[tasks_no].resourceType; i++) {
                for (int j = 0; j < (sizeof(resources)/sizeof(resources[0])); j++) {
                    if (!strcmp(tasks[tasks_no].requiredResources[i].name, resources[j].name)) {
                        resources[j].value -= tasks[tasks_no].requiredResources[i].value;
                        break;
                    }
                }
            }
            mutex_unlock (&create_mutex);
            u = clock() - u;
            double waitTime = (((double)u)/CLOCKS_PER_SEC)*1000;
            tasks[tasks_no].wait_time += u;
            
            // RUN
            while (monitorFlag != 0);
            tasks[tasks_no].flag = 1;
            msleep(tasks[tasks_no].busyTime);
            // give back resources
            mutex_lock(&create_mutex);
            for (int i = 0; i < tasks[tasks_no].resourceType; i++) {
                for (int j = 0; j < (sizeof(resources)/sizeof(resources[0])); j++) {
                    if (!strcmp(tasks[tasks_no].requiredResources[i].name, resources[j].name)) {
                        resources[j].value += tasks[tasks_no].requiredResources[i].value;
                        break;
                    }
                }
            }
            mutex_unlock (&create_mutex);
            
            // IDLE
            while (monitorFlag != 0);
            tasks[tasks_no].flag = 2;
            msleep(tasks[tasks_no].idleTime);
            iterations++;
            tasks[tasks_no].iterations_done = iterations;
            t = clock() - t;
            double time_taken = (((double)t)/CLOCKS_PER_SEC)*1000;
            while (monitorFlag != 0);
            printf("\ntask: %s (tid= 0x%lx, iter= %d, time= %.0f msec)\n", tasks[tasks_no].taskName, tid[tasks_no], iterations, time_taken);
        }
        else {
            // should unlock be here
            mutex_unlock (&create_mutex);
        }
    }
    pthread_exit(NULL);
}

// This thread runs every monitorTime millisec. When active, it prints all tasks in
// each of the three possible states: WAIT, RUN, and IDLE.

// ************** STOP OTHER THREADS FROM CHANGING STATE ****************
void *monitor_thread(void* arg) {

    int no_of_tasks =  *((int *) arg); 
    printf("\n");

    while (!monitor_done) {
        monitorFlag = 1;
        printf("\nmonitor: ");

        printf("[WAIT] ");
        print_thread_state(no_of_tasks, 0);
        printf("\n");

        printf("         [RUN] ");
        print_thread_state(no_of_tasks, 1);
        printf("\n");
        
        printf("         [IDLE] ");
        print_thread_state(no_of_tasks, 2);
        printf("\n\n");
        monitorFlag = 0;
        msleep(monitorTime);
    }
    pthread_exit(NULL);
}

// ------------------------------
int main (int argc, char *argv[])
{
    clock_t t;
    int i = 0;
    int error;

    monitor_done = 0;

    if (argc < 4)  FATAL ("Usage: %s inputFile monitorTime NITER \n", argv[0]);
    t = clock();
    monitorTime = atoi(argv[2]);
    NITER = atoi(argv[3]);
    
    int no_of_tasks = parse_file(argv[1]);

    // initializing mutex
    mutex_init(&create_mutex);
    
    // creating task threads and monitor thread
    while (i < no_of_tasks) {
        error = pthread_create(&(tid[i]),
                               NULL,
                               &task_thread, (void *)i);
        if (error != 0)
            printf("\nThread can't be created :[%s]",
                   strerror(error));
        i++;
    }
    error = pthread_create(&(tid[i]),
                               NULL,
                               &monitor_thread, (void*) &no_of_tasks);
    if (error != 0)
        printf("\nThread can't be created :[%s]",
                strerror(error));
    i++;

    for (int j = 0; j < i - 1; j++) {
        pthread_join(tid[j], NULL);
    }

    monitor_done = 1;
    
    // Termination
    print_sys_res();
    print_sys_tasks(no_of_tasks);
    t = clock() - t;
    double time_taken = (((double)t)/CLOCKS_PER_SEC)*1000;
    printf("Running time= %.0f msec\n\n", time_taken);

    return 0;
}