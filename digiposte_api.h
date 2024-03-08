#include <errno.h>
#include "data_structures.h"

#ifndef DGP_API_H
#define DGP_API_H

#define BUF_SIZE 4096

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
Takes a pointer to the folder object
Return 0 on success, -1 otherwise
*/
int get_folder_content(c_folder *folder);

/*
Download the file at index into folder object to dest_path
Return 0 on success, -1 otherwise
*/
int get_file(c_file *file, const char *dest_path);

/*
FOLLOWING NOT IMPLEMENTED YET
*/

//int put_file(char **id, const char *src_path);
int create_folder(const char *name);
int delete_item(const char *id);
int rename_folder(const char *id, const char *new_name);
int rename_file(const char *id, const char *new_name);

#endif
