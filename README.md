# fuse-digiposte

A FUSE implementation for Digiposte online storage service

Read-write, single threaded, authorization token in argument. Run with:

```
./fuse-digiposte --auth xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx -s /mnt/dgpfs
```

You may want to add the `-f` flag to have error messages.

Unmount with `fusermount -u /mnt/dgpfs`

## Dependancies

On Debian 12, install these packages:
```
libfuse3-dev
libjson-c-dev
```

The Python subsystem use `requests` to handle API communication.

## Security

If you use (or not) AppArmor you should modify the Makefile to enable/disable the definition of `USE_APPARMOR` macro in `CFLAGS` and enable/disable the linking with apparmor lib (`-lapparmor` option) in `LFLAGS`.

Here is the AppArmor profile to confine fuse-digiposte and its subsystem:

```
#include <tunables/global>

profile fuse-digiposte /usr/local/bin/fuse-digiposte {
    #include <abstractions/base>
    #include <abstractions/consoles>
    #include <abstractions/user-tmp>

    mount fstype=(fuse.fuse-digiposte),
    umount,

    change_profile -> fuse-digiposte//api-subsystem,

    /dev/fuse rw,
    /usr/bin/fusermount3 PUx,

    /usr/local/bin/fuse-digiposte mr,

    profile api-subsystem {
        #include <abstractions/base>
        #include <abstractions/consoles>
        #include <abstractions/nameservice>
        #include <abstractions/python>
        #include <abstractions/user-tmp>
        
        /usr/local/bin/{,DigiposteAPI.py} r,
        /usr/bin/python3* r,
        /etc/ssl/openssl.cnf r,
    }
}
```
