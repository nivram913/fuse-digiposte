#define FUSE_USE_VERSION 31

#define _GNU_SOURCE

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "digiposte_api.h"
#include "data_structures.h"

#ifndef DGP_FUSE_H
#define DGP_FUSE_H

#define CACHE_PATH "/tmp/.cache-dgp-fuse/"
#define AUTHORIZATION_TOKEN ""

typedef struct dgp_ctx {
    c_folder *dgp_root;
    char root_loaded;
    char *authrization_token;
} dgp_ctx;

#endif
