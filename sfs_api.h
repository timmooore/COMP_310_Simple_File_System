/*******************************************************************************
 *                                                                             *
 * AUTHOR: Timothy Moore                                                       *
 * MCGILL ID: 260587927                                                        *
 * FILE: sfs_api.h                                                             *
 *                                                                             *
 ******************************************************************************/


#ifndef _INCLUDE_SFS_API_H_
#define _INCLUDE_SFS_API_H_
#include <stdint.h>


// Function macro for printing error messages and exiting with EXIT_FAIILURE
#define die(msg) { perror( msg ); exit( EXIT_FAILURE ); }


/* 
 * A struct representing an inode needs to be made that contains fields for the
 * mode, link content, size, uid, gid, etc. A struct representing the super 
 * block that contains fields for the magic number, block size, file system
 * size, inode table length, etc.
 * A struct representing a file descriptor is also needed that contains fields
 * to store both the inode of a file and the rw_ptr of that file.
 * A struct representing a directory entry is also created that stores a 
 * filename as well as the inode of a file
 */
 typedef struct {
    uint32_t magic_num;
    uint32_t block_size;
    uint32_t fs_size;
    uint32_t inode_table_len;
    uint32_t root_dir_inode;
 } super_block_t;


typedef struct  {
    unsigned int mode;
    unsigned int link_cnt;
    unsigned int uid;
    unsigned int gid;
    unsigned int size;
    unsigned int blk_ptr[12];
    unsigned int indirect;
} inode_t;


typedef struct {
    uint32_t inode;
    uint32_t rw_ptr;
} file_descriptor_t;



typedef struct {
    // The total length of the filename is 20 for a maximum filname length of 
    // 16 plus a period plus an extension of length 3. This decision is 
    // arbitrary. (21 is used for the null byte.)
    char filename[21];
    uint32_t inode;
} dir_entry_t;


// Declared function prototypes
void mksfs( int fresh );
int sfs_getnextfilename( char *fname );
int sfs_getfilesize( const char *path );
int sfs_fopen(char *name );
int sfs_fclose( int fileID );
int sfs_fwrite( int fileID, char *buf, int length ); 
int sfs_fread( int fileID, char *buf, int length ); 
int sfs_fseek( int fileID, int loc );
int sfs_remove( char *file );


#endif
