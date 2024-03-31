#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifndef DGP_DTSTRUCT_H
#define DGP_DTSTRUCT_H

typedef struct c_file {
    char id[32];
    char *name;
    size_t size;
    char dirty;
    char cached;
    char *cache_path;
} c_file;

typedef struct c_folder {
    char id[32];
    char *name;
    int nb_folders;
    int nb_files;
    char files_loaded;

    struct c_folder *parent;
    struct c_folder **folders;
    c_file **files;
} c_folder;

/*
Create and add a new folder into its parent
The new folder have no child folders or child files at creation
If the parent, id and name are NULL, its the root folder
Return a pointer to the new folder object
Return NULL on error
*/
c_folder* add_folder(c_folder *parent, const char *id, const char *name);

/*
Add a new file into its parent folder
The new file cache_path is set to NULL
Return a pointer to the new file object
Return NULL on error
*/
c_file* add_file(c_folder *parent, const char *id, const char *name, const size_t size);

/*
Remove file from its parent folder
Takes the index of the file in the files table of the parent
Return -1 on error, 0 otherwise
*/
int remove_file(c_folder *parent, const int index);

/*
Remove folder from its parent folder
The folder should have no child files and no child folders
Return -1 on error, 0 otherwise
*/
int remove_folder(c_folder *folder);

/*
Remove folder from its parent folder recursively
Takes the index of the folder in the folders table of the parent
If folder has child files or child folders, they will be removed as well
Return -1 on error, 0 otherwise
*/
int remove_folder_rec(c_folder *parent, const int index);

/*
Free memory
*/
void free_root(c_folder *root);

/*
Find a file by its name
Return the index of the file into files table
Return -1 if not found
*/
int find_file_name(const c_folder *base, const char *name);

/*
Find a folder by its name
Return the index of the file into folders table
Return -1 if not found
*/
int find_folder_name(const c_folder *base, const char *name);

/*
Find a file by its id
Return the index of the file into files table
Return -1 if not found
*/
int find_file_id(const c_folder *base, const char *id);

/*
Find a folder by its id
Return the index of the file into folders table
Return -1 if not found
*/
int find_folder_id(const c_folder *base, const char *id);

/*
Move the file at index from a folder to another
Return -1 on error, 0 otherwise
*/
c_file* move_file(c_folder *from, c_folder *to, const int index);

/*
Move the folder to another folder
Return -1 on error, 0 otherwise
*/
c_folder* move_folder(c_folder *folder, c_folder *to);

#endif