# fuse-digiposte

A FUSE implementation for Digiposte online storage service

Read-write, single threaded, interactive authentication. Run with:

```
./fuse-digiposte -s /mnt/dgpfs
```

You may want to add the `-f` flag to have error messages.

Unmount with `fusermount -u /mnt/dgpfs`

## Dependancies

On Debian 12, install these packages:
```
libfuse3-dev
libjson-c-dev
```

The Python subsystem use `requests` to handle API communication and `webview` (`python3-webview` Debian package) to handle interactive authentication.

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
        #include <abstractions/X>
        #include <abstractions/dconf>
        #include <abstractions/fonts>
        #include <abstractions/freedesktop.org>
        #include <abstractions/gtk>

        /usr/local/bin/{,DigiposteAPI.py} r,
        /usr/bin/python3* r,
        /etc/ssl/openssl.cnf r,
        /usr/lib/x86_64-linux-gnu/webkit2gtk-4.0/WebKitNetworkProcess PUx,
        /usr/lib/x86_64-linux-gnu/webkit2gtk-4.0/WebKitWebProcess PUx,

    }
}
```
