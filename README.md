# fuse-digiposte

A FUSE implementation for Digiposte online storage service

For the moment: read-only filesystem, authorization token in argument

To do:
- Better handling of API communications (check HTTP response code, ...)
- Fix resolve_path() when returns NULL => not found/error
- Implement write functions (rw fs)
- Better authentication

Because I'm not a profesional programmer but a security engineer (and this FS is written in C), I created an AppArmor profile to prevent potential vulnerabilities to harm the system ;)
