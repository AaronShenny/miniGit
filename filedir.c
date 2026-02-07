#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

static void list_entries(const char *base_path)
{
    DIR *dirp;
    struct dirent *dp;
    struct stat st;
    char path[4096];

    dirp = opendir(base_path);
    if (!dirp) {
        perror("opendir");
        return;
    }

    while ((dp = readdir(dirp)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 ||
            strcmp(dp->d_name, "..") == 0 || strcmp(dp->d_name, ".git") == 0 || dp->d_name[0] =='.')
            continue;

        snprintf(path, sizeof(path), "%s/%s", base_path, dp->d_name);

        if (stat(path, &st) == -1) {
            perror("stat");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            printf("[DIR ] %s\n", path);
            list_entries(path);
        }
        else if (S_ISREG(st.st_mode)) {
            printf("[FILE] %s\n", path);
        }
        else {
            printf("[OTHER] %s\n", path);
        }
    }

    closedir(dirp);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        return 1;
    }

    list_entries(argv[1]);
    return 0;
}
