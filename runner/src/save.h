/* Story progress on memory card slot 1, file bu00:UUUVVV-RUNNER.
 *
 * SAVE_VERSION is the record layout. This build reads that version only.
 * An older file is replaced the next time progress is stored. A newer
 * file stays on the card, unread and unwritten.
 *
 * DuckStation stores the card itself. The browser build writes the same
 * file into the emulated card; the page keeps that card image. */
#pragma once

#define SAVE_VERSION 1

void save_init(void);
/* 1 once the card has been probed. *reach is 0 when this version has
 * nothing to resume. */
int save_poll(int *reach);
void save_store(int reach);
/* 0 until a write has finished this session, then 1–255. The web row
 * repeats it so the page can flush the emulated card. */
int save_generation(void);
