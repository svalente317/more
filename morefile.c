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
 * morefile.c -- Functions to implement the "mfile" data type,
 * which allows you to rewind an otherwise unrewindable file.
 *
 * You can only read from an mfile one character at a time.
 * Using Getc() and Ungetc() required almost no modifications to more.c,
 * since it already used them for everything.
 *
 * When writing the mfile data type, I considered using a temporary file
 * to store the data that is read. But I chose to use memory instead,
 * because I consider it unlikely that the user will pipe too much data.
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "morefile.h"

static struct area *new_area (void);

struct mfile *Fopen (char *filename)
{
    struct mfile *mp;
    FILE *fp;

    fp = fopen (filename, "r");
    if (! fp)
        return (NULL);
    mp = malloc (sizeof (struct mfile));
    mp->fp = fp;
    mp->contents = mp->cur_area = NULL;
    mp->cur_area_idx = 0;
    mp->file_pos = 0;
    return (mp);
}

struct mfile *Fopen_stdin (void)
{
    struct mfile *mp;

    mp = malloc (sizeof (struct mfile));
    mp->fp = stdin;
    mp->contents = mp->cur_area = new_area ();
    mp->cur_area_idx = 0;
    mp->file_pos = 0;
    return (mp);
}

int Fseek (struct mfile *mp, size_t off)
{
    int size;

    /*
     *  Quickly:  If not saving the contents, just do the fseek.
     */
    if (! mp->contents) {
        mp->file_pos = off;
        return (fseek (mp->fp, off, SEEK_SET));
    }
    /*
     *  Reset all pointers to the beginning of the contents.
     */
    mp->cur_area = mp->contents;
    mp->cur_area_idx = mp->file_pos = 0;
    /*
     *  Loop through the content areas until we find the offset.
     */
    while (off > 0) {
        size = mp->cur_area->size;
        if (off <= size) {
            mp->cur_area_idx = off;
            mp->file_pos += off;
            off = 0;
            break;
        }
        /*
         *  Advance the file position past the entire area.
         */
        mp->cur_area_idx = size;
        mp->file_pos += size;
        off -= size;
        /*
         *  Go to the next area.
         */
        if (! mp->cur_area->next)
            break;
        mp->cur_area = mp->cur_area->next;
        mp->cur_area_idx = 0;
    }
    /*
     *  Check if the offset was inside the known contents.
     *  If not, then skip forward?
     */
    if (off > 0)
        return (-1);
    return (0);
}

int Getc_contents (struct mfile *mp)
{
    int c;

    if (mp->cur_area_idx < mp->cur_area->size) {
        /*
         *  This character was already read from stdin. Return it.
         */
        c = mp->cur_area->buf[mp->cur_area_idx];
        mp->cur_area_idx++;
        /*
         *  Handle the case where we've reached the end of the current area
         *  but have not reached the end of the string of areas.
         */
        if ((mp->cur_area_idx == mp->cur_area->size) &&
            (mp->cur_area->next != NULL)) {
            mp->cur_area = mp->cur_area->next;
            mp->cur_area_idx = 0;
        }
    } else {
        /*
         *  Read from stdin and store it in memory.
         */
        c = getc (mp->fp);
        if (mp->cur_area_idx == AREA_SIZE) {
            mp->cur_area->next = new_area ();
            mp->cur_area = mp->cur_area->next;
            mp->cur_area_idx = 0;
        }
        mp->cur_area->buf[mp->cur_area_idx] = c;
        mp->cur_area->size++;
        mp->cur_area_idx++;
    }
    return (c);
}

int Ungetc_contents (int c, struct mfile *mp)
{
    struct area *ap;

    if (mp->cur_area_idx == 0) {
        if (mp->cur_area == mp->contents)
            return (-1);
        /*
         *  Move backwards on a singly-linked list.
         */
        for (ap = mp->contents; ap->next != mp->cur_area; ap = ap->next);
        mp->cur_area = ap;
        mp->cur_area_idx = ap->size;
    }
    mp->cur_area_idx--;
    mp->cur_area->buf[mp->cur_area_idx] = c;
    return (c);
}

void Fclose (struct mfile *mp)
{
    struct area *ap, *next;

    fclose (mp->fp);
    for (ap = mp->contents; ap != NULL; ap = next) {
        next = ap->next;
        free (ap);
    }
    free (mp);
}

static struct area *new_area (void)
{
    struct area *ap;

    ap = malloc (sizeof (struct area));
    ap->size = 0;
    ap->next = NULL;
    return (ap);
}
