//
// Created by Oliver Waldhorst on 20.03.20.
//  Copyright © 2017-2020 Oliver Waldhorst. All rights reserved.
//

#include "myinmemoryfs.h"

// The functions fuseGettattr(), fuseRead(), and fuseReadDir() are taken from
// an example by Mohammed Q. Hussain. Here are original copyrights & licence:

/**
 * Simple & Stupid Filesystem.
 *
 * Mohammed Q. Hussain - http://www.maastaar.net
 *
 * This is an example of using FUSE to build a simple filesystem. It is a part of a tutorial in MQH Blog with the title
 * "Writing a Simple Filesystem Using FUSE in C":
 * http://www.maastaar.net/fuse/linux/filesystem/c/2016/05/21/writing-a-simple-filesystem-using-fuse/
 *
 * License: GNU GPL
 */

// For documentation of FUSE methods see https://libfuse.github.io/doxygen/structfuse__operations.html

#undef DEBUG

// TODO: Comment lines to reduce debug messages
#define DEBUG
#define DEBUG_METHODS
#define DEBUG_RETURN_VALUES

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "macros.h"
#include "myfs.h"
#include "myfs-info.h"
#include "blockdevice.h"

/// @brief Constructor of the in-memory file system class.
///
/// You may add your own constructor code here.
MyInMemoryFS::MyInMemoryFS() : MyFS() {

    // TODO: [PART 1] Add your constructor code here

}

/// @brief Destructor of the in-memory file system class.
///
/// You may add your own destructor code here.
MyInMemoryFS::~MyInMemoryFS() {

    // TODO: [PART 1] Add your cleanup code here

}

/// @brief Create a new file.
///
/// Create a new file with given name and permissions.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] mode Permissions for file access.
/// \param [in] dev Can be ignored.
/// \return 0 on success, -ERRNO on failure.
int MyInMemoryFS::fuseMknod(const char *path, mode_t mode, dev_t dev) {
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
        MyFsFileInfo info = myFsFiles[i];
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
    strcpy(myFsFiles[index].cName, (path+1));
    strcpy(myFsFiles[index].cPath, path);
    myFsFiles[index].size = 0;
    myFsFiles[index].data = nullptr;
    myFsFiles[index].atime.tv_sec = myFsFiles[index].ctime.tv_sec = myFsFiles[index].mtime.tv_sec = time(NULL);
    myFsFiles[index].gid = getgid();
    myFsFiles[index].uid = getuid();
    myFsFiles[index].mode = mode;
    myFsEmpty[index] = false;

    //increment file counter
    iCounterFiles++;

    LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", index, myFsFiles[index].cPath, myFsFiles[index].size, myFsFiles[index].atime.tv_sec);
    LOGF("iCounterFiles: %d", iCounterFiles);

    RETURN(0);
}

/// @brief Delete a file.
///
/// Delete a file with given name from the file system.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \return 0 on success, -ERRNO on failure.
int MyInMemoryFS::fuseUnlink(const char *path) {
    LOGM();

    int index = -1;
    // Get index of file by  path
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++)
    {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsFileInfo info = myFsFiles[i];
        if (strcmp(path, info.cPath) == 0)
        {
            index = i;
            break;
        }
    }

    if (index < 0)
    {
        RETURN(-ENOENT);
    }

    LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", index, myFsFiles[index].cPath, myFsFiles[index].size, myFsFiles[index].atime.tv_sec);

    free(myFsFiles[index].data);
    myFsFiles[index].size = 0;

    memset(&myFsFiles[index], 0, sizeof(MyFsFileInfo));

    myFsOpenFiles[index] = false;
    myFsEmpty[index] = true;

    iCounterFiles--;

    LOGF("iCounterFiles: %d", iCounterFiles);

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
int MyInMemoryFS::fuseRename(const char *path, const char *newpath) {
    LOGM();
    LOGF("Old filepath: %s, New filepath: %s", path, newpath);

    //check length of new filename
    if (strlen(newpath) - 1 > NAME_LENGTH) {
        RETURN(-EINVAL);
    }

    size_t index = -1;
    bool bNewNameAlreadyInUse = false;

    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsFileInfo info = myFsFiles[i];
        if (strcmp(path, info.cPath) == 0) {
            index = i;
        }
        if (strcmp(path, newpath) == 0) {
            bNewNameAlreadyInUse = true;
        }
    }

    if (bNewNameAlreadyInUse) {
        RETURN(-EEXIST);
    }

    // file found?
    if (index < 0)
    {
        RETURN(-ENOENT);
    }

    LOGF("Index: %d", index);


    //overwrite fileinfo values
    strcpy(myFsFiles[index].cName, (newpath+1));
    strcpy(myFsFiles[index].cPath, newpath);
    myFsFiles[index].atime.tv_sec = myFsFiles[index].ctime.tv_sec = myFsFiles[index].mtime.tv_sec = time(NULL);


    LOGF("Index Changed: %d", index);

    RETURN(0);
}

/// @brief Get file meta data.
///
/// Get the metadata of a file (user & group id, modification times, permissions, ...).
/// \param [in] path Name of the file, starting with "/".
/// \param [out] statbuf Structure containing the meta data, for details type "man 2 stat" in a terminal.
/// \return 0 on success, -ERRNO on failure.
int MyInMemoryFS::fuseGetattr(const char *path, struct stat *statbuf) {
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
            LOGF("Comparing string1: %s, and string2: %s", path, myFsFiles[i].cPath);
            if (strcmp(path, myFsFiles[i].cPath) == 0)
            {
                statbuf->st_mode = myFsFiles[i].mode;
                statbuf->st_nlink = 1;
                statbuf->st_size = myFsFiles[i].size;
                statbuf->st_mtime = myFsFiles[i].mtime.tv_sec; // The last "m"odification of the file/directory is right now
                LOG("fuseGetAttr()");
                LOG("filled statbuf with data");
                LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", i, myFsFiles[i].cPath, myFsFiles[i].size, myFsFiles[i].atime.tv_sec);
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
int MyInMemoryFS::fuseChmod(const char *path, mode_t mode) {
    LOGM();

    size_t index = -1;

    // Get index of file by  path
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++)
    {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsFileInfo info = myFsFiles[i];
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
    myFsFiles[index].mode = mode;
    myFsFiles[index].atime.tv_sec = myFsFiles[index].ctime.tv_sec = myFsFiles[index].mtime.tv_sec = time(NULL);


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
int MyInMemoryFS::fuseChown(const char *path, uid_t uid, gid_t gid) {
    LOGM();

    size_t index = -1;

    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++)
    {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsFileInfo info = myFsFiles[i];
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
    myFsFiles[index].uid = uid;
    myFsFiles[index].gid = gid;
    myFsFiles[index].atime.tv_sec = myFsFiles[index].ctime.tv_sec = myFsFiles[index].mtime.tv_sec = time(NULL);


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
int MyInMemoryFS::fuseOpen(const char *path, struct fuse_file_info *fileInfo) {
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

        if (strcmp(path, myFsFiles[i].cPath) == 0)
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
                myFsFiles[i].atime.tv_sec = time( NULL );
                LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", i, myFsFiles[i].cPath, myFsFiles[i].size, myFsFiles[i].atime.tv_sec);
                LOGF("index: %d, iCounterOpen: %d", i, iCounterOpen);
                break;
            }
        }
    }

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
int MyInMemoryFS::fuseRead(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    LOGM();

    LOGF("--> Trying to read %s, %lu, %lu\n", path, (unsigned long) offset, size);

    int index = iIsPathValid(path, fileInfo->fh);
    if (index < 0)
    {
        RETURN(index);
    }

    LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", index, myFsFiles[index].cPath, myFsFiles[index].size,
         myFsFiles[index].atime.tv_sec);

    if (myFsFiles[index].size < size + offset)
    {
        if (myFsFiles[index].size < offset) {
            LOGF("Offset %ld is bigger than file size %ld", offset, myFsFiles[index].size);
            RETURN(-EINVAL);
        }
        LOGF("Trying to read more bytes than file is long. Reading %ld bytes starting from offset %ld instead", myFsFiles[index].size - offset, offset);
        void* out = memcpy(buf, myFsFiles[index].data + offset, myFsFiles[index].size - offset);
        if (out == nullptr)
        {
            RETURN(-EAGAIN);
        }
        RETURN(myFsFiles[index].size - offset);
    }
    else {
        void* out = memcpy(buf, myFsFiles[index].data + offset, size);
        if (out == nullptr)
        {
            RETURN(-EAGAIN);
        }
        RETURN(size);
    }
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
int MyInMemoryFS::fuseWrite(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    LOGM();

    if (0 > iIsPathValid(path, fileInfo->fh)){
        RETURN(iIsPathValid(path, fileInfo->fh));
    }

    LOGF("Trying to write to path: %s, %ld bytes, starting with offset: %ld", path, size, offset);

    // need more space??
    if (myFsFiles[fileInfo->fh].size < size + offset) {
        LOGF("Need more space. Reallocating %ld bytes", size+offset);
        void* tmpdata = realloc(myFsFiles[fileInfo->fh].data, size + offset);
        if (tmpdata != nullptr) {
            LOGF("Realloc was succesful, size: %ld -> %ld, data: %ld -> %ld", myFsFiles[fileInfo->fh].size, size + offset, myFsFiles[fileInfo->fh].data, (unsigned char*) tmpdata);
            myFsFiles[fileInfo->fh].data = (unsigned char*) tmpdata;
            myFsFiles[fileInfo->fh].size = size + offset;
        }
        LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", fileInfo->fh, myFsFiles[fileInfo->fh].cPath, myFsFiles[fileInfo->fh].size, myFsFiles[fileInfo->fh].atime.tv_sec);
    }

    // memcpy buf onto file.data + offset
    void* tmpdata = memcpy(myFsFiles[fileInfo->fh].data + offset, buf, size);
    if (tmpdata == nullptr) {
        LOG("memcpy failed");
        RETURN(-300);
    }

    myFsFiles[fileInfo->fh].atime.tv_sec = myFsFiles[fileInfo->fh].mtime.tv_sec = time(NULL);

    RETURN(size);
}

/// @brief Close a file.
///
/// In Part 1 this includes decrementing the open file count.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] fileInfo Can be ignored in Part 1 .
/// \return 0 on success, -ERRNO on failure.
int MyInMemoryFS::fuseRelease(const char *path, struct fuse_file_info *fileInfo) {
    LOGM();

    int valid = iIsPathValid(path, fileInfo->fh);
    if (valid < 0) {
        RETURN(valid);
    }

    if (!myFsOpenFiles[valid]) {
        RETURN(-EBADF);
    }

    LOGF("index: %d, filepath: %s, filesize: %ld, timestamp: %ld", fileInfo->fh, myFsFiles[fileInfo->fh].cPath, myFsFiles[fileInfo->fh].size, myFsFiles[fileInfo->fh].atime.tv_sec);

    myFsOpenFiles[valid] = false;
    iCounterOpen--;
    fileInfo->fh = -EBADF;

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
int MyInMemoryFS::fuseTruncate(const char *path, off_t newSize) {
    LOGM();

    int index = iFindFileIndex(path);

    if (0 > index) {
        RETURN(index);
    }

    LOGF("index: %ld, data: %ld, filepath: %s, filesize: %ld, timestamp: %ld", index, myFsFiles[index].data, myFsFiles[index].cPath, myFsFiles[index].size, myFsFiles[index].atime.tv_sec);
    //index is valid onto given file
    void* tmpdata = realloc(myFsFiles[index].data, newSize);
    if (tmpdata != nullptr) {
        LOGF("Realloc was succesful, size: %ld -> %ld, data: %ld -> %ld", myFsFiles[index].size, newSize, myFsFiles[index].data, (unsigned char*) tmpdata);
        myFsFiles[index].data = (unsigned char*) tmpdata;
        myFsFiles[index].size = newSize;
        RETURN (0);
    }
    RETURN (-EAGAIN);
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
int MyInMemoryFS::fuseTruncate(const char *path, off_t newSize, struct fuse_file_info *fileInfo) {
    LOGM();

    int x = iIsPathValid(path, fileInfo->fh);
    int index = x;
    if (0 > x) {
        //find file with string
        x = iFindFileIndex(path);
        if (0 > x) {
            //file not found
            RETURN(-EEXIST);
        }
        //found file
        index = x;
    }
    //index is valid onto given file
    void* tmpdata = realloc(myFsFiles[index].data, newSize);
    if (tmpdata != nullptr) {
        LOGF("Realloc was succesful, size: %ld -> %ld, data: %ld -> %ld", myFsFiles[fileInfo->fh].size, newSize, myFsFiles[fileInfo->fh].data, (unsigned char*) tmpdata);
        myFsFiles[fileInfo->fh].data = (unsigned char*) tmpdata;
        myFsFiles[fileInfo->fh].size = newSize;
        RETURN (0);
    }
    RETURN (-EAGAIN);
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
int MyInMemoryFS::fuseReaddir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fileInfo) {
    LOGM();

    LOGF( "--> Getting The List of Files of %s\n", path );

    filler( buf, ".", NULL, 0 ); // Current Directory
    filler( buf, "..", NULL, 0 ); // Parent Directory

    if ( strcmp( path, "/" ) == 0 ) // If the user is trying to show the files/directories of the root directory show the following
    {
        for (size_t i = 0; i < NUM_DIR_ENTRIES; i++){
            if (!myFsEmpty[i]) {
                LOGF("adding to filler: %s", myFsFiles[i].cName);
                filler( buf, myFsFiles[i].cName, NULL, 0);
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
void* MyInMemoryFS::fuseInit(struct fuse_conn_info *conn) {
    // Open logfile
    this->logFile= fopen(((MyFsInfo *) fuse_get_context()->private_data)->logFile, "w+");
    if(this->logFile == NULL) {
        fprintf(stderr, "ERROR: Cannot open logfile %s\n", ((MyFsInfo *) fuse_get_context()->private_data)->logFile);
    } else {
        // turn of logfile buffering
        setvbuf(this->logFile, NULL, _IOLBF, 0);

        LOG("Starting logging...\n");

        LOG("Using in-memory mode");


        iCounterFiles = iCounterOpen = 0;
        memset(&myFsFiles, 0, sizeof(myFsFiles));
        memset(&myFsEmpty, 1, sizeof(myFsEmpty));
        memset(&myFsOpenFiles, 0, sizeof(myFsOpenFiles));
    }

    RETURN(0);
}

/// @brief Clean up a file system.
///
/// This function is called when the file system is unmounted. You may add some cleanup code here.
void MyInMemoryFS::fuseDestroy() {
    LOGM();

    for (MyFsFileInfo file : myFsFiles) {
        LOGF("Freeing memory. filename: %s", file.cName);
        free(file.data);
    }
}

int MyInMemoryFS::iIsPathValid(const char *path, uint64_t fh) {
    if (fh < 0 || fh >= NUM_DIR_ENTRIES) {
        return (-100);
    }
    if (myFsEmpty[fh]) {
        return (-100);
    }
    if (strcmp(path, myFsFiles[fh].cPath) == 0) {
        return (fh);
    }
    return (-100);
}

int MyInMemoryFS::iFindEmptySpot()
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

int MyInMemoryFS::iFindFileIndex(const char *path)
{
    LOGM();
    for (size_t i = 0; i < NUM_DIR_ENTRIES; i++)
    {
        if (myFsEmpty[i]) {
            continue;
        }
        MyFsFileInfo info = myFsFiles[i];
        if (strcmp(path, info.cPath) == 0)
        {
            RETURN(i);
        }
    }
    RETURN(-EEXIST);
}


// DO NOT EDIT ANYTHING BELOW THIS LINE!!!

/// @brief Set the static instance of the file system.
///
/// Do not edit this method!
void MyInMemoryFS::SetInstance() {
    MyFS::_instance= new MyInMemoryFS();
}

