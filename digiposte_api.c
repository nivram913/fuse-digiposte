#include <curl/curl.h>
#include <json-c/json.h>
#include "digiposte_api.h"

static char *response;
static size_t response_allocated_size;
static size_t response_actual_size;
static CURL *curl_handle;
static char *authorization_token;

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t realsize;
    char *tmp;

    realsize = size * nmemb;
    if (realsize > response_allocated_size - response_actual_size) {
        tmp = realloc(response, response_actual_size + realsize);
        if (tmp == NULL) {
            perror("realloc()");
            return 0;
        }
        response = tmp;
        response_allocated_size = response_actual_size + realsize;
    }

    memcpy(response + response_actual_size, ptr, realsize);
    response_actual_size += realsize;

    return realsize;
}

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

    response = malloc(BUF_SIZE * sizeof(char));
    if (response == NULL) {
        perror("malloc()");
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        return -1;
    }
    response_allocated_size = BUF_SIZE;
    response_actual_size = 0;

    len = strlen(authorization);
    authorization_token = malloc((len+23) * sizeof(char));
    if (authorization_token == NULL) {
        perror("malloc()");
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        free(response);
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
    free(response);
    free(authorization_token);
}

static int perform_get(const char *url)
{
    CURLcode code;
    struct curl_slist *slist = NULL;

    curl_easy_reset(curl_handle);
    response_actual_size = 0;

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

    code = curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, NULL);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    slist = curl_slist_append(slist, "Accept: application/json");
    slist = curl_slist_append(slist, authorization_token);
 
    code = curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, slist);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    code = curl_easy_perform(curl_handle);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    return 0;
}

static int perform_post(const char *url, const char *data, const int len)
{
    CURLcode code;
    struct curl_slist *slist = NULL;

    curl_easy_reset(curl_handle);
    response_actual_size = 0;

    code = curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    code = curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, data);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    code = curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, len);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    code = curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    code = curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, NULL);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    slist = curl_slist_append(slist, "Content-Type: application/json");
    slist = curl_slist_append(slist, "Accept: application/json");
    slist = curl_slist_append(slist, authorization_token);
 
    code = curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, slist);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_setopt(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    code = curl_easy_perform(curl_handle);
    if(code != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform(): %s\n", curl_easy_strerror(code));
        return -1;
    }

    return 0;
}

static int perform_put(const char *url)
{
    return 0;
}

static int perform_delete(const char *url)
{
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
    c_folder *folder;
    json_object *root;
    int r, n, i;

    r = perform_get("https://api.digiposte.fr/api/v3/folders");
    if (r == -1) {
        fputs("perform_get(): error\n", stderr);
        return NULL;
    }
    root = json_tokener_parse(response);
    if (root == NULL) {
        fputs("json_tokener_parse(): error\n", stderr);
        return NULL;
    }

    folder = add_folder(NULL, NULL, NULL);
    if (folder == NULL) return NULL;

    construct_folder_rec(folder, root);

    json_object_put(root);

    return folder;
}

int get_folder_content(c_folder *folder)
{
    json_object *root, *j_file, *field_id, *field_name, *field_size, *tmp;
    int r, i, n, post_data_len;
    char post_data[77];

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

    r = perform_post("https://api.digiposte.fr/api/v3/documents/search?max_results=1000&sort=TITLE", post_data, post_data_len);
    if (r == -1) {
        fputs("perform_post(): error\n", stderr);
        return NULL;
    }
    root = json_tokener_parse(response);
    if (root == NULL) {
        fputs("json_tokener_parse(): error\n", stderr);
        return NULL;
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

    return 0;
}

int get_file(c_file *file, const char *dest_path)
{
    return -1;
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
