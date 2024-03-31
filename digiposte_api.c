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

static resp_stuct* prepare_request(const req_type req_type, const char *url, const void *data, const int data_len, resp_stuct *rs)
{
    CURLcode code;
    struct curl_slist *slist = NULL;
    post_multipart_data *post_mp_data;
    curl_mime *form;
    curl_mimepart *field;
    char size[10];

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
    if (req_type == REQ_POST_MULTIPART) {
        post_mp_data = (post_multipart_data*)data;

        form = curl_mime_init(curl_handle);
        if (form == NULL) return -1;

        field = curl_mime_addpart(form);
        curl_mime_name(field, "archive");
        curl_mime_filedata(field, post_mp_data->file->cache_path);

        field = curl_mime_addpart(form);
        curl_mime_name(field, "health_document");
        curl_mime_data(field, "false", CURL_ZERO_TERMINATED);

        field = curl_mime_addpart(form);
        curl_mime_name(field, "title");
        curl_mime_data(field, post_mp_data->file->name, CURL_ZERO_TERMINATED);

        snprintf(size, 10, "%d", post_mp_data->file->size);
        field = curl_mime_addpart(form);
        curl_mime_name(field, "archive_size");
        curl_mime_data(field, size, CURL_ZERO_TERMINATED);

        if (post_mp_data->folder_parent_id[0] != 'r') {
            field = curl_mime_addpart(form);
            curl_mime_name(field, "folder_id");
            curl_mime_data(field, post_mp_data->folder_parent_id, 32);
        }

        code = curl_easy_setopt(curl_handle, CURLOPT_MIMEPOST, form);
        if(code != CURLE_OK) {
            fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
            return -1;
        }
    }
    else {
        if (req_type == REQ_POST || req_type == REQ_PUT_WITH_DATA) {
            slist = curl_slist_append(slist, "Content-Type: application/json");
            
            code = curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, (char*)data);
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
        if (req_type == REQ_PUT || req_type == REQ_PUT_WITH_DATA) {
            code = curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PUT");
            if(code != CURLE_OK) {
                fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
                return -1;
            }
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
    long response_code;

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

    r = prepare_request(REQ_GET, "https://api.digiposte.fr/api/v3/folders", NULL, 0, rs);
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

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code/100 != 2) {
        fprintf(stderr, "get_folders(): API returned code %d\n", response_code);
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
    CURLcode code;
    json_object *root, *j_file, *field_id, *field_name, *field_size, *tmp;
    resp_stuct *rs;
    int r, i, n, post_data_len;
    long response_code;
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

    r = prepare_request(REQ_POST, "https://api.digiposte.fr/api/v3/documents/search?max_results=1000&sort=TITLE", (void*)post_data, post_data_len, rs);
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

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code/100 != 2) {
        fprintf(stderr, "get_folder_content(): API returned code %d\n", response_code);
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
    CURLcode code;
    resp_stuct *rs;
    int fd, r;
    long response_code;
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

    r = prepare_request(REQ_GET, url, NULL, 0, rs);
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

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code/100 != 2) {
        fprintf(stderr, "get_file(): API returned code %d\n", response_code);
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

int create_folder(const char *name, const char *parent_id, char *new_id)
{
    CURLcode code;
    resp_stuct *rs;
    json_object *root, *field_id;
    int r, name_len, post_data_len;
    long response_code;
    char *post_data;

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

    name_len = strlen(name);
    post_data = malloc((name_len + 80)*sizeof(char));
    if (post_data == NULL) {
        perror("malloc()");
        free(rs->ptr);
        free(rs);
        return -1;
    }
    sprintf(post_data, "{\"name\": \"%s\", \"favorite\": false, \"parent_id\": \"", name);
    if (parent_id[0] == 'r') {
        memcpy(post_data+46+name_len, "\"}", 2);
        post_data_len = name_len + 48;
    }
    else {
        memcpy(post_data+46+name_len, parent_id, 32);
        memcpy(post_data+78+name_len, "\"}", 2);
        post_data_len = name_len + 80;
    }

    r = prepare_request(REQ_POST, "https://api.digiposte.fr/api/v3/folder", (void*)post_data, post_data_len, rs);
    if (r == -1) {
        fputs("prepare_request(): error\n", stderr);
        free(rs->ptr);
        free(rs);
        free(post_data);
        return -1;
    }

    code = curl_easy_perform(curl_handle);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform(): %s\n", curl_easy_strerror(code));
        free(rs->ptr);
        free(rs);
        free(post_data);
        return -1;
    }

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code/100 != 2) {
        fprintf(stderr, "create_folder(): API returned code %d\n", response_code);
        free(rs->ptr);
        free(rs);
        free(post_data);
        return -1;
    }

    root = json_tokener_parse(rs->ptr);
    if (root == NULL) {
        fputs("json_tokener_parse(): error\n", stderr);
        free(rs->ptr);
        free(rs);
        free(post_data);
        return -1;
    }

    field_id = json_object_object_get(root, "id");
    memcpy(new_id, json_object_get_string(field_id), 32);

    json_object_put(root);
    free(rs->ptr);
    free(rs);
    free(post_data);

    return 0;
}

int rename_object(const char *id, const char *new_name, const char is_file)
{
    CURLcode code;
    resp_stuct *rs;
    int r, name_len;
    long response_code;
    char *url;

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

    if (is_file) {
        name_len = strlen(new_name);
        url = malloc((name_len + 82)*sizeof(char));
        if (url == NULL) {
            perror("malloc()");
            free(rs->ptr);
            free(rs);
            return -1;
        }
        memcpy(url, "https://api.digiposte.fr/api/v3/document/", 41);
        memcpy(url+41, id, 32);
        memcpy(url+73, "/rename/", 8);
        memcpy(url+81, new_name, name_len);
        url[name_len+81] = '\0';
    }
    else {
        name_len = strlen(new_name);
        url = malloc((name_len + 80)*sizeof(char));
        if (url == NULL) {
            perror("malloc()");
            free(rs->ptr);
            free(rs);
            return -1;
        }
        memcpy(url, "https://api.digiposte.fr/api/v3/folder/", 39);
        memcpy(url+39, id, 32);
        memcpy(url+71, "/rename/", 8);
        memcpy(url+79, new_name, name_len);
        url[name_len+79] = '\0';
    }

    r = prepare_request(REQ_PUT, url, NULL, 0, rs);
    if (r == -1) {
        fputs("prepare_request(): error\n", stderr);
        free(rs->ptr);
        free(rs);
        free(url);
        return -1;
    }

    code = curl_easy_perform(curl_handle);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform(): %s\n", curl_easy_strerror(code));
        free(rs->ptr);
        free(rs);
        free(url);
        return -1;
    }

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code/100 != 2) {
        fprintf(stderr, "rename_object(): API returned code %d\n", response_code);
        free(rs->ptr);
        free(rs);
        free(url);
        return -1;
    }

    free(rs->ptr);
    free(rs);
    free(url);

    return 0;
}

int delete_object(const char *id, const char is_file)
{
    CURLcode code;
    resp_stuct *rs;
    int r;
    long response_code;
    char post_data[72];

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

    if (is_file) {
        memcpy(post_data, "{\"document_ids\": [\"", 19);
        memcpy(post_data+19, id, 32);
        memcpy(post_data+51, "\"], \"folder_ids\": []}", 21);
    }
    else {
        memcpy(post_data, "{\"document_ids\": [], \"folder_ids\": [\"", 37);
        memcpy(post_data+37, id, 32);
        memcpy(post_data+69, "\"]}", 3);
    }

    r = prepare_request(REQ_POST, "https://api.digiposte.fr/api/v3/file/tree/trash", (void*)post_data, 72, rs);
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

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code/100 != 2) {
        fprintf(stderr, "delete_object(): API returned code %d\n", response_code);
        free(rs->ptr);
        free(rs);
        return -1;
    }

    free(rs->ptr);
    free(rs);

    return 0;
}

int move_object(const char *id, const char *to_folder_id, const char is_file)
{
    CURLcode code;
    resp_stuct *rs;
    int r;
    long response_code;
    char post_data[72], url[83];

    if (to_folder_id[0] == 'r') memcpy(url, "https://api.digiposte.fr/api/v3/file/tree/move", 47);
    else {
        memcpy(url, "https://api.digiposte.fr/api/v3/file/tree/move?to=", 50);
        memcpy(url+50, to_folder_id, 32);
        url[82] = '\0';
    }

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

    if (is_file) {
        memcpy(post_data, "{\"document_ids\": [\"", 19);
        memcpy(post_data+19, id, 32);
        memcpy(post_data+51, "\"], \"folder_ids\": []}", 21);
    }
    else {
        memcpy(post_data, "{\"document_ids\": [], \"folder_ids\": [\"", 37);
        memcpy(post_data+37, id, 32);
        memcpy(post_data+69, "\"]}", 3);
    }

    r = prepare_request(REQ_PUT_WITH_DATA, url, (void*)post_data, 72, rs);
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

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code/100 != 2) {
        fprintf(stderr, "move_object(): API returned code %d\n", response_code);
        free(rs->ptr);
        free(rs);
        return -1;
    }

    free(rs->ptr);
    free(rs);

    return 0;
}

int upload_file(const c_file *file, const char *to_folder_id, char *new_id)
{
    CURLcode code;
    c_folder *folder;
    json_object *root, *field_id;
    resp_stuct *rs;
    int r, n, i;
    long response_code;
    post_multipart_data post_mp_data;

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

    post_mp_data.file = file;
    post_mp_data.folder_parent_id = to_folder_id;

    r = prepare_request(REQ_POST_MULTIPART, "https://api.digiposte.fr/api/v3/document", (void*)&post_mp_data, 0, rs);
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

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code/100 != 2) {
        fprintf(stderr, "upload_file(): API returned code %d\n", response_code);
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

    field_id = json_object_object_get(root, "id");
    memcpy(new_id, json_object_get_string(field_id), 32);

    json_object_put(root);
    free(rs->ptr);
    free(rs);

    return 0;
}
