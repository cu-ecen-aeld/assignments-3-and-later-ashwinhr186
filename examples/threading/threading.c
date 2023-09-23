#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    unsigned int time_to_sleep = ((struct thread_data*)thread_param)->wait_to_obtain_ms;
    unsigned int time_to_hold = ((struct thread_data*)thread_param)->wait_to_release_ms;
    pthread_mutex_t* mutex = ((struct thread_data*)thread_param)->mutex;
    usleep(time_to_sleep*1000);
    int ret = pthread_mutex_lock(mutex);
    if(ret != 0)
    {
        syslog(LOG_ERR, "pthread_mutex_lock() failed: %d", ret);
        return NULL;
    }
    usleep(time_to_hold*1000);
    ret = pthread_mutex_unlock(mutex);
    if(ret != 0)
    {
        syslog(LOG_ERR, "pthread_mutex_unlock() failed: %d", ret);
        return NULL;
    }
    ((struct thread_data*)thread_param)->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data* thread_func_args = (struct thread_data*)malloc(sizeof(struct thread_data));
    thread_func_args->mutex = mutex;
    thread_func_args->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_func_args->wait_to_release_ms = wait_to_release_ms;
    int ret = pthread_create(thread, NULL, threadfunc, thread_func_args);
    if(ret == 0)
        return true;
    syslog(LOG_ERR, "pthread_create() failed: %s", strerror(ret));
    return false;
}

