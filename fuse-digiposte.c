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

    if (init_api(ctx->authrization_token) == -1) {
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

static void dgp_destroy(void* private_data)
{
    dgp_ctx *ctx = (dgp_ctx*)private_data;

    //sync fs

    free_root(ctx->dgp_root);
    free_api();
    free(ctx->authrization_token);
    free(ctx);

    //delete cached file

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

        //Directory with r-xr-x---
        stbuf->st_mode = (S_IFMT & S_IFDIR) | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP;
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

        //File with r--r-----
        stbuf->st_mode = (S_IFMT & S_IFREG) | S_IRUSR | S_IRGRP;
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
    if (mask & W_OK) return -EROFS;
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
        st.st_mode = (S_IFMT & S_IFDIR) | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP;
        if (filler(buf, folder->folders[index]->name, &st, 0, 0))
            return 0;
    }

    if (!folder->files_loaded && folder_cache_fault(folder) == -1) return -EIO;
    for (index=0; index < folder->nb_files; index++) {
        memset(&st, 0, sizeof(st));
        st.st_ino = 0;
        st.st_mode = (S_IFMT & S_IFREG) | S_IRUSR | S_IRGRP;
        if (filler(buf, folder->files[index]->name, &st, 0, 0))
            break;
    }

    return 0;
}

static int dgp_mknod(const char *path, mode_t mode, dev_t rdev)
{
    if (!S_ISREG(mode)) return -EPERM;
    //dgp_create(path, mode, NULL);
    return -ENOSYS;
}

static int dgp_mkdir(const char *path, mode_t mode)
{
    c_folder *folder;
    int i, j, index, path_len;
    char *subpath, *name, id[32];
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    path_len = strlen(path);
    subpath = malloc((path_len+1)*sizeof(char));
    if (subpath == NULL) {
        perror("malloc()");
        return -errno;
    }

    i = path_len-1;
    while (path[i] != '/') i--;
    for (j=0; j<i; j++) subpath[j] = path[j];
    subpath[j] = '\0';
    
    name = malloc((path_len-j+1)*sizeof(char));
    if (name == NULL) {
        perror("malloc()");
        free(subpath);
        return -errno;
    }

    memcpy(name, path+j, path_len-j+1);

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
    return -ENOSYS;
}

static int dgp_rmdir(const char *path)
{
    return -ENOSYS;
}

static int dgp_rename(const char *from, const char *to, unsigned int flags)
{
    return -ENOSYS;
}

static int dgp_link(const char *from, const char *to)
{
    return -ENOSYS;
}

static int dgp_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    return -ENOSYS;
}

static int dgp_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
    return -ENOSYS;
}

static int dgp_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    return -ENOSYS;
}

static int dgp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    return -ENOSYS;
}

static int dgp_open(const char *path, struct fuse_file_info *fi)
{
    c_folder *folder;
    c_file *file;
    int index;
    struct fuse_context *fctx = fuse_get_context();
    dgp_ctx *ctx = (dgp_ctx*)fctx->private_data;

    folder = resolve_path(path, &index, ctx);
    if (folder == NULL) return -ENOENT;
    if (index == -1) {
        return -EISDIR;
    }

    file = folder->files[index];
    if (!file->cached && file_cache_fault(file) == -1) return -EIO;
    
    if (fi->flags & O_APPEND || fi->flags & O_CREAT || fi->flags & O_TRUNC || fi->flags & O_RDWR || fi->flags & O_WRONLY) return -EROFS;

    fi->fh = open(file->cache_path, O_RDONLY);
    if (fi->fh == -1) {
        perror("open()");
        return -errno;
    }

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
    return -ENOSYS;
}

static int dgp_statfs(const char *path, struct statvfs *stbuf)
{
    return -ENOSYS;
}

static int dgp_release(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    close(fi->fh);
    return 0;
}

static int dgp_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    (void) path;
    (void) isdatasync;
    (void) fi;
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
    enum { MAX_ARGS = 10 };
    int i, new_argc, token_len;
    char *new_argv[MAX_ARGS];
    dgp_ctx *ctx;

    ctx = malloc(sizeof(dgp_ctx));
    if (ctx == NULL) {
        perror("malloc()");
        return -errno;
    }
    ctx->dgp_root = NULL;
    ctx->root_loaded = 0;
    ctx->authrization_token = NULL;

    umask(0);
    for (i=0, new_argc=0; (i<argc) && (new_argc<MAX_ARGS); i++) {
        if (!strcmp(argv[i], "--auth")) {
            if (i == argc) {
                fputs("Authorization token needed\n", stderr);
                return -1;
            }
            token_len = strlen(argv[i+1]);
            ctx->authrization_token = malloc(token_len * sizeof(char));
            if (ctx->authrization_token == NULL) {
                perror("malloc()");
                return -errno;
            }
            strcpy(ctx->authrization_token, argv[i+1]);
            i++;
        } else {
            new_argv[new_argc++] = argv[i];
        }
    }

    if (ctx->authrization_token == NULL) {
        fputs("Authorization token needed\n", stderr);
        return -1;
    }

    return fuse_main(new_argc, new_argv, &dgp_oper, ctx);
}

