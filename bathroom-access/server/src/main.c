/* MAIN HEADER */

/* INCLUDE HEADERS */
#include "constants.h"
#include "error.h"
#include "log.h"
#include "parse.h"
#include "protocol.h"
#include "sync.h"
#include "utils.h"

/* SYSTEM CALLS HEADERS */
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* C LIBRARY HEADERS */
#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Miscellaneous */
#include <pthread.h>

// TEMPORARY
#include <time.h>

// Check if still accepting requests
int bath_open = 1;

// Alarm status & Handler
int alarm_status = ALARM_CHILL;

void close_bathroom(int signo);

// Cleanup
void cleanup(void);

// Order of entrance
int order = 0;

// Public Request FIFO
char req_fifo_path[BUFFER_SIZE];

// Request processor
void* request_processor(void *arg);

// Thread limiting semaphore
sem_t thread_locker;
// Maximum number of threads
int max_threads;

// Bathroom limiting semaphore
sem_t bathroom_locker;
// Bathroom capacity
int bath_capacity;

int* bath_places;

#define OPEN    0
#define USED    1


/* -------------------------------------------------------------------------
                            MAIN THREAD
/-------------------------------------------------------------------------*/
int main(int argc, char *argv[]) {

    if (argc < 4) {
        char error[BUFFER_SIZE];
        sprintf(error, "%s: error %d: %s\n"
                       "Program usage: Qn <-t nsecs> [-l nplaces] [-n nthreads] fifoname\n", argv[0], EINVAL, strerror(EINVAL));
        write(STDERR_FILENO, error, strlen(error));
        return -1;
    }

    /* -------------------------------------------------------------------------
                                PARSING
    /-------------------------------------------------------------------------*/
    // error | | | | fifoname | threads | places | secs
    int flags;
    parse_info_t info;
    init_parse_info(&info);
    flags = parse_cmd(argc - 1, &argv[1], &info);

    if (flags & FLAG_ERR) {
        free_parse_info(&info);
        return -1;
    }

    // Public Request FIFO
    if (sprintf(req_fifo_path, "/tmp/%s", info.path) < 0) {
        char error[BUFFER_SIZE];
        sprintf(error, "error creating public request FIFO path\n");
        write(STDERR_FILENO, error, strlen(error));
        return -1;
    }

    // Execution duration
    int exec_secs = info.exec_secs;
    // Maximum number of threads
    max_threads = info.max_threads;
    // Bathroom capacity
    bath_capacity = info.capacity;

    free_parse_info(&info);

    /* -------------------------------------------------------------------------
                            INITIALIZE THREAD LOCKER
    /-------------------------------------------------------------------------*/
    if (max_threads != -1) {
        if (sem_init(&thread_locker, SEM_NOT_SHARED, max_threads)) {
            error_sys(argv[0], "couldn't initialize thread locker");
            return -1;
        }
    }

    /* -------------------------------------------------------------------------
                            INITIALIZE BATHROOM LOCKER
    /-------------------------------------------------------------------------*/
    if (bath_capacity != -1) {
        if (sem_init(&bathroom_locker, SEM_NOT_SHARED, bath_capacity)) {
            error_sys(argv[0], "couldn't initialize bathroom locker");
            return -1;
        }
        bath_places = (int*)malloc(sizeof(int) * bath_capacity);
        memset(bath_places, OPEN, sizeof(int) * bath_capacity);
    }

    /* -------------------------------------------------------------------------
                                INSTALL ALARM
    /-------------------------------------------------------------------------*/
    struct sigaction alarm_action;
    alarm_action.sa_handler = close_bathroom;
    sigemptyset(&alarm_action.sa_mask);
    alarm_action.sa_flags = 0;
    if (sigaction(SIGALRM, &alarm_action, NULL)) {
        error_sys(argv[0], "couldn't install alarm");
        return -1;
    }

    alarm(exec_secs);
    time_t initial = time(NULL);

    /* -------------------------------------------------------------------------
                                PREPARE REPLY ERROR HANDLER
    /-------------------------------------------------------------------------*/
    struct sigaction pipe_action;
    pipe_action.sa_handler = SIG_IGN;
    sigemptyset(&pipe_action.sa_mask);
    pipe_action.sa_flags = 0;
    if (sigaction(SIGALRM, &alarm_action, NULL)) {
        error_sys(argv[0], "couldn't install reply error handler");
        return -1;
    }

    /* -------------------------------------------------------------------------
                                OPENING PUBLIC REQUEST FIFO
    /-------------------------------------------------------------------------*/

    if (mkfifo(req_fifo_path, 0660)) {
        error_sys(argv[0], "couldn't create public request FIFO");
        return -1;
    }

    int req_fifo;
    if ((req_fifo = open(req_fifo_path, O_RDONLY)) == -1) {
        if (error_sys_ignore_alarm(argv[0], "couldn't open public request FIFO", alarm_status)) {
            unlink(req_fifo_path);
            return -1;
        }
    }

    /* -------------------------------------------------------------------------
                                CLEANUP
    /-------------------------------------------------------------------------*/
    if (atexit(cleanup)) {
        cleanup();
        error_sys(argv[0], "failed to install cleanup");
        return -1;
    }

    /* -------------------------------------------------------------------------
                                REQUEST READING
    /-------------------------------------------------------------------------*/
    request_t *request;
    pthread_t tid;

    int empty = 1;

    int ret;
    // main loop
    while (bath_open || !empty) {

        // check thread locker
        if (max_threads != -1) {
            if (sem_wait(&thread_locker)) {
                // error wasn't caused by alarm
                if (error_sys_ignore_alarm(argv[0], "couldn't wait for thread locker", alarm_status)) {
                    close(req_fifo);
                    pthread_exit(NULL);
                }
                // error was caused by alarm, proceed as normal
                empty = 0;
                continue;
            }
        }

        int bath_assigned = -1;
        if (bath_open && (bath_capacity != -1)) {
            if (sem_wait(&bathroom_locker)) {
                // error wasn't caused by alarm
                if (error_sys_ignore_alarm(argv[0], "couldn't wait for bathroom locker", alarm_status)) {
                    close(req_fifo);
                    pthread_exit(NULL);
                }
                // error was caused by alarm, proceed as normal
                empty = 0;
                if (max_threads != -1) sem_post(&thread_locker);
                continue;
            }

            for (int i = 0; i < bath_capacity; i++) {
                if (bath_places[i] == OPEN) {
                    bath_places[i] = USED;
                    bath_assigned = i;
                    break;
                }
            }

            if (bath_assigned == -1) {
                char error[BUFFER_SIZE];
                sprintf(error, "%s: failed to assign bathroom\n", argv[0]);
                write(STDERR_FILENO, error, strlen(error));
                if (max_threads != -1) sem_post(&thread_locker);
                sem_post(&bathroom_locker);
                empty = 0;
                continue;
            }
        }

        void **arg = malloc(sizeof(void*) * 2);
        arg[1] = malloc(sizeof(int));

        *((int*)arg[1]) = (bath_capacity == -1) ? order++ : bath_assigned;

        request = (request_t*)malloc(sizeof(request_t));

        ret = read(req_fifo, request, sizeof(request_t));

        if (ret == -1) { // error
            // error wasn't caused by alarm
            if (error_sys_ignore_alarm(argv[0], "couldn't read request queue", alarm_status)) {
                close(req_fifo);
                free(request);
                free(arg[1]);
                free(arg);
                pthread_exit(NULL);
            }
            // error was caused by alarm, proceed as normal
            free(request);
            free(arg[1]);
            free(arg);
            continue;
        } else if (ret == 0) { // EOF
            free(request);
            free(arg[1]);
            free(arg);
            empty = 1;
            usleep(FIFO_WAIT_TIME);
            continue;
        }

        arg[0] = (void*)request;

        empty = 0;

        if (write_log(request, "RECVD")) {
            error_sys(argv[0], "couldn't write log");
        }

        // process request
        if (pthread_create(&tid, NULL, request_processor, arg)) {
            error_sys(argv[0], "couldn't create thread to process request");
            usleep(1000); // sleep and try again
            if (pthread_create(&tid, NULL, request_processor, arg)) {
                error_sys(argv[0], "couldn't create thread to process request");
                free(request);
                free(arg[1]);
                free(arg);
                close(req_fifo);
                pthread_exit(NULL);
            }
        }

        if (pthread_detach(tid)) {
            usleep(1000); // sleep and try again
            if (pthread_detach(tid)) {
                char error[BUFFER_SIZE];
                sprintf(error, "couldn't detach processing thread %ld", tid);
                error_sys(argv[0], error);
                close(req_fifo);
                pthread_exit(NULL);
            }
        }
    }

    // DEV
    printf("Closed in %ds\n", (int)(time(NULL) - initial));

    if (req_fifo != -1) {
        if (close(req_fifo)) {
            error_sys(argv[0], "couldn't close public request FIFO");
        }
    }

    pthread_exit(NULL);
}

/* -------------------------------------------------------------------------
                ACTION TO EXECUTE UPON ALARM SIGNAL
/-------------------------------------------------------------------------*/
void close_bathroom(int signo) {
    bath_open = 0;
    alarm_status = ALARM_TRIGGERED;
    if (req_fifo_path == NULL) return;
    if (unlink(req_fifo_path)) {
        error_sys("closing", "error on unlinking public FIFO");
    }
}

/* -------------------------------------------------------------------------
                            CLEANUP FUNCTION
/-------------------------------------------------------------------------*/
void cleanup(void) {
    if (alarm_status == ALARM_CHILL) {
        if (unlink(req_fifo_path)) {
            error_sys("cleanup", "error on unlinking public FIFO");
        }
    }

    if (max_threads != -1) {
        if (sem_destroy(&thread_locker)) {
            error_sys("cleanup", "couldn't destroy thread locker");
        }
    }

    if (bath_capacity != -1) {
        if (sem_destroy(&bathroom_locker)) {
            error_sys("cleanup", "couldn't destroy bathroom locker");
        }
        free(bath_places);
    }
}

/* -------------------------------------------------------------------------
                            REQUEST PROCESSOR
/-------------------------------------------------------------------------*/
void* request_processor(void *arg) {
    if (arg == NULL) {
        // unlock space for threads
        if (max_threads != -1) {
            if (sem_post(&thread_locker)) {
                error_sys("request_processor", "couldn't unlock thread locker");
            }
        }
        // unlock space in bathroom
        if (bath_capacity != -1) {
            if (sem_post(&bathroom_locker)) {
                error_sys("request_processor", "couldn't unlock bathroom locker");
            }
        }
        return NULL;
    }

    void **args = (void**)arg;

    if (args[0] == NULL || args[1] == NULL) {
        if (max_threads != -1) {
            if (sem_post(&thread_locker)) {
                error_sys("request_processor", "couldn't unlock thread locker");
            }
        }
        // unlock space in bathroom
        if (bath_capacity != -1) {
            if (args[1] != NULL) {
                int bath_freed = *((int*)args[1]);
                if (bath_freed >= 0 && bath_freed < bath_capacity)
                    bath_places[bath_freed] = OPEN;
            }
            if (sem_post(&bathroom_locker)) {
                error_sys("request_processor", "couldn't unlock bathroom locker");
            }
        }
        if (args[0] != NULL) free(args[0]);
        if (args[1] != NULL) free(args[1]);
        free(args);
        return NULL;
    }

    request_t *request = ((request_t*)args[0]);
    int bath_order = *((int*)args[1]);
    /* -------------------------------------------------------------------------
                                CREATE REPLY
    /-------------------------------------------------------------------------*/
    request_t reply;

    if (bath_open) {
        fill_reply(&reply, request->id, request->pid, request->tid, request->dur, bath_order + 1);
    } else {
        fill_reply_error(&reply, request->id, request->pid, request->tid);
    }

    free(args[0]);
    free(args[1]);
    free(args);

    /* -------------------------------------------------------------------------
                                PREPARE REPLY FIFO
    /-------------------------------------------------------------------------*/
    char reply_fifo_path[BUFFER_SIZE];
    sprintf(reply_fifo_path, "/tmp/%d.%ld", reply.pid, reply.tid);

    int reply_fifo;

    reply_fifo = open(reply_fifo_path, O_WRONLY);

    if (reply_fifo == -1) {
        if (errno == ENXIO || errno == ENOENT) {
            char program[BUFFER_SIZE];
            sprintf(program, "reply %d", reply.id);
            if (write_log(&reply, "GAVUP")) {
                error_sys(program, "couldn't write log");
            }
        } else {
            char program[BUFFER_SIZE];
            sprintf(program, "reply %d", reply.id);
            error_sys(program, "couldn't open private reply FIFO");
        }
    }

    /* -------------------------------------------------------------------------
                                SEND REPLY
    /-------------------------------------------------------------------------*/
    if (reply_fifo != -1) {
        int ret;

        while (1) {
            ret = write(reply_fifo, &reply, sizeof(request_t));

            // on error
            if (ret == -1) {
                if (errno == EPIPE) {   // Client closed reply FIFO
                    if (write_log(&reply, "GAVUP")) {
                        char program[BUFFER_SIZE];
                        sprintf(program, "reply %d", reply.id);
                        error_sys(program, "couldn't write log");
                    }
                    break;
                /*} else if (errno == EAGAIN) { // Reply FIFO is full, try again after some time
                    usleep(FIFO_WAIT_TIME);
                    continue;*/
                } else { // Couldn't send reply
                    char program[BUFFER_SIZE];
                    sprintf(program, "reply %d", reply.id);
                    error_sys(program, "couldn't write to private reply FIFO");
                    break;
                }
            }
            // on success
            break;
        }

        if (close(reply_fifo)) {
            char program[BUFFER_SIZE];
            sprintf(program, "reply %d", reply.id);
            error_sys(program, "couldn't close private reply FIFO");
        }
    }

    if (reply.dur > 0) {
        if (write_log(&reply, "ENTER")) {
            char program[BUFFER_SIZE];
            sprintf(program, "reply %d", reply.id);
            error_sys(program, "couldn't write log");
        }
    } else {
        if (write_log(&reply, "2LATE")) {
            char program[BUFFER_SIZE];
            sprintf(program, "reply %d", reply.id);
            error_sys(program, "couldn't write log");
        }
    }

    /* -------------------------------------------------------------------------
                                PROCESS REQUEST USAGE TIME
    /-------------------------------------------------------------------------*/
    if (reply.dur > 0) { // if it was accepted
        if (usleep(reply.dur)) {
            char program[BUFFER_SIZE];
            sprintf(program, "reply %d", reply.id);
            error_sys(program, "couldn't process request duration");
        }

        if (write_log(&reply, "TIMUP")) {
            char program[BUFFER_SIZE];
            sprintf(program, "reply %d", reply.id);
            error_sys(program, "couldn't write log");
        }
    }

    /* -------------------------------------------------------------------------
                                UNLOCKING LOCKERS
    /-------------------------------------------------------------------------*/
    // unlock space for threads
    if (max_threads != -1) {
        if (sem_post(&thread_locker)) {
            error_sys("request_processor", "couldn't unlock thread locker");
        }
    }
    // unlock space in bathroom
    if (bath_capacity != -1) {
        if (bath_order >= 0 && bath_order < bath_capacity) {
            bath_places[bath_order] = OPEN;
        }
        if (sem_post(&bathroom_locker)) {
            error_sys("request_processor", "couldn't unlock bathroom locker");
        }
    }

    return NULL;
}
