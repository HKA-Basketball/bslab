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

        //LOGF("comparing path = %s, info.cPath = %s, strcmp(path, info.cPath) = %ld", path, info.cPath, strcmp(path, info.cPath));
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
    syncRoot();

    //LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", index, myRoot[index].cPath, myRoot[index].size, myRoot[index].atime);
    //LOGF("iCounterFiles: %d", iCounterFiles);

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

    u_int32_t index = -1;

    //find file index
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (strcmp(path, myRoot[i].cPath) == 0) {
            //found file
            index = i;
        }
    }
    if (index < 0) {
        //file doesn't exist
        RETURN(-EEXIST);
    }

    if (myFsOpenFiles[index]) {
        RETURN(-EBUSY);
    }

    //unlink blocks
    unlinkBlocks(myRoot[index].data);
    //reset myRoot
    memset(&myRoot[index], 0, sizeof(MyFsDiskInfo));
    myRoot[index].data = POS_NULLPTR;
    myRoot[index].cPath[0] = '\0';
    syncRoot();
    //adjust helpers
    myFsEmpty[index] = true;
    iCounterFiles--;

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
    //LOGF("Old filepath: %s, New filepath: %s", path, newpath);

    //check length of new filename
    if (strlen(newpath) - 1 > NAME_LENGTH) {
        RETURN(-EINVAL);
    }

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

    //LOGF("Index: %d", index);

    //overwrite fileinfo values
    strcpy(myRoot[index].cPath, newpath);
    myRoot[index].atime = myRoot[index].ctime = myRoot[index].mtime = time(NULL);

    syncRoot();

    //LOGF("Index Changed: %d", index);

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


    //LOGF( "\tAttributes of %s requested\n", path );

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

    int ret = 0;

    if (strcmp(path, "/") == 0) {
        //LOG("path is rootdirectory \'/\'");
        statbuf->st_mode = S_IFDIR | 0755;
        statbuf->st_nlink = 2; // Why "two" hardlinks instead of "one"? The answer is here: http://unix.stackexchange.com/a/101536
    } else if (strlen(path) > 0) {
        //LOG("path-length > 0");
        for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
            if (myFsEmpty[i]) {
                continue;
            }
            //LOGF("Comparing string1: %s, and string2: %s", path, myRoot[i].cPath);
            if (strcmp(path, myRoot[i].cPath) == 0) {
                statbuf->st_mode = myRoot[i].mode;
                statbuf->st_nlink = 1;
                statbuf->st_size = myRoot[i].size;
                statbuf->st_mtime = myRoot[i].mtime; // The last "m"odification of the file/directory is right now
                //LOG("fuseGetAttr()");
                //LOG("filled statbuf with data");
                //LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", i, myRoot[i].cPath, myRoot[i].size, myRoot[i].atime);
                RETURN(0);
            }
        }
        //LOG("havent found file in myFsFiles-array");

        ret = -ENOENT;
    } else {
        //LOG("path-length <= 0");
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

    size_t index = -1;

    // Get index of file by  path
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

    // file found?
    if (index < 0) {
        RETURN(-ENOENT);
    }

    //overwrite fileinfo values
    myRoot[index].mode = mode;
    myRoot[index].atime = myRoot[index].ctime = myRoot[index].mtime = time(NULL);

    syncRoot();
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

    // file found?
    if (index < 0) {
        RETURN(-ENOENT);
    }

    //overwrite fileinfo values
    myRoot[index].uid = uid;
    myRoot[index].gid = gid;
    myRoot[index].atime = myRoot[index].ctime = myRoot[index].mtime = time(NULL);
    syncRoot();
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

    if (iCounterOpen >= NUM_OPEN_FILES) {
        RETURN(-EMFILE);
    }

    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (myFsEmpty[i]) {
            continue;
        }

        if (strcmp(path, myRoot[i].cPath) == 0) {
            if (myFsOpenFiles[i]) {
                RETURN(-EPERM); // Already Open
            } else {
                // Set Handle etc
                myFsOpenFiles[i] = true;
                fileInfo->fh = i; // can be used in fuseRead and fuseRelease
                iCounterOpen++;
                myRoot[i].atime = time(NULL);
                //LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", i, myRoot[i].cPath, myRoot[i].size, myRoot[i].atime);
                //LOGF("index: %d, iCounterOpen: %d", i, iCounterOpen);
                break;
            }
        }
    }

    syncRoot();
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

    //open container
    int ret = this->blockDevice->open(containerFilePath);
    //LOG("opened container");
    if (ret < 0) {
        RETURN(-3000);
    }

    MyFsDiskInfo *info = &myRoot[fileInfo->fh];

    //LOGF("Trying to read from path = %s, size = %ld, offset = %ld",path, size, offset);

    int32_t iterBlock = info->data; // Block number where we are in container right now
    off_t iterByte = offset; // offset inside Block from container to read
    off_t bufByte = 0; //offset inside the buf Buffer
    char blockBuf[BLOCK_SIZE];  //buffer to read block into
    size_t leftToRead = size; //number of Bytes we still need to read
    if (size + offset > info->size) {
        //LOGF("Trying to read to the %ld th byte which is more than info->size = %ld, reading all of file instead", size+offset, info->size);
        leftToRead = info->size;
    }

    //LOGF("iterBlock = %ld, iterByte = %ld, bufByte = %ld, leftToRead = %ld", iterBlock, iterByte, bufByte, leftToRead);
    //LOGF("lefToRead = %ld", leftToRead);

    //failsafe condition "i < (info->size/BLOCK_SIZE + 1)" to ensure we only read as many blocks as the file has
    for (int i = 0; leftToRead > 0 && iterBlock >= 0 && i < (info->size / BLOCK_SIZE + 1); i++) {
        if (iterByte >= BLOCK_SIZE) {
            //LOGF("iterByte = %ld >= 512, need to go at least 1 block further", iterByte);
            iterBlock = myFAT[iterBlock];
            iterByte -= BLOCK_SIZE;
        } else {
            //determine how many bytes shall be memcpied
            //Either it is leftToRead(initially = size), meaning we don't read the block to it's end OR we read it fully
            size_t copiedBytes = std::min(leftToRead, (size_t) (BLOCK_SIZE - iterByte));
            //LOGF("Will read from iterBlock = %ld, with (block-)offset iterByte = %ld ", iterBlock, iterByte);
            //LOGF("and write copiedBytes = %ld many bytes to buf+(bufByte = %ld)", copiedBytes, bufByte);
            int ret = this->blockDevice->read(POS_DATA + iterBlock, blockBuf);
            if (ret < 0) {
                //LOG("Couldn't read from Container");
                RETURN (-4000);
            }
            void *retPtr = memcpy(buf + bufByte, blockBuf + iterByte, copiedBytes);
            if (retPtr == nullptr) {
                //LOG("memcpy failed");
                RETURN (-5000);
            }
            bufByte += copiedBytes; //iterate on buf
            leftToRead -= copiedBytes; //iterate on size
            iterByte = 0; //read one block at least, so blockoffset is 0 for the coming segments
            iterBlock = myFAT[iterBlock]; //iterate block
            //LOGF("lefToRead = %ld", leftToRead);

            /*
            char debugBuf[BLOCK_SIZE + 1];
            memcpy(debugBuf, blockBuf, BLOCK_SIZE);
            debugBuf[BLOCK_SIZE] = '\0';
            LOGF("BlockBuf = %s", debugBuf);
             */
        }
    }

    /*
    char* debugBuf2 = (char*)malloc(size + 1);
    if (debugBuf2 != nullptr) {
        memcpy(debugBuf2, buf, size);
        debugBuf2[size] = '\0';
        LOGF("BlockBuf2 = %s", debugBuf2);
    }
    free(debugBuf2);
     */

    this->blockDevice->close();
    RETURN(bufByte);
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

    //open container
    int ret = this->blockDevice->open(containerFilePath);
    //LOG("opened container");
    if (ret < 0) {
        RETURN(-3000);
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

        //enough space in container?
        if (containerFull(size, offset)) {
            //LOG("container is full");
            RETURN(-ENOSPC);
        }

        //empty file?
        if (info->data == POS_NULLPTR) {
            //LOG("file was empty before");
            tmpBlock = findFreeBlock();
            if (tmpBlock >= ERROR_BLOCKNUMBER) {
                //LOG("can't find free block. THIS SHOULD NOT OCCUR!");
                RETURN(-ENOSPC);
            }
            info->data = tmpBlock;
            //LOGF("info->data = %ld", info->data);
            haveBlocks = 1;
            // Sync DMAP & FAT
            syncDmapFat(tmpBlock);
        }

        iterBlock = info->data;
        //LOG("getting needed blocks");
        for (int i = 1; i < totalNeededBlocks; i++) {
            if (i < haveBlocks) {
                //Travel through blocks of the file to reach the file's end
                tmpBlock = myFAT[iterBlock];
                //LOGF("iterBlock = %ld, myFAT[iterBlock] = %ld (inside existing blocks)", iterBlock, tmpBlock);
                if (tmpBlock == -1) {
                    //LOG("myFAT ended prematurely. THIS SHOULD NOT OCCUR!");
                    RETURN(-2000);
                }
                iterBlock = tmpBlock;
            } else {
                //reserve next block and iterate
                tmpBlock = findFreeBlock();
                //LOGF("Reserved tmpBlock = %ld block", tmpBlock);
                if (tmpBlock >= ERROR_BLOCKNUMBER) {
                    //LOG("can't find free block. THIS SHOULD NOT OCCUR!");
                    RETURN(-ENOSPC);
                }

                //LOGF("i = %ld, iterBlock = %ld, tmpBlock = %ld", i, iterBlock, tmpBlock);
                myFAT[iterBlock] = (int32_t) tmpBlock;
                //LOGF("iterBlock = %ld, myFAT[iterBlock] = %ld", iterBlock, myFAT[iterBlock]);
                // Sync DMAP & FAT
                syncDmapFat(iterBlock);
                // Set next Block
                iterBlock = myFAT[iterBlock];
                //LOGF("iterBlock = %ld, myFAT[iterBlock] = %ld (Should be 0xFF (4.294.967.295) since it should be a free area)", iterBlock, myFAT[iterBlock]);
            }
        }
        //still need to sync last block of file, because of dmap
        //if the file only has 1 block, it is needlessly synced twice
        syncDmapFat(iterBlock);
    } else {
        //LOG("we have enough space");
    }

    //LOG("Find start-position to write");
    int64_t startBlock = offset / BLOCK_SIZE;
    u_int32_t offsetBlock = 0;
    u_int32_t offsetByte = offset % BLOCK_SIZE;
    //LOGF("startBlock = %ld, offsetByte = %ld", startBlock, offsetByte);

    iterBlock = info->data;

    // Set offsetBlock if startBlock == 0
    if (startBlock == 0) {
        offsetBlock = iterBlock;
    }

    //LOG("Travel through blocks of the file to reach the offset");
    for (int i = 0; i < startBlock; i++) {
        //Travel through blocks of the file to reach the offset
        tmpBlock = myFAT[iterBlock];
        //LOGF("it %ld, iterBlock = %ld, myFAT[iterBlock] = %ld", i, iterBlock, tmpBlock);
        if (tmpBlock == -1) {
            //LOG("myFAT ended prematurely. THIS SHOULD NOT OCCUR!");
            RETURN(-2000);
        }
        iterBlock = tmpBlock;
        offsetBlock = iterBlock;
        //LOGF("iterBlock = %ld", iterBlock);
    }
    //LOG("start to write");
    //LOGF("iterBlock = %ld", iterBlock);
    char *iterBuf = (char *) buf;
    char *bufEND = (char *) (buf + size); //first invalid adress after buf
    char blockBuf[BLOCK_SIZE];
    void *tmpPtr = nullptr;
    //LOGF("iterBuf = %X", iterBuf);

    //offset is inside iterBlock
    if (offsetByte > 0) {
        //LOG("need to read prefix, overwrite it and save it");
        this->blockDevice->read(POS_DATA + offsetBlock, blockBuf);
        tmpPtr = memcpy(blockBuf + offsetByte, iterBuf, std::min(size, (size_t) BLOCK_SIZE - offsetByte));
        iterBuf += std::min(size, (size_t) BLOCK_SIZE - offsetByte);
        //LOGF("iterBuf = %X", iterBuf);
        if (tmpPtr == nullptr) {
            //LOG("memcpy of of first bytes of buf");
            RETURN(-4000);
        }
        this->blockDevice->write(POS_DATA + offsetBlock, blockBuf);
        iterBlock = myFAT[iterBlock];
    }

    //write full blocks with no prefix and no suffix (writing from 0th byte in block to the 511th)
    //LOG("Writing full blocks");
    for (int i = 0; i < NUM_DATA_BLOCK_COUNT; i++) {
        if (iterBuf > bufEND - BLOCK_SIZE || iterBlock < 0) {
            //LOG("wrote all full blocks");
            break;
        }
        this->blockDevice->write(POS_DATA + iterBlock, iterBuf);
        iterBuf += BLOCK_SIZE;
        //LOGF("iterBuf = %X", iterBuf);
        //LOGF("iterBlock = %ld, myFAT[iterBlock] = %ld", iterBlock, myFAT[iterBlock]);
        iterBlock = myFAT[iterBlock];
    }

    //LOG("write last block");
    if (iterBlock >= 0 && bufEND - iterBuf > 0) {
        //LOG("actually writing last block (didn't reach end of file yet)");
        memset(blockBuf, 0, BLOCK_SIZE);
        if (info->size > size + offset) {
            //we need to read block first since there is old data in this block
            this->blockDevice->read(POS_DATA + iterBlock, blockBuf);
        }
        tmpPtr = memcpy(blockBuf, iterBuf, std::max((long) 0, bufEND - iterBuf));
        if (tmpPtr == nullptr) {
            //LOG("memcpy of iterBuf till end of buf failed");
            RETURN(-4000);
        }
        //LOGF("iterBlock = %ld, myFAT[iterBlock] = %ld", iterBlock, myFAT[iterBlock]);
        this->blockDevice->write(POS_DATA + iterBlock, blockBuf);
    }

    //LOG("sync superblock");
    memset(blockBuf, 0, BLOCK_SIZE);
    memcpy(blockBuf, &mySuperBlock, sizeof(SuperBlock));
    ret = this->blockDevice->write(POS_SPBLOCK, blockBuf);
    if (ret < 0) {
        //LOGF("ERROR: blockDevice couldn't write superblock %d", ret);
        RETURN(-4000);
    }
    //LOG("wrote superblock");


    info->size = std::max(size + offset, info->size);
    syncRoot();

    this->blockDevice->close();

    //dumpStructures();

    RETURN(size);
}

/// @brief Close a file.
///
/// \param [in] path Name of the file, starting with "/".
/// \param [in] File handel for the file set by fuseOpen.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRelease(const char *path, struct fuse_file_info *fileInfo) {
    //LOGM();

    int valid = iIsPathValid(path, fileInfo->fh);
    if (valid < 0) {
        RETURN(valid);
    }

    if (!myFsOpenFiles[valid]) {
        RETURN(-EBADF);
    }

    //LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", fileInfo->fh, myRoot[fileInfo->fh].cPath, myRoot[fileInfo->fh].size, myRoot[fileInfo->fh].atime);

    myFsOpenFiles[valid] = false;
    iCounterOpen--;
    fileInfo->fh = -EBADF;

    syncRoot();
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

    if (newSize < 0) {
        RETURN(-EINVAL);
    }

    if (0 > iIsPathValid(path, fileInfo->fh)) {
        RETURN(iIsPathValid(path, fileInfo->fh));
    }
    //open container
    int ret = this->blockDevice->open(containerFilePath);
    //LOG("opened container");
    if (ret < 0) {
        RETURN(-3000);
    }

    MyFsDiskInfo *info = &myRoot[fileInfo->fh];

    size_t newBlocks = ceil(newSize / BLOCK_SIZE);
    size_t oldBlocks = ceil(info->size / BLOCK_SIZE);

    if (oldBlocks <= 0 && newBlocks >= 1) {
        //LOG("file was empty");
        int32_t num = iFindEmptySpot();
        if (num < 0) {
            //LOG("iFindEmptySpot() failed");
            RETURN(num);
        }
        info->data = num;
        syncRoot();
        newBlocks--;
        oldBlocks++;
    }

    if (newBlocks > oldBlocks) {
        //LOG("file is getting bigger, we need more blocks");
        int32_t num = info->data;
        int32_t tmpNum = -1;
        //iterate through myFat to reach last block of oldSize
        //careful 0th iteration is already done with num = info->data
        for (int i = 1; i < newBlocks; i++) {
            if (myFAT[num] < 0) {
                //need to reserve next block
                tmpNum = iFindEmptySpot();
                if (tmpNum < 0) {
                    //LOG("iFindEmptySpot() failed");
                    RETURN(tmpNum);
                }
                myDmap[tmpNum] = 0;
                myFAT[num] = tmpNum;
                syncDmapFat(num);
                //LOGF("synced block = %ld", num);
                num = myFAT[num];
            } else {
                //we can still iterate at least one more block
                num = myFAT[num];
            }
        }
        //still need to sync dmap-entry for the last block in file
        syncDmapFat(tmpNum);
        //LOGF("synced block = %ld", tmpNum);

    } else if (newBlocks < oldBlocks) {
        //file is getting smaller, we can free blocks
        int32_t num = info->data;
        //iterate through myFat to reach last block of newSize
        //careful 0th iteration is already done with num = info->data
        for (int i = 1; i < newBlocks; i++) {
            num = myFAT[num];
        }
        ret = unlinkBlocks(num);
        if (ret < 0) {
            //LOG("failed inside unlinkBlocks");
            RETURN (ret);
        }
    } else {
        //don't need new Blocks -> do nothing
    }

    info->size = newSize;
    syncRoot();

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
int MyOnDiskFS::fuseReaddir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                            struct fuse_file_info *fileInfo) {
    //LOGM();

    //LOGF( "--> Getting The List of Files of %s\n", path );

    filler(buf, ".", NULL, 0); // Current Directory
    filler(buf, "..", NULL, 0); // Parent Directory

    if (strcmp(path, "/") ==
        0) // If the user is trying to show the files/directories of the root directory show the following
    {
        for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
            if (!myFsEmpty[i]) {
                //LOGF("adding to filler: %s", myRoot[i].cPath+1);
                filler(buf, myRoot[i].cPath + 1, NULL, 0);
            }
        }
    }

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

        //LOG("Starting logging...\n");

        //LOG("Using on-disk mode");

        containerFilePath = ((MyFsInfo *) fuse_get_context()->private_data)->contFile;

        //LOGF("Container file name: %s", containerFilePath);

        int ret = this->blockDevice->open(containerFilePath);

        if (ret >= 0) {
            //LOG("Container file does exist, reading");


            //open container
            ret = this->blockDevice->open(containerFilePath);
            //LOG("opened container");
            if (ret >= 0) {
                char *buf = (char *) malloc(BLOCK_SIZE);
                //LOG("created buffer");

                if (buf == nullptr) {
                    //LOGF("ERROR: Buf is null %d", ret);
                    RETURN(nullptr);
                }
                //LOG("checked for nullptr");

                void *retPtr = nullptr;

                //read superblock
                ret = this->blockDevice->read(POS_SPBLOCK, buf);
                if (ret >= 0) {
                    //LOG("read superblock from container");
                    retPtr = memcpy(&mySuperBlock, buf, sizeof(SuperBlock));
                    if (retPtr == nullptr) {
                        //LOG("memcpy of superblock failed");
                        RETURN(retPtr);
                    }
                    //LOG("successfully read superblock");
                }

                //read DMAP
                for (int i = 0; i < BLOCKS_DMAP; i++) {
                    ret = this->blockDevice->read(POS_DMAP + i, buf);
                    if (ret < 0) {
                        //LOGF("ERROR: blockDevice couldn't read DMAP %d", ret);
                        RETURN(nullptr);
                    }
                    retPtr = memcpy(myDmap + i * BLOCK_SIZE, buf, BLOCK_SIZE);
                    if (retPtr == nullptr) {
                        //LOG("memcpy of DMAP failed");
                        RETURN(retPtr);
                    }
                }
                //LOG("successfully read DMAP");

                //read FAT
                for (int i = 0; i < BLOCKS_FAT; i++) {
                    ret = this->blockDevice->read(POS_FAT + i, buf);
                    if (ret < 0) {
                        //LOGF("ERROR: blockDevice couldn't read FAT %d", ret);
                        RETURN(nullptr);
                    }
                    retPtr = memcpy(((char *) myFAT) + i * BLOCK_SIZE, buf, BLOCK_SIZE);
                    if (retPtr == nullptr) {
                        //LOG("memcpy of FAT failed");
                        RETURN(retPtr);
                    }
                }
                //LOG("successfully read FAT");

                //read Root
                for (int i = 0; i < BLOCKS_ROOT; i++) {
                    ret = this->blockDevice->read(POS_ROOT + i, buf);
                    if (ret < 0) {
                        //LOGF("ERROR: blockDevice couldn't read Root %d", ret);
                        RETURN(nullptr);
                    }
                    retPtr = memcpy(&myRoot[i], buf, sizeof(MyFsDiskInfo));
                    if (retPtr == nullptr) {
                        //LOG("memcpy of Root failed");
                        RETURN(retPtr);
                    }
                }
                //LOG("successfully read Root");
                this->blockDevice->close();
                //LOG("closed container");
                free(buf);

                initializeHelpers();

                //dumpStructures();
            }


        } else if (ret == -ENOENT) {
            //LOG("Container file does not exist, creating a new one");

            ret = this->blockDevice->create(containerFilePath);

            if (ret >= 0) {


                //initialise superblock
                mySuperBlock.infoSize = POS_DATA;
                mySuperBlock.dataSize = NUM_DATA_BLOCK_COUNT * 512;
                mySuperBlock.blockPos = POS_SPBLOCK;
                mySuperBlock.dataPos = POS_DATA;
                mySuperBlock.dmapPos = POS_DMAP;
                mySuperBlock.rootPos = POS_ROOT;
                mySuperBlock.fatPos = POS_FAT;

                //initialise heap structures
                memset(&myDmap, 1, sizeof(myDmap));
                for (int i = 0; i < NUM_DATA_BLOCK_COUNT; i++) {
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
                //dumpStructures();
                //sync to container

                //open container
                ret = this->blockDevice->open(containerFilePath);
                //LOG("opened container");
                if (ret >= 0) {
                    char *buf = (char *) malloc(BLOCK_SIZE);
                    //LOG("created buffer");

                    if (buf == nullptr) {
                        //LOGF("ERROR: Buf is null %d", ret);
                        RETURN(nullptr);
                    }
                    //LOG("checked for nullptr");

                    //write superblock
                    memset(buf, 0, BLOCK_SIZE);
                    //LOG("cleared buffer");
                    memcpy(buf, &mySuperBlock, sizeof(SuperBlock));
                    //LOG("memcopied superblock to buffer");
                    ret = this->blockDevice->write(POS_SPBLOCK, buf);
                    if (ret < 0) {
                        //LOGF("ERROR: blockDevice couldn't write superblock %d", ret);
                        RETURN(nullptr);
                    }
                    //LOG("wrote superblock");

                    //write DMAP
                    for (int i = 0; i < BLOCKS_DMAP; i++) {
                        memcpy(buf, myDmap + i * BLOCK_SIZE, BLOCK_SIZE);
                        ret = this->blockDevice->write(POS_DMAP + i, buf);
                        if (ret < 0) {
                            //LOGF("ERROR: blockDevice couldn't write DMAP %d", ret);
                            RETURN(nullptr);
                        }
                    }
                    //LOG("wrote DMAP");

                    //write FAT
                    for (int i = 0; i < BLOCKS_FAT; i++) {
                        memcpy(buf, ((char *) myFAT) + i * BLOCK_SIZE, BLOCK_SIZE);
                        ret = this->blockDevice->write(POS_FAT + i, buf);
                        if (ret < 0) {
                            //LOGF("ERROR: blockDevice couldn't write FAT %d", ret);
                            RETURN(nullptr);
                        }
                    }
                    //LOG("wrote FAT");

                    this->blockDevice->close();
                    free(buf);
                }

                syncRoot();
            }
        }

        if (ret < 0) {
            //LOGF("ERROR: Access to container file failed with error %d", ret);
        }
    }

    RETURN(0);
}

/// @brief Clean up a file system.
///
/// This function is called when the file system is unmounted. You may add some cleanup code here.
void MyOnDiskFS::fuseDestroy() {
    //LOGM();

    //dumpStructures();

}


/// unlinks all blocks of the file starting with Block "num"
/// \param num first Block to be unlinked
/// \return 0 on success, -ERRORNUMBER on failure
int MyOnDiskFS::unlinkBlocks(int32_t num) {
    if (num >= NUM_DATA_BLOCK_COUNT && num < 0) {
        //LOGF("num = %ld not valid", num);
        RETURN(-1111);
    }
    int32_t iterNum = num;
    for (int i = 0; iterNum >= 0 && iterNum < NUM_DATA_BLOCK_COUNT && i < NUM_DATA_BLOCK_COUNT; i++) {
        int32_t tmpNum = iterNum;
        myDmap[tmpNum] = 1;
        //iterate through FAT and delete Fat-entry for the link we just used
        iterNum = myFAT[tmpNum];
        myFAT[tmpNum] = -1;
        syncDmapFat(tmpNum);
        //LOGF("synced block = %ld", tmpNum);
    }
    RETURN (0);
}

/// BLOCKDEVICE NEEDS TO BE OPEN
/// \param num blck num with first datablock as 0
/// \return
int MyOnDiskFS::syncDmapFat(u_int32_t num) {
    //LOGM();
    //LOG("Sync DMAP and FAT");
    //LOGF("num = %ld", num);
    char *dmapPtr = &myDmap[num];
    int32_t dmapBlock = num / BLOCK_SIZE;
    u_int32_t dmapOffset = num % BLOCK_SIZE;
    char *fatPtr = (char *) &myFAT[num];
    int32_t fatBlock = num * sizeof(int32_t) / BLOCK_SIZE;
    u_int32_t fatOffset = num * sizeof(int32_t) % BLOCK_SIZE;
    //LOGF("fatPtr = %X, fatBlock = %ld, fatOffset = %ld", fatPtr, fatBlock, fatOffset);

    int ret = this->blockDevice->open(containerFilePath);
    if (ret < 0) {
        //LOG("couldn't open container");
        RETURN(-10000);
    }

    char blockBuf[512];
    int blkDev = this->blockDevice->read(POS_DMAP + dmapBlock, blockBuf);
    if (blkDev < 0) {
        //LOG("reading dmapBlock failed");
        RETURN(-10000);
    }
    void *ptr = memcpy(blockBuf + dmapOffset, dmapPtr, 1);
    if (ptr == nullptr) {
        //LOG("memcpy of dmapBlock failed");
        RETURN(-10000);
    }
    blkDev = this->blockDevice->write(POS_DMAP + dmapBlock, blockBuf);
    if (blkDev < 0) {
        //LOG("writing dmapBlock failed");
        RETURN(-10000);
    }

    blkDev = this->blockDevice->read(POS_FAT + fatBlock, blockBuf);
    if (blkDev < 0) {
        //LOG("reading fatBlock failed");
        RETURN(-10000);
    }
    ptr = memcpy(blockBuf + fatOffset, fatPtr, sizeof(int32_t));
    //LOGF("fatPtr = %ld", (int32_t)(*fatPtr));
    if (ptr == nullptr) {
        //LOG("memcpy of fatBlock failed");
        RETURN(-10000);
    }
    blkDev = this->blockDevice->write(POS_FAT + fatBlock, blockBuf);
    if (blkDev < 0) {
        //LOG("writing fatBlock failed");
        RETURN(-10000);
    }
    RETURN(0);
}

size_t MyOnDiskFS::findFreeBlock() {
    //LOGM();
    if (containerFull(1, 0)) {
        RETURN(ERROR_BLOCKNUMBER);
    }

    for (size_t i = 0; i < NUM_DATA_BLOCK_COUNT; i++) {
        if (myDmap[i] == 1) {
            memset(&myDmap[i], 0, sizeof(char));
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

void MyOnDiskFS::dumpStructures() {
    /*
    //LOGM();
    //LOG("Dumping structures");
    //LOGF("Dumping Superblock:\n"
         "                mySuperBlock.infoSize = %ld;\n"
         "                mySuperBlock.dataSize = %ld;\n"
         "                mySuperBlock.blockPos = %ld;\n"
         "                mySuperBlock.dataPos = %ld;\n"
         "                mySuperBlock.dmapPos = %ld;\n"
         "                mySuperBlock.rootPos = %ld;\n"
         "                mySuperBlock.fatPos = %ld;",
         mySuperBlock.infoSize, mySuperBlock.dataSize, mySuperBlock.blockPos,
         mySuperBlock.dataPos, mySuperBlock.dmapPos, mySuperBlock.rootPos, mySuperBlock.fatPos);
    //LOG("Dumping DMAP");
    for (int i = 0; i < NUM_DATA_BLOCK_COUNT; i++) {
        if (myDmap[i] != 1) {
            //LOGF("myDmap[%ld] = %ld", i, myDmap[i]);
        }
    }
    //LOG("Dumping FAT");
    for (int i = 0; i < NUM_DATA_BLOCK_COUNT; i++) {
        if (myFAT[i] != -1) {
            //LOGF("myFAT[%ld] = %d", i, myFAT[i]);
        }
    }
    //LOG("Dumping Root");
    for (int i = 0; i < NUM_DIR_ENTRIES && myRoot[i].data != POS_NULLPTR; i++) {
        //LOGF("File %ld:"
             "    size_t size = %ld\n"
             "    int32_t data = %ld\n"
             "    __uid_t uid = %ld\n"
             "    __gid_t gid = %ld\n"
             "    __mode_t mode = %ld\n"
             "    __time_t atime = %ld\n"
             "    __time_t mtime = %ld\n"
             "    __time_t ctime = %ld\n"
             "    char cPath[NAME_LENGTH+1] = %s", i, myRoot[i].size, myRoot[i].data,
             myRoot[i].uid, myRoot[i].gid, myRoot[i].mode, myRoot[i].atime, myRoot[i].mtime, myRoot[i].ctime, myRoot[i].cPath);
    }
    //LOG("Dumping Helpers");
    //LOGF("    bool myFsOpenFiles[0] = %ld\n"
         "    bool myFsEmpty[0] = %ld\n"
         "    unsigned int iCounterFiles = %ld\n"
         "    unsigned int iCounterOpen = %ld", myFsOpenFiles[0], myFsEmpty[0], iCounterFiles, iCounterOpen);
    //LOG("END OF Dumping structures");
    */
}

int MyOnDiskFS::containerFull(size_t size, off_t offset) {
    //LOGM();

    size_t filesize = 0;
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        filesize += ceil((myRoot[i].size) / BLOCK_SIZE);
    }

    if (filesize + ceil((size + offset) / BLOCK_SIZE) > NUM_DATA_BLOCK_COUNT) {
        RETURN(1);
    }

    RETURN(0);
}

int MyOnDiskFS::syncRoot() {
    //LOGM();
    //LOGF("Synchronising Root onto container %s ", containerFilePath);

    //open container
    int ret = this->blockDevice->open(containerFilePath);
    //LOG("opened container");
    if (ret >= 0) {
        char *buf = (char *) malloc(BLOCK_SIZE);
        //LOG("created buffer");

        if (buf == nullptr) {
            RETURN(-20);
        }
        //LOG("checked for nullptr");

        //write Root
        for (int i = 0; i < BLOCKS_ROOT; i++) {
            memset(buf, 0, BLOCK_SIZE);
            memcpy(buf, &myRoot[i], sizeof(MyFsDiskInfo));
            ret = this->blockDevice->write(POS_ROOT + i, buf);
            if (ret < 0) {
                RETURN(ret);
            }
        }
        //LOG("wrote Root");

        this->blockDevice->close();
        free(buf);
        RETURN(0);

    }
    RETURN(-10);
}

int MyOnDiskFS::iIsPathValid(const char *path, uint64_t fh) {
    //LOGM();
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

// DO NOT EDIT ANYTHING BELOW THIS LINE!!!

/// @brief Set the static instance of the file system.
///
/// Do not edit this method!
void MyOnDiskFS::SetInstance() {
    MyFS::_instance = new MyOnDiskFS();
}
