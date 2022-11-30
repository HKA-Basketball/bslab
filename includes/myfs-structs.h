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
#define NUM_DATA_BLOCK_COUNT 268435456 // 2^28
#define BLOCKS_SPBLOCK 1
#define BLOCKS_DMAP (NUM_DATA_BLOCK_COUNT/BLOCK_SIZE)
#define BLOCKS_FAT ((NUM_DATA_BLOCK_COUNT*4)/BLOCK_SIZE)
#define BLOCKS_ROOT NUM_DIR_ENTRIES
#define POS_SPBLOCK 0
#define POS_DMAP BLOCKS_SPBLOCK
#define POS_FAT POS_DMAP + BLOCKS_DMAP
#define POS_ROOT POS_FAT + BLOCKS_FAT
#define POS_DATA POS_ROOT + BLOCKS_ROOT
#define POS_NULLPTR -124 //used for empty files which need a blocknumber
#define ERROR_BLOCKNUMBER 4294967296 // 2^32

// TODO: Add structures of your file system here
struct MyFsFileInfo {
    char cName[NAME_LENGTH];    // Name
    size_t size;                // Data Size
    unsigned char *data;        // Data
    __uid_t uid;                // User ID
    __gid_t gid;                // Gruppen ID 
    __mode_t mode;              // File mode
    struct timespec atime;        // Time of last access.
    struct timespec mtime;        // Time of last modification.
    struct timespec ctime;        // Time of last status change.

    //const char* cPath;                // Path to the file
    char cPath[NAME_LENGTH + 1];    // Path to the file
};

// Aufgabe 2.

struct MyFsDiskInfo {
    size_t size;                // Data Size    64bit
    int32_t data;               // Block Pos    32bit
    __uid_t uid;                // User ID      32bit
    __gid_t gid;                // Gruppen ID   32bit
    __mode_t mode;              // File mode    32bit
    __time_t atime;                // Time of last access.         64bit
    __time_t mtime;                // Time of last modification.   64bit
    __time_t ctime;                // Time of last status change.  64bit
    char cPath[NAME_LENGTH + 1];   // Path to the file    256bit
};

struct SuperBlock {
    //Informationen zum File-System (z.B. Größe, Positionen der Einträge unten...)
    size_t infoSize;
    size_t dataSize;
    int32_t blockPos;
    int32_t dmapPos;
    int32_t fatPos;
    int32_t rootPos;
    int32_t dataPos;
};

#endif /* myfs_structs_h */
