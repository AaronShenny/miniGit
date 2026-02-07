#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#ifdef _WIN32
    #include <direct.h>   // _mkdir
#else
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

typedef struct TreeNode {
    char name[256];               // file or folder name
    int is_file;                  // 1 = file, 0 = directory
    unsigned long blob_hash;      // valid only if file
    struct TreeNode *child;       // first child
    struct TreeNode *sibling;     // next node in same directory
} TreeNode;




/* ---------- Cross-platform mkdir wrapper ---------- */
int make_dir(const char *path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

/* ---------- Check if .bit directory exists ---------- */
int checkdir() {
    errno = 0;
    DIR *dir = opendir(".bit");
    if (dir) {
        closedir(dir);
        return 0;   // exists
    } else if (errno == ENOENT) {
        return 1;   // does not exist
    } else {
        return -1;  // error
    }
}

/* ---------- djb2 hash for file ---------- */
unsigned long hash_file(const char *filepath) {
    unsigned long hash = 5381;
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        return 0;
    }

    int c;
    while ((c = fgetc(fp)) != EOF) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }

    fclose(fp);
    return hash;
}

/* ---------- Store blob with deduplication ---------- */
void store_blob(const char *filepath) {
    unsigned long hash = hash_file(filepath);
    if (hash == 0) return;

    char blob_path[256];
    sprintf(blob_path, ".bit/objects/blobs/%lu", hash);

    /* Check if blob already exists */
    FILE *test = fopen(blob_path, "rb");
    if (test) {
        fclose(test);
        return;   // blob already stored
    }

    FILE *src = fopen(filepath, "rb");
    FILE *dst = fopen(blob_path, "wb");

    if (!src || !dst) {
        if (src) fclose(src);
        if (dst) fclose(dst);
        return;
    }

    int c;
    while ((c = fgetc(src)) != EOF) {
        fputc(c, dst);
    }

    fclose(src);
    fclose(dst);
}

TreeNode* build_tree(const char *base_path){
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
            //printf("[DIR ] %s\n", path);
            build_tree(path);
        }
        else if (S_ISREG(st.st_mode)) {
            //TreeNode 
        }
        else {
            //printf("[OTHER] %s\n", path);
        }
    }

    closedir(dirp);
}



/* ---------- MAIN ---------- */
int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("bit: <usage>\n");
        printf("bit init\n");
        return 1;
    }

    /* ---------- INIT COMMAND ---------- */
    if (strcmp(argv[1], "init") == 0) {

        int status = checkdir();

        if (status == 0) {
            printf("bit: Repository already initialized.\n");
            return 0;
        }

        if (status == -1) {
            printf("bit: Error checking repository.\n");
            return 1;
        }

        /* Create repository structure */
        make_dir(".bit");
        make_dir(".bit/objects");
        make_dir(".bit/objects/blobs");
        make_dir(".bit/objects/trees");
        make_dir(".bit/commits");

        FILE *head = fopen(".bit/HEAD", "w");
        if (head) fclose(head);

        FILE *log = fopen(".bit/activity.log", "w");
        if (log) fclose(log);

        printf("bit: Initialized empty repository.\n");
        return 0;
    }

    /* ---------- UNKNOWN COMMAND ---------- */
    printf("bit: Unknown command. Use `bit init`\n");
    return 1;
}

