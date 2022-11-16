//
// Created by Oliver Waldhorst on 20.03.20.
// Copyright © 2017-2020 Oliver Waldhorst. All rights reserved.
//

#include "myondiskfs.h"

// For documentation of FUSE methods see https://libfuse.github.io/doxygen/structfuse__operations.html

#undef DEBUG

// TODO: Comment lines to reduce debug messages
#define DEBUG
#define DEBUG_METHODS
#define DEBUG_RETURN_VALUES

#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "macros.h"
#include "myfs.h"
#include "myfs-info.h"
#include "blockdevice.h"

/// @brief Constructor of the on-disk file system class.
///
/// You may add your own constructor code here.
MyOnDiskFS::MyOnDiskFS() : MyFS() {
    // create a block device object
    this->blockDevice= new BlockDevice(BLOCK_SIZE);

    // TODO: [PART 2] Add your constructor code here

}

/// @brief Destructor of the on-disk file system class.
///
/// You may add your own destructor code here.
MyOnDiskFS::~MyOnDiskFS() {
    // free block device object
    delete this->blockDevice;

    // TODO: [PART 2] Add your cleanup code here

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
    LOGM();

    //filesystem full?
    if (iCounterFiles >= NUM_DIR_ENTRIES) {
        RETURN(-ENOSPC);
    }

    //file with same name exists?
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsDiskInfo info = myRoot[i];
        if (strcmp(path, info.cPath) == 0) {
            RETURN(-EEXIST); // already exists
        }
    }

    //find index to put fileinfo in
    int index = iFindEmptySpot();
    if (index < 0) {
        RETURN(index);
    }

    //check length of given filename
    if (strlen(path) - 1 > NAME_LENGTH)
    {
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

    LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", index, myRoot[index].cPath, myRoot[index].size, myRoot[index].atime);
    LOGF("iCounterFiles: %d", iCounterFiles);

    RETURN(0);
}

/// @brief Delete a file.
///
/// Delete a file with given name from the file system.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseUnlink(const char *path) {
    LOGM();

    // TODO: [PART 2] Implement this!

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
    LOGM();

    // TODO: [PART 2] Implement this!

    RETURN(0);
}

/// @brief Get file meta data.
///
/// Get the metadata of a file (user & group id, modification times, permissions, ...).
/// \param [in] path Name of the file, starting with "/".
/// \param [out] statbuf Structure containing the meta data, for details type "man 2 stat" in a terminal.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseGetattr(const char *path, struct stat *statbuf) {
    LOGM();


    LOGF( "\tAttributes of %s requested\n", path );

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
    statbuf->st_atime = time( NULL ); // The last "a"ccess of the file/directory is right now

    int ret= 0;

    if ( strcmp( path, "/" ) == 0 )
    {
        LOG("path is rootdirectory \'/\'");
        statbuf->st_mode = S_IFDIR | 0755;
        statbuf->st_nlink = 2; // Why "two" hardlinks instead of "one"? The answer is here: http://unix.stackexchange.com/a/101536
    }
    else if ( strlen(path) > 0 )
    {
        LOG("path-length > 0");
        for (size_t i = 0; i < NUM_DIR_ENTRIES; i++)
        {
            if (myFsEmpty[i]){
                continue;
            }
            LOGF("Comparing string1: %s, and string2: %s", path, myRoot[i].cPath);
            if (strcmp(path, myRoot[i].cPath) == 0)
            {
                statbuf->st_mode = myRoot[i].mode;
                statbuf->st_nlink = 1;
                statbuf->st_size = myRoot[i].size;
                statbuf->st_mtime = myRoot[i].mtime; // The last "m"odification of the file/directory is right now
                LOG("fuseGetAttr()");
                LOG("filled statbuf with data");
                LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", i, myRoot[i].cPath, myRoot[i].size, myRoot[i].atime);
                RETURN(0);
            }
        }
        LOG("havent found file in myFsFiles-array");

        ret= -ENOENT;
    }
    else {
        LOG("path-length <= 0");
        ret= -ENOENT;
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
    LOGM();

    size_t index = -1;

    // Get index of file by  path
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++)
    {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsDiskInfo info = myRoot[i];
        if (strcmp(path, info.cPath) == 0)
        {
            index = i;
            break;
        }
    }

    // file found?
    if (index < 0)
    {
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
    LOGM();

    size_t index = -1;

    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++)
    {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsDiskInfo info = myRoot[i];
        if (strcmp(path, info.cPath) == 0)
        {
            index = i;
            break;
        }
    }

    // file found?
    if (index < 0)
    {
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
    LOGM();

    if (iCounterOpen >= NUM_OPEN_FILES)
    {
        RETURN(-EMFILE);
    }

    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++)
    {
        if (myFsEmpty[i]) {
            continue;
        }

        if (strcmp(path, myRoot[i].cPath) == 0)
        {
            if (myFsOpenFiles[i])
            {
                RETURN(-EPERM); // Already Open
            }
            else
            {
                // Set Handle etc
                myFsOpenFiles[i] = true;
                fileInfo->fh = i; // can be used in fuseRead and fuseRelease
                iCounterOpen++;
                myRoot[i].atime = time( NULL );
                LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", i, myRoot[i].cPath, myRoot[i].size, myRoot[i].atime);
                LOGF("index: %d, iCounterOpen: %d", i, iCounterOpen);
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
    LOGM();

    // TODO: [PART 2] Implement this!

    RETURN(0);
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
int MyOnDiskFS::fuseWrite(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    LOGM();

    // TODO: [PART 2] Implement this!

    RETURN(0);
}

/// @brief Close a file.
///
/// \param [in] path Name of the file, starting with "/".
/// \param [in] File handel for the file set by fuseOpen.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRelease(const char *path, struct fuse_file_info *fileInfo) {
    LOGM();

    int valid = iIsPathValid(path, fileInfo->fh);
    if (valid < 0) {
        RETURN(valid);
    }

    if (!myFsOpenFiles[valid]) {
        RETURN(-EBADF);
    }

    LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", fileInfo->fh, myRoot[fileInfo->fh].cPath, myRoot[fileInfo->fh].size, myRoot[fileInfo->fh].atime);

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
    LOGM();

    // TODO: [PART 2] Implement this!

    RETURN(0);
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
    LOGM();

    // TODO: [PART 2] Implement this!

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
    LOGM();

    LOGF( "--> Getting The List of Files of %s\n", path );

    filler( buf, ".", NULL, 0 ); // Current Directory
    filler( buf, "..", NULL, 0 ); // Parent Directory

    if ( strcmp( path, "/" ) == 0 ) // If the user is trying to show the files/directories of the root directory show the following
    {
        for (size_t i = 0; i < NUM_DIR_ENTRIES; i++){
            if (!myFsEmpty[i]) {
                LOGF("adding to filler: %s", myRoot[i].cPath+1);
                filler( buf, myRoot[i].cPath+1, NULL, 0);
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
void* MyOnDiskFS::fuseInit(struct fuse_conn_info *conn) {
    // Open logfile
    this->logFile= fopen(((MyFsInfo *) fuse_get_context()->private_data)->logFile, "w+");
    if(this->logFile == NULL) {
        fprintf(stderr, "ERROR: Cannot open logfile %s\n", ((MyFsInfo *) fuse_get_context()->private_data)->logFile);
    } else {
        // turn of logfile buffering
        setvbuf(this->logFile, NULL, _IOLBF, 0);

        LOG("Starting logging...\n");

        LOG("Using on-disk mode");

        containerFilePath = ((MyFsInfo *) fuse_get_context()->private_data)->contFile;

        LOGF("Container file name: %s", containerFilePath);

        int ret= this->blockDevice->open(containerFilePath);

        if(ret >= 0) {
            LOG("Container file does exist, reading");

            // TODO: [PART 2] Read existing structures form file

            //open container
            ret = this->blockDevice->open(containerFilePath);
            LOG("opened container");
            if (ret >= 0) {
                char* buf = (char*) malloc(BLOCK_SIZE);
                LOG("created buffer");

                if (buf == nullptr) {
                    LOGF("ERROR: Buf is null %d", ret);
                    RETURN(nullptr);
                }
                LOG("checked for nullptr");

                void* retPtr = nullptr;

                //read superblock
                ret = this->blockDevice->read(POS_SPBLOCK, buf);
                if (ret >= 0) {
                    LOG("read superblock from container");
                    retPtr = memcpy(&mySuperBlock, buf, sizeof(SuperBlock));
                    if (retPtr == nullptr) {
                        LOG("memcpy of superblock failed");
                        RETURN(retPtr);
                    }
                    LOG("successfully read superblock");
                }

                //read DMAP
                for(int i = 0; i < BLOCKS_DMAP; i++) {
                    ret = this->blockDevice->read(POS_DMAP + i, buf);
                    if (ret < 0) {
                        LOGF("ERROR: blockDevice couldn't read DMAP %d", ret);
                        RETURN(nullptr);
                    }
                    retPtr = memcpy(myDmap + i * BLOCK_SIZE, buf,BLOCK_SIZE);
                    if (retPtr == nullptr) {
                        LOG("memcpy of DMAP failed");
                        RETURN(retPtr);
                    }
                }
                LOG("successfully read DMAP");

                //read FAT
                for (int i = 0; i < BLOCKS_FAT; i++) {
                    ret = this->blockDevice->read(POS_FAT + i, buf);
                    if (ret < 0) {
                        LOGF("ERROR: blockDevice couldn't read FAT %d", ret);
                        RETURN(nullptr);
                    }
                    retPtr = memcpy(((char*)myFAT) + i * BLOCK_SIZE, buf,BLOCK_SIZE);
                    if (retPtr == nullptr) {
                        LOG("memcpy of FAT failed");
                        RETURN(retPtr);
                    }
                }
                LOG("successfully read FAT");

                //read Root
                for (int i = 0; i < BLOCKS_ROOT; i++) {
                    ret = this->blockDevice->read(POS_ROOT + i, buf);
                    if (ret < 0) {
                        LOGF("ERROR: blockDevice couldn't read Root %d", ret);
                        RETURN(nullptr);
                    }
                    retPtr = memcpy(&myRoot[i], buf, sizeof(MyFsDiskInfo));
                    if (retPtr == nullptr) {
                        LOG("memcpy of Root failed");
                        RETURN(retPtr);
                    }
                }
                LOG("successfully read Root");
                this->blockDevice->close();
                LOG("closed container");

                initializeHelpers();

                //dumpStructures();
            }



        } else if(ret == -ENOENT) {
            LOG("Container file does not exist, creating a new one");

            ret = this->blockDevice->create(containerFilePath);

            if (ret >= 0) {

                // TODO: [PART 2] Create empty structures in file

                //initialise superblock
                mySuperBlock.infoSize = POS_DATA;
                mySuperBlock.dataSize = NUM_DATA_BLOCK_COUNT;
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
                LOG("opened container");
                if (ret >= 0) {
                    char* buf = (char*) malloc(BLOCK_SIZE);
                    LOG("created buffer");

                    if (buf == nullptr) {
                        LOGF("ERROR: Buf is null %d", ret);
                        RETURN(nullptr);
                    }
                    LOG("checked for nullptr");

                    //write superblock
                    memset(buf, 0, BLOCK_SIZE);
                    LOG("cleared buffer");
                    memcpy(buf, &mySuperBlock, sizeof(SuperBlock));
                    LOG("memcopied superblock to buffer");
                    ret = this->blockDevice->write(POS_SPBLOCK, buf);
                    if (ret < 0) {
                        LOGF("ERROR: blockDevice couldn't write superblock %d", ret);
                        RETURN(nullptr);
                    }
                    LOG("wrote superblock");

                    //write DMAP
                    for (int i = 0; i < BLOCKS_DMAP; i++) {
                        memcpy(buf, myDmap + i * BLOCK_SIZE,BLOCK_SIZE);
                        ret = this->blockDevice->write(POS_DMAP + i, buf);
                        if (ret < 0) {
                            LOGF("ERROR: blockDevice couldn't write DMAP %d", ret);
                            RETURN(nullptr);
                        }
                    }
                    LOG("wrote DMAP");

                    //write FAT
                    for (int i = 0; i < BLOCKS_FAT; i++) {
                        memcpy(buf, ((char*)myFAT) + i * BLOCK_SIZE,BLOCK_SIZE);
                        ret = this->blockDevice->write(POS_FAT + i, buf);
                        if (ret < 0) {
                            LOGF("ERROR: blockDevice couldn't write FAT %d", ret);
                            RETURN(nullptr);
                        }
                    }
                    LOG("wrote FAT");

                    this->blockDevice->close();
                }

                syncRoot();
            }
        }

        if(ret < 0) {
            LOGF("ERROR: Access to container file failed with error %d", ret);
        }
     }

    return 0;
}

/// @brief Clean up a file system.
///
/// This function is called when the file system is unmounted. You may add some cleanup code here.
void MyOnDiskFS::fuseDestroy() {
    LOGM();

    // TODO: [PART 2] Implement this!

}

// TODO: [PART 2] You may add your own additional methods here!

void MyOnDiskFS::initializeHelpers() {
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        myFsOpenFiles[i] = false;
        if (myRoot[i].cPath[0] != '/') {
            myFsEmpty[i] = true;
        }
        else {
            myFsEmpty[i] = false;
            iCounterFiles++;
        }
    }
    iCounterOpen = 0;

    LOG("initialized myFsEmpty, myFsOpenFiles, iCounterOpen, iCounterFiles");
}

void MyOnDiskFS::dumpStructures() {
    LOG("Dumping structures");
    LOGF("Dumping Superblock:\n"
         "                mySuperBlock.infoSize = %ld;\n"
         "                mySuperBlock.dataSize = %ld;\n"
         "                mySuperBlock.blockPos = %ld;\n"
         "                mySuperBlock.dataPos = %ld;\n"
         "                mySuperBlock.dmapPos = %ld;\n"
         "                mySuperBlock.rootPos = %ld;\n"
         "                mySuperBlock.fatPos = %ld;",
         mySuperBlock.infoSize, mySuperBlock.dataSize, mySuperBlock.blockPos,
         mySuperBlock.dataPos, mySuperBlock.dmapPos, mySuperBlock.rootPos, mySuperBlock.fatPos);
    LOG("Dumping DMAP");
    for (int i = 0; i < NUM_DATA_BLOCK_COUNT; i++) {
        if (i % (1024*1024*16) == 0) {
            LOGF("myDmap[%ld] = %ld", i, myDmap[i]);
        }
    }
    LOG("Dumping FAT");
    for (int i = 0; i < NUM_DATA_BLOCK_COUNT; i++) {
        if (i % (1024*1024*16) == 0) {
            LOGF("myFAT[%ld] = %d", i, myFAT[i]);
        }
    }
    LOG("Dumping Root");
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        LOGF("File %ld:"
             "    size_t size = %ld\n"
             "    int32_t data = %ld\n"
             "    __uid_t uid = %ld\n"
             "    __gid_t gid = %ld\n"
             "    __mode_t mode = %ld\n"
             "    __time_t atime = %ld\n"
             "    __time_t mtime = %ld\n"
             "    __time_t ctime = %ld\n"
             "    char cPath[NAME_LENGTH+1] = %s", i, myRoot[i].size, myRoot[i].data,
             myRoot[i].uid, myRoot[i].gid, myRoot[i].mode, myRoot[i].atime, myRoot[i].mtime, myRoot[i].ctime);
    }
    LOG("Dumping Helpers");
    LOGF("    bool myFsOpenFiles[0] = %ld\n"
         "    bool myFsEmpty[0] = %ld\n"
         "    unsigned int iCounterFiles = %ld\n"
         "    unsigned int iCounterOpen = %ld", myFsOpenFiles[0], myFsEmpty[0], iCounterFiles, iCounterOpen);
    LOG("END OF Dumping structures");
}

int MyOnDiskFS::syncRoot() {
    LOGF("Synchronising Structures onto container %s ", containerFilePath);

    //open container
    int ret = this->blockDevice->open(containerFilePath);
    LOG("opened container");
    if (ret >= 0) {
        char* buf = (char*) malloc(BLOCK_SIZE);
        LOG("created buffer");

        if (buf == nullptr) {
            RETURN(-20);
        }
        LOG("checked for nullptr");

        //write Root
        for (int i = 0; i < BLOCKS_ROOT; i++) {
            memset(buf, 0, BLOCK_SIZE);
            memcpy(buf, &myRoot[i],sizeof(MyFsDiskInfo));
            ret = this->blockDevice->write(POS_ROOT + i, buf);
            if (ret < 0) {
                RETURN(ret);
            }
        }
        LOG("wrote Root");

        this->blockDevice->close();
        RETURN(0);

    }
    RETURN(-10);
}

int MyOnDiskFS::iIsPathValid(const char *path, uint64_t fh) {
    if (fh < 0 || fh >= NUM_DIR_ENTRIES) {
        return (-1);
    }
    if (myFsEmpty[fh]) {
        return (-1);
    }
    if (strcmp(path, myRoot[fh].cPath) == 0) {
        return (fh);
    }
    return (-1);
}

int MyOnDiskFS::iFindEmptySpot()
{
    LOGM();
    for (int i = 0; i < NUM_DIR_ENTRIES; i++)
    {
        if (myFsEmpty[i])
        {
            LOGF("index %ld is free", i);
            RETURN(i);
        }
    }
    LOG("NOT EMPTY BY FUNC");
    RETURN(-ENOSPC);
}

// DO NOT EDIT ANYTHING BELOW THIS LINE!!!

/// @brief Set the static instance of the file system.
///
/// Do not edit this method!
void MyOnDiskFS::SetInstance() {
    MyFS::_instance= new MyOnDiskFS();
}
