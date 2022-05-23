#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sfs_api.h"


int main( int argc, char* argv[] )
{
    mksfs( 1 );
    char *fname = "example.txt";
    int fd = sfs_fopen( fname );
    char *test_str = "The quick brown fox jumps over the lazy dog.\n";
    int bytes = sfs_fwrite( fd, test_str, strlen( test_str) );
    printf( "No of bytes written: %d\n", bytes );
    sfs_fseek( fd, 10 );
    char test_str2[46];
    test_str2[45] = '\0';
    bytes = sfs_fread( fd, test_str2, strlen( test_str ) - 10 );
    printf( "No. of bytes read: %d\n%s", bytes, test_str2 );
    return EXIT_SUCCESS;
}