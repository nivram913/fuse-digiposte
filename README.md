# fuse-digiposte

A FUSE implementation for Digiposte online storage service

For the moment: read-only filesystem, authorization token in argument

Because I'm not a profesional programmer but a security engineer (and this FS is written in C), I created an AppArmor profile to prevent potential vulnerabilities to harm the system ;)

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
