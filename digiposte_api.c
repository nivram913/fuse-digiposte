#include <json-c/json.h>
#include "digiposte_api.h"

static int read_fd, write_fd;

int init_api(const char *authorization)
{
    int cts_pipe[2], stc_pipe[2], child;
    char args_read_fd[4], args_write_fd[4];
    char buf[6];
    
    if (pipe(cts_pipe) == -1 || pipe(stc_pipe) == -1) {
        perror("pipe()");
        return -1;
    }
    
    child = fork();
    if (child == -1) {
        perror("fork()");
        return -1;
    }
    else if (child == 0) {
        //AppArmor change profile here
        
        close(stc_pipe[0]);
        close(cts_pipe[1]);
        
        snprintf(args_read_fd, 4, "%d", cts_pipe[0]);
        snprintf(args_write_fd, 4, "%d", stc_pipe[1]);
        
        execlp("python3", "python3", DGP_API_SUBSYSTEM, "--server", args_read_fd, args_write_fd, "--token", authorization, NULL);
        perror("execlp()");
        exit(-errno);
    }
    
    close(stc_pipe[1]);
    close(cts_pipe[0]);
    
    read_fd = stc_pipe[0];
    write_fd = cts_pipe[1];
    
    if (read(read_fd, buf, 6) == -1) {
        perror("read()");
        return -1;
    }
    
    return 0;
}

void free_api()
{
    close(read_fd);
    close(write_fd);
}

static void construct_folder_rec(c_folder *folder, json_object *root)
{
    int n, i;
    json_object *j_folders, *tmp, *field_id, *field_name, *field_location;
    c_folder *new;

    j_folders = json_object_object_get(root, "folders");
    n = json_object_array_length(j_folders);
    for (i=0; i<n; i++) {
        tmp = json_object_array_get_idx(j_folders, i);
        field_id = json_object_object_get(tmp, "id");
        field_name = json_object_object_get(tmp, "name");
        field_location = json_object_object_get(tmp, "location");
        if (strncmp(json_object_get_string(field_location), "TRASH", 6) != 0)
            new = add_folder(folder, json_object_get_string(field_id), json_object_get_string(field_name));

        construct_folder_rec(new, tmp);
    }
}

c_folder* get_folders()
{
    c_folder *folder;
    json_object *root;
    resp_stuct *rs;
    char *tmp;
    int r;

    rs = malloc(sizeof(resp_stuct));
    if (rs == NULL) {
        perror("malloc()");
        return NULL;
    }
    rs->ptr = malloc(BUF_SIZE*sizeof(char));
    if (rs->ptr == NULL) {
        perror("malloc()");
        free(rs);
        return NULL;
    }
    rs->response_actual_size = 0;
    rs->response_allocated_size = BUF_SIZE;
    
    r = write(write_fd, "get_folders_tree\n", 17);
    if (r != 17) {
        perror("write()");
        free(rs->ptr);
        free(rs);
        return NULL;
    }
    
    do {
        r = read(read_fd, rs->ptr + rs->response_actual_size, CHUNK_SIZE);
        if (r == -1) {
            perror("read()");
            free(rs->ptr);
            free(rs);
            return NULL;
        }
        
        rs->response_actual_size += r;
        if (rs->ptr[rs->response_actual_size-1] == '\0') break;
        
        if (r == CHUNK_SIZE && rs->response_allocated_size <= rs->response_actual_size + CHUNK_SIZE) {
            tmp = realloc(rs->ptr, rs->response_actual_size + CHUNK_SIZE*2);
            if (tmp == NULL) {
                perror("realloc()");
                free(rs->ptr);
                free(rs);
                return NULL;
            }
            rs->ptr = tmp;
            rs->response_allocated_size = rs->response_actual_size + CHUNK_SIZE*2;
        }
    } while (r == CHUNK_SIZE);
    
    if (rs->ptr[0] == 'e' && rs->ptr[1] == 'r' && rs->ptr[2] == 'r') {
        fputs("API returned an error\n", stderr);
        free(rs->ptr);
        free(rs);
        return NULL;
    }

    root = json_tokener_parse(rs->ptr);
    if (root == NULL) {
        fputs("json_tokener_parse(): error\n", stderr);
        free(rs->ptr);
        free(rs);
        return NULL;
    }

    folder = add_folder(NULL, "root-000000000000000000000000000", NULL);
    if (folder == NULL) {
        free(rs->ptr);
        free(rs);
        return NULL;
    }

    construct_folder_rec(folder, root);

    json_object_put(root);
    free(rs->ptr);
    free(rs);

    return folder;
}

int get_folder_content(c_folder *folder)
{
    json_object *root, *j_file, *field_id, *field_name, *field_size, *tmp;
    resp_stuct *rs;
    char req[64], *tmp_ptr;
    int r, i, n;

    rs = malloc(sizeof(resp_stuct));
    if (rs == NULL) {
        perror("malloc()");
        return -1;
    }
    rs->ptr = malloc(BUF_SIZE*sizeof(char));
    if (rs->ptr == NULL) {
        perror("malloc()");
        free(rs);
        return -1;
    }
    rs->response_actual_size = 0;
    rs->response_allocated_size = BUF_SIZE;
    
    if (folder->id[0] == 'r') {
        memcpy(req, "get_folder_content", 19);
        req[19] = '\n';
        i = 20;
    }
    else {
        memcpy(req, "get_folder_content", 19);
        memcpy(req+19, folder->id, 32);
        req[51] = '\n';
        i = 52;
    }
    
    r = write(write_fd, req, i);
    if (r != i) {
        perror("write()");
        free(rs->ptr);
        free(rs);
        return -1;
    }
    
    do {
        r = read(read_fd, rs->ptr + rs->response_actual_size, CHUNK_SIZE);
        if (r == -1) {
            perror("read()");
            free(rs->ptr);
            free(rs);
            return -1;
        }
        
        rs->response_actual_size += r;
        if (rs->ptr[rs->response_actual_size-1] == '\0') break;
        
        if (r == CHUNK_SIZE && rs->response_allocated_size <= rs->response_actual_size + CHUNK_SIZE) {
            tmp_ptr = realloc(rs->ptr, rs->response_actual_size + CHUNK_SIZE*2);
            if (tmp_ptr == NULL) {
                perror("realloc()");
                free(rs->ptr);
                free(rs);
                return -1;
            }
            rs->ptr = tmp_ptr;
            rs->response_allocated_size = rs->response_actual_size + CHUNK_SIZE*2;
        }
    } while (r == CHUNK_SIZE);
    
    if (rs->ptr[0] == 'e' && rs->ptr[1] == 'r' && rs->ptr[2] == 'r') {
        fputs("API returned an error\n", stderr);
        free(rs->ptr);
        free(rs);
        return -1;
    }

    root = json_tokener_parse(rs->ptr);
    if (root == NULL) {
        fputs("json_tokener_parse(): error\n", stderr);
        free(rs->ptr);
        free(rs);
        return -1;
    }

    j_file = json_object_object_get(root, "documents");
    n = json_object_array_length(j_file);
    for (i=0; i<n; i++) {
        tmp = json_object_array_get_idx(j_file, i);
        field_id = json_object_object_get(tmp, "id");
        field_name = json_object_object_get(tmp, "filename");
        field_size = json_object_object_get(tmp, "size");
        add_file(folder, json_object_get_string(field_id), json_object_get_string(field_name), json_object_get_int(field_size));
    }

    folder->files_loaded = 1;

    json_object_put(root);
    free(rs->ptr);
    free(rs);

    return 0;
}

int get_file(const c_file *file, const char *dest_path)
{
    char req[128], resp[4];
    int r, len;
    
    memcpy(req, "get_file", 9);
    memcpy(req+9, file->id, 32);
    req[41] = '\0';
    len = strlen(dest_path);
    memcpy(req+42, dest_path, len);
    req[42+len] = '\n';
    
    r = write(write_fd, req, len+43);
    if (r != len+43) {
        perror("write()");
        return -1;
    }
    
    r = read(read_fd, resp, 4);
    if (r == -1) {
        perror("read()");
        return -1;
    }
    
    if (resp[0] == 'e' && resp[1] == 'r' && resp[2] == 'r') {
        fputs("API returned an error\n", stderr);
        return -1;
    }
    if (resp[0] != 'O' || resp[1] != 'K') {
        fputs("API returned an unexpected error\n", stderr);
        return -1;
    }

    return 0;
}

int create_folder(const char *name, const char *parent_id, char *new_id)
{
    char req[128], resp[33];
    int r, i, name_len;
    
    memcpy(req, "create_folder", 14);
    name_len = strlen(name);
    memcpy(req+14, name, name_len+1);
    if (parent_id[0] == 'r') {
        i = 15;
    }
    else {
        memcpy(req+15+name_len, parent_id, 32);
        i = 47;
    }
    req[name_len+i] = '\n';
    
    r = write(write_fd, req, name_len+i+1);
    if (r != name_len+i+1) {
        perror("write()");
        return -1;
    }
    
    r = read(read_fd, resp, 33);
    if (r == -1) {
        perror("read()");
        return -1;
    }
    
    if (resp[0] == 'e' && resp[1] == 'r' && resp[2] == 'r') {
        fputs("API returned an error\n", stderr);
        return -1;
    }
    
    memcpy(new_id, resp, 32);

    return 0;
}

int rename_object(const char *id, const char *new_name, const char is_file)
{
    int r, name_len;
    char req[128], resp[4];
    
    memcpy(req, "rename_object", 14);
    if (is_file) req[14] = '1';
    else req[14] = '0';
    req[15] = '\0';
    memcpy(req+16, id, 32);
    req[48] = '\0';
    name_len = strlen(new_name);
    memcpy(req+49, new_name, name_len);
    req[name_len+49] = '\n';
    
    r = write(write_fd, req, name_len+50);
    if (r != name_len+50) {
        perror("write()");
        return -1;
    }
    
    r = read(read_fd, resp, 4);
    if (r == -1) {
        perror("read()");
        return -1;
    }
    
    if (resp[0] == 'e' && resp[1] == 'r' && resp[2] == 'r') {
        fputs("API returned an error\n", stderr);
        return -1;
    }
    if (resp[0] != 'O' || resp[1] != 'K') {
        fputs("API returned an unexpected error\n", stderr);
        return -1;
    }

    return 0;
}

int delete_object(const char *id, const char is_file)
{
    int r;
    char req[128], resp[4];
    
    memcpy(req, "delete_object", 14);
    if (is_file) req[14] = '1';
    else req[14] = '0';
    req[15] = '\0';
    memcpy(req+16, id, 32);
    req[48] = '\n';
    
    r = write(write_fd, req, 49);
    if (r != 49) {
        perror("write()");
        return -1;
    }

    r = read(read_fd, resp, 4);
    if (r == -1) {
        perror("read()");
        return -1;
    }
    
    if (resp[0] == 'e' && resp[1] == 'r' && resp[2] == 'r') {
        fputs("API returned an error\n", stderr);
        return -1;
    }
    if (resp[0] != 'O' || resp[1] != 'K') {
        fputs("API returned an unexpected error\n", stderr);
        return -1;
    }

    return 0;
}

int move_object(const char *id, const char *to_folder_id, const char is_file)
{
    int r, i;
    char req[128], resp[4];
    
    memcpy(req, "move_object", 12);
    if (is_file) req[12] = '1';
    else req[12] = '0';
    req[13] = '\0';
    memcpy(req+14, id, 32);
    req[46] = '\0';
    if (to_folder_id[0] == 'r') {
        req[47] = '\n';
        i = 48;
    }
    else {
        memcpy(req+47, to_folder_id, 32);
        req[79] = '\n';
        i = 80;
    }
    
    r = write(write_fd, req, i);
    if (r != i) {
        perror("write()");
        return -1;
    }
    
    r = read(read_fd, resp, 4);
    if (r == -1) {
        perror("read()");
        return -1;
    }
    
    if (resp[0] == 'e' && resp[1] == 'r' && resp[2] == 'r') {
        fputs("API returned an error\n", stderr);
        return -1;
    }
    if (resp[0] != 'O' || resp[1] != 'K') {
        fputs("API returned an unexpected error\n", stderr);
        return -1;
    }

    return 0;
}

int upload_file(const c_file *file, const char *to_folder_id, char *new_id)
{
    int r, len, i;
    char req[128], resp[33];
    
    memcpy(req, "upload_file", 12);
    if (to_folder_id[0] == 'r') {
        req[12] = '\0';
        i = 13;
    }
    else {
        memcpy(req+12, to_folder_id, 32);
        req[44] = '\0';
        i = 45;
    }
    len = strlen(file->cache_path);
    memcpy(req+i, file->cache_path, len+1);
    i += len+1;
    len = strlen(file->name);
    memcpy(req+i, file->name, len+1);
    i += len+1;
    snprintf(req+i, 10, "%ld\n", file->size);
    i += strlen(req+i);
    
    r = write(write_fd, req, i);
    if (r != i) {
        perror("write()");
        return -1;
    }
    
    r = read(read_fd, resp, 33);
    if (r == -1) {
        perror("read()");
        return -1;
    }
    
    if (resp[0] == 'e' && resp[1] == 'r' && resp[2] == 'r') {
        fputs("API returned an error\n", stderr);
        return -1;
    }
    
    memcpy(new_id, resp, 32);

    return 0;
}
