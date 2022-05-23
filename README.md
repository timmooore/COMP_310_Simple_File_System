# COMP_310_Simple_File_System

### by Timothy Moore

The files sfs_api.h, sfs_api.c, and tim_test.c are written by me, while the other files were provided for completion of the assignment.

The goal of the assignment was to design and implement a simple file system that can be mounted by a user on their machine, using the provided FUSE wrapper and disk emulator. 

The files sfs_api.h and sfs_api.c implement an API for interfacing with the FUSE wrapper. In those files, a super block, inode, inode table, free block table, and other features are implemented. 

The implementation still requires work as it works with simple test cases but sometimes results in segmentation faults or stack smash errors. 
