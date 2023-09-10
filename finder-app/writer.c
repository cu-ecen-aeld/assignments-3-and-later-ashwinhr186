#include <stdio.h>
#include <syslog.h>

int main(int argc, char* argv[]) {
    openlog("Writer", 0, LOG_USER);
    if(argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: %d , expected: 2\nUsage: %s <pathname> <writestr", argc, argv[0]);
        return 1;
    }
    FILE *file = fopen(argv[1], "w");
    if(file == NULL) {
        syslog(LOG_ERR, "Error opening file: %s\n", argv[1]);
        return 1;
    }
    syslog(LOG_DEBUG, "Writing %s to file: %s\n", argv[2], argv[1]);
    int result = fprintf(file, "%s", argv[2]);
    if(result < 0) {
        syslog(LOG_ERR, "Error writing to file: %s\n", argv[1]);
        return 1;
    }
    syslog(LOG_INFO, "Content written successfully into %s\n", argv[1]);
    fclose(file);
    syslog(LOG_INFO, "File closed successfully\n");
    return 0;
}
