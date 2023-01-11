//
// Created by Oliver Waldhorst on 20.03.20.
// Copyright © 2017-2020 Oliver Waldhorst. All rights reserved.
//

#include "myondiskfs.h"

// For documentation of FUSE methods see https://libfuse.github.io/doxygen/structfuse__operations.html

#undef DEBUG

#define DEBUG
#define DEBUG_METHODS
#define DEBUG_RETURN_VALUES

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <algorithm>

#include "macros.h"
#include "myfs.h"
#include "myfs-info.h"
#include "blockdevice.h"

/// @brief Constructor of the on-disk file system class.
///
/// You may add your own constructor code here.
MyOnDiskFS::MyOnDiskFS() : MyFS() {
    // create a block device object
    this->blockDevice = new BlockDevice(BLOCK_SIZE);

    this->blocks4DATA = NUM_DATA_BLOCKS; // 65.536
    this->blocks4SPBlock = 1; // 1 Block = 512
    this->blocks4DMAP = this->blocks4DATA / BLOCK_SIZE; // 128 Blöcke = 65.536
    this->blocks4FAT = (this->blocks4DATA / BLOCK_SIZE) * 4; // 512 Blöcke = 262.144
    this->blocks4ROOT = NUM_DIR_ENTRIES; // 64 Blöcke = 32.768

    this->posSPBlock = 0;
    this->posDMAP = this->blocks4SPBlock; // 1 Block = 512
    this->posFAT = this->posDMAP + this->blocks4DMAP; // 129 Blöcke = 66.048
    this->posROOT = this->posFAT + this->blocks4FAT; // 641 Blöcke = 328.192
    this->posDATA = this->posROOT + this->blocks4ROOT; // 705 Blöcke = 360.960

    this->posENDofDATA = this->posDATA + this->blocks4DATA; // 66.241 Blöcke = 33.915.392

    //initialise superblock
    mySuperBlock.infoSize = this->posDATA;
    mySuperBlock.dataSize = this->blocks4DATA * BLOCK_SIZE;
    mySuperBlock.blockPos = this->posSPBlock;
    mySuperBlock.dataPos = this->posDATA;
    mySuperBlock.dmapPos = this->posDMAP;
    mySuperBlock.rootPos = this->posROOT;
    mySuperBlock.fatPos = this->posFAT;
    mySuperBlock.numFreeBlocks = this->blocks4DATA;

    //initialise heap structures
    memset(&myDmap, 1, sizeof(myDmap));
    for (int i = 0; i < this->blocks4DATA; i++) {
        myFAT[i] = -1;
    }
    memset(&myRoot, 0, sizeof(myRoot));
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        myRoot[i].data = POS_NULLPTR;
        myRoot[i].cPath[0] = '\0';
    }
    iCounterFiles = iCounterOpen = 0;
    memset(&myFsEmpty, 1, sizeof(myFsEmpty));
    memset(&myFsOpenFiles, 0, sizeof(myFsOpenFiles));

    initializeHelpers();
}

/// @brief Destructor of the on-disk file system class.
///
/// You may add your own destructor code here.
MyOnDiskFS::~MyOnDiskFS() {
    // free block device object
    delete this->blockDevice;
}

/// @brief Create a new file.
///
/// Create a new file with given name and permissions.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] mode Permissions for file access.
/// \param [in] dev Can be ignored.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseMknod(const char *path, mode_t mode, dev_t dev) {
    //LOGM();
    readRoot();

    //filesystem full?
    if (iCounterFiles >= NUM_DIR_ENTRIES) {
        RETURN(-ENOSPC);
    }

    //check length of given filename
    if (strlen(path) - 1 > NAME_LENGTH) {
        RETURN(-EINVAL);
    }

    //file with same name exists?
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (myFsEmpty[i]) {
            continue;
        }

        if (strcmp(path, myRoot[i].cPath) == 0) {
            RETURN(-EEXIST); // already exists
        }
    }

    //find index to put fileinfo in
    int index = iFindEmptySpot();
    if (index < 0) {
        RETURN(index);
    }

    //check length of given filename
    if (strlen(path) - 1 > NAME_LENGTH) {
        RETURN(-EINVAL);
    }

    //overwrite all fileinfo values
    strcpy(myRoot[index].cPath, path);
    myRoot[index].size = 0;
    myRoot[index].data = POS_NULLPTR;
    myRoot[index].atime = myRoot[index].ctime = myRoot[index].mtime = time(NULL);
    myRoot[index].gid = getgid();
    myRoot[index].uid = getuid();
    myRoot[index].mode = mode;
    myFsEmpty[index] = false;

    //increment file counter
    iCounterFiles++;

    writeRoot();
    RETURN(0);
}

/// @brief Delete a file.
///
/// Delete a file with given name from the file system.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseUnlink(const char *path) {
    //LOGM();
    readRoot();

    // Get index of file by path
    size_t index = -1;
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (strcmp(path, myRoot[i].cPath) == 0) {
            //found file
            index = i;
        }
    }

    // Check if the file has been found
    if (index < 0) {
        // No such file or directory
        RETURN(-ENOENT);
    }

    if (myFsOpenFiles[index]) {
        RETURN(-EBUSY);
    }

    // Check if there are blocks to be freed
    if (myRoot[index].data != POS_NULLPTR) {
        // Free allocated blocks
        int retEr = freeBlocks(myRoot[index].data);
        if (retEr < 0) {
            RETURN(retEr);
        }
    }

    //reset myRoot
    memset(&myRoot[index], 0, sizeof(MyFsDiskInfo));
    myRoot[index].data = POS_NULLPTR;
    myRoot[index].cPath[0] = '\0';

    //adjust helpers
    myFsEmpty[index] = true;
    iCounterFiles--;

    writeRoot();
    RETURN(0);
}

/// @brief Rename a file.
///
/// Rename the file with with a given name to a new name.
/// Note that if a file with the new name already exists it is replaced (i.e., removed
/// before renaming the file.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newpath  New name of the file, starting with "/".
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRename(const char *path, const char *newpath) {
    //LOGM();
    readRoot();

    // Check length of new filename
    if (strlen(newpath) - 1 > NAME_LENGTH) {
        RETURN(-EINVAL);
    }

    // Get index of file by path
    size_t index = -1;
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (myFsEmpty[i]) {
            continue;
        }

        // Search for the index of the file
        if (strcmp(path, myRoot[i].cPath) == 0) {
            index = i;
        }

        // File with the new name already exists
        if (strcmp(myRoot[i].cPath, newpath) == 0) {
            RETURN(-EEXIST);
        }
    }

    // file found?
    if (index < 0) {
        RETURN(-ENOENT);
    }

    // Overwrite fileinfo values
    strcpy(myRoot[index].cPath, newpath);
    myRoot[index].atime = myRoot[index].ctime = time(NULL);

    writeRoot();
    RETURN(0);
}

/// @brief Get file meta data.
///
/// Get the metadata of a file (user & group id, modification times, permissions, ...).
/// \param [in] path Name of the file, starting with "/".
/// \param [out] statbuf Structure containing the meta data, for details type "man 2 stat" in a terminal.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseGetattr(const char *path, struct stat *statbuf) {
    //LOGM();
    readRoot();

    // GNU's definitions of the attributes (http://www.gnu.org/software/libc/manual/html_node/Attribute-Meanings.html):
    // 		st_uid: 	The user ID of the file’s owner.
    //		st_gid: 	The group ID of the file.
    //		st_atime: 	This is the last access time for the file.
    //		st_mtime: 	This is the time of the last modification to the contents of the file.
    //		st_mode: 	Specifies the mode of the file. This includes file type information (see Testing File Type) and
    //		            the file permission bits (see Permission Bits).
    //		st_nlink: 	The number of hard links to the file. This count keeps track of how many directories have
    //	             	entries for this file. If the count is ever decremented to zero, then the file itself is
    //	             	discarded as soon as no process still holds it open. Symbolic links are not counted in the
    //	             	total.
    //		st_size:	This specifies the size of a regular file in bytes. For files that are really devices this field
    //		            isn’t usually meaningful. For symbolic links this specifies the length of the file name the link
    //		            refers to.

    statbuf->st_uid = getuid(); // The owner of the file/directory is the user who mounted the filesystem
    statbuf->st_gid = getgid(); // The group of the file/directory is the same as the group of the user who mounted the filesystem
    statbuf->st_atime = time(NULL); // The last "a"ccess of the file/directory is right now

    int ret = 0; // Return value

    // Check if the path given is the root
    if (strcmp(path, "/") == 0) {
        statbuf->st_mode = S_IFDIR | 0755;
        statbuf->st_nlink = 2; // Why "two" hardlinks instead of "one"? The answer is here: http://unix.stackexchange.com/a/101536

    // Check if a filepath is given
    } else if (strlen(path) > 0) {

        // Find the file
        for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
            if (myFsEmpty[i]) {
                continue;
            }

            // Read metadata if the file was found
            if (strcmp(path, myRoot[i].cPath) == 0) {
                statbuf->st_mode = myRoot[i].mode;
                statbuf->st_nlink = 1;
                statbuf->st_size = myRoot[i].size;
                statbuf->st_mtime = myRoot[i].mtime; // The last "m"odification of the file/directory is right now
                RETURN(0);
            }
        }

        // No such file or directory
        ret = -ENOENT;

    } else {
        // Path length is <= 0
        ret = -ENOENT;
    }

    RETURN(ret);
}

/// @brief Change file permissions.
///
/// Set new permissions for a file.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] mode New mode of the file.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseChmod(const char *path, mode_t mode) {
    //LOGM();
    readRoot();

    // Get index of file by path
    size_t index = -1;
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsDiskInfo info = myRoot[i];
        if (strcmp(path, info.cPath) == 0) {
            index = i;
            break;
        }
    }

    // Check if the file has been found
    if (index < 0) {
        // No such file or directory
        RETURN(-ENOENT);
    }

    // Overwrite fileinfo values
    myRoot[index].mode = mode;
    myRoot[index].atime = myRoot[index].ctime = time(NULL);

    writeRoot();
    RETURN(0);
}

/// @brief Change the owner of a file.
///
/// Change the user and group identifier in the meta data of a file.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] uid New user id.
/// \param [in] gid New group id.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseChown(const char *path, uid_t uid, gid_t gid) {
    //LOGM();
    readRoot();

    // Get index of file by path
    size_t index = -1;
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsDiskInfo info = myRoot[i];
        if (strcmp(path, info.cPath) == 0) {
            index = i;
            break;
        }
    }

    // Check if the file has been found
    if (index < 0) {
        // No such file or directory
        RETURN(-ENOENT);
    }

    // Overwrite fileinfo values
    myRoot[index].uid = uid;
    myRoot[index].gid = gid;
    myRoot[index].atime = myRoot[index].ctime = time(NULL);

    writeRoot();
    RETURN(0);
}

/// @brief Open a file.
///
/// Open a file for reading or writing. This includes checking the permissions of the current user and incrementing the
/// open file count.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [out] fileInfo Can be ignored in Part 1
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseOpen(const char *path, struct fuse_file_info *fileInfo) {
    //LOGM();
    readRoot();

    // Check if too many files are open
    if (iCounterOpen >= NUM_OPEN_FILES) {
        // Too many open files
        RETURN(-EMFILE);
    }

    // Find the file and open it
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (myFsEmpty[i]) {
            continue;
        }

        if (strcmp(path, myRoot[i].cPath) == 0) {
            // Check if the file is already open
            if (myFsOpenFiles[i]) {
                RETURN(-EPERM); // Already Open
            } else {
                // Set Handle etc
                myFsOpenFiles[i] = true;
                fileInfo->fh = i; // can be used in fuseRead and fuseRelease
                iCounterOpen++;
                myRoot[i].atime = myRoot[i].ctime = time(NULL);
                break;
            }
        }
    }

    writeRoot();
    RETURN(0);
}

/// @brief Read from a file.
///
/// Read a given number of bytes from a file starting form a given position.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// Note that the file content is an array of bytes, not a string. I.e., it is not (!) necessarily terminated by '\0'
/// and may contain an arbitrary number of '\0'at any position. Thus, you should not use strlen(), strcpy(), strcmp(),
/// ... on both the file content and buf, but explicitly store the length of the file and all buffers somewhere and use
/// memcpy(), memcmp(), ... to process the content.
/// \param [in] path Name of the file, starting with "/".
/// \param [out] buf The data read from the file is stored in this array. You can assume that the size of buffer is at
/// least 'size'
/// \param [in] size Number of bytes to read
/// \param [in] offset Starting position in the file, i.e., number of the first byte to read relative to the first byte of
/// the file
/// \param [in] fileInfo Can be ignored in Part 1
/// \return The Number of bytes read on success. This may be less than size if the file does not contain sufficient bytes.
/// -ERRNO on failure.
int MyOnDiskFS::fuseRead(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    //LOGM();
    readFat();
    readRoot();

    if (size < 0 || offset < 0) {
        RETURN(-EINVAL);
    }

    if (0 > iIsPathValid(path, fileInfo->fh)) {
        RETURN(iIsPathValid(path, fileInfo->fh));
    }

    //file opened
    if (!myFsOpenFiles[fileInfo->fh]) {
        LOG("File not open");
        RETURN(-EPERM);
    }

    MyFsDiskInfo *info = &myRoot[fileInfo->fh];

    // Check if the offset is within the file bounds
    if (offset < 0 || offset >= info->size) {
        LOG("Offset is not within the file bounds");
        size = 0; // The Number of bytes read
    }

    // Check if the file has blocks to read
    if (info->data == POS_NULLPTR || info->size == 0) {
        LOG("File is empty");
        size = 0; // The Number of bytes read
    }

    if (size > 0) {
        if (size > info->size) {
            LOG("Reading size is greater than filesize");
            size = info->size;
        }

        if ((offset + size) > info->size) {
            LOG("Reading size exceeds filesize with offset");
            size = info->size - offset;
        }

        int32_t byteOffset = offset % BLOCK_SIZE;

        int32_t startBlockOffset = offset / BLOCK_SIZE;
        int32_t endBlockOffset = (offset + size) / BLOCK_SIZE;
        int32_t numBlocks2Read = std::ceil(((double) (size + byteOffset) / BLOCK_SIZE));

        int32_t curFATBlock = info->data;
        int32_t start2ReadFAT;

        //LOGF("startBlockOffset = %ld, endBlockOffset = %ld, numBlocks2Read = %ld, curFATBlock = %ld", startBlockOffset, endBlockOffset, numBlocks2Read, curFATBlock);

        if (startBlockOffset == 0) {
            start2ReadFAT = curFATBlock;
        }

        for (size_t i = 0; i < startBlockOffset; i++) {
            //Travel through blocks of the file to reach the offset
            int32_t nextFATBlock = myFAT[curFATBlock];
            if (nextFATBlock == -1) {
                //LOG("myFAT ended prematurely. THIS SHOULD NOT OCCUR!");
                RETURN(-2000);
            }
            curFATBlock = nextFATBlock;
            start2ReadFAT = curFATBlock;
        }
        //LOGF("start2ReadFAT = %ld", start2ReadFAT);

        char buffer[BLOCK_SIZE];
        char* bufIter = buf;

        for (size_t i = 0; i < numBlocks2Read; i++) {
            int ret = this->blockDevice->read(this->posDATA + start2ReadFAT, buffer);
            if (ret < 0) {
                //LOG("Couldn't read from Container");
                RETURN (-4000);
            }
            void *retPtr = nullptr;
            if (byteOffset > 0) {
                //read first block (read less than 512 bytes)
                retPtr = memcpy(bufIter, buffer + byteOffset,
                                std::min(size, (size_t) BLOCK_SIZE - byteOffset));
                bufIter += std::min(size, (size_t) BLOCK_SIZE - byteOffset);
                byteOffset = 0;
            }

            else if (bufIter + BLOCK_SIZE > buf + size) {
                //read last block (read less than 512 bytes)
                retPtr = memcpy(bufIter, buffer,
                                std::max(0UL, (u_long) (buf + size - bufIter)));
                break;
            }

            else if (bufIter + BLOCK_SIZE <= buf + size) {
                //read full blocks
                retPtr = memcpy(bufIter, buffer, BLOCK_SIZE);
                bufIter += BLOCK_SIZE;
            }

            if (retPtr == nullptr) {
                //LOG("memcpy failed");
                RETURN (-5000);
            }

            start2ReadFAT = myFAT[start2ReadFAT];

            /*char debugBuf[BLOCK_SIZE + 1];
            memcpy(debugBuf, buffer, BLOCK_SIZE);
            debugBuf[BLOCK_SIZE] = '\0';
            LOGF("BlockBuf = %s", debugBuf);*/
        }

        char* debugBuf2 = (char*)malloc(size + 1);
        if (debugBuf2 != nullptr) {
            memcpy(debugBuf2, buf, size);
            debugBuf2[size] = '\0';
            LOGF("BlockBuf2 = %s", debugBuf2);
        }
        free(debugBuf2);
    }

    info->atime = info->ctime = time(NULL);

    writeRoot();

    RETURN(size);
}

/// @brief Write to a file.
///
/// Write a given number of bytes to a file starting at a given position.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// Note that the file content is an array of bytes, not a string. I.e., it is not (!) necessarily terminated by '\0'
/// and may contain an arbitrary number of '\0'at any position. Thus, you should not use strlen(), strcpy(), strcmp(),
/// ... on both the file content and buf, but explicitly store the length of the file and all buffers somewhere and use
/// memcpy(), memcmp(), ... to process the content.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] buf An array containing the bytes that should be written.
/// \param [in] size Number of bytes to write.
/// \param [in] offset Starting position in the file, i.e., number of the first byte to read relative to the first byte of
/// the file.
/// \param [in] fileInfo Can be ignored in Part 1 .
/// \return Number of bytes written on success, -ERRNO on failure.
int
MyOnDiskFS::fuseWrite(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    //LOGM();
    //LOGF("parameters: path = %s, buf = %X, size = %ld, offset = %ld, fileinfo->fh = %ld", path, buf, size, offset, fileInfo->fh);
    readDmap();
    readFat();
    readRoot();

    if (size < 0 || offset < 0) {
        RETURN(-EINVAL);
    }

    if (0 > iIsPathValid(path, fileInfo->fh)) {
        RETURN(iIsPathValid(path, fileInfo->fh));
    }

    //file opened
    if (!myFsOpenFiles[fileInfo->fh]) {
        LOG("File not open");
        RETURN(-EPERM);
    }

    MyFsDiskInfo *info = &myRoot[fileInfo->fh];
    size_t totalNeededBlocks = ceil((double) (size + offset) / BLOCK_SIZE);
    size_t haveBlocks = ceil(((double) info->size) / BLOCK_SIZE);
    //LOGF("(size+offset)/BLOCK_SIZE = %lf", ((double)(size+offset))/BLOCK_SIZE);
    //LOGF("info->size/BLOCK_SIZE = %lf", ((double)info->size)/BLOCK_SIZE);
    //LOGF("totalNeededBlocks = %ld, haveBlocks = %ld", totalNeededBlocks, haveBlocks);
    int64_t tmpBlock = 0;
    int64_t iterBlock = 0;

    if (haveBlocks < totalNeededBlocks) {
        //LOG("we need more blocks");
        int ret = allocateBlocks(totalNeededBlocks - haveBlocks, fileInfo->fh);
        readDmap();
        readFat();
        readRoot();
        //LOGF("Alloc ret = %ld", ret);
        if (ret < 0) {
            RETURN(ret);
        }
    } else {
        //LOG("we have enough space");
    }

    //LOG("Find start-position to write");
    int64_t startBlock = offset / BLOCK_SIZE;
    u_int32_t offsetBlock = 0;
    u_int32_t offsetByte = offset % BLOCK_SIZE;
    //LOGF("startBlock = %ld, offsetByte = %ld", startBlock, offsetByte);
    int32_t numBlocks2Write = std::ceil(((double) (size + offsetByte) / BLOCK_SIZE));
    const char* bufIter = buf;

    iterBlock = info->data;

    // Set offsetBlock if startBlock == 0
    if (startBlock == 0) {
        offsetBlock = iterBlock;
    }

    for (size_t i = 0; i < startBlock; i++) {
        //Travel through blocks of the file to reach the offset
        tmpBlock = myFAT[iterBlock];
        if (tmpBlock == -1) {
            //LOG("myFAT ended prematurely. THIS SHOULD NOT OCCUR!");
            RETURN(-2000);
        }
        iterBlock = tmpBlock;
        offsetBlock = iterBlock;
    }

    char buffer[BLOCK_SIZE];

    for (size_t i = 0; i < numBlocks2Write; i++) {
        void *retPtr = nullptr;
        if (offsetByte > 0) {
            //write first block (writing less than 512 bytes)
            memset(buffer, 0, BLOCK_SIZE);
            int ret = this->blockDevice->read(this->posDATA + offsetBlock, buffer);
            if (ret < 0) {
                //LOG("Couldn't read from Container");
                RETURN (-4000);
            }

            retPtr = memcpy(buffer + offsetByte, bufIter,
                            std::min(size, (size_t) BLOCK_SIZE - offsetByte));
            bufIter += std::min(size, (size_t) BLOCK_SIZE - offsetByte);
            offsetByte = 0;
        }

        else if (bufIter + BLOCK_SIZE > buf + size) {
            //write last block (writing less than 512 bytes)
            memset(buffer, 0, BLOCK_SIZE);
            if (info->size > size + offset) {
                int ret = this->blockDevice->read(this->posDATA + offsetBlock, buffer);
                if (ret < 0) {
                    //LOG("Couldn't read from Container");
                    RETURN (-4000);
                }
            }

            retPtr = memcpy(buffer, bufIter,
                            std::max(0UL, (u_long) (buf + size - bufIter)));
        }

        else if (bufIter + BLOCK_SIZE <= buf + size) {
            //write full blocks inbetween first and last
            retPtr = memcpy(buffer, buf + i * BLOCK_SIZE, BLOCK_SIZE);
            bufIter += BLOCK_SIZE;
        }

        if (retPtr == nullptr) {
            //LOG("memcpy failed");
            RETURN (-5000);
        }

        this->blockDevice->write(this->posDATA + offsetBlock, buffer);

        offsetBlock = myFAT[offsetBlock];

        char debugBuf[BLOCK_SIZE + 1];
        memcpy(debugBuf, buffer, BLOCK_SIZE);
        debugBuf[BLOCK_SIZE] = '\0';
        LOGF("BlockBuf = %s", debugBuf);
    }

    info->size = std::max(size + offset, info->size);

    info->atime = info->ctime = info->mtime = time(NULL);

    writeDmap();
    writeFat();
    writeRoot();

    RETURN(size);
}

/// @brief Close a file.
///
/// \param [in] path Name of the file, starting with "/".
/// \param [in] File handel for the file set by fuseOpen.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRelease(const char *path, struct fuse_file_info *fileInfo) {
    //LOGM();
    readRoot();

    int valid = iIsPathValid(path, fileInfo->fh);
    if (valid < 0) {
        RETURN(valid);
    }

    // Check if the file is open
    if (!myFsOpenFiles[valid]) {
        RETURN(-EBADF);
    }

    myFsOpenFiles[valid] = false;
    iCounterOpen--;
    fileInfo->fh = -EBADF;

    writeRoot();
    RETURN(0);
}

/// @brief Truncate a file.
///
/// Set the size of a file to the new size. If the new size is smaller than the old size, spare bytes are removed. If
/// the new size is larger than the old size, the new bytes may be random.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newSize New size of the file.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseTruncate(const char *path, off_t newSize) {
    //LOGM();
    readRoot();

    if (newSize < 0) {
        RETURN(-EINVAL);
    }

    //get file-index and call other fuseTruncate with it
    fuse_file_info *info = (fuse_file_info *) malloc(sizeof(fuse_file_info));
    info->fh = -1;

    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (strcmp(path, myRoot[i].cPath) == 0) {
            //found file
            info->fh = i;
        }
    }
    if (info->fh < 0) {
        //file doesn't exist
        RETURN(-EEXIST);
    }
    int ret = fuseTruncate(path, newSize, info);

    free(info);

    RETURN(ret);
}

/// @brief Truncate a file.
///
/// Set the size of a file to the new size. If the new size is smaller than the old size, spare bytes are removed. If
/// the new size is larger than the old size, the new bytes may be random. This function is called for files that are
/// open.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newSize New size of the file.
/// \param [in] fileInfo Can be ignored in Part 1.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseTruncate(const char *path, off_t newSize, struct fuse_file_info *fileInfo) {
    //LOGM();
    readSuperBlock();
    readDmap();
    readFat();
    readRoot();

    if (newSize < 0) {
        RETURN(-EINVAL);
    }

    if (0 > iIsPathValid(path, fileInfo->fh)) {
        RETURN(iIsPathValid(path, fileInfo->fh));
    }

    MyFsDiskInfo *info = &myRoot[fileInfo->fh];

    size_t newBlocks = ceil(newSize / BLOCK_SIZE);
    size_t oldBlocks = ceil(info->size / BLOCK_SIZE);

    if (oldBlocks <= 0 && newBlocks >= 1) {
        //LOG("file was empty");
        int32_t num = findFreeBlock();
        if (num < 0) {
            //LOG("findFreeBlock() failed");
            RETURN(num);
        }
        info->data = num;

        newBlocks--;
        oldBlocks++;
    }

    if (newBlocks > oldBlocks) {
        //LOG("file is getting bigger, we need more blocks");
        allocateBlocks(newBlocks - oldBlocks, fileInfo->fh);

    } else if (newBlocks < oldBlocks) {
        //LOG("file is getting smaller, we can free blocks");
        int32_t num = info->data;
        //iterate through myFat to reach last block of newSize
        //careful 0th iteration is already done with num = info->data
        for (int i = 1; i < newBlocks; i++) {
            num = myFAT[num];
        }
        int ret = freeBlocks(num);
        if (ret < 0) {
            //LOG("failed inside freeBlocks");
            RETURN (ret);
        }
        info->mtime = time(NULL);
        //LOGF("synced block = %ld", num);
    } else {
        //LOG("don't need new Blocks -> do nothing");
    }

    //LOGF("info->size NEW = %ld | info->size OLD = %ld", newSize, info->size);
    info->size = newSize;

    if (newSize <= 0) {
        info->size = newSize;
        size_t tmpBlockNum = info->data;
        info->data = POS_NULLPTR;
        freeBlocks(tmpBlockNum);
        info->mtime = time(NULL);
    }

    info->atime = info->ctime = time(NULL);

    writeSuperBlock();
    writeDmap();
    writeFat();
    writeRoot();
    //LOGF("info->size = %ld", info->size);

    RETURN(0);
}

/// @brief Read a directory.
///
/// Read the content of the (only) directory.
/// You do not have to check file permissions, but can assume that it is always ok to access the directory.
/// \param [in] path Path of the directory. Should be "/" in our case.
/// \param [out] buf A buffer for storing the directory entries.
/// \param [in] filler A function for putting entries into the buffer.
/// \param [in] offset Can be ignored.
/// \param [in] fileInfo Can be ignored.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseReaddir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fileInfo) {
    //LOGM();
    readRoot();

    filler(buf, ".", NULL, 0); // Current Directory
    filler(buf, "..", NULL, 0); // Parent Directory

    // If the user is trying to show the files/directories of the root directory show the following
    if (strcmp(path, "/") == 0) {

        // Iterate through all the files
        for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
            if (!myFsEmpty[i]) {
                // Add file to the readdir output
                filler(buf, myRoot[i].cPath + 1, NULL, 0);

                // Change access and changed time
                myRoot[i].atime = myRoot[i].ctime = time(NULL);
            }
        }
    }

    writeRoot();
    RETURN(0);
}

/// Initialize a file system.
///
/// This function is called when the file system is mounted. You may add some initializing code here.
/// \param [in] conn Can be ignored.
/// \return 0.
void *MyOnDiskFS::fuseInit(struct fuse_conn_info *conn) {
    // Open logfile
    this->logFile = fopen(((MyFsInfo *) fuse_get_context()->private_data)->logFile, "w+");
    if (this->logFile == NULL) {
        fprintf(stderr, "ERROR: Cannot open logfile %s\n", ((MyFsInfo *) fuse_get_context()->private_data)->logFile);
    } else {
        // turn of logfile buffering
        setvbuf(this->logFile, NULL, _IOLBF, 0);

        LOG("Starting logging...\n");

        LOG("Using on-disk mode");

        this->containerFilePath = ((MyFsInfo *) fuse_get_context()->private_data)->contFile;

        LOGF("Container file name: %s", containerFilePath);

        int ret = this->blockDevice->open(this->containerFilePath);

        if (ret >= 0) {
            LOG("Container file does exist, reading");

            readSuperBlock();
            readDmap();
            readFat();
            readRoot();

            initializeHelpers();

        } else if (ret == -ENOENT) {
            LOG("Container file does not exist, creating a new one");

            ret = this->blockDevice->create(this->containerFilePath);

            if (ret >= 0) {
                // Sync to container
                writeSuperBlock();
                writeDmap();
                writeFat();
                writeRoot();
            }
        }

        if (ret < 0) {
            LOGF("ERROR: Access to container file failed with error %d", ret);
        }
    }

    RETURN(0);
}

/// @brief Clean up a file system.
///
/// This function is called when the file system is unmounted. You may add some cleanup code here.
void MyOnDiskFS::fuseDestroy() {
    //LOGM();
    this->blockDevice->close();
}

/// unlinks all blocks of the file starting with Block "num"
/// \param num first Block to be unlinked
/// \return 0 on success, -ERRORNUMBER on failure
int MyOnDiskFS::freeBlocks(int32_t num) {
    readSuperBlock();
    readDmap();
    readFat();

    if (myFAT[num] == -1 && myDmap[num] == 1) {
        RETURN(2);
    }

    if (num > this->blocks4DATA && num < 0) {
        //LOGF("num = %ld not valid", num);
        RETURN(-1111);
    }

    u_int64_t tmpBlock = 0;
    u_int64_t iterBlock = 0;

    iterBlock = num;

    while (tmpBlock != -1) {
        myDmap[iterBlock] = 1;
        mySuperBlock.numFreeBlocks++;

        tmpBlock = myFAT[iterBlock];
        myFAT[iterBlock] = -1;

        iterBlock = tmpBlock;
    }

    writeSuperBlock();
    writeDmap();
    writeFat();

    RETURN(0);
}

size_t MyOnDiskFS::findFreeBlock() {
    //LOGM();
    readSuperBlock();
    readDmap();
    if (containerFull(1)) {
        RETURN(ERROR_BLOCKNUMBER);
    }

    for (size_t i = 0; i < this->blocks4DATA; i++) {
        if (myDmap[i]) {
            memset(&myDmap[i], 0, sizeof(bool));
            mySuperBlock.numFreeBlocks--;
            //LOGF("  numFreeBlocks = %ld ", mySuperBlock.numFreeBlocks);
            writeSuperBlock();
            writeDmap();
            RETURN (i);
        }
    }

    RETURN(ERROR_BLOCKNUMBER);
}

void MyOnDiskFS::initializeHelpers() {
    //LOGM();
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        myFsOpenFiles[i] = false;
        if (myRoot[i].cPath[0] != '/') {
            myFsEmpty[i] = true;
        } else {
            myFsEmpty[i] = false;
            iCounterFiles++;
        }
    }
    iCounterOpen = 0;

    //LOG("initialized myFsEmpty, myFsOpenFiles, iCounterOpen, iCounterFiles");
}

int MyOnDiskFS::containerFull(size_t neededBlocks) {
    //LOGM();
    readSuperBlock();
    //LOGF("numFreeBlocks %ld ; %ld", mySuperBlock.numFreeBlocks, neededBlocks);
    if (mySuperBlock.numFreeBlocks >= neededBlocks) {
        RETURN(0);
    }

    RETURN(1);
}

int MyOnDiskFS::iIsPathValid(const char *path, uint64_t fh) {
    //LOGM();
    readRoot();
    if (fh < 0 || fh >= NUM_DIR_ENTRIES) {
        RETURN (-155);
    }
    if (myFsEmpty[fh]) {
        RETURN (-156);
    }
    if (strcmp(path, myRoot[fh].cPath) == 0) {
        RETURN (fh);
    }
    RETURN (-157);
}

int MyOnDiskFS::iFindEmptySpot() {
    //LOGM();
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (myFsEmpty[i]) {
            //LOGF("index %ld is free", i);
            RETURN(i);
        }
    }
    //LOG("NOT EMPTY BY FUNC");
    RETURN(-ENOSPC);
}

int MyOnDiskFS::allocateBlocks(int32_t numBlocks2Allocate, uint64_t fileHandle) {
    readFat();
    readRoot();

    u_int64_t tmpBlock = 0;
    u_int64_t iterBlock = 0;
    //LOGF("numBlocks2Allocate = %ld", numBlocks2Allocate);

    //enough space in container?
    if (containerFull(numBlocks2Allocate)) {
        RETURN(-ENOSPC);
    }

    //empty file?
    if (myRoot[fileHandle].data == POS_NULLPTR) {
        int32_t startFAT = findFreeBlock();
        if (startFAT >= ERROR_BLOCKNUMBER) {
            RETURN(-ENOSPC);
        }
        myRoot[fileHandle].data = startFAT;
        numBlocks2Allocate--;
    }

    iterBlock = myRoot[fileHandle].data;
    int32_t endFAT = 0;

    while (tmpBlock != -1) {
        tmpBlock = myFAT[iterBlock];
        if (tmpBlock == -1) {
            endFAT = iterBlock;
            break;
        }
        iterBlock = tmpBlock;
    }

    int32_t addFAT = endFAT;
    //LOG("getting needed blocks");
    for (int i = 0; i < numBlocks2Allocate; i++) {
        tmpBlock = findFreeBlock();
        //LOGF("Reserved tmpBlock = %ld block", tmpBlock);
        if (tmpBlock >= ERROR_BLOCKNUMBER) {
            //LOG("can't find free block. THIS SHOULD NOT OCCUR!");
            RETURN(-ENOSPC);
        }

        //LOGF("i = %ld, iterBlock = %ld, tmpBlock = %ld", i, addFAT, tmpBlock);
        myFAT[addFAT] = (int32_t) tmpBlock;
        //LOGF("iterBlock = %ld, myFAT[iterBlock] = %ld", addFAT, myFAT[addFAT]);
        // Set next Block
        addFAT = myFAT[addFAT];
        //LOGF("iterBlock = %ld, myFAT[iterBlock] = %ld (Should be 0xFF (4.294.967.295) since it should be a free area)", addFAT, myFAT[addFAT]);
    }

    writeFat();
    writeRoot();

    RETURN(1);
}

int MyOnDiskFS::readSuperBlock() {
    // Allocate a buffer for the SuperBlock
    char *buffer = (char *) malloc(BLOCK_SIZE);
    memset(buffer, 0, BLOCK_SIZE);

    int ret = this->blockDevice->read(this->posSPBlock, buffer);
    if (ret >= 0) {
        //LOG("read superblock from container");
        void *retPtr = memcpy(&mySuperBlock, buffer, sizeof(SuperBlock));
        if (retPtr == nullptr) {
            // Free the buffer
            free(buffer);
            RETURN(-300);
        }
    }
    // Free the buffer
    free(buffer);

    return 0;
}

int MyOnDiskFS::writeSuperBlock() {
    char *buffer = (char *) malloc(BLOCK_SIZE);
    memset(buffer, 0, BLOCK_SIZE);

    memcpy(buffer, &mySuperBlock, sizeof(SuperBlock));

    int ret = this->blockDevice->write(this->posSPBlock, buffer);
    if (ret < 0) {
        // Free the buffer
        free(buffer);
        RETURN(ret);
    }

    // Free the buffer
    free(buffer);

    return 0;
}

int MyOnDiskFS::readDmap() {
    // Read the blocks of the DMAP to the file system

    // Allocate a buffer for the DMAP
    char *buffer = (char *) malloc(BLOCK_SIZE);
    memset(buffer, 0, BLOCK_SIZE);

    for (int i = 0; i < this->blocks4DMAP; i++) {
        int ret = this->blockDevice->read(this->posDMAP + i, buffer);
        if (ret < 0) {
            // Free the buffer
            free(buffer);
            RETURN(ret);
        }

        void *retPtr = memcpy(myDmap + i * BLOCK_SIZE, buffer, BLOCK_SIZE);
        if (retPtr == nullptr) {
            // Free the buffer
            free(buffer);
            RETURN(-300);
        }
    }

    // Free the buffer
    free(buffer);

    return 0;
}

int MyOnDiskFS::writeDmap() {
    char *buffer = (char *) malloc(BLOCK_SIZE);
    memset(buffer, 0, BLOCK_SIZE);

    for (int i = 0; i < this->blocks4DMAP; i++) {
        memcpy(buffer, myDmap + i * BLOCK_SIZE, BLOCK_SIZE);
        int ret = this->blockDevice->write(this->posDMAP + i, buffer);
        if (ret < 0) {
            // Free the buffer
            free(buffer);
            RETURN(ret);
        }
    }

    // Free the buffer
    free(buffer);

    return 0;
}

int MyOnDiskFS::readFat() {
    char *buffer = (char *) malloc(BLOCK_SIZE);
    memset(buffer, 0, BLOCK_SIZE);

    for (int i = 0; i < this->blocks4FAT; i++) {
        int ret = this->blockDevice->read(this->posFAT + i, buffer);
        if (ret < 0) {
            // Free the buffer
            free(buffer);
            RETURN(ret);
        }
        void *retPtr = memcpy(((char *) myFAT) + i * BLOCK_SIZE, buffer, BLOCK_SIZE);
        if (retPtr == nullptr) {
            // Free the buffer
            free(buffer);
            RETURN(-300);
        }
    }

    return 0;
}

int MyOnDiskFS::writeFat() {
    char *buffer = (char *) malloc(BLOCK_SIZE);
    memset(buffer, 0, BLOCK_SIZE);

    for (int i = 0; i < this->blocks4FAT; i++) {
        memcpy(buffer, ((char *) myFAT) + i * BLOCK_SIZE, BLOCK_SIZE);
        int ret = this->blockDevice->write(this->posFAT + i, buffer);
        if (ret < 0) {
            // Free the buffer
            free(buffer);
            RETURN(ret);
        }
    }
    // Free the buffer
    free(buffer);

    return 0;
}

int MyOnDiskFS::readRoot() {
    char *buffer = (char *) malloc(BLOCK_SIZE);
    memset(buffer, 0, BLOCK_SIZE);

    for (int i = 0; i < this->blocks4ROOT; i++) {
        int ret = this->blockDevice->read(this->posROOT + i, buffer);
        if (ret < 0) {
            //LOGF("ERROR: blockDevice couldn't read Root %d", ret);
            // Free the buffer
            free(buffer);
            RETURN(ret);
        }
        void *retPtr = memcpy(&myRoot[i], buffer, sizeof(MyFsDiskInfo));
        if (retPtr == nullptr) {
            //LOG("memcpy of Root failed");
            // Free the buffer
            free(buffer);
            RETURN(-300);
        }
    }
    // Free the buffer
    free(buffer);

    return 0;
}

int MyOnDiskFS::writeRoot() {
    char *buffer = (char *) malloc(BLOCK_SIZE);

    for (int i = 0; i < this->blocks4ROOT; i++) {
        memset(buffer, 0, BLOCK_SIZE);
        memcpy(buffer, &myRoot[i], sizeof(MyFsDiskInfo));
        int ret = this->blockDevice->write(this->posROOT + i, buffer);
        if (ret < 0) {
            // Free the buffer
            free(buffer);
            RETURN(ret);
        }
    }

    // Free the buffer
    free(buffer);

    return 0;
}

// DO NOT EDIT ANYTHING BELOW THIS LINE!!!

/// @brief Set the static instance of the file system.
///
/// Do not edit this method!
void MyOnDiskFS::SetInstance() {
    MyFS::_instance = new MyOnDiskFS();
}