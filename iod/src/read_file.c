#include <read_file.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

void read_file_to(void *scope, const char *property, const char *filename, setter set_value) {
    struct stat fs;
    int res = stat(filename, &fs);
    if (res == 0) {
        const size_t bufsize = fs.st_size + 1;
        char *buf = malloc(bufsize);
        FILE *f = fopen(filename, "r");
        if (f) {
            size_t nread = fread(buf, 1, bufsize, f);
            if (nread != bufsize - 1) {
                fprintf(stderr, "read %ld bytes, expected %ld\n", nread, bufsize);
            }
            if (ferror(f)) {
                fprintf(stderr, "error during read of %s: %d\n", filename, ferror(f));
                clearerr(f);
            }
            buf[nread] = 0;
            set_value(scope, property, buf);
        }
        fclose(f);
        free(buf);
    }
}
