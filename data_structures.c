#include "data_structures.h"

c_folder* add_folder(struct c_folder *parent, const char *id, const char *name)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    c_folder *new, **folder_table;
    int name_len = 0;

    new = malloc(sizeof(c_folder));
    if (new == NULL) {
        perror("malloc()");
        return NULL;
    }

    if (parent == NULL) {
        new->id[0] = '\0';
        new->name = NULL;
    }
    else {
        if (id == NULL || name == NULL) {
            fputs("add_folder(): id and name cannot be NULL\n", stderr);
            free(new);
            return NULL;
        }
        memcpy(new->id, id, 32);
        name_len = strlen(name);
        new->name = malloc((name_len+1) * sizeof(char));
        if (new == NULL) {
            perror("malloc()");
            free(new);
            return NULL;
        }
        strncpy(new->name, name, name_len);
        new->name[name_len] = '\0';

        folder_table = realloc(parent->folders, (parent->nb_folders+1) * sizeof(c_folder*));
        if (folder_table == NULL) {
            perror("realloc()");
            free(new->name);
            free(new);
            return NULL;
        }

        parent->folders = folder_table;
        parent->folders[parent->nb_folders] = new;
        parent->nb_folders++;
    }
    new->parent = parent;
    new->nb_files = 0;
    new->nb_folders = 0;
    new->files_loaded = 0;
    new->files = NULL;
    new->folders = NULL;

    return new;
}

c_file* add_file(struct c_folder *parent, const char *id, const char *name, const size_t size)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    c_file *new, **file_table;
    int name_len = 0;

    if (parent == NULL || name == NULL || id == NULL) {
        fputs("add_file(): parent, name and id cannot be NULL\n", stderr);
        return NULL;
    }

    new = malloc(sizeof(c_file));
    if (new == NULL) {
        perror("malloc()");
        return NULL;
    }

    memcpy(new->id, id, 32);
    name_len = strlen(name);
    new->name = malloc((name_len+1) * sizeof(char));
    if (new == NULL) {
        perror("malloc()");
        free(new);
        return NULL;
    }
    strncpy(new->name, name, name_len);
    new->name[name_len] = '\0';
    new->size = size;
    new->dirty = 0;
    new->cached = 0;
    new->cache_path = NULL;

    file_table = realloc(parent->files, (parent->nb_files+1) * sizeof(c_file*));
    if (file_table == NULL) {
        perror("realloc()");
        free(new->name);
        free(new);
        return NULL;
    }

    parent->files = file_table;
    parent->files[parent->nb_files] = new;
    parent->nb_files++;

    return new;
}

int remove_file(struct c_folder *parent, const int index)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    int i = index;
    c_file *ptr, **file_table;

    if (parent == NULL || index >= parent->nb_files || index < 0 || !parent->files_loaded) {
        fputs("remove_file(): parent is NULL or index is out of range\n", stderr);
        return -EINVAL;
    }

    ptr = parent->files[index];
    free(ptr->cache_path);
    free(ptr->name);
    free(ptr);

    while (i < parent->nb_files) {
        parent->files[i] = parent->files[i+1];
        i++;
    }

    parent->nb_files--;
    file_table = realloc(parent->files, parent->nb_files*sizeof(c_file*));
    if (file_table == NULL) {
        perror("realloc()");
        return -errno;
    }

    if (parent->nb_files == 0) parent->files = NULL;
    else parent->files = file_table;

    return 0;
}

int remove_folder(struct c_folder *parent, const int index)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    c_folder *ptr, **folder_table;
    int i = index;

    if (parent == NULL || index >= parent->nb_folders || index < 0) {
        fputs("remove_folder(): parent is NULL or index is out of range\n", stderr);
        return -EINVAL;
    }

    ptr = parent->folders[index];

    if (ptr->nb_files != 0 || ptr->nb_folders != 0) {
        fputs("remove_folder(): folder object not empty\n", stderr);
        return -ENOTEMPTY;
    }

    free(ptr->name);
    free(ptr);

    while (i < parent->nb_folders) {
        parent->folders[i] = parent->folders[i+1];
        i++;
    }

    parent->nb_folders--;
    folder_table = realloc(parent->folders, parent->nb_folders*sizeof(c_folder*));
    if (folder_table == NULL) {
        perror("realloc()");
        return -errno;
    }

    if (parent->nb_folders == 0) parent->folders = NULL;
    else parent->folders = folder_table;

    return 0;
}

int remove_folder_rec(struct c_folder *parent, const int index)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    c_folder *ptr;
    int i;

    if (parent == NULL || index >= parent->nb_folders || index < 0) {
        fputs("remove_folder_rec(): parent is NULL or index is out of range\n", stderr);
        return -EINVAL;
    }

    ptr = parent->folders[index];

    for (i=0; i<ptr->nb_files; i++) remove_file(ptr, 0);
    for (i=0; i<ptr->nb_folders; i++) remove_folder_rec(ptr, 0);

    return remove_folder(parent, index);
}

void free_root(struct c_folder *root)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    int i;

    if (root == NULL) {
        fputs("free_root(): root cannot be NULL\n", stderr);
        return -EINVAL;
    }

    for (i=0; i<root->nb_files; i++) remove_file(root, 0);
    for (i=0; i<root->nb_folders; i++) remove_folder_rec(root, 0);

    free(root->name);
    free(root);
}

int find_file_name(const c_folder *base, const char *name)
{
    fprintf(stderr, "Entering %s(name=%s)\n", __func__, name);
    int i = 0;

    if (base == NULL) return -1;

    while (i < base->nb_files) {
        if (strcmp(base->files[i]->name, name) == 0) return i;
        i++;
    }

    return -1;
}

int find_folder_name(const c_folder *base, const char *name)
{
    fprintf(stderr, "Entering %s(name=%s)\n", __func__, name);
    int i = 0;

    if (base == NULL) return -1;

    while (i < base->nb_folders) {
        if (strcmp(base->folders[i]->name, name) == 0) return i;
        i++;
    }

    return -1;
}

int find_file_id(const c_folder *base, const char *id)
{
    fprintf(stderr, "Entering %s\n", __func__);
    int i = 0;

    if (base == NULL) return -1;

    while (i < base->nb_files) {
        if (strncmp(base->files[i]->id, id, 32) == 0) return i;
        i++;
    }

    return -1;
}

int find_folder_id(const c_folder *base, const char *id)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    int i = 0;

    if (base == NULL) return -1;

    while (i < base->nb_folders) {
        if (strncmp(base->folders[i]->id, id, 32) == 0) return i;
        i++;
    }

    return -1;
}
