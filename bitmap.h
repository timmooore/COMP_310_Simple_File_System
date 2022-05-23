#ifndef _INCLUDE_BITMAP_H_
#define _INCLUDE_BITMAP_H_

#include <stdint.h>

#define NUM_BLOCKS 100

/*
 * @short force an index to be set.
 * @long Use this to setup your superblock, inode table and free bit map
 *       This has been left unimplemented. You should fill it out.
 *
 * @param index index to set 
 *
 */
void force_set_index(uint32_t index);

/*
 * @short find the first free data block
 * @return index of data block to use
 */
uint32_t get_index();

/*
 * @short frees an index
 * @param index the index to free
 */
void rm_index(uint32_t index);


// free bitmap for OS file systems assignment


#include <strings.h>    // for `ffs`

/* constants */
// how far to loop in array
// Actually need NUM_BLOCKS/8 + 1
#define SIZE (NUM_BLOCKS/8 + 1)

/* globals */
// the actual data. initialize all bits to high
uint8_t free_bit_map[SIZE] = { [0 ... SIZE-1] = UINT8_MAX };

/* macros */
#define FREE_BIT(_data, _which_bit) \
    _data = _data | (1 << _which_bit)

#define USE_BIT(_data, _which_bit) \
    _data = _data & ~(1 << _which_bit)

void force_set_index(uint32_t index) {
    // TODO
    // Used to force indicies for superblock and others
    uint32_t i = index/8;
    uint8_t bit = index % 8;
    USE_BIT( free_bit_map[i], bit );
}


uint32_t get_index() {
    uint32_t i = 0;

    // find the first section with a free bit
    // let's ignore overflow for now...
    // I edited this to check for overflow
    while (free_bit_map[i] == 0 && i < SIZE ) { i++; }

    // now, find the first free bit
    // ffs has the lsb as 1, not 0. So we need to subtract
    uint8_t bit = ffs(free_bit_map[i]) - 1;

    // set the bit to used
    USE_BIT(free_bit_map[i], bit);

    //return which bit we used
    return i*8 + bit;
}

void rm_index(uint32_t index) {

    // get index in array of which bit to free
    uint32_t i = index / 8;

    // get which bit to free
    uint8_t bit = index % 8;

    // free bit
    FREE_BIT(free_bit_map[i], bit);
}


#endif //_INCLUDE_BITMAP_H_


