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
    int is_file;                 // 1 = file, 0 = dir
    unsigned long blob_hash;     // valid if file
    struct TreeNode *child;
    struct TreeNode *sibling;
} TreeNode;

/* ===================== FILESYSTEM HELPERS ===================== */

void ensure_dir(const char *path) {
    MKDIR(path);
}

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
    DIR *dir = opendir(".");
    struct dirent *dp;

    while ((dp = readdir(dir))) {
        if (!strcmp(dp->d_name, ".") ||
            !strcmp(dp->d_name, "..") ||
            !strcmp(dp->d_name, ".bit") ||
            !strcmp(dp->d_name, "bit.exe") ||
            !strcmp(dp->d_name, "bit"))
            continue;

        delete_dir(dp->d_name);
        remove(dp->d_name);
    }
    closedir(dir);
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

void store_blob(const char *path) {
    unsigned long h = hash_file(path);
    if (!h) return;

    char blob[256];
    sprintf(blob, ".bit/objects/blobs/%lu", h);

    FILE *test = fopen(blob, "rb");
    if (test) { fclose(test); return; }

    FILE *src = fopen(path, "rb");
    FILE *dst = fopen(blob, "wb");

    int c;
    while ((c = fgetc(src)) != EOF)
        fputc(c, dst);

    fclose(src);
    fclose(dst);
}

/* ===================== TREE BUILD ===================== */

TreeNode *new_node(const char *name, int is_file) {
    TreeNode *n = malloc(sizeof(TreeNode));
    strcpy(n->name, name);
    n->is_file = is_file;
    n->blob_hash = 0;
    n->child = n->sibling = NULL;
    return n;
}

TreeNode *build_tree(const char *base) {
    DIR *dir = opendir(base);
    if (!dir) return NULL;

    TreeNode *root = new_node(base, 0);
    TreeNode *last = NULL;

    struct dirent *dp;
    while ((dp = readdir(dir))) {
        if (!strcmp(dp->d_name, ".") ||
            !strcmp(dp->d_name, "..") ||
            !strcmp(dp->d_name, ".bit"))
            continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", base, dp->d_name);

        TreeNode *n = NULL;
        DIR *test = opendir(path);
        if (test) {
            closedir(test);
            n = build_tree(path);
        } else {
            n = new_node(dp->d_name, 1);
            n->blob_hash = hash_file(path);
            store_blob(path);
        }

        if (!root->child) root->child = n;
        else last->sibling = n;
        last = n;
    }

    closedir(dir);
    return root;
}

/* ===================== TREE OBJECT ===================== */

void save_tree(TreeNode *n, FILE *fp, int d) {
    for (; n; n = n->sibling) {
        for (int i = 0; i < d; i++) fprintf(fp, "  ");
        if (n->is_file)
            fprintf(fp, "F %s %lu\n", n->name, n->blob_hash);
        else
            fprintf(fp, "D %s\n", n->name);

        save_tree(n->child, fp, d + 1);
    }
}

void generate_id(char *out) {
    static int c = 0;
    sprintf(out, "%lu_%d", (unsigned long)time(NULL), ++c);
}

void save_tree_object(TreeNode *root, char *tree_id) {
    char path[256];
    sprintf(path, ".bit/objects/trees/%s.txt", tree_id);
    FILE *fp = fopen(path, "w");
    save_tree(root->child, fp, 0);
    fclose(fp);
}

/* ===================== RESTORE ===================== */

void restore_commit(const char *id) {
    char commit_path[256], tree_id[64];
    sprintf(commit_path, ".bit/commits/%s.txt", id);

    FILE *fp = fopen(commit_path, "r");
    if (!fp) { printf("commit not found\n"); return; }

    while (fscanf(fp, "Tree: %63s\n", tree_id) != 1)
        fscanf(fp, "%*[^\n]\n");
    fclose(fp);

    clean_working_directory();

    char tree_path[256];
    sprintf(tree_path, ".bit/objects/trees/%s.txt", tree_id);
    fp = fopen(tree_path, "r");

    char line[512], stack[50][512];
    while (fgets(line, sizeof(line), fp)) {
        int sp = 0;
        while (line[sp] == ' ') sp++;
        int depth = sp / 2;

        char name[256]; unsigned long h;
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

/* ===================== MAIN ===================== */

int main(int argc, char **argv) {
    if (argc < 2) return 0;

    if (!strcmp(argv[1], "init")) {
        ensure_dir(".bit");
        ensure_dir(".bit/objects");
        ensure_dir(".bit/objects/blobs");
        ensure_dir(".bit/objects/trees");
        ensure_dir(".bit/commits");
        FILE *h = fopen(".bit/HEAD", "w"); fclose(h);
        puts("Initialized repository");
    }

    else if (!strcmp(argv[1], "commit")) {
        TreeNode *t = build_tree(".");
        char tree_id[64], commit_id[64];
        generate_id(tree_id);
        generate_id(commit_id);

        save_tree_object(t, tree_id);

        char path[256];
        sprintf(path, ".bit/commits/%s.txt", commit_id);
        FILE *fp = fopen(path, "w");
        fprintf(fp, "Commit: %s\nTree: %s\n", commit_id, tree_id);
        fclose(fp);

        fp = fopen(".bit/HEAD", "w");
        fprintf(fp, "%s", commit_id);
        fclose(fp);

        puts("Committed");
    }

    else if (!strcmp(argv[1], "restore")) {
        restore_commit(argv[2]);
        puts("Restored");
    }

    return 0;
}
