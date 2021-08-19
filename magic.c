
#include <stdio.h>

/*
 * magic --
 *  check for file magic numbers.  This code would best be shared with
 *  the file(1) program or, perhaps, more should not try and be so smart?
 */
int
magic(FILE *f, char *fs)
{
    unsigned char bytes[4];
    int bad;

    bad = 0;
    if (fread(bytes, 1, 4, f) == 4) {
        if (bytes[3] == 0xFE && bytes[2] == 0xED && bytes[1] == 0xFA) {
            bad = bytes[0] == 0xCE || bytes[0] == 0xCF;
        }
        if (bytes[0] == 0177 && bytes[1] == 'E' && bytes[2] == 'L' &&
            bytes[3] == 'F') {
            bad = 1;
        }
    }
    if (bad) {
        printf("\n******** %s: Not a text file ********\n\n", fs);
        fclose(f);
        return(1);
    }
    fseek(f, 0L, SEEK_SET);
    return(0);
}
