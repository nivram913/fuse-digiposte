#include "fuse-digiposte.h"

static int folder_cache_fault(c_folder *folder)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return get_folder_content(folder);
}

static int file_cache_fault(c_file *file)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -1;
}

/*
If path point to a directory, return the directory and set index to -1
If path point to a file, return the containing directory and set index to the index of the file in files table
If path doesn't exist or error occured, return NULL
*/
static c_folder* resolve_path(const char *path, int *index, const dgp_ctx *ctx)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    fprintf(stderr, "\tPath: %s\n", path);
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
                if (!current_folder->files_loaded) folder_cache_fault(current_folder);
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

static int fill_dir_plus = 0;

static void *dgp_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    dgp_ctx *ctx;

    cfg->use_ino = 0;
    cfg->direct_io = 1;
    //cfg->parallel_direct_writes = 1;
    cfg->entry_timeout = 0;
    cfg->attr_timeout = 0;
    cfg->negative_timeout = 0;

    ctx = malloc(sizeof(dgp_ctx));
    if (ctx == NULL) {
        perror("malloc()");
        exit(-errno);
    }
    ctx->dgp_root = NULL;
    ctx->root_loaded = 0;

    if (init_api(AUTHORIZATION_TOKEN) == -1) {
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

    return (void*)ctx;
}

static void *dgp_destroy(void* private_data)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    dgp_ctx *ctx = (dgp_ctx*)private_data;

    //sync fs

    free_root(ctx->dgp_root);
    free_api();
    free(ctx);

    //delete cached file

    return;
}

static int dgp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
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

        //Directory with r--r-----
        stbuf->st_mode = (S_IFMT & S_IFREG) | S_IRUSR | S_IRGRP;
        stbuf->st_nlink = 1;
        stbuf->st_uid = fctx->uid;
        stbuf->st_gid = fctx->gid;
        stbuf->st_size = folder->files[index]->size;
        stbuf->st_atim = now;
        stbuf->st_mtim = now;
        stbuf->st_ctim = now;
    }

    fprintf(stderr, "\t%s() tells %s is %s with %o\n", __func__, path, S_ISREG(stbuf->st_mode)?"a file":"a directory", stbuf->st_mode);

    return 0;
}

static int dgp_access(const char *path, int mask)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    struct stat stbuf;

    if (dgp_getattr(path, &stbuf, NULL) == -ENOENT) return -ENOENT;
    if (mask & W_OK) return -EROFS;
    if (mask & X_OK && S_ISREG(stbuf.st_mode)) return -EACCES;

    return 0;
}

static int dgp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                       struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    fprintf(stderr, "Entering %s()\n", __func__);
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
        if (filler(buf, folder->folders[index]->name, &st, 0, fill_dir_plus))
            return 0;
    }

    if (!folder->files_loaded) folder_cache_fault(folder);
    for (index=0; index < folder->nb_files; index++) {
        memset(&st, 0, sizeof(st));
        st.st_ino = 0;
        st.st_mode = (S_IFMT & S_IFREG) | S_IRUSR | S_IRGRP;
        if (filler(buf, folder->files[index]->name, &st, 0, fill_dir_plus))
            break;
    }

    return 0;
}

static int dgp_mknod(const char *path, mode_t mode, dev_t rdev)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_mkdir(const char *path, mode_t mode)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_unlink(const char *path)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_rmdir(const char *path)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_rename(const char *from, const char *to, unsigned int flags)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_link(const char *from, const char *to)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_open(const char *path, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    int res;

    return -ENOSYS;

    res = open(path, fi->flags);
    if (res == -1)
        return -errno;

        /* Enable direct_io when open has flags O_DIRECT to enjoy the feature
        parallel_direct_writes (i.e., to get a shared lock, not exclusive lock,
        for writes to the same file). */
    if (fi->flags & O_DIRECT) {
        fi->direct_io = 1;
        //fi->parallel_direct_writes = 1;
    }

    fi->fh = res;
    return 0;
}

static int dgp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -ENOSYS;
}

static int dgp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -EROFS;
}

static int dgp_statfs(const char *path, struct statvfs *stbuf)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -ENOSYS;
}

static int dgp_release(const char *path, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    (void) path;
    close(fi->fh);
    return 0;
}

static int dgp_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

static off_t dgp_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi)
{
    fprintf(stderr, "Entering %s()\n", __func__);
    return -ENOSYS;
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
    fprintf(stderr, "Entering %s()\n", __func__);
    enum { MAX_ARGS = 10 };
    int i,new_argc;
    char *new_argv[MAX_ARGS];

    umask(0);
            /* Process the "--plus" option apart */
    for (i=0, new_argc=0; (i<argc) && (new_argc<MAX_ARGS); i++) {
        if (!strcmp(argv[i], "--plus")) {
            fill_dir_plus = FUSE_FILL_DIR_PLUS;
        } else {
            new_argv[new_argc++] = argv[i];
        }
    }
    return fuse_main(new_argc, new_argv, &dgp_oper, NULL);
}

