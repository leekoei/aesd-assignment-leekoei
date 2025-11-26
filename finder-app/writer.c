#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    openlog(NULL, 0, LOG_USER);

    // Check if two arguments are provided
    if (argc != 3) {
        syslog(LOG_ERR, "Require exactly two arguments, but provided: %d\n", argc - 1);
        closelog();
        return 1;
    }
    
    // Open the file for writing
    FILE *file = fopen(argv[1], "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Error: could not open file: %s\n", argv[1]);
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s\n", argv[2], argv[1]);

    // Write the string to the file
    if (fprintf(file, "%s", argv[2]) < 0) {
        syslog(LOG_ERR, "Error writing to file: %s\n", argv[1]);
        fclose(file);
        closelog();
        return 1;
    }
    fclose(file);

    closelog();
    return 0;
}
