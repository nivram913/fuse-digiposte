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
libcurl4-openssl-dev
libjson-c-dev
```

## Security

Here is an AppArmor profile to confine fuse-digiposte:

```
#include <tunables/global>

profile fuse-digiposte /path/to/fuse-digiposte {
  #include <abstractions/base>
  #include <abstractions/consoles>
  #include <abstractions/nameservice>
  #include <abstractions/user-tmp>
  
  mount fstype=(fuse.fuse-digiposte),
  umount,
  
  /dev/fuse rw,
  /usr/bin/fusermount3 PUx,
  
  /path/to/fuse-digiposte mr,
  
  /etc/ssl/openssl.cnf r,
}
```
