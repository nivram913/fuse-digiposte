# fuse-digiposte

A FUSE implementation for Digiposte online storage service

For the moment: read-only filesystem without file fetch, authorization token in argument

To do:
- File fetching (read-only)
- Better handling of API communications (check HTTP response code, ...)
- Fix bugs in data_structures remove_* functions
- Implement write functions (rw fs)
- Better authentication
