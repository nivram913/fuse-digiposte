#include "fuse-digiposte.h"

static int folder_cache_fault(c_folder *folder)
{
    return get_folder_content(folder);
}

static int file_cache_fault(c_file *file)
{
    char dest_path[PATH_MAX];
    int path_len;

    memcpy(dest_path, CACHE_PATH, sizeof(CACHE_PATH)-1);
    memcpy(dest_path + sizeof(CACHE_PATH)-1, file->id, 32);
    dest_path[sizeof(CACHE_PATH)+31] = '\0';
    path_len = strlen(dest_path);

    if (get_file(file, dest_path) == -1) return -1;

    file->cache_path = malloc(path_len+1);
    if (file->cache_path == NULL) {
        perror("malloc()");
        return -1;
    }

    strcpy(file->cache_path, dest_path);
    file->cached = 1;

    return 0;
}

static void generate_new_id(char *id)
{
    static int counter = 0;
    sprintf(id, "new-%028d", counter);
    counter++;
}

static int get_subpath(const char *path, char *subpath, char *name)
{
    int i, j, k, path_len;

    path_len = strlen(path);
    i = path_len-1;
    while (path[i] != '/') i--;
    for (j=0; j<i; j++) subpath[j] = path[j];
    if (j==0) {
        subpath[j] = '/';
        subpath[j+1] = '\0';
    }
    else subpath[j] = '\0';
    for (k=i+1; k<=path_len; k++) name[k-(i+1)] = path[k];

    return strlen(name);
}

/*
If path point to a directory, return the directory and set index to -1
If path point to a file, return the containing directory and set index to the index of the file in files table
If path doesn't exist or error occured, return NULL
*/
static c_folder* resolve_path(const char *path, int *index, const dgp_ctx *ctx)
{
    int path_len, path_i, subpath_i, i;
    char type = -1;
    char subpath[PATH_MAX];
    c_folder *current_folder;

    current_folder = ctx->dgp_root;
    *index = -1;

    path_len = strlen(path);
    if (path_len == 1 && path[0] == '/') {
        *index = -1;
        return current_folder;
    }

    path_i = 1;
    do {
        subpath_i = 0;
        while (path_i < path_len && path[path_i] != '/') {
            subpath[subpath_i] = path[path_i];
            path_i++;
            subpath_i++;
        }
        subpath[subpath_i] = '\0';
        if (path_i < path_len) {
            i = find_folder_name(current_folder, subpath);
            if (i == -1) return NULL;
            current_folder = current_folder->folders[i];
        }
        else {
            i = find_folder_name(current_folder, subpath);
            if (i != -1) {
                *index = -1;
                return current_folder->folders[i];
            }
            else {
                if (!current_folder->files_loaded && folder_cache_fault(current_folder) == -1) return NULL;
                i = find_file_name(current_folder, subpath);
                if (i == -1) return NULL;
                *index = i;
                return current_folder;
            }
        }
        path_i++;
    } while (path_i < path_len);

    return NULL;
}

static void *dgp_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    dgp_ctx *ctx;
    struct stat st;

    cfg->use_ino = 0;
    cfg->direct_io = 1;
    //cfg->parallel_direct_writes = 1;
    cfg->entry_timeout = 0;
    cfg->attr_timeout = 0;
    cfg->negative_timeout = 0;

    ctx = fuse_get_context()->private_data;

    if (init_api() == -1) {
        free(ctx);
        fputs("init_api(): error\n", stderr);
        exit(-1);
    }

    ctx->dgp_root = get_folders();
    if (ctx->dgp_root == NULL) {
        free(ctx);
        fputs("get_folders(): error\n", stderr);
        exit(-1);
    }

    ctx->root_loaded = 1;

    if (stat(CACHE_PATH, &st) == -1) {
        if (mkdir(CACHE_PATH, 0770) != 0) {
            perror("mkdir()");
            exit(-errno);
        }
    }

    return (void*)ctx;
}

static int dgp_internal_fsync(c_folder *parent, c_file *file)
{
    char new_id[32], new_cache_path[sizeof(CACHE_PATH)+32];
    struct stat st;

    if (!file->cached || !file->dirty) return 0;

    if (file->id[0] != 'n' && delete_object(file->id, 1) == -1) {
        fputs("dgp_internal_fsync(): Error deleting remote file\n", stderr);
        return -EIO;
    }

    stat(file->cache_path, &st);
    file->size = st.st_size;

    if (file->size == 0) {
        file->dirty = 0;
        file->id[0] = 'n';
        return 0;
    }

    if (upload_file(file, parent->id, new_id) == -1) {
        fputs("dgp_internal_fsync(): Error uploading file\n", stderr);
        file->id[0] = 'n';
        return -EIO;
    }

    memcpy(file->id, new_id, 32);
    file->dirty = 0;
    memcpy(new_cache_path, file->cache_path, sizeof(CACHE_PATH)-1);
    memcpy(new_cache_path+sizeof(CACHE_PATH)-1, new_id, 32);
    new_cache_path[sizeof(CACHE_PATH)+31] = '\0';

    if (rename(file->cache_path, new_cache_path) != 0) {
        perror("rename()");
        file->cached = 0;
        return -errno;
    }

    memcpy(file->cache_path, new_cache_path, sizeof(CACHE_PATH)+32);

    return 0;
}

static void dgp_folder_sync(c_folder *folder)
{
    int i;

    for (i=0; i<folder->nb_files; i++) {
        if (dgp_internal_fsync(folder, folder->files[i]) != 0) {
            fprintf(stderr, "dgp_internal_fsync(): Syncing error on %s/%s. Retrying in 2 seconds...\n", folder->name, folder->files[i]->name);
            sleep(2);
            if (dgp_internal_fsync(folder, folder->files[i]) != 0)
                fprintf(stderr, "dgp_internal_fsync(): Syncing error on %s/%s. Manual upload needed\n", folder->name, folder->files[i]->name);
        }
    }
    for (i=0; i<folder->nb_folders; i++) dgp_folder_sync(folder->folders[i]);
}

static void dgp_destroy(void* private_data)
{
    dgp_ctx *ctx = (dgp_ctx*)private_data;
    c_folder *root;
    DIR *directory;
    struct dirent *entry;
    char filename[sizeof(CACHE_PATH)+34];

    dgp_folder_sync(ctx->dgp_root);

    free_root(ctx->dgp_root);
    free_api();
    free(ctx);

    directory = opendir(CACHE_PATH);
    if (directory == NULL) {
        perror("opendir()");
        return;
    }

    while ((entry = readdir(directory))) {
        if (!strcmp(".", entry->d_name) || !strcmp("..", entry->d_name)) {
            continue;
        }

        memcpy(filename, CACHE_PATH, strlen(CACHE_PATH));
        filename[strlen(CACHE_PATH)] = '/';
        memcpy(filename+strlen(CACHE_PATH)+1, entry->d_name, 32);
        filename[strlen(CACHE_PATH)+33] = '\0';

        if (remove(filename) == -1) perror("remove()");
    }
    closedir(directory);

    return;
}

static int dgp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    c_folder *folder;
    int index;
    struct timespec now;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    timespec_get(&now, TIME_UTC);

    folder = resolve_path(path, &index, ctx);
    if (folder == NULL) return -ENOENT;
    if (index == -1) {
        //Ignored by FUSE
        stbuf->st_dev = 0;
        stbuf->st_blksize = 0;
        stbuf->st_ino = 0;
        stbuf->st_rdev = 0;
        stbuf->st_blocks = 0;

        //Directory with rwxrwx---
        stbuf->st_mode = (S_IFMT & S_IFDIR) | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
        stbuf->st_nlink = 2 + folder->nb_folders;
        stbuf->st_uid = fctx->uid;
        stbuf->st_gid = fctx->gid;
        stbuf->st_size = 0;
        stbuf->st_atim = now;
        stbuf->st_mtim = now;
        stbuf->st_ctim = now;
    }
    else {
        //Ignored by FUSE
        stbuf->st_dev = 0;
        stbuf->st_blksize = 0;
        stbuf->st_ino = 0;
        stbuf->st_rdev = 0;
        stbuf->st_blocks = 0;

        //File with rw-rw----
        stbuf->st_mode = (S_IFMT & S_IFREG) | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
        stbuf->st_nlink = 1;
        stbuf->st_uid = fctx->uid;
        stbuf->st_gid = fctx->gid;
        stbuf->st_size = folder->files[index]->size;
        stbuf->st_atim = now;
        stbuf->st_mtim = now;
        stbuf->st_ctim = now;
    }

    return 0;
}

static int dgp_access(const char *path, int mask)
{
    struct stat stbuf;

    if (dgp_getattr(path, &stbuf, NULL) == -ENOENT) return -ENOENT;
    if (mask & X_OK && S_ISREG(stbuf.st_mode)) return -EACCES;

    return 0;
}

static int dgp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                       struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    c_folder *folder;
    int index;
    struct stat st;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    folder = resolve_path(path, &index, ctx);
    if (folder == NULL) return -ENOENT;
    if (index != -1) return -ENOTDIR;

    for (index=0; index < folder->nb_folders; index++) {
        memset(&st, 0, sizeof(st));
        st.st_ino = 0;
        st.st_mode = (S_IFMT & S_IFDIR) | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
        if (filler(buf, folder->folders[index]->name, &st, 0, 0))
            return 0;
    }

    if (!folder->files_loaded && folder_cache_fault(folder) == -1) return -EIO;
    for (index=0; index < folder->nb_files; index++) {
        memset(&st, 0, sizeof(st));
        st.st_ino = 0;
        st.st_mode = (S_IFMT & S_IFREG) | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
        if (filler(buf, folder->files[index]->name, &st, 0, 0))
            break;
    }

    return 0;
}

static int dgp_mkdir(const char *path, mode_t mode)
{
    c_folder *folder;
    int index, path_len;
    char *subpath, *name, id[32];
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    path_len = strlen(path);
    subpath = malloc((path_len+1)*sizeof(char));
    if (subpath == NULL) {
        perror("malloc()");
        return -errno;
    }
    name = malloc((path_len+1)*sizeof(char));
    if (name == NULL) {
        perror("malloc()");
        free(subpath);
        return -errno;
    }

    get_subpath(path, subpath, name);

    folder = resolve_path(subpath, &index, ctx);
    if (folder == NULL) {
        free(name);
        free(subpath);
        return -ENOENT;
    }
    if (index != -1) {
        free(name);
        free(subpath);
        return -ENOTDIR;
    }

    if (create_folder(name, folder->id, id) == -1) {
        fputs("create_folder(): API error\n", stderr);
        free(name);
        free(subpath);
        return -EIO;
    }

    if (add_folder(folder, id, name) == NULL) {
        fputs("add_folder(): Error creating folder into c_folder struct\n", stderr);
        free(name);
        free(subpath);
        return -EIO;
    }

    free(name);
    free(subpath);

    return 0;
}

static int dgp_unlink(const char *path)
{
    c_folder *folder;
    c_file *file;
    int index;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    folder = resolve_path(path, &index, ctx);
    if (folder == NULL) return -ENOENT;
    if (index == -1) return -EISDIR;

    file = folder->files[index];

    if (file->id[0] != 'n' && delete_object(file->id, 1) == -1) return -EIO;

    if (file->cached && unlink(file->cache_path) == 0) file->cached = 0;

    if (remove_file(folder, index) == -1) {
        fputs("dgp_unlink(): Error removing file from struct\n", stderr);
        return -EIO;
    }

    return 0;
}

static int dgp_rmdir(const char *path)
{
    c_folder *folder;
    int index;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    folder = resolve_path(path, &index, ctx);
    if (folder == NULL) return -ENOENT;
    if (index != -1) return -ENOTDIR;
    if (folder->nb_files + folder->nb_folders > 0) return -ENOTEMPTY;
    if (folder == ctx->dgp_root) return -EPERM;

    if (delete_object(folder->id, 0) == -1) return -EIO;

    if (remove_folder(folder) == -1) {
        fputs("dgp_rmdir(): Error removing folder from struct\n", stderr);
        return -EIO;
    }

    return 0;
}

static int dgp_rename_simple(const char *from, const char *to, const char *to_name)
{
    c_folder *from_folder, *to_folder;
    c_file *from_file;
    int from_index, to_index, new_name_len;
    char *ptr;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    new_name_len = strlen(to_name);

    from_folder = resolve_path(from, &from_index, ctx);
    to_folder = resolve_path(to, &to_index, ctx);
    if (from_folder == NULL) return -ENOENT;
    if (to_folder != NULL) return -EEXIST;

    if (from_index == -1) { //folder
        if (rename_object(from_folder->id, to_name, 0) == -1) return -EIO;
        ptr = realloc(from_folder->name, new_name_len+1);
        if (ptr == NULL) {
            perror("realloc()");
            if (rename_object(from_folder->id, from_folder->name, 0) == -1) {
                fputs("dgp_rename_simple(): Unrecoverable error\n", stderr);
                return -EIO;
            }
            return -EIO;
        }
        from_folder->name = ptr;
        memcpy(from_folder->name, to_name, new_name_len+1);
    }
    else { //file
        from_file = from_folder->files[from_index];
        if (rename_object(from_file->id, to_name, 1) == -1) return -EIO;
        ptr = realloc(from_file->name, new_name_len+1);
        if (ptr == NULL) {
            perror("realloc()");
            if (rename_object(from_file->id, from_file->name, 0) == -1) {
                fputs("dgp_rename_simple(): Unrecoverable error\n", stderr);
                return -EIO;
            }
            return -EIO;
        }
        from_file->name = ptr;
        memcpy(from_file->name, to_name, new_name_len+1);
    }

    return 0;
}

static int dgp_rename_move(const char *from, const char *to, const char *to_subpath, const char *to_name)
{
    c_folder *from_folder, *to_folder;
    c_file *from_file;
    int from_index, to_index, new_name_len;
    char *ptr;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    new_name_len = strlen(to_name);

    from_folder = resolve_path(from, &from_index, ctx);
    to_folder = resolve_path(to, &to_index, ctx);
    if (from_folder == NULL) return -ENOENT;
    if (to_folder != NULL) return -EEXIST;

    to_folder = resolve_path(to_subpath, &to_index, ctx);
    if (to_folder == NULL) return -ENOENT;
    if (to_index != -1) return -ENOTDIR;

    if (from_index == -1) { //folder
        if (move_object(from_folder->id, to_folder->id, 0) == -1) return -EIO;

        from_folder = move_folder(from_folder, to_folder);
        if (from_folder == NULL) {
            fputs("dgp_rename_move(): Error moving folder in struct\n", stderr);
            return -EIO;
        }

        ptr = realloc(from_folder->name, new_name_len+1);
        if (ptr == NULL) {
            perror("realloc()");
            fputs("dgp_rename_move(): Unrecoverable error\n", stderr);
            return -errno;
        }
        from_folder->name = ptr;
        memcpy(from_folder->name, to_name, new_name_len+1);
    }
    else { //file
        from_file = from_folder->files[from_index];
        if (move_object(from_file->id, to_folder->id, 1) == -1) return -EIO;

        from_file = move_file(from_folder, to_folder, from_index);
        if (from_file == NULL) {
            fputs("dgp_rename_move(): Error moving folder in struct\n", stderr);
            return -EIO;
        }

        ptr = realloc(from_file->name, new_name_len+1);
        if (ptr == NULL) {
            perror("realloc()");
            fputs("dgp_rename_move(): Unrecoverable error\n", stderr);
            return -errno;
        }
        from_file->name = ptr;
        memcpy(from_file->name, to_name, new_name_len+1);
    }

    return 0;
}

static int dgp_rename(const char *from, const char *to, unsigned int flags)
{
    int r, from_path_len, from_name_len, to_path_len, to_name_len;
    char *from_subpath, *from_name, *to_subpath, *to_name;

    from_path_len = strlen(from);
    to_path_len = strlen(to);

    from_subpath = malloc(from_path_len+1);
    if (from_subpath == NULL) {
        perror("malloc()");
        return -errno;
    }
    from_name = malloc(from_path_len+1);
    if (from_name == NULL) {
        perror("malloc()");
        free(from_subpath);
        return -errno;
    }
    to_subpath = malloc(to_path_len+1);
    if (to_subpath == NULL) {
        perror("malloc()");
        free(from_subpath);
        free(from_name);
        return -errno;
    }
    to_name = malloc(to_path_len+1);
    if (to_name == NULL) {
        perror("malloc()");
        free(from_subpath);
        free(from_name);
        free(to_subpath);
        return -errno;
    }

    get_subpath(from, from_subpath, from_name);
    get_subpath(to, to_subpath, to_name);

    if (strcmp(from_subpath, to_subpath) == 0) r = dgp_rename_simple(from, to, to_name);
    else r = dgp_rename_move(from, to, to_subpath, to_name);

    free(from_subpath);
    free(from_name);
    free(to_subpath);
    free(to_name);

    return r;
}

static int dgp_link(const char *from, const char *to)
{
    return -ENOTSUP;
}

static int dgp_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    return -ENOTSUP;
}

static int dgp_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
    return -ENOTSUP;
}

static int dgp_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    int index, r;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;
    c_folder *folder;
    c_file *file;

    folder = resolve_path(path, &index, ctx);
    if (folder == NULL) return -ENOENT;
    if (index == -1) return -EISDIR;
    file = folder->files[index];

    if (!file->cached && file_cache_fault(file) == -1) return -EIO;

	if (fi != NULL) {
		if (ftruncate(fi->fh, size) == -1) {
            perror("ftruncate()");
            return -errno;
        }
    }
	else {
		if (truncate(file->cache_path, size) == -1) {
            perror("truncate()");
            return -errno;
        }
    }

    file->size = size;
    file->dirty = 1;

	return 0;
}

static int dgp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    c_folder *folder;
    c_file *file;
    int index, path_len, fh;
    char *subpath, *name, id[32];
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    folder = resolve_path(path, &index, ctx);
    if (folder != NULL) return -EEXIST;

    path_len = strlen(path);
    subpath = malloc(path_len+1);
    if (subpath == NULL) {
        perror("malloc()");
        return -errno;
    }
    name = malloc(path_len+1);
    if (name == NULL) {
        perror("malloc()");
        free(subpath);
        return -errno;
    }

    get_subpath(path, subpath, name);

    folder = resolve_path(subpath, &index, ctx);
    if (folder == NULL) {
        free(subpath);
        free(name);
        return -ENOENT;
    }
    if (index != -1) {
        free(subpath);
        free(name);
        return -ENOTDIR;
    }

    //TODO: check mode

    generate_new_id(id);

    file = add_file(folder, id, name, 0);
    if (file == NULL) {
        fputs("dgp_create(): Error adding file to struct\n", stderr);
        free(subpath);
        free(name);
        return -EIO;
    }

    file->cache_path = malloc(sizeof(CACHE_PATH)+32);
    if (file->cache_path == NULL) {
        perror("malloc()");
        free(subpath);
        free(name);
        return -errno;
    }
    memcpy(file->cache_path, CACHE_PATH, sizeof(CACHE_PATH)-1);
    memcpy(file->cache_path+sizeof(CACHE_PATH)-1, id, 32);
    file->cache_path[sizeof(CACHE_PATH)+31] = '\0';

    fh = open(file->cache_path, fi->flags, mode);
    if (fh == -1) {
        perror("open()");
        free(subpath);
        free(name);
        return -errno;
    }
    if (fi != NULL) fi->fh = fh;

    file->dirty = 1;
    file->cached = 1;

    free(subpath);
    free(name);

    return 0;
}

static int dgp_mknod(const char *path, mode_t mode, dev_t rdev)
{
    if (!S_ISREG(mode)) return -EPERM;
    return dgp_create(path, mode, NULL);
}

static int dgp_open(const char *path, struct fuse_file_info *fi)
{
    c_folder *folder;
    c_file *file;
    int r, index;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    //Handled by dgp_create()
    if (fi->flags & O_CREAT) return -EINVAL;

    folder = resolve_path(path, &index, ctx);
    if (folder == NULL) return -ENOENT;
    if (index == -1) return -EISDIR;

    file = folder->files[index];
    if (!file->cached && file_cache_fault(file) == -1) return -EIO;
    
    if (fi->flags & O_APPEND || fi->flags & O_CREAT || fi->flags & O_TRUNC || fi->flags & O_RDWR || fi->flags & O_WRONLY)
        file->dirty = 1;

    fi->fh = open(file->cache_path, fi->flags);
    if (fi->fh == -1) {
        perror("open()");
        return -errno;
    }

    if (fi->flags & O_TRUNC) return dgp_truncate(path, 0, fi);

    return 0;
}

static int dgp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int r;

    if (fi == NULL) {
        r = dgp_open(path, fi);
        if (r != 0) return r;
    }

    r = pread(fi->fh, buf, size, offset);
    if (r == -1) {
        perror("pread()");
        return -errno;
    }

    return r;
}

static int dgp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int r;

    if (fi == NULL) {
        r = dgp_open(path, fi);
        if (r != 0) return r;
    }

    r = pwrite(fi->fh, buf, size, offset);
    if (r == -1) {
        perror("pwrite()");
        return -errno;
    }

    return r;
}

static int dgp_statfs(const char *path, struct statvfs *stbuf)
{
    return -ENOTSUP;
}

static int dgp_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    c_folder *folder;
    c_file *file;
    int index;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    folder = resolve_path(path, &index, ctx);
    if (folder == NULL) return -ENOENT;
    if (index == -1) return -EISDIR;

    file = folder->files[index];

    return dgp_internal_fsync(folder, file);
}

static int dgp_release(const char *path, struct fuse_file_info *fi)
{
    c_folder *folder;
    c_file *file;
    int index;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    folder = resolve_path(path, &index, ctx);
    if (folder == NULL) return -ENOENT;
    if (index == -1) return -EISDIR;
    if (folder->files[index]->dirty) dgp_fsync(path, 1, fi);

    close(fi->fh);

    return 0;
}

static off_t dgp_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi)
{
    int r;

    if (fi == NULL) {
        r = dgp_open(path, fi);
        if (r != 0) return r;
    }

    r = lseek(fi->fh, off, whence);
    if (r == -1) {
        perror("lseek()");
        return -errno;
    }

    return r;
}

static const struct fuse_operations dgp_oper = {
    .init       = dgp_init,
    .destroy    = dgp_destroy,
    .getattr    = dgp_getattr,
    .access     = dgp_access,
    .readdir    = dgp_readdir,
    .mknod      = dgp_mknod,
    .mkdir      = dgp_mkdir,
    .unlink     = dgp_unlink,
    .rmdir      = dgp_rmdir,
    .rename     = dgp_rename,
    .link       = dgp_link,
    .chmod      = dgp_chmod,
    .chown      = dgp_chown,
    .truncate   = dgp_truncate,
    .open       = dgp_open,
    .create     = dgp_create,
    .read       = dgp_read,
    .write      = dgp_write,
    .statfs     = dgp_statfs,
    .release    = dgp_release,
    .fsync      = dgp_fsync,
    .lseek      = dgp_lseek,
};

int main(int argc, char *argv[])
{
    dgp_ctx *ctx;

    ctx = malloc(sizeof(dgp_ctx));
    if (ctx == NULL) {
        perror("malloc()");
        return -errno;
    }
    ctx->dgp_root = NULL;
    ctx->root_loaded = 0;

    umask(0);

    return fuse_main(argc, argv, &dgp_oper, ctx);
}

