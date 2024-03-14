#include <curl/curl.h>
#include <json-c/json.h>
#include "digiposte_api.h"

static CURL *curl_handle;
static char *authorization_token;

int init_api(const char *authorization)
{
    CURLcode code;
    int len;

    if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
        fputs("curl_global_init() init error\n", stderr);
        return -1;
    }

    curl_handle = curl_easy_init();
    if (curl_handle == NULL) {
        fputs("curl_easy_init() init error\n", stderr);
        curl_global_cleanup();
        return -1;
    }

    len = strlen(authorization);
    authorization_token = malloc((len+23) * sizeof(char));
    if (authorization_token == NULL) {
        perror("malloc()");
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        return -1;
    }
    memcpy(authorization_token, "Authorization: Bearer ", 22);
    memcpy(authorization_token+22, authorization, len);
    authorization_token[len+22] = '\0';

    return 0;
}

void free_api()
{
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    free(authorization_token);
}

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t realsize;
    char *tmp;
    resp_stuct *rs = (resp_stuct*)userdata;

    realsize = size * nmemb;
    if (realsize > rs->response_allocated_size - rs->response_actual_size) {
        if (rs->fixed_size) {
            fputs("write_callback(): cannot realloc memory mapping (response larger than file size)\n", stderr);
            return 0;
        }
        tmp = realloc(rs->ptr, rs->response_actual_size + realsize);
        if (tmp == NULL) {
            perror("realloc()");
            return 0;
        }
        rs->ptr = tmp;
        rs->response_allocated_size = rs->response_actual_size + realsize;
    }

    memcpy(rs->ptr + rs->response_actual_size, ptr, realsize);
    rs->response_actual_size += realsize;

    return realsize;
}

static resp_stuct* prepare_request(const char *url, const char *data, const int data_len, resp_stuct *rs)
{
    CURLcode code;
    struct curl_slist *slist = NULL;

    curl_easy_reset(curl_handle);

    code = curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    code = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    code = curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, rs);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    slist = curl_slist_append(slist, "Accept: application/json");
    slist = curl_slist_append(slist, authorization_token);
    if (data != NULL) {
        slist = curl_slist_append(slist, "Content-Type: application/json");
        
        code = curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, data);
        if(code != CURLE_OK) {
            fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
            return -1;
        }

        code = curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, data_len);
        if(code != CURLE_OK) {
            fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
            return -1;
        }
    }
 
    code = curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, slist);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    return 0;
}

static void construct_folder_rec(c_folder *folder, json_object *root)
{
    int n, i;
    json_object *j_folders, *tmp, *field_id, *field_name;
    c_folder *new;

    j_folders = json_object_object_get(root, "folders");
    n = json_object_array_length(j_folders);
    for (i=0; i<n; i++) {
        tmp = json_object_array_get_idx(j_folders, i);
        field_id = json_object_object_get(tmp, "id");
        field_name = json_object_object_get(tmp, "name");
        new = add_folder(folder, json_object_get_string(field_id), json_object_get_string(field_name));

        construct_folder_rec(new, tmp);
    }
}

c_folder* get_folders()
{
    CURLcode code;
    c_folder *folder;
    json_object *root;
    resp_stuct *rs;
    int r, n, i;

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
    rs->fixed_size = 0;
    rs->response_actual_size = 0;
    rs->response_allocated_size = BUF_SIZE;

    r = prepare_request("https://api.digiposte.fr/api/v3/folders", NULL, 0, rs);
    if (r == -1) {
        fputs("prepare_request(): error\n", stderr);
        free(rs->ptr);
        free(rs);
        return NULL;
    }

    code = curl_easy_perform(curl_handle);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform(): %s\n", curl_easy_strerror(code));
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

    folder = add_folder(NULL, NULL, NULL);
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
    CURLcode code;
    json_object *root, *j_file, *field_id, *field_name, *field_size, *tmp;
    resp_stuct *rs;
    int r, i, n, post_data_len;
    char post_data[77];

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
    rs->fixed_size = 0;
    rs->response_actual_size = 0;
    rs->response_allocated_size = BUF_SIZE;

    if (folder->name == NULL) {
        memcpy(post_data, "{\"locations\":[\"INBOX\",\"SAFE\"],\"folder_id\":\"\"}", 45);
        post_data_len = 45;
    }
    else {
        memcpy(post_data, "{\"locations\":[\"INBOX\",\"SAFE\"],\"folder_id\":\"", 43);
        memcpy(post_data+43, folder->id, 32);
        memcpy(post_data+75, "\"}", 2);
        post_data_len = 77;
    }

    r = prepare_request("https://api.digiposte.fr/api/v3/documents/search?max_results=1000&sort=TITLE", post_data, post_data_len, rs);
    if (r == -1) {
        fputs("prepare_request(): error\n", stderr);
        free(rs->ptr);
        free(rs);
        return -1;
    }

    code = curl_easy_perform(curl_handle);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform(): %s\n", curl_easy_strerror(code));
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

int get_file(c_file *file, const char *dest_path)
{
    CURLcode code;
    resp_stuct *rs;
    int fd, r;
    char url[82];

    rs = malloc(sizeof(resp_stuct));
    if (rs == NULL) {
        perror("malloc()");
        return -1;
    }
    rs->ptr = NULL;
    rs->fixed_size = 1;
    rs->response_actual_size = 0;
    rs->response_allocated_size = file->size;

    fd = open(dest_path, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    if (fd == -1) {
        perror("open()");
        free(rs);
        return -1;
    }

    if (lseek(fd, file->size-1, SEEK_SET) == -1) {
        perror("lseek()");
        close(fd);
        free(rs);
        return -1;
    }

    if (write(fd, "", 1) != 1) {
        perror("write()");
        close(fd);
        free(rs);
        return -1;
    }

    rs->ptr = mmap(NULL, file->size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (rs->ptr == MAP_FAILED) {
        perror("mmap()");
        close(fd);
        free(rs);
        return -1;
    }

    memcpy(url, "https://api.digiposte.fr/api/v3/document/", 41);
    memcpy(url+41, file->id, 32);
    memcpy(url+73, "/content", 9);

    r = prepare_request(url, NULL, 0, rs);
    if (r == -1) {
        fputs("prepare_request(): error\n", stderr);
        munmap(rs->ptr, file->size);
        close(fd);
        free(rs);
        return -1;
    }

    code = curl_easy_perform(curl_handle);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform(): %s\n", curl_easy_strerror(code));
        munmap(rs->ptr, file->size);
        close(fd);
        free(rs);
        return -1;
    }

    munmap(rs->ptr, file->size);
    close(fd);
    free(rs);

    return 0;
}

//int put_file(char **id, const char *src_path);

int create_folder(const char *name)
{
    return -EROFS;
}

int delete_item(const char *id)
{
    return -EROFS;
}

int rename_folder(const char *id, const char *new_name)
{
    return -EROFS;
}

int rename_file(const char *id, const char *new_name)
{
    return -EROFS;
}
