# fuse-digiposte

A FUSE implementation for Digiposte online storage service

For the moment: read-only filesystem, authorization token in argument

To do:
- Better handling of API communications (check HTTP response code, ...)
- Fix resolve_path() when returns NULL => not found/error
- Fix bugs in data_structures remove_* functions
- Implement write functions (rw fs)
- Better authentication
