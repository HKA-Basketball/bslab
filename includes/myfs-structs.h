//
//  myfs-structs.h
//  myfs
//
//  Created by Oliver Waldhorst on 07.09.17.
//  Copyright © 2017 Oliver Waldhorst. All rights reserved.
//

#ifndef myfs_structs_h
#define myfs_structs_h

#define NAME_LENGTH 255
#define BLOCK_SIZE 512
#define NUM_DIR_ENTRIES 64
#define NUM_OPEN_FILES 64

// TODO: Add structures of your file system here
struct MyFsFileInfo
{
    char cName[NAME_LENGTH];    // Name
    size_t size;                // Data Size
    unsigned char* data;        // Data
    __uid_t uid;                // User ID
    __gid_t gid;                // Gruppen ID 
    __mode_t mode;              // File mode
    struct timespec atime;	    // Time of last access.
    struct timespec mtime;	    // Time of last modification.
    struct timespec ctime;	    // Time of last status change.

    char* cPath;                // Path to the file
};

#endif /* myfs_structs_h */
