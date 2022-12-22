//
// Created by Oliver Waldhorst on 20.03.20.
// Copyright © 2017-2020 Oliver Waldhorst. All rights reserved.
//

#ifndef MYFS_MYONDISKFS_H
#define MYFS_MYONDISKFS_H

#include "myfs.h"

/// @brief On-disk implementation of a simple file system.
class MyOnDiskFS : public MyFS {
protected:
    // BlockDevice blockDevice;

public:
    static MyOnDiskFS *Instance();

    // TODO: [PART 1] Add attributes of your file system here
    SuperBlock mySuperBlock;
    char myDmap[NUM_DATA_BLOCK_COUNT];      //Verzeichnis der freien Datenblöcke, 1 = empty, 0 = occupied
    /*
     *  myDmap[n] holds information about the nth block INSIDE the data segment, meaning it is indexed with 0 being the start of the data segment
     */
    int32_t myFAT[NUM_DATA_BLOCK_COUNT];    //File Allocation Table FAT
    /*
     *  myFAT[0] returns what block comes after. It is indexed with 0 being the start of the data segment
     *  If one wants to traverse through the FAT, one can simply myFAT[myFAT[myFAT[n]]] do like this, meaning no arithmetics between iterations are needed
     */
    MyFsDiskInfo myRoot[NUM_DIR_ENTRIES];
    bool myFsOpenFiles[NUM_DIR_ENTRIES];
    bool myFsEmpty[NUM_DIR_ENTRIES]; //1 = empty, 0 = occupied
    unsigned int iCounterFiles;
    unsigned int iCounterOpen;
    char *containerFilePath;

    MyOnDiskFS();

    ~MyOnDiskFS();

    static void SetInstance();

    // --- Methods called by FUSE ---
    // For Documentation see https://libfuse.github.io/doxygen/structfuse__operations.html
    virtual int fuseGetattr(const char *path, struct stat *statbuf);

    virtual int fuseMknod(const char *path, mode_t mode, dev_t dev);

    virtual int fuseUnlink(const char *path);

    virtual int fuseRename(const char *path, const char *newpath);

    virtual int fuseChmod(const char *path, mode_t mode);

    virtual int fuseChown(const char *path, uid_t uid, gid_t gid);

    virtual int fuseTruncate(const char *path, off_t newSize);

    virtual int fuseOpen(const char *path, struct fuse_file_info *fileInfo);

    virtual int fuseRead(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo);

    virtual int
    fuseWrite(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo);

    virtual int fuseRelease(const char *path, struct fuse_file_info *fileInfo);

    virtual void *fuseInit(struct fuse_conn_info *conn);

    virtual int
    fuseReaddir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fileInfo);

    virtual int fuseTruncate(const char *path, off_t offset, struct fuse_file_info *fileInfo);

    virtual void fuseDestroy();

    // TODO: Add methods of your file system here
    size_t findFreeBlock();

    void initializeHelpers();

    void dumpStructures();

    int unlinkBlocks(int32_t num);

    int syncRoot();

    int syncDmapFat(u_int32_t num);

    int containerFull(size_t neededBlocks);

    int iIsPathValid(const char *path, uint64_t fh);

    int iFindEmptySpot();
};

#endif //MYFS_MYONDISKFS_H
