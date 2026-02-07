#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

int main(int argc, char **argv)
{
    DIR *dirp;
    struct dirent *dp;
    struct stat st;
    char path[4096];
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        return 1;
    }

    dirp = opendir(argv[1]);
    if (!dirp) {
        perror("opendir");
        return 1;
    }

    while ((dp = readdir(dirp)) != NULL) {

        /* Skip . and .. */
        if (strcmp(dp->d_name, ".") == 0 ||
            strcmp(dp->d_name, "..") == 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s", argv[1], dp->d_name);
        if (stat(path, &st) == -1) {
            perror("stat");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            printf("[DIR ] %s\n", path);
        }
        else if (S_ISREG(st.st_mode)) {
            printf("[FILE] %s\n", path);
        }
        else {
            printf("[OTHER] %s\n", path);
        }
    }

    closedir(dirp);
    return 0;
}
