#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

/* ===================== DATA STRUCTURES ===================== */

typedef struct TreeNode {
    char name[256];
    int is_file;
    unsigned long blob_hash;
    struct TreeNode *child;
    struct TreeNode *sibling;
} TreeNode;

/* ===================== FS HELPERS ===================== */

void ensure_dir(const char *p) { MKDIR(p); }

void delete_dir(const char *path) {
#ifdef _WIN32
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", path);
    system(cmd);
#else
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    system(cmd);
#endif
}

void clean_working_directory() {
    DIR *d = opendir(".");
    struct dirent *e;

    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") ||
            !strcmp(e->d_name, "..") ||
            !strcmp(e->d_name, ".bit") ||
            !strcmp(e->d_name, "bit") ||
            !strcmp(e->d_name, "bit.exe"))
            continue;

        delete_dir(e->d_name);
        remove(e->d_name);
    }
    closedir(d);
}

/* ===================== HASH + BLOB ===================== */

unsigned long hash_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    unsigned long h = 5381;
    int c;
    while ((c = fgetc(fp)) != EOF)
        h = ((h << 5) + h) + c;

    fclose(fp);
    return h;
}

void store_blob(const char *path, unsigned long h) {
    char blob[256];
    sprintf(blob, ".bit/objects/blobs/%lu", h);

    FILE *t = fopen(blob, "rb");
    if (t) { fclose(t); return; }

    FILE *s = fopen(path, "rb");
    FILE *d = fopen(blob, "wb");

    int c;
    while ((c = fgetc(s)) != EOF)
        fputc(c, d);

    fclose(s);
    fclose(d);
}

/* ===================== TREE ===================== */

TreeNode *new_node(const char *name, int is_file) {
    TreeNode *n = malloc(sizeof(TreeNode));
    strcpy(n->name, name);
    n->is_file = is_file;
    n->blob_hash = 0;
    n->child = n->sibling = NULL;
    return n;
}

TreeNode *build_tree(const char *base_path) {
    DIR *d = opendir(base_path);
    if (!d) return NULL;

    TreeNode *head = NULL, *last = NULL;
    struct dirent *e;

    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") ||
            !strcmp(e->d_name, "..") ||
            !strcmp(e->d_name, ".bit") ||
            !strcmp(e->d_name, "bit") ||
            !strcmp(e->d_name, "bit.exe"))
            continue;

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", base_path, e->d_name);

        DIR *test = opendir(full);
        TreeNode *n;

        if (test) {
            closedir(test);
            n = new_node(e->d_name, 0);
            n->child = build_tree(full);
        } else {
            unsigned long h = hash_file(full);
            if (!h) continue;
            store_blob(full, h);
            n = new_node(e->d_name, 1);
            n->blob_hash = h;
        }

        if (!head) head = n;
        else last->sibling = n;
        last = n;
    }

    closedir(d);
    return head;
}

/* ===================== TREE OBJECT ===================== */

void save_tree(TreeNode *n, FILE *fp, int depth) {
    for (; n; n = n->sibling) {
    	int i;
        for ( i = 0; i < depth; i++) fprintf(fp, "  ");
        if (n->is_file)
            fprintf(fp, "F %s %lu\n", n->name, n->blob_hash);
        else
            fprintf(fp, "D %s\n", n->name);
        save_tree(n->child, fp, depth + 1);
    }
}

void gen_id(char *out) {
    /* Make IDs unique across separate process runs. */
    unsigned long t = (unsigned long)time(NULL);
    unsigned long r = (unsigned long)rand();
    sprintf(out, "%lu_%lu", t, r);
}

void save_tree_object(TreeNode *root, const char *tree_id) {
    char path[256];
    sprintf(path, ".bit/objects/trees/%s.txt", tree_id);
    FILE *fp = fopen(path, "w");
    save_tree(root, fp, 0);
    fclose(fp);
}

/* ===================== RESTORE ===================== */

void restore_commit(const char *id) {
    char commit_path[256], tree_id[64];
    sprintf(commit_path, ".bit/commits/%s.txt", id);

    FILE *fp = fopen(commit_path, "r");
    if (!fp) { puts("commit not found"); return; }

    char line[256];
    while (fgets(line, sizeof(line), fp))
        if (sscanf(line, "Tree: %63s", tree_id) == 1)
            break;
    fclose(fp);

    clean_working_directory();

    char tree_path[256];
    sprintf(tree_path, ".bit/objects/trees/%s.txt", tree_id);
    fp = fopen(tree_path, "r");

    char stack[50][512];
    while (fgets(line, sizeof(line), fp)) {
        int sp = 0;
        while (line[sp] == ' ') sp++;
        int depth = sp / 2;

        char name[256];
        unsigned long h;

        if (line[sp] == 'D') {
            sscanf(line + sp, "D %s", name);
            if (depth == 0) strcpy(stack[0], name);
            else sprintf(stack[depth], "%s/%s", stack[depth-1], name);
            ensure_dir(stack[depth]);
        } else {
            sscanf(line + sp, "F %s %lu", name, &h);
            char dst[512];
            sprintf(dst, "%s/%s", stack[depth-1], name);
            char blob[256];
            sprintf(blob, ".bit/objects/blobs/%lu", h);

            FILE *s = fopen(blob, "rb");
            FILE *d = fopen(dst, "wb");
            int c;
            while ((c = fgetc(s)) != EOF) fputc(c, d);
            fclose(s); fclose(d);
        }
    }
    fclose(fp);
}

/* ===================== LOG ===================== */

void print_log() {
    FILE *h = fopen(".bit/HEAD", "r");
    if (!h) {
        puts("[log] No commits (.bit/HEAD missing)");
        return;
    }

    char current[64];
    if (!fgets(current, sizeof(current), h)) {
        puts("[log] Failed to read .bit/HEAD");
        fclose(h);
        return;
    }
    current[strcspn(current, "\n")] = 0;
    fclose(h);

    printf("[log] Starting from HEAD=%s\n", current);

    char visited[512][64];
    int visited_count = 0;

    while (strcmp(current, "None") != 0) {
        int i;
        for (i = 0; i < visited_count; i++) {
            if (!strcmp(visited[i], current)) {
                printf("[log] ERROR: Detected commit cycle at %s\n", current);
                puts("[log] Stopping log traversal to prevent infinite loop.");
                return;
            }
        }
        if (visited_count < 512) strcpy(visited[visited_count++], current);
        char path[256], parent[64] = "None", msg[256], timebuf[64];
        sprintf(path, ".bit/commits/%s.txt", current);

        printf("[log] Reading commit file: %s\n", path);

        FILE *fp = fopen(path, "r");
        if (!fp) {
            printf("[log] ERROR: could not open %s\n", path);
            break;
        }

        char line[256];
        printf("Commit: %s\n", current);
        while (fgets(line, sizeof(line), fp)) {
            if (sscanf(line, "Parent: %63s", parent) == 1) {}
            else if (sscanf(line, "Time: %63[^\n]", timebuf) == 1)
                printf("Time: %s\n", timebuf);
            else if (sscanf(line, "Message: %255[^\n]", msg) == 1)
                printf("Message: %s\n", msg);

            /* Backward compatible parse for malformed one-line
               "Parent: <id>Time: ..." entries from older commits. */
            if (!strncmp(line, "Parent:", 7) && strstr(line, "Time:")) {
                if (sscanf(line, "Parent: %63[^T]", parent) == 1) {
                    parent[strcspn(parent, " \t\n")] = 0;
                    printf("[log] parsed legacy parent=%s\n", parent);
                }
            }
        }
        printf("\n");
        fclose(fp);

        printf("[log] Moving to parent=%s\n", parent);
        strcpy(current, parent);
    }

    puts("[log] Reached root commit.");
}

/* ===================== MAIN ===================== */

int main(int argc, char **argv) {
    if (argc < 2) return 0;

    srand((unsigned)time(NULL));

    if (!strcmp(argv[1], "init")) {
        ensure_dir(".bit");
        ensure_dir(".bit/objects");
        ensure_dir(".bit/objects/blobs");
        ensure_dir(".bit/objects/trees");
        ensure_dir(".bit/commits");
        FILE *h = fopen(".bit/HEAD", "w");
        fprintf(h, "None");
        fclose(h);
        puts("Initialized repository");
    }

    else if (!strcmp(argv[1], "commit")) {
        if (argc < 3) { puts("commit message required"); return 1; }

        printf("[commit] Building tree from working directory\n");

        TreeNode *tree = build_tree(".");
        char tree_id[64], commit_id[64];
        gen_id(tree_id);
        gen_id(commit_id);

        printf("[commit] tree_id=%s commit_id=%s\n", tree_id, commit_id);

        save_tree_object(tree, tree_id);

        char parent[64] = "None";
        FILE *h = fopen(".bit/HEAD", "r");
        if (h) {
            fgets(parent, sizeof(parent), h);
            parent[strcspn(parent, "\n")] = 0;
            fclose(h);
        }

        printf("[commit] parent=%s\n", parent);

        char path[256];
        sprintf(path, ".bit/commits/%s.txt", commit_id);
        FILE *fp = fopen(path, "w");

        time_t now = time(NULL);
        fprintf(fp,
            "Commit: %s\nParent: %s\nTime: %sMessage: %s\nTree: %s\n",
            commit_id, parent,
            ctime(&now),
            argv[2],
            tree_id);

        fclose(fp);

        h = fopen(".bit/HEAD", "w");
        fprintf(h, "%s", commit_id);
        fclose(h);

        puts("Committed");
    }

    else if (!strcmp(argv[1], "log")) {
        print_log();
    }

    else if (!strcmp(argv[1], "restore")) {
        if (argc < 3) {
            puts("restore commit id required");
            return 1;
        }
        printf("[restore] restoring commit id=%s\n", argv[2]);
        restore_commit(argv[2]);
        puts("Restored");
    }

    return 0;
}
