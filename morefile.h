/*
 * Copyright (c) 1995 by Salvatore Valente <svalente@mit.edu>
 *
 * Redistribution and use in source and binary forms are permitted.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
/*
 * morefile.h -- Functions to implement the "mfile" data type,
 * which allows you to rewind an otherwise unrewindable file.
 *
 * What's important here is to make Getc() as efficient as possible.
 * It's called repeatedly in an extremely tight loop.
 * Any cycle that can be shaved off of the running time of Getc()
 * makes the program significantly more efficient.
 */

#ifndef MOREFILE_H_INCLUDED
#define MOREFILE_H_INCLUDED

#define AREA_SIZE 32768

struct area {
    char buf[AREA_SIZE];
    int size;
    struct area *next;
};

struct mfile {
    FILE *fp;
    struct area *contents;
    struct area *cur_area;
    int cur_area_idx;
    int file_pos;
};

#if !defined(__GNUC__) && !defined(inline)
#define inline
#endif

extern struct mfile *Fopen (char *filename);
extern struct mfile *Fopen_stdin (void);

static inline off_t Ftell (struct mfile *mp)
{
    return (mp->file_pos);
}

extern int Fseek (struct mfile *mp, size_t off);

/*  These functions should only be called through Getc() and Ungetc(). */
extern int Getc_contents (struct mfile *mp);
extern int Ungetc_contents (int c, struct mfile *mp);

static inline int Getc (struct mfile *mp)
{
    mp->file_pos++;
    return (mp->contents != NULL ? Getc_contents (mp) : getc (mp->fp));
}

static inline int Ungetc (int c, struct mfile *mp)
{
    mp->file_pos--;
    return (mp->contents != NULL ? Ungetc_contents (c, mp) : ungetc (c, mp->fp));
}

extern void Fclose (struct mfile *mp);

#endif /* MOREFILE_H_INCLUDED */
