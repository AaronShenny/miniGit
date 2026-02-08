#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

int main(){
    DIR* dirp;
    dirp = opendir(".");
    if (!dirp) {
        perror("opendir");
        return 1;
    }
    printf("%p\n", (void*)dirp);
    return 0;
}
