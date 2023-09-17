#include "systemcalls.h"
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

bool do_system(const char *cmd)
{
    openlog("writer app", LOG_PID, LOG_USER);
    int ret;
    if(cmd==NULL) {
        if(system(cmd)==0) {
            syslog(LOG_ERR, "Shell not available !\n");
            //printf("Shell not available !\n");
        }
        else {
            syslog(LOG_INFO, "No command available to execute, please provide a command\n");
            //printf("No command available to execute, please provide a command\n");
        }
        closelog();
        return false;

    }
    else {
        ret=system(cmd);
        if(ret==0) {
            syslog(LOG_INFO, "Command %s successfully executed\n", cmd);
            closelog();
            return true;
        }        
        else {
            syslog(LOG_ERR, "Error: %s\n", strerror(errno));
            closelog();
            return false;
        }
    }
}

bool do_exec(int count, ...)
{
    int status;
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    openlog("writer app", LOG_PID, LOG_USER);
    pid_t childPid = fork();
    if(childPid == -1) {
        syslog(LOG_ERR, "Error in do_exec function: %s\n", strerror(errno));
        va_end(args);
        closelog();
        //return false;
        exit(EXIT_FAILURE);
    }

    if(childPid==0) {
        syslog(LOG_INFO, "Child process created successfully !\n");
        if(execv(command[0], command)==-1) {
            syslog(LOG_ERR, "Error in do_exec function: %s\n", strerror(errno));
            va_end(args);
            closelog();
            exit(EXIT_FAILURE);
        }
    }
    else {
        wait(&status);
        if(WIFEXITED(status)) {
            syslog(LOG_INFO, "Child process exited successfully !\n");
            va_end(args);
            closelog();
            return (WEXITSTATUS(status)==0);
        }
        va_end(args);
        closelog();
        return false;
    }
    return true;
}

bool do_exec_redirect(const char *outputfile, int count, ...)
{
    int status;
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    openlog("writer app", LOG_PID, LOG_USER);


    syslog(LOG_INFO, "File %s opened successfully !\n", outputfile);
    pid_t childPid = fork();
    if(childPid == -1) {
        syslog(LOG_ERR, "Error in do_exec function: %s\n", strerror(errno));
        va_end(args);
        closelog();
        return false;
    }
    else if(childPid==0) {
        syslog(LOG_INFO, "Child process created successfully !\n");
        FILE* file = freopen(outputfile, "w", stdout); //redirect stdout to outputfile;
        if(file==NULL) {
            syslog(LOG_ERR, "Error in do_exec_redirect function: %s\n", strerror(errno));
            va_end(args);
            closelog();
            exit(EXIT_FAILURE);
        }
        const char* filepath = command[0];
        execv(filepath, command);
        syslog(LOG_ERR, "Error in do_exec_redirect function: %s\n", strerror(errno));
        va_end(args);
        closelog();
        exit(EXIT_FAILURE);
    }

    wait(&status);

    if(WIFEXITED(status)) {
        syslog(LOG_INFO, "Child process exited successfully !\n");
        va_end(args);
        closelog();
        return (WEXITSTATUS(status)==0);
    }
    va_end(args);
    closelog();
    return false;
}
