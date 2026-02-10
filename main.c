#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>

#ifdef _WIN32
    #include <direct.h>   // _mkdir
    #include <windows.h>  // GetModuleFileNameA
#else
    #include <unistd.h>   // rmdir
#endif
void ensure_dir(const char *path) {
    #ifdef _WIN32
        _mkdir(path);
    #else
        mkdir(path, 0755);
    #endif
}

void delete_dir(const char *path) {
#ifdef _WIN32
    char cmd[500];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", path);
    system(cmd);
#else
    char cmd[500];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    system(cmd);
#endif
}

#include <sys/stat.h>
#include <sys/types.h>
typedef struct TreeNode {
    char name[256];               // file or folder name
    int is_file;                  // 1 = file, 0 = directory
    unsigned long blob_hash;      // valid only if file
    struct TreeNode *child;       // first child
    struct TreeNode *sibling;     // next node in same directory
} TreeNode;
typedef struct Commit {
    char id[41];
    char parent_id[41];   // store parent COMMIT ID, not pointer
    char message[256];
    char timestamp[64];
    TreeNode *tree;
} Commit;







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
TreeNode* create_node(const char *name, int is_file) {
    TreeNode *node = (TreeNode*)malloc(sizeof(TreeNode));
    if (!node) return NULL;

    strcpy(node->name, name);
    node->is_file = is_file;
    node->blob_hash = 0;
    node->child = NULL;
    node->sibling = NULL;

    return node;
}

Commit* create_commit(const char *message, TreeNode *tree){
    Commit *commit = (Commit*)malloc(sizeof(Commit));
    if (!commit) return NULL;

    // Generate commit ID (simple hash of message + timestamp)
    //snprintf(commit->id, sizeof(commit->id), "%lu", hash_file(message) ^ hash_file(tree->name));
    unsigned long id = (unsigned long)time(NULL);
    snprintf(commit->id, sizeof(commit->id), "%lu", id);


    strncpy(commit->message, message, sizeof(commit->message) - 1);
    commit->message[sizeof(commit->message) - 1] = '\0';

    time_t now = time(NULL);
    strftime(commit->timestamp, sizeof(commit->timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    commit->tree = tree;
    FILE *head = fopen(".bit/HEAD", "r");
    char parent_id[41];
    if (head && fgets(commit->parent_id, sizeof(commit->parent_id), head)) {
        commit->parent_id[strcspn(commit->parent_id, "\n")] = 0;
    } else {
        strcpy(commit->parent_id, "None");
    }
    if (head) fclose(head);

    

    return commit;
}
void print_commit(const char *commit_id) {
    char path[256];
    snprintf(path, sizeof(path), ".bit/commits/%s.txt", commit_id);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("error: cannot open commit %s\n", commit_id);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Tree Snapshot:", 14) == 0)
            break;  // skip tree details in log
        printf("%s", line);
    }
    printf("\n");

    fclose(fp);
}

TreeNode* build_tree(const char *base_path) {
    DIR *dirp;
    struct dirent *dp;
    struct stat st;
    char path[4096];

    dirp = opendir(base_path);
    if (!dirp) {
        perror("opendir");
        return NULL;
    }

    // Create root node for this directory
    TreeNode *root = create_node(base_path, 0);
    TreeNode *last_child = NULL;

    while ((dp = readdir(dirp)) != NULL) {

        // Ignore . .. .bit and hidden files
        if (strcmp(dp->d_name, ".") == 0 ||
            strcmp(dp->d_name, "..") == 0 ||
            strcmp(dp->d_name, ".bit") == 0 ||
            dp->d_name[0] == '.')
            continue;

        snprintf(path, sizeof(path), "%s/%s", base_path, dp->d_name);

        if (stat(path, &st) == -1) {
            perror("stat");
            continue;
        }

        TreeNode *new_node = NULL;

        // Directory
        if (S_ISDIR(st.st_mode)) {
            new_node = build_tree(path);
            if (!new_node) continue;
        }
        // Regular file
        else if (S_ISREG(st.st_mode)) {
            new_node = create_node(dp->d_name, 1);
            new_node->blob_hash = hash_file(path);
            store_blob(path);
        }
        else {
            continue;
        }

        // Attach to tree (child–sibling)
        if (root->child == NULL) {
            root->child = new_node;
        } else {
            last_child->sibling = new_node;
        }

        last_child = new_node;
    }

    closedir(dirp);
    return root;
}
void save_tree(TreeNode *node, FILE *fp, int depth) {
    if (!node) return;

    for (TreeNode *cur = node; cur; cur = cur->sibling) {
        for (int i = 0; i < depth; i++)
            fprintf(fp, "  ");

        if (cur->is_file)
            fprintf(fp, "F %s %lu\n", cur->name, cur->blob_hash);
        else
            fprintf(fp, "D %s\n", cur->name);

        save_tree(cur->child, fp, depth + 1);
    }
}

/* ---------- Print tree structure ---------- */
void print_tree(TreeNode *node, int depth) {
    if (!node) return;

    // Print siblings
    for (TreeNode *sibling = node; sibling; sibling = sibling->sibling) {
        // Print indentation
        for (int i = 0; i < depth; i++) {
            printf("  ");
        }

        // Print node name and type
        if (sibling->is_file) {
            printf("[FILE] %s (hash: %lu)\n", sibling->name, sibling->blob_hash);
        } else {
            printf("[DIR] %s\n", sibling->name);
        }

        // Print children
        if (sibling->child) {
            print_tree(sibling->child, depth + 1);
        }
    }
}

static int is_dot_or_dotdot(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static int should_preserve_entry(const char *name) {
    return strcmp(name, ".bit") == 0 ||
           strcmp(name, "bit") == 0 ||
           strcmp(name, "bit.exe") == 0;
}

static int remove_path_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        struct dirent *dp;
        if (!dir) return -1;

        while ((dp = readdir(dir)) != NULL) {
            if (is_dot_or_dotdot(dp->d_name)) continue;

            char child[4096];
#ifdef _WIN32
            snprintf(child, sizeof(child), "%s\\%s", path, dp->d_name);
#else
            snprintf(child, sizeof(child), "%s/%s", path, dp->d_name);
#endif
            remove_path_recursive(child);
        }
        closedir(dir);
#ifdef _WIN32
        return _rmdir(path);
#else
        return rmdir(path);
#endif
    }

    return remove(path);
}

void clean_working_directory() {
    DIR *dir = opendir(".");
    struct dirent *dp;

    if (!dir) return;

    while ((dp = readdir(dir)) != NULL) {
        if (is_dot_or_dotdot(dp->d_name) || should_preserve_entry(dp->d_name)) {
            continue;
        }

        remove_path_recursive(dp->d_name);
    }

    closedir(dir);
}

void restore_commit(const char *commit_id) {
    char commit_path[256];
    snprintf(commit_path, sizeof(commit_path), ".bit/commits/%s.txt", commit_id);

    FILE *fp = fopen(commit_path, "r");
    if (!fp) {
        printf("bit: Commit %s not found\n", commit_id);
        return;
    }

    clean_working_directory();

    char line[1024];
    int found_tree = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Tree Snapshot:", 14) == 0) {
            found_tree = 1;
            break;
        }
    }

    if (!found_tree) {
        fclose(fp);
        return;
    }

    char dir_stack[256][4096];
    int base_depth = -1;

    while (fgets(line, sizeof(line), fp)) {
        int spaces = 0;
        while (line[spaces] == ' ') spaces++;

        if (line[spaces] != 'D' && line[spaces] != 'F') {
            continue;
        }

        int raw_depth = spaces / 2;
        if (base_depth < 0) base_depth = raw_depth;
        int depth = raw_depth - base_depth;
        if (depth < 0 || depth >= 256) continue;

        if (line[spaces] == 'D') {
            char name[256];
            if (sscanf(line + spaces, "D %255s", name) != 1) continue;

            if (depth == 0) {
                if (strcmp(name, ".") == 0) {
                    snprintf(dir_stack[depth], sizeof(dir_stack[depth]), ".");
                } else {
                    snprintf(dir_stack[depth], sizeof(dir_stack[depth]), "%s", name);
                    ensure_dir(dir_stack[depth]);
                }
            } else {
                if (strcmp(dir_stack[depth - 1], ".") == 0) {
                    snprintf(dir_stack[depth], sizeof(dir_stack[depth]), "%s", name);
                } else {
                    char parent_path[4096];
                    snprintf(parent_path, sizeof(parent_path), "%s", dir_stack[depth - 1]);
#ifdef _WIN32
                    snprintf(dir_stack[depth], sizeof(dir_stack[depth]), "%s\\%s", parent_path, name);
#else
                    snprintf(dir_stack[depth], sizeof(dir_stack[depth]), "%s/%s", parent_path, name);
#endif
                }
                ensure_dir(dir_stack[depth]);
            }
        } else {
            char name[256];
            unsigned long hash;
            if (sscanf(line + spaces, "F %255s %lu", name, &hash) != 2) continue;

            char dst_path[4096];
            if (depth <= 0 || strcmp(dir_stack[depth - 1], ".") == 0) {
                snprintf(dst_path, sizeof(dst_path), "%s", name);
            } else {
#ifdef _WIN32
                snprintf(dst_path, sizeof(dst_path), "%s\\%s", dir_stack[depth - 1], name);
#else
                snprintf(dst_path, sizeof(dst_path), "%s/%s", dir_stack[depth - 1], name);
#endif
            }

            char blob_path[256];
            snprintf(blob_path, sizeof(blob_path), ".bit/objects/blobs/%lu", hash);

            FILE *src = fopen(blob_path, "rb");
            FILE *dst = fopen(dst_path, "wb");
            if (src && dst) {
                int c;
                while ((c = fgetc(src)) != EOF) {
                    fputc(c, dst);
                }
            }
            if (src) fclose(src);
            if (dst) fclose(dst);
        }
    }

    fclose(fp);
}



/* ---------- MAIN ---------- */
int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("bit: usage:\n");
        printf("  bit init\n");
        printf("  bit add\n");
        printf("  bit commit <message>\n");
        printf("  bit log\n");
        printf("  bit restore <commit_id>\n");
        printf("  bit help\n");
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
    else if(strcmp(argv[1], "add") == 0) {
        if (checkdir() != 0) {
            printf("bit: Not a bit repository. Use `bit init` first.\n");
            return 1;
        }

        TreeNode *root = build_tree(".");
        
        // Print the tree
        print_tree(root, 0);
    
        printf("bit: Added files to staging area (not fully implemented).\n");
        return 0;
    }
    else if(strcmp(argv[1], "commit") == 0) {
        if (checkdir() != 0) {
            printf("bit: Not a bit repository. Use `bit init` first.\n");
            return 1;
        }

        if (argc < 3) {
            printf("bit: Commit message required. Usage: `bit commit <message>`\n");
            return 1;
        }

        TreeNode *root = build_tree(".");
        Commit *new_commit = create_commit(argv[2], root);

        // For demonstration, we just print the commit info
        printf("Commit ID: %s\n", new_commit->id);
        printf("Message: %s\n", new_commit->message);
        printf("Timestamp: %s\n", new_commit->timestamp);
        
        //Passing the commit details to a txt file in .bit/commits directory
        char commit_path[256];
        sprintf(commit_path, ".bit/commits/%s.txt", new_commit->id);
        FILE *commit_file = fopen(commit_path, "w");
        if (commit_file) {  
            fprintf(commit_file, "Commit ID: %s\n", new_commit->id);
            fprintf(commit_file, "Message: %s\n", new_commit->message);
            fprintf(commit_file, "Timestamp: %s\n", new_commit->timestamp);
            fprintf(commit_file, "Tree Snapshot:\n");
            save_tree(root, commit_file, 1);

            fprintf(commit_file, "Parent Commit: %s\n", strcmp(new_commit->parent_id, "None") == 0 ? "None" : new_commit->parent_id);
            fclose(commit_file);
        }

        FILE *HEAD = fopen(".bit/HEAD", "w");
        if (HEAD) {
            fprintf(HEAD, "%s", new_commit->id);
            fclose(HEAD);
        }
        // In a full implementation, we would save the commit and update HEAD
        printf("bit: Committed changes (not fully implemented).\n");
        return 0;
    }
    else if (strcmp(argv[1], "log") == 0) {
        if (checkdir() != 0) {
            printf("bit: Not a bit repository.\n");
            return 1;
        }

        FILE *head = fopen(".bit/HEAD", "r");
        if (!head) {
            printf("bit: No commits yet.\n");
            return 0;
        }

        char current_id[41];
        if (!fgets(current_id, sizeof(current_id), head)) {
            fclose(head);
            printf("bit: No commits yet.\n");
            return 0;
        }
        fclose(head);

        current_id[strcspn(current_id, "\n")] = 0;

        while (strcmp(current_id, "None") != 0) {
            print_commit(current_id);

            char path[256];
            snprintf(path, sizeof(path), ".bit/commits/%s.txt", current_id);
            FILE *fp = fopen(path, "r");
            if (!fp) break;

            char line[512];
            char parent[41] = "None";

            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "Parent Commit:", 14) == 0) {
                    sscanf(line, "Parent Commit: %40s", parent);
                    break;
                }
            }

            fclose(fp);
            strcpy(current_id, parent);
        }

        return 0;
    }
    else if (strcmp(argv[1], "restore") == 0) {
        if (argc < 3) {
            printf("Usage: bit restore <commit_id>\n");
            return 1;
        }
        restore_commit(argv[2]);
        printf("bit: Restore complete.\n");
        return 0;
    }

    else if (strcmp(argv[1], "help") == 0) {
        printf("bit: usage:\n");
        printf("  bit init\n");
        printf("  bit add\n");
        printf("  bit commit <message>\n");
        printf("  bit log\n");
        printf("  bit restore <commit_id>\n");
        printf("  bit help\n");
        return 0;
    }


    /* ---------- UNKNOWN COMMAND ---------- */
    printf("bit: Unknown command '%s'. See `bit help`\n", argv[1]);
    return 1;
}
