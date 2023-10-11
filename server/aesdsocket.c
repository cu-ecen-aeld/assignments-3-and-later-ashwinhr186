#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT "9000"
#define BACKLOG 10
#define BUF_LEN 1000000

char *pathname = "/var/tmp/aesdsocketdata";
int sockfd;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

static void signal_handler(int signo) {
    syslog(LOG_INFO, "Caught Signal, exiting\n");
    close(sockfd);
    remove(pathname);
    closelog();
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    /*Open syslog connection for logging*/
    openlog("aesdsocket", LOG_PID, LOG_USER);

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
    char s[INET6_ADDRSTRLEN];
    int status;
    int newsockfd;
    int yes = 1;
    char buf[BUF_LEN];
    char transmit_buffer[BUF_LEN];
    size_t buflen = 0; 

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

    freeaddrinfo(servinfo);

    if(p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(EXIT_FAILURE);
    }

    if(listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while(1) {
        memset(transmit_buffer, '\0', BUF_LEN);
        newsockfd = accept(sockfd, (struct sockaddr*)&clientaddr, &clientsize);
        if(newsockfd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(clientaddr.ss_family, get_in_addr((struct sockaddr *)&clientaddr), s, sizeof s);
        syslog(LOG_USER | LOG_INFO, "Accepted connection from %s\n", s);
        int fd = open(pathname, O_RDWR | O_APPEND | O_CREAT, 0666);
        if(fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        while(1) {
            memset(buf, '\0', BUF_LEN);
            buflen = recv(newsockfd, buf, BUF_LEN, 0);
            //printf("buf received: %s\n", buf);
            write(fd, buf, buflen);
            if(buf[buflen-1] == '\n')
                break;
        }
        //off_t filesize = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        memset(transmit_buffer, '\0', BUF_LEN);
        if(transmit_buffer == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        int read_bytes = read(fd, transmit_buffer, BUF_LEN);
        if(read_bytes == -1) {
            perror("read");
            exit(EXIT_FAILURE);
        }
        //printf("transmit_buffer: %s\n", transmit_buffer);
        send(newsockfd, transmit_buffer, read_bytes, 0);
        close(newsockfd);
        close(fd);
        syslog(LOG_INFO, "Closed connection from %s\n", s);
    }    
}





