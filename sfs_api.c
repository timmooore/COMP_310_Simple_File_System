/*******************************************************************************
 *                                                                             *
 * AUTHOR: Timothy Moore                                                       *
 * MCGILL ID: 260587927                                                        *
 * FILE: sfs_api.c                                                             *
 *                                                                             *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "sfs_api.h"
#include "bitmap.h"


// Define the name of the disk, block size, number of blocks, number of inodes,
// and the number of blocks storing the inodes. These may be changed.
#define DISK_NAME "test_disk.disk"
#define BLOCK_SIZE 1024
#define NUM_BLOCKS 100
#define NUM_INODES 10
#define NUM_INODE_BLOCKS ( sizeof( inode_t ) * NUM_INODES/BLOCK_SIZE + 1 )
#define NO_DIR_BLKS ( sizeof( dir_entry_t ) * NUM_INODES/BLOCK_SIZE + 1 )
#define MAX_FILE_SIZE \
    ( 12 * BLOCK_SIZE + BLOCK_SIZE/sizeof( unsigned int ) * BLOCK_SIZE )
#define MAGIC_NUM 0xABCD0005
#define reset_buf(buf) { int i; for( i = 0; i < BLOCK_SIZE; i++ ) buf[i] = 0; }


// The in-memory copies of the super block, inode table and directory, as well 
// as a buffer to copy blocks with and the in-memory data structures such as the
// file descriptor table. The in-memory free bitmap was declared in the bitmap.h
// header file and is stored there. 
// An index into the in-memory directory cache is also maintained as a global
// variable for functions such as sfs_getnextfilename()
super_block_t sb;
inode_t table[NUM_INODES];
file_descriptor_t fdt[NUM_INODES - 1];
uint8_t glb_buf[BLOCK_SIZE];
dir_entry_t mem_dir[NUM_INODES - 1];
int dir_i = 0;

// This function initializes the fields of the super block with the parameters
// defined above.
void init_super_block()
{
    sb.magic_num = MAGIC_NUM;
    sb.block_size =  BLOCK_SIZE;
    sb.fs_size = BLOCK_SIZE * NUM_BLOCKS;
    sb.inode_table_len = NUM_INODE_BLOCKS;
    sb.root_dir_inode = 0;
}


// Initializes the root directory by creating its inode and copying it to the
// inode table in memory. It also allocates data blocks in the free bit map to
// the first data block pointers to have enough space to store all 
// (NUM_INODES - 1) directory entries in memory, and the rest of the pointers
// are initialized to 0;
void init_root_dir()
{
    int i;
    inode_t root;
    root.link_cnt = 1;
    root.mode = 0666;
    root.uid = 0;
    root.gid = 1;
    for ( i = 0; i < NO_DIR_BLKS; i++ ) {
        if ( i == 12 ) break;
        root.blk_ptr[i] = get_index();
    }
    // If the maximum number of files is greater than what can be stored in 12
    // blocks, indirection is used; an index is obtained for the indirect
    // pointer, a buffer of unsigned integers corresponding to block indexes is
    // initialized and filled with indices using get_index() while there are
    // still blocks required. The indirect block is then written to disk.
    if ( NO_DIR_BLKS > 12 ) {
        root.indirect = get_index();
        unsigned int buf[BLOCK_SIZE/sizeof( unsigned int )];
        while ( i < NO_DIR_BLKS ) {
            buf[i] = get_index();
            i++;
        }
        write_blocks( root.indirect, 1, buf );
    } else {
        while ( i < 12 ) { 
            root.blk_ptr[i] = 0; 
            i++; 
        }
        root.indirect = 0;
    }
    memcpy( table, &root, sizeof( inode_t ) );
    for ( i = 0; i < NUM_INODES - 1; i++ ) mem_dir[i].inode = 0;
}


// This initializes all of the link_cnt fields in the inode table to 0, as this
// will be used to check whether the inode at a specified index is still active.
void init_inode_table()
{
    int i;
    for ( i = 0; i < NUM_INODES; i++ ) table[i].link_cnt = 0;
}


// Initializes the inode field of the the file descriptor entries to 0 as this 
// field will be used in other functions to determine whether the entry is still
// active. 
void init_fdt()
{
    int i;
    for ( i = 0; i < NUM_INODES - 1; i++ ) fdt[i].inode = 0;
}


int check_filename( char* fname )
{
    int i, name_len, ext_len;
    int len = strlen( fname );

    // If the filename length is 0 or greater than 20, return 1
    if ( len > 20 || len == 0 ) {
        printf("Filename too long or too short\n");
        return 1;
    }
    i = 0;
    name_len = 0;
    while ( i < len ) {
        // Break once the period has been found
        if ( fname[i] == '.' ) {
            i++;
            break;
        }
        i++;
        name_len++;

        // The length of the filename exceeds 16
        if ( name_len > 16 ) return 1;
    }
    ext_len = 0;
    while ( i < len ) {
        i++;
        ext_len++;
        if ( ext_len > 3 ) return 1;
    }
    return 0;
}


void init_inode( inode_t *n )
{
    int i;
    n -> mode = 0666;
    n -> link_cnt = 1;
    n -> uid = 0;
    n -> gid = 1;
    n -> size = 0;
    for ( i = 0; i < 12; i++ ) n -> blk_ptr[i] = 0;
    n -> indirect = 0;
    n -> blk_ptr[0] = get_index();
}


// mksfs is used to initialize the disk emulator. If fresh is specified, a new
// disk is created using init_fresh_disk(), the super block is initialized along
// with the file descriptor table, in-memory directory cache and, inode table,
// and the super block, inode table, and free block bitmap are wrote to the disk.
// Else, a pre-existing disk is initialized using init_disk, and the super
// block, inode table, and free block bitmap are read into "memory." The free
// bitmap is stored at the end of the disk partition in the last blocks, so the 
// block or blocks it is stored in is calculated based on the number of blocks
// in the file system.
void mksfs( int fresh )
{
    int i, addr;
    if ( fresh ) {
        if ( init_fresh_disk( DISK_NAME, BLOCK_SIZE, NUM_BLOCKS ) == -1 )
            die( "Failed to initialize fresh disk.\n" );
        init_super_block();
        if ( write_blocks( 0, 1, &sb ) != 1 )
            die( "Only one block should have been written.\n" );

        // The bits for the super block, inode table and free bitmap are forced
        // to used state so that they are not accidentally allocated to a file
        force_set_index( 0 );
        for ( i = 1; i < NUM_INODE_BLOCKS + 1; i++ ) force_set_index( i );
        for ( i = 1; i < SIZE/BLOCK_SIZE + 2; i++ )
            force_set_index( NUM_BLOCKS - i );

        // Root directory and inode table are initialized before being stored on
        // disk; file descriptor table is initialized
        init_inode_table();
        init_root_dir();
        init_fdt();
        if ( write_blocks(1, NUM_INODE_BLOCKS, table ) != NUM_INODE_BLOCKS )
            die( "Incorrect number of blocks written for inode table.\n" );
        for ( i = 0; i < NO_DIR_BLKS; i++ ) {
            if ( i == 12 ) break;
            write_blocks( table[sb.root_dir_inode].blk_ptr[i], 
                          1, mem_dir + BLOCK_SIZE * i );
        }
        if ( NO_DIR_BLKS > 12 ) {
            unsigned int buf[BLOCK_SIZE/sizeof( unsigned int )];
            read_blocks( table[sb.root_dir_inode].indirect, 1, buf );
            while ( i < NO_DIR_BLKS ) {
                write_blocks( buf[i], 1, mem_dir + BLOCK_SIZE * i );
                i++;
            }
        }
        addr = NUM_BLOCKS - (SIZE/BLOCK_SIZE + 1);
        if ( write_blocks( addr, SIZE/BLOCK_SIZE + 1, free_bit_map ) != 
             SIZE/BLOCK_SIZE + 1 )
            die( "Incorrect number of blocks written from free bitmap.\n" );
    } else {
        if ( init_disk( DISK_NAME, BLOCK_SIZE, NUM_BLOCKS ) == -1 )
            die( "Failed to initialize pre-existing disk.\n" );
        if( read_blocks( 0, 1, &sb ) != 1 )
            die( "Failed to read super block from disk" );
        if ( read_blocks( 1, sb.inode_table_len, table ) != NUM_INODE_BLOCKS )
            die( "Incorrect number of blocks read to inode table" );
        addr = NUM_BLOCKS - (SIZE/sb.block_size + 1 );
        if ( read_blocks( addr, SIZE/sb.block_size + 1, free_bit_map ) != 
             SIZE/sb.block_size + 1)
            die( "Incorrect number of blocks read to free bitmap.\n" );

        // Read each block pointed to by the block pointers of the root
        // directory inode into memory. 
        i = 0;
        for ( i = 0; i < NO_DIR_BLKS; i++ ) {
            if ( i == 12 ) break;
            read_blocks( table[sb.root_dir_inode].blk_ptr[i], 
                         1, mem_dir + i * BLOCK_SIZE );
        }
        if ( NO_DIR_BLKS > 12 ) {
            unsigned int buf[BLOCK_SIZE/sizeof( unsigned int )];
            read_blocks( table[sb.root_dir_inode].indirect, 1, buf );
            while ( i < NO_DIR_BLKS ) {
                read_blocks( buf[i], 1, mem_dir + BLOCK_SIZE * i );
                i++;
            }
        }
        // Initialize the file descriptor table.
        init_fdt();
    }
}


// To get the next filename and remember the current position in the directory, 
// a global variable, dir_i (declared above), is initialized to 0 and 
// maintained. On each call of sfs_getnextfilename(), the filename stored at the
// index dir_i in the in-memory directory cache is copied to the return string
// and dir_i is incremented and returned. If dir_i is equal to the length of the
// directory table, it is reset to 0 and 0 is returned, indicating that all 
// files have been read. Thus, it is assumed that on the first call of
// sfs_getnextfilename(), dir_i is equal to 0, which is actively maintained
// before calls to sfs_getnextfilename() are made.
int sfs_getnextfilename( char *fname ) 
{
    int *index = &dir_i;
    strcpy( fname, mem_dir[*index].filename );
    ( *index )++;
    if ( *index == NUM_INODES - 1 ) *index = 0;
    return *index; 
}


// sfs_getfilesize is simple to implement; if the file exists in the directory
// cache, find its file size from its inode. If it doesn't exist, return 0.
int sfs_getfilesize( const char *fname )
{
    char name[21];
    dir_i = 0;
    while ( sfs_getnextfilename( name ) ) {

        if ( strcmp( name, fname ) == 0 ) {
            int inode_i = mem_dir[dir_i].inode;
            return table[inode_i].size;
        }
        dir_i++;
    }
    return 0;
}

// First, the in-memory directory index is scanned through using 
// sfs_getnextfilename(), and the filenames returned are compared with fname to 
// determine if the file already exists. If it does, the loop is broken and
// dir_i stores the index in the in-memory directory mapping where the inode
// index of the requested file is stored, so this index is saved into inode_i.
// The file descriptor table is then looped over to search for an empty or
// inactive entry by checking whether the inode field of the fdt is equal to 0,
// and the inode index of the file is then stored there. The read/write pointer
// of the file is then initialized in append mode, so it is set to the size
// field of the file in its inode. 
//
// If the file doesn't exist, an inode for the file must be created and stored
// in the inode table in-memory and then written to disk. A directory entry for
// the file must also be created and then written to disk. Error checking must
// also be performed to ensure that the length of the filename and extension are
// valid. A fresh data block must be allocated, and the modified free bitmap
// must also be written to disk

// @return the fileID of the file that was opened, or -1 on failure.
int sfs_fopen( char *fname ) 
{
    int i;
    char name[21];
    dir_i = 0;
    if (check_filename( fname ) ) {
            perror( "Filename is incorrectly formatted.\n" );
            return -1;
        }
    while( sfs_getnextfilename( name ) ) if (strcmp( fname, name ) == 0 ) break;
    if ( dir_i ) {
        for ( i = 0; i < NUM_INODES - 1; i++ ) {
            if ( mem_dir[dir_i].inode == fdt[i].inode ) return -1;
        }
        int inode_i = mem_dir[dir_i].inode;
        for ( i = 1; i < NUM_INODES; i++ ) {
            if ( fdt[i].inode == 0 ) {
                fdt[i].inode = inode_i;
                fdt[i].rw_ptr = table[inode_i].size;
                return i;
            }
            if ( i == NUM_INODES - 1 ) {
                perror( "Inode table full" );
                return -1;
            }
        }
    } else {
        // Loop through inodes in table to find one that is no longer in use
        // (link_cnt == 0), and if one is found loop through the file
        // descriptor table to find an entry that is not in use (inode = 0).
        // Initialize a new inode, copy it to the inode table at the valid entry
        // save the index of the inode entry into the fdt table and set the 
        // read/write pointer to 0. Return the index of the file in the file
        // descriptor table.
        // TODO: Make a directory entry for the file.
        for ( i = 1; i < NUM_INODES; i++ ) {
            if ( table[i].link_cnt == 0 ) {
                int j, k, addr;
                for ( j = 0; j < NUM_INODES - 1; j++ ) {
                    if ( fdt[j].inode == 0 ) {
                        for ( k = 0; k < NUM_INODES - 1; k++ ) {
                            if ( mem_dir[k].inode == 0 ) {
                                inode_t node;
                                init_inode( &node );
                                memcpy( &table[i], &node, sizeof( inode_t ) );
                                fdt[j].inode = i;
                                fdt[j].rw_ptr = 0;
                                mem_dir[k].inode = i;
                                strcpy( mem_dir[k].filename, fname );

                                // DEBUGGING
                                printf( "%s\n", mem_dir[k].filename );

                                // Write the inode table and modified bitmap to 
                                // disk
                                write_blocks(1, NUM_INODE_BLOCKS, table );
                                addr = NUM_BLOCKS - (SIZE/BLOCK_SIZE + 1);
                                write_blocks( addr, SIZE/BLOCK_SIZE + 1, 
                                              free_bit_map );

                                // Write the block of the directory that
                                // was modified if an empty directory slot was
                                // found. This requires determining whether or 
                                // not the indirection pointer is required.
                                if ( k/BLOCK_SIZE > 11 ) {
                                    unsigned int buf[BLOCK_SIZE/sizeof( unsigned int )];
                                    read_blocks( table[sb.root_dir_inode].indirect,
                                                 1, buf );
                                    addr = buf[k % BLOCK_SIZE];
                                    write_blocks( addr, 1, 
                                                  mem_dir + 
                                                  ( k/BLOCK_SIZE ) * 
                                                  BLOCK_SIZE );
                                }
                                else {
                                    addr = 
                                    table[sb.root_dir_inode].blk_ptr[k/BLOCK_SIZE];
                                    write_blocks( addr, 1, 
                                                  mem_dir + 
                                                  ( k/BLOCK_SIZE ) * 
                                                  BLOCK_SIZE );
                                }
                                return j;
                            }
                        }
                    }
                }
            }
        }
    }
    return -1;
}


// To close a file, the entry in the file descriptor table must be reset. Error
// checking must also be performed to ensure that a fileID that has already been
// closed isn't closed again. The inode number is the parameter being used to 
// determine whether the file descriptor is already closed. 
// @return 0 upon success or -1 on failure.
int sfs_fclose( int fileID )
{
    if ( fdt[fileID].inode == 0 ) {
        perror( "Cannot close an fileID that is already closed.\n" );
        return -1;
    } else {
        fdt[fileID].inode = 0;
        fdt[fileID].rw_ptr = 0;
    }
    return 0;
}


// The implementation that I have chosen does not assume that blocks are
// allocated contiguously for a file, so I can only use the function
// write_blocks() to write a single block at a time. The increase in the size of 
// the file must be calculated carefully because it is not assumed that the
// read/write pointer points to the end of the file. Thus, the new file size
// should be rw_ptr + length, and not size + length. If the rw_ptr is at the end
// of the file, it will be equal to size and the file size will just be
// increased by length. 
// Error checking must be done before writing: whether the fileID is valid must
// be checked; shouldn't write to a closed file. It must also be checked that
// the number of bytes written will not exceed the maximum file size.
// It is assumed that the block pointers in the inode are stored in the order of 
// the file, so the read/write pointer determines which block will be written to
// first. That block is loaded into a buffer, and the first portion of bytes to
// be written are used to fill the block buffer from the read/write pointer to 
// the end of the block, and then this block is written back onto the disk.
// Next, subsequent blocks are first checked to see if they are allocated, and
// allocated if not, and bytes are written from buf until the last 1024 bytes
// are reached. Once this happens, the final block is read into the block
// buffer, the remaining bytes are copied, and this buffer is written back onto
// the disk; this is to ensure that the end of a file isn't lost if the writing
// is being done to the middle of a file. 
// Finally, The modified inode table
// EDIT: The first block to be written only needs to be read if the read/write
// pointer points to somewhere in the middle of the block. If it is at the
// beginning of a block, i.e. (rw_ptr % BLOCK_SIZE == 0), then this step can be
// skipped. 
int sfs_fwrite( int fileID, char *buf, int length )
{
    int i, blk_no, buf_i;
    unsigned int rw_ptr;
    uint8_t blk_buf[BLOCK_SIZE];
    if ( fdt[fileID].inode == 0 ) {
        perror( "Cannot write to a close file.\n" );
        return -1;
    } 
    file_descriptor_t *fd = &fdt[fileID];
    inode_t *n = &table[fd -> inode];
    rw_ptr = fd -> rw_ptr;
    if ( rw_ptr + length > MAX_FILE_SIZE ) {
        perror( "Buffer to write will exceed maximum file size.\n" );
        return -1;
    }
    blk_no = rw_ptr/BLOCK_SIZE;
    buf_i = 0;

    // Check if the first block needs to be partially written based on the 
    // modulo of the rw_ptr and if so update that blocks
    if ( rw_ptr % BLOCK_SIZE != 0 ) {
        if ( n -> blk_ptr[blk_no] == 0 ) n -> blk_ptr[blk_no] = get_index();
        read_blocks( n -> blk_ptr[blk_no], 1, blk_buf );
        for ( i = rw_ptr % BLOCK_SIZE; i < BLOCK_SIZE; i++ ) {
            blk_buf[i] = buf[buf_i];
            buf_i++;
        }
        write_blocks( n -> blk_ptr[blk_no], 1, blk_buf );
        rw_ptr += buf_i;
        blk_no++;
    }
    // While the rest of the buf hasn't been written, write the blocks pointed
    // to by the twelve direct pointers.
    while ( buf_i < length - BLOCK_SIZE ) {
        if (blk_no >= 12 ) break;
        if ( n -> blk_ptr[blk_no] == 0 ) n -> blk_ptr[blk_no] = get_index();
        write_blocks( n-> blk_ptr[blk_no], 1, buf + buf_i );
        buf_i += BLOCK_SIZE;
        rw_ptr += BLOCK_SIZE;
        blk_no++;
    }
    // Once the loop has terminated, either the last bytes to be written have 
    // been reached and the final block has to be partially updated, or the 
    // blocks pointed to by the indirect pointer need to start being written to.
    if ( blk_no < 11 ) {
        if ( n -> blk_ptr[blk_no] == 0 ) n -> blk_ptr[blk_no] = get_index();
        read_blocks( n -> blk_ptr[blk_no], 1, blk_buf );
        int max = length - buf_i;
        for ( i = 0; i < max; i++ ) {
            blk_buf[i] = buf[buf_i];
            buf_i++;
            rw_ptr++;
        }
        write_blocks( n -> blk_ptr[blk_no], 1, blk_buf );
    } else {
        // Initialize the indirect pointer if this hasn't already been done and
        // write the block with 0's.
        if ( n -> indirect == 0 ) {
            n -> indirect = get_index();
            reset_buf( glb_buf );
            write_blocks( n -> indirect, 1, glb_buf );
        }
        // Read the block indices into the new buffer
        unsigned int blk_indices[BLOCK_SIZE/sizeof( unsigned int )];
        blk_no = 0;
        read_blocks( n -> indirect, 1, blk_indices );
        // Check if the first block needs to be partially written based on the 
        // modulo of the rw_ptr and if so update that block, as before
        if ( rw_ptr % BLOCK_SIZE != 0 ) {
            if ( blk_indices[blk_no] == 0 ) blk_indices[blk_no] = get_index();
            read_blocks( blk_indices[blk_no], 1, blk_buf );
            for ( i = rw_ptr % BLOCK_SIZE; i < BLOCK_SIZE; i++ ) {
                blk_buf[i] = buf[buf_i];
                buf_i++;
            }
        write_blocks( blk_indices[blk_no], 1, blk_buf );
        rw_ptr += buf_i;
        blk_no++;
        }
        // Continue writing blocks from the buffer
        while ( buf_i < length - BLOCK_SIZE ) {
            if ( blk_indices[blk_no] == 0 ) blk_indices[blk_no] = get_index();
            write_blocks( blk_indices[blk_no], 1, buf + buf_i );
            buf_i += BLOCK_SIZE;
            rw_ptr += BLOCK_SIZE;
            blk_no++;
        }
        if ( blk_indices[blk_no] == 0 ) blk_indices[blk_no] = get_index();
        read_blocks( blk_indices[blk_no], 1, blk_buf );
        int max = length = buf_i;
        for ( i = 0; i < max; i++ ) {
            blk_buf[i] = buf[buf_i];
            buf_i++;
            rw_ptr++;
        }
        write_blocks( blk_indices[blk_no], 1, blk_buf );
        write_blocks( n -> indirect, 1, blk_indices );
    }
    if ( n -> size < rw_ptr ) n -> size = rw_ptr;
    fdt -> rw_ptr = rw_ptr;

    // Write the inode table and modified bitmap to disk
    write_blocks(1, NUM_INODE_BLOCKS, table );
    int addr = NUM_BLOCKS - (SIZE/BLOCK_SIZE + 1);
    write_blocks( addr, SIZE/BLOCK_SIZE + 1, free_bit_map );
    return buf_i;
}


// sfs_fread() follows the same kind of structure as sfs_fwrite() but is simpler
// because no writing needs to be done on the disk, only reading the specified
// blocks. 
// Error checking must be done to prevent reading from invalid or closed file 
// handles. Error checking must also be done to ensure that reading is not being
// done past the end of the file. 
// First, it is determined whether the read/write pointer points to the middle
// of a block using modulo, and if so, the block is read to a temporary buffer
// and a segment of that block is copied to buf. Then, successive blocks are
// read and copied to buf until either the last block has been reached or all of
// the blocks pointed to by the direct pointers have been read, in which case
// the block indices pointed to by the indirect pointer are loaded and the same
// procedure is applied to reading these blocks until the last block is reached.
// Once the last block is reached, it is read and the last segment of bytes are
// copied to buf. the read/write pointer is then updated in the file descriptor
// table and the number of bytes copied is returned.
// @return the number of bytes read to buf on success, -1 on failure.
int sfs_fread( int fileID, char *buf, int length )
{
    int i, blk_no, buf_i;
    unsigned int rw_ptr;
    uint8_t blk_buf[BLOCK_SIZE];
    // Check whether the file handle is closed and whether an attempt to read
    // past the end of the file is made
    if ( fdt[fileID].inode == 0 ) {
        perror( "Cannot read from a closed or invalid file handle.\n" );
        return -1;
    }
    file_descriptor_t *fd = &fdt[fileID];
    inode_t *n = &table[fd -> inode];
    rw_ptr = fd -> rw_ptr;
    if ( rw_ptr + length > n -> size ) {
        perror( "Cannot read past the end of the file.\n" );
        return -1;
    }
    // Initialize the index into buf and the block number and, if the read/write
    // pointer points to the middle of that block, load it and copy the segment
    buf_i = 0;
    blk_no = rw_ptr/BLOCK_SIZE;
    if ( (i = rw_ptr % BLOCK_SIZE ) != 0 ) {
        read_blocks( n -> blk_ptr[blk_no], 1, blk_buf );
        i = rw_ptr % BLOCK_SIZE;
        while ( i < BLOCK_SIZE && buf_i < length ) {
            buf[buf_i] = blk_buf[i];
            i++;
            buf_i++;
            rw_ptr++;
        }
        blk_no++;
    }
    // Loop over the rest of the blocks pointed to by the direct pointers until
    // the last block has been reached or the indirect blocks are required.
    while ( buf_i < length - BLOCK_SIZE ) {
        if ( blk_no >= 12 ) break;
        read_blocks( n -> blk_ptr[blk_no], 1, buf + buf_i );
        buf_i += BLOCK_SIZE;
        rw_ptr += BLOCK_SIZE;
        blk_no++;
    }
    // If the indirect blocks are required, load the indirect block into
    // blk_indices and copy blocks as above.
    if ( blk_no > 11 ) {
        unsigned int blk_indices[BLOCK_SIZE/sizeof( unsigned int )];
        read_blocks( n -> indirect, 1, blk_indices );
        blk_no = 0;
        if ( ( i = rw_ptr % BLOCK_SIZE ) != 0 ) {
            read_blocks( blk_indices[blk_no], 1, blk_buf );
            i = rw_ptr % BLOCK_SIZE;
            while ( i < BLOCK_SIZE && buf_i < length ) {
                buf[buf_i] = blk_buf[i];
                i++;
                buf_i++;
                rw_ptr++;
            }
            blk_no++;
        }
        while ( buf_i < length - BLOCK_SIZE ) {
            read_blocks( blk_indices[blk_no], 1, buf + buf_i );
            buf_i += BLOCK_SIZE;
            rw_ptr+= BLOCK_SIZE;
            blk_no++;
        }
        read_blocks( blk_indices[blk_no], 1, blk_buf );
        int max = length - buf_i;
        for ( i = 0; i < max; i++ ) {
            buf[buf_i] = blk_buf[i];
            buf_i++;
            rw_ptr++;
        }
    // Load the final block and copy the final bytes from it.
    } else {
        read_blocks( n -> blk_ptr[blk_no], 1, blk_buf );
        int max = length - buf_i;
        for ( i = 0; i < max; i++ ) {
            buf[buf_i] = blk_buf[i];
            buf_i++;
            rw_ptr++;
        }
    }
    // Update the read/write pointer and return the number of bytes read.
    fd -> rw_ptr = rw_ptr;
    return buf_i;
}


// To implement sfs_fseek(), the read/write pointer in the file descriptor table
// simply needs to be updated. However, error checking must be done to ensure
// that the file handle provided is valid and that the location being seeked is
// valid.
int sfs_fseek( int fileID, int loc )
{
    if ( fdt[fileID].inode == 0 ) {
        perror( "Cannot seek on a closed or invalid file handle.\n" );
        return -1;
    }
    file_descriptor_t *fd = &fdt[fileID];
    inode_t *n = &table[fd -> inode];
    if ( loc < 0 || loc > n -> size ) {
        perror( "The location requested is either negative or past the end of file.\n" );
        return -1;
    }
    fd -> rw_ptr = loc;
    return 0;
}


// To remove a file, all of the allocated blocks in the free bitmap must be
// deallocated. If the indirect pointer is also allocated, then the indirect
// block must be loaded and loop through, deallocating any blocks there. The
// indirect pointer itself is then deallocated. The fields in the file's inode
// are then all set to 0, and the inode in the in-memory directory map is set to
// 0, and the first character of the filename is set to null, '\0', so that the
// filename can no longer be looked up. The free bitmap, inode table, and
// modified directory entry are then written to disk. 
// Error checking is done to see if the file exists in the first place.
int sfs_remove( char *fname )
{
    int i;
    char name[21];
    dir_i = 0;
    while( sfs_getnextfilename( name ) ) {
        if (strcmp( name, fname ) == 0 ) break;
    }
    if ( dir_i == 0 ) {
        perror( "The filename specified does not exist.\n" );
        return -1;
    }
    int inode_i = mem_dir[dir_i].inode;
    inode_t *n = &table[inode_i];
    for ( i = 0; i < 12; i++ )
        if ( n -> blk_ptr[i] != 0 ) {
            rm_index( n -> blk_ptr[i] );
            n -> blk_ptr[i] = 0;
        }
    if ( n -> indirect != 0 ) {
        unsigned int blk_indices[BLOCK_SIZE/sizeof( unsigned int )];
        read_blocks( n -> indirect, 1, blk_indices );
        for ( i = 0; i < BLOCK_SIZE/sizeof( unsigned int ); i++ )
            if ( blk_indices[i] != 0 ) rm_index( blk_indices[i] ); 
        rm_index( n -> indirect );
        n -> indirect = 0;
    }
    n -> mode = 0;
    n -> link_cnt = 0;
    n -> uid = 0;
    n -> gid = 0;
    n -> size = 0;
    mem_dir[dir_i].inode = 0;
    mem_dir[dir_i].filename[0] = '\0';
    write_blocks(1, NUM_INODE_BLOCKS, table );
    int addr = NUM_BLOCKS - (SIZE/BLOCK_SIZE + 1);
    write_blocks( addr, SIZE/BLOCK_SIZE + 1, free_bit_map );
    if ( dir_i/BLOCK_SIZE > 11 ) {
        unsigned int blk_indices[BLOCK_SIZE/sizeof( unsigned int )];
        read_blocks( table[sb.root_dir_inode].indirect, 1, blk_indices );
        addr = blk_indices[dir_i % BLOCK_SIZE];
        write_blocks( addr, 1, mem_dir + ( dir_i/BLOCK_SIZE ) * BLOCK_SIZE );
    } else {
        addr = table[sb.root_dir_inode].blk_ptr[dir_i/BLOCK_SIZE];
        write_blocks( addr, 1, mem_dir + ( dir_i/BLOCK_SIZE ) * BLOCK_SIZE );
    }
    return 0;
}





