#include "data_structures.h"

c_folder* add_folder(c_folder *parent, const char *id, const char *name)
{
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

c_file* add_file(c_folder *parent, const char *id, const char *name, const size_t size)
{
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

int remove_file(c_folder *parent, const int index)
{
    int i;
    c_file *ptr, **file_table;

    if (parent == NULL || index >= parent->nb_files || index < 0 || !parent->files_loaded) {
        fputs("remove_file(): parent is NULL or index is out of range\n", stderr);
        return -1;
    }

    ptr = parent->files[index];
    free(ptr->cache_path);
    free(ptr->name);
    free(ptr);

    for (i=index; i < parent->nb_files-1; i++)
        parent->files[i] = parent->files[i+1];

    parent->nb_files--;
    file_table = realloc(parent->files, parent->nb_files*sizeof(c_file*));
    if (file_table == NULL && parent->nb_files != 0) {
        perror("realloc()");
        return -1;
    }

    if (parent->nb_files == 0) parent->files = NULL;
    else parent->files = file_table;

    return 0;
}

int remove_folder(c_folder *folder)
{
    c_folder *parent, **folder_table;
    int index, i;

    parent = folder->parent;

    if (parent == NULL) {
        fputs("remove_folder(): Cannot remove root\n", stderr);
        return -1;
    }

    if (folder->nb_folders > 0 || folder->nb_files > 0) {
        fputs("remove_folder(): Folder object is not empty\n", stderr);
        return -1;
    }

    index = find_folder_id(parent, folder->id);

    free(folder->name);

    for (i=index; i < parent->nb_folders-1; i++)
        parent->folders[i] = parent->folders[i+1];

    parent->nb_folders--;
    folder_table = realloc(parent->folders, parent->nb_folders*sizeof(c_folder*));
    if (folder_table == NULL && parent->nb_folders != 0) {
        perror("realloc()");
        return -1;
    }

    if (parent->nb_folders == 0) parent->folders = NULL;
    else parent->folders = folder_table;

    return 0;
}

int remove_folder_rec(c_folder *parent, const int index)
{
    c_folder *ptr;
    int i;

    if (parent == NULL || index >= parent->nb_folders || index < 0) {
        fputs("remove_folder_rec(): parent is NULL or index is out of range\n", stderr);
        return -1;
    }

    ptr = parent->folders[index];

    for (i=ptr->nb_files-1; i>=0; i--) remove_file(ptr, i);
    for (i=ptr->nb_folders-1; i>=0; i--) remove_folder_rec(ptr, i);

    return remove_folder(ptr);
}

void free_root(c_folder *root)
{
    int i;

    if (root == NULL) {
        fputs("free_root(): root cannot be NULL\n", stderr);
        return;
    }

    for (i=root->nb_files-1; i>=0; i--) remove_file(root, i);
    for (i=root->nb_folders-1; i>=0; i--) remove_folder_rec(root, i);

    free(root->name);
    free(root);
}

int find_file_name(const c_folder *base, const char *name)
{
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
    int i = 0;

    if (base == NULL) return -1;

    while (i < base->nb_folders) {
        if (strncmp(base->folders[i]->id, id, 32) == 0) return i;
        i++;
    }

    return -1;
}

int move_file(c_folder *from, c_folder *to, const int index)
{
    c_file *file;
    int cache_path_len;

    file = add_file(to, from->files[index]->id, from->files[index]->name, from->files[index]->size);
    if (file == NULL) return -1;

    file->cached = from->files[index]->cached;
    file->dirty = from->files[index]->dirty;
    if (file->cached) {
        cache_path_len = strlen(from->files[index]->cache_path);
        file->cache_path = malloc(cache_path_len+1);
        if (file->cache_path == NULL) {
            perror("malloc()");
            return -1;
        }
        memcpy(file->cache_path, from->files[index]->cache_path, cache_path_len+1);
    }

    return remove_file(from, index);
}

int move_folder(c_folder *folder, c_folder *to)
{
    c_folder *new_folder, *parent, **folder_table;
    int index, i;

    parent = folder->parent;

    index = find_folder_id(parent, folder->id);
    if (index == -1) {
        fputs("move_folder(): Unexpected error\n", stderr);
        return -1;
    }

    new_folder = add_folder(to, folder->id, folder->name);
    if (new_folder == NULL) return -1;

    new_folder->files_loaded = folder->files_loaded;
    new_folder->nb_files = folder->nb_files;
    new_folder->nb_folders = folder->nb_folders;

    new_folder->files = malloc(new_folder->nb_files * sizeof(c_file*));
    if (new_folder->files == NULL) {
        perror("malloc()");
        return -1;
    }
    new_folder->folders = malloc(new_folder->nb_folders * sizeof(c_folder*));
    if (new_folder->folders == NULL) {
        perror("malloc()");
        return -1;
    }

    memcpy(new_folder->files, folder->files, new_folder->nb_files*sizeof(c_file*));
    for (i=0; i<new_folder->nb_folders; i++) {
        new_folder->folders[i] = folder->folders[i];
        new_folder->folders[i]->parent = new_folder;
    }

    free(folder->name);
    for (i=index; i < parent->nb_folders-1; i++)
        parent->folders[i] = parent->folders[i+1];
    
    parent->nb_folders--;
    folder_table = realloc(parent->folders, parent->nb_folders*sizeof(c_folder*));
    if (folder_table == NULL && parent->nb_folders != 0) {
        perror("realloc()");
        return -1;
    }

    if (parent->nb_folders == 0) parent->folders = NULL;
    else parent->folders = folder_table;

    return 0;
}
