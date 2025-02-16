#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "data_structures.h"

#ifndef DGP_API_H
#define DGP_API_H

#define BUF_SIZE 4096
#define CHUNK_SIZE 1024

typedef struct resp_stuct {
    char *ptr;
    size_t response_allocated_size;
    size_t response_actual_size;
} resp_stuct;

/*
Initialize API communication
Takes authorization bearer
Return 0 on success, -1 otherwise
*/
int init_api(const char *authorization);

/*
Free all objects set up earlier by init_api()
*/
void free_api();

/*
Get folders tree from API
Return c_folder object or NULL on error
*/
c_folder* get_folders();

/*
Get folder content (files)
Takes a pointer to the folder object and update it
Return 0 on success, -1 otherwise
*/
int get_folder_content(c_folder *folder);

/*
Download the file at index into folder object to dest_path
Return 0 on success, -1 otherwise
*/
int get_file(const c_file *file, const char *dest_path);

/*
Create a folder named "name" into the folder of id parent_id
Put the id of the newly created folder into new_id
Return 0 on success, -1 otherwise
*/
int create_folder(const char *name, const char *parent_id, char *new_id);

/*
Rename the object of id "id" with new_name
If is_file is true, the object is a file, otherwise a folder
Return 0 on success, -1 otherwise
*/
int rename_object(const char *id, const char *new_name, const char is_file);

/*
Delete the object of id "id"
If is_file is true, the object is a file, otherwise a folder
Return 0 on success, -1 otherwise
*/
int delete_object(const char *id, const char is_file);

/*
Move the object of id "id" to destination folder id "to_folder_id"
If to_folder_id is NULL, move to root folder
If is_file is true, the object is a file, otherwise a folder
Return 0 on success, -1 otherwise
*/
int move_object(const char *id, const char *to_folder_id, const char is_file);

/*
Upload the file pointed by "file" to folder id "to_folder_id"
If to_folder_id is NULL, upload to root folder
Put the id of the newly created file into new_id
Return 0 on success, -1 otherwise
*/
int upload_file(const c_file *file, const char *to_folder_id, char *new_id);

#endif
