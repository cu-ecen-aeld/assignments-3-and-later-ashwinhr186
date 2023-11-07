#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <stdbool.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT "9000"
#define BACKLOG 10
#define BUF_LEN 1024
#define TIMESTAMP_STRING_LENGTH 100

#define USE_AESD_CHAR_DEVICE 1

#ifdef USE_AESD_CHAR_DEVICE
    #define PATHNAME "/dev/aesdchar"
#else
    #define PATHNAME "/var/tmp/aesdsocketdata"
#endif

int sockfd;
int fd;
pthread_mutex_t mutex;

#ifndef USE_AESD_CHAR_DEVICE
/*Structure for timer thread*/
typedef struct {
    pthread_t thread_id;
    pthread_mutex_t mutex;
    int interval_in_s;
}timestamp_t;

timestamp_t timestamp;
#endif

/*structure for thread*/
typedef struct thread_data {
    pthread_t thread_id;
    int newsockfd;
    struct sockaddr_storage clientaddr;
    pthread_mutex_t mutex;
    int thread_complete_success;
}thread_data_t;


/*structure for slist nodes*/
typedef struct slist_data_s {
    thread_data_t thread_params;
    SLIST_ENTRY(slist_data_s) entries;
} slist_data_t;

/*Initialize slist head*/
slist_data_t *datap = NULL;
SLIST_HEAD(slisthead, slist_data_s) head;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/************************************************************************************************
                        Thread function to handle socket communication
************************************************************************************************/

void* thread_socket(void* thread_param) {

    char buf[BUF_LEN];
    char transmit_buffer[BUF_LEN];
    size_t recv_bytes;
    thread_data_t *thread_func_param = (thread_data_t *)thread_param;
    struct sockaddr_storage clientaddr = thread_func_param->clientaddr;
    char s[INET6_ADDRSTRLEN];
    inet_ntop(clientaddr.ss_family, get_in_addr((struct sockaddr *)&clientaddr), s, sizeof s);
    syslog(LOG_USER | LOG_INFO, "Accepted connection from %s\n", s);

    bool newline = false;
    bool ioctl_present = false;
    char ioctl_string[] = "AESDCHAR_IOCSEEKTO";
    memset(buf, '\0', BUF_LEN);

    while(1) {
        recv_bytes = recv(thread_func_param->newsockfd, buf, BUF_LEN, 0);
        if(recv_bytes > 0) {
            printf("recv_bytes: %zu\n", recv_bytes);
            printf("recv buffer: %s", buf);
        }
           
        if(recv_bytes == -1) {
            perror("recv");
            exit(EXIT_FAILURE);
        }

        if(recv_bytes == 0) {
            break;
        }

        #ifndef USE_AESD_CHAR_DEVICE
        pthread_mutex_lock(&thread_func_param->mutex);
        #endif

        #ifndef USE_AESD_CHAR_DEVICE
        pthread_mutex_unlock(&thread_func_param->mutex);
        #endif
        if(memchr(buf, '\n', recv_bytes) != NULL) {
            printf("Newline found\n");
            newline = true;
        }
        printf("here\n");
        if(newline) {
            if(strncmp(buf, ioctl_string, strlen(ioctl_string))==0) {
                ioctl_present = true;
                struct aesd_seekto seekto;
                sscanf(buf, "AESDCHAR_IOCSEEKTO:%d,%d", &seekto.write_cmd, &seekto.write_cmd_offset);
                fd = open(PATHNAME, O_RDWR, 0666);
                ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto);
            }

            else {
                printf("Not an ioctl\n");
                fd = open(PATHNAME, O_RDWR | O_APPEND | O_CREAT, 0666);
                write(fd, buf, recv_bytes);
                close(fd);
            }

            if(!ioctl_present) {
                printf("Reopening file\n");
                fd = open(PATHNAME, O_RDONLY, 0666);
            }
            //lseek(fd, 0, SEEK_SET);
            memset(transmit_buffer, '\0', BUF_LEN);
            ssize_t read_bytes;
            while((read_bytes = read(fd, transmit_buffer, BUF_LEN)) > 0) {
                printf("Read: %s\n", transmit_buffer);
                send(thread_func_param->newsockfd, transmit_buffer, read_bytes, 0);
            }
        }
    }
    
    printf("Read complete\n");
    close(thread_func_param->newsockfd);
    //close(fd);
    syslog(LOG_INFO, "Closed connection from %s\n", s);
    thread_func_param->thread_complete_success = 1;
    return thread_func_param;
    //pthread_exit(NULL);
}

/************************************************************************************************
                        Thread function to log timestamp
************************************************************************************************/

/*This part of the code was referred from Suraj Ajjampur's github repository
Credits to: https://github.com/cu-ecen-aeld/assignments-3-and-later-Suraj-Ajjampur/blob/master/server/aesdsocket.c*/

#ifndef USE_AESD_CHAR_DEVICE
void* thread_timestamp(void* thread_param) {
    timestamp_t *thread_func_param = (timestamp_t *)thread_param;

    time_t current_time;
    struct tm *local_time;
    char formatted_timestamp[TIMESTAMP_STRING_LENGTH];
    struct timespec time_spec;
    syslog(LOG_INFO, "Time logging thread activated.");
    while(1) {
        clock_gettime(CLOCK_MONOTONIC, &time_spec);
        time_spec.tv_sec += thread_func_param->interval_in_s;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time_spec, NULL);

        time(&current_time);
        local_time = localtime(&current_time);
        int length_of_timestamp = strftime(formatted_timestamp, sizeof(formatted_timestamp), "timestamp: %Y, %b %d, %H:%M:%S\n", local_time);

        /*Get mutex lock and write to file*/

        pthread_mutex_lock(&thread_func_param->mutex);
        int bytes_written = write(fd, formatted_timestamp, length_of_timestamp);
        if(bytes_written == -1) {
            perror("write");
        }
        pthread_mutex_unlock(&thread_func_param->mutex);

        /*Unlock mutex*/
    }
    pthread_exit(NULL);
}
#endif


/************************************************************************************************
                        Signal handler to handle SIGINT and SIGTERM
************************************************************************************************/

static void signal_handler(int signo) {
    syslog(LOG_INFO, "Caught Signal, exiting\n");
    close(sockfd);
    close(fd);
    #ifndef USE_AESD_CHAR_DEVICE
    remove(PATHNAME);
    #endif
    closelog();
    while(!SLIST_FIRST(&head)) {
        datap = SLIST_FIRST(&head);
        SLIST_REMOVE_HEAD(&head, entries);
        pthread_join(datap->thread_params.thread_id, NULL);
        pthread_mutex_destroy(&datap->thread_params.mutex);
        free(datap);
    }
    pthread_mutex_destroy(&mutex);
    exit(EXIT_SUCCESS);
}


/************************************************************************************************
                        Main function
************************************************************************************************/

int main(int argc, char *argv[]) {

    /*Open syslog connection for logging*/
    openlog("aesdsocket", LOG_PID, LOG_USER);

    /*Initialize mutex*/
    pthread_mutex_init(&mutex, NULL);

    /*Check whether -d argument is present*/
    if(argc == 2) {
        if(strcmp(argv[1], "-d") == 0) {
            /*Daemonize the process*/
            pid_t pid, sid;
            pid = fork();
            if(pid < 0) {
                exit(EXIT_FAILURE);
            }
            if(pid > 0) {
                exit(EXIT_SUCCESS);
            }
            //umask(0);
            sid = setsid();
            if(sid < 0) {
                exit(EXIT_FAILURE);
            }
            if((chdir("/")) < 0) {
                exit(EXIT_FAILURE);
            }
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
        }
    }
    #ifndef USE_AESD_CHAR_DEVICE
    remove(PATHNAME);
    #endif

    /*Register signal_handler as our signal handler for SIGINT*/
    if (signal (SIGINT, signal_handler) == SIG_ERR) {
        fprintf (stderr, "Cannot handle SIGINT!\n");
        exit (EXIT_FAILURE);
    }

    /*Register signal_handler as our signal handler for SIGTERM*/
    if (signal (SIGTERM, signal_handler) == SIG_ERR) {
        fprintf (stderr, "Cannot handle SIGINT!\n");
        exit (EXIT_FAILURE);
    }    

    /*Define variables and structures for socket communication*/
    struct addrinfo hints;
    struct sockaddr_storage clientaddr;
    struct addrinfo *servinfo, *p;
    socklen_t clientsize = sizeof(struct sockaddr_storage);
    int status;
    int newsockfd;
    int yes = 1;

    /*Define head for slist*/
    SLIST_INIT(&head);

    /*Fill up hints structure for getaddrinfo*/
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    /*Get address information using getaddrinfo*/
    if((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    /*loop through all the results, create a socket and bind to the first we can*/
    for(p = servinfo; p != NULL;p = p->ai_next) {
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket:");
            continue;
        }
        
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(EXIT_FAILURE);
        }

        if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("bind");
            continue;
        }
        break;
    }

    /*servinfo is no longer required*/
    freeaddrinfo(servinfo);

    if(p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(EXIT_FAILURE);
    }

    #ifndef USE_AESD_CHAR_DEVICE
    timestamp.interval_in_s = 10;
    timestamp.mutex = mutex;
    pthread_create(&timestamp.thread_id, NULL, thread_timestamp, &timestamp);
    #endif

    if(listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while(1) {
        /*Accept incoming connections*/
        newsockfd = accept(sockfd, (struct sockaddr*)&clientaddr, &clientsize);
        if(newsockfd == -1) {
            perror("accept");
            continue;
        }

        /*Add thread_params to the slist*/
        datap = (slist_data_t *)malloc(sizeof(slist_data_t));
        if(datap == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        datap->thread_params.newsockfd = newsockfd;
        datap->thread_params.clientaddr = clientaddr;
        datap->thread_params.thread_complete_success = 0;
        datap->thread_params.mutex = mutex;

        pthread_create(&(datap->thread_params.thread_id), NULL, thread_socket, &datap->thread_params);
        SLIST_INSERT_HEAD(&head, datap, entries);

        /*Check if any thread has completed*/
        SLIST_FOREACH(datap, &head, entries) {
            if(datap->thread_params.thread_complete_success == 1) {
                int ret = pthread_join(datap->thread_params.thread_id, NULL);
                if(ret != 0) {
                    perror("pthread_join");
                    exit(EXIT_FAILURE);
                }
                SLIST_REMOVE(&head, datap, slist_data_s, entries);
                free(datap);           
            }
        }
    }    
}