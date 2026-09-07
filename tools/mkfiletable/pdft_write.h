/**
 * PDFT encoder.
 *
 * Structs in, bytes out. No file I/O, no JSON, no policy — everything that
 * needs a filesystem, a ROM or an allocation decision belongs to the caller.
 * Kept that way so the port can link this if the console-mod importer is ever
 * to finish a filetable in process, without a second implementation.
 *
 * The format is whatever romdataParseFileTable() in port/src/romdata.c
 * accepts; see filetable-writer-plan.md for the layout and for the reader
 * behaviour that constrains it.
 */
#ifndef _IN_PDFT_WRITE_H
#define _IN_PDFT_WRITE_H

#include <stdint.h>
#include <stdbool.h>

#define PDFT_MAX_FILES      8192  /* ROMDATA_MAX_FILES; the reader skips above this */
#define PDFT_MAX_ROMSOURCES 8     /* ROMSOURCES_MAX; the reader drops the rest */
#define PDFT_ROMSOURCE_ID   16    /* char id[16] in the reader, terminator included */
#define PDFT_ROMSOURCE_FILE 64    /* char filename[64], likewise */

enum pdftFallback {
	PDFT_FALLBACK_SKIP = 0,
	PDFT_FALLBACK_VANILLA = 1,
	PDFT_FALLBACK_ERROR = 2,
};

struct pdftRomSource {
	const char *id;
	const char *filename;
	uint32_t expectedSize;
	bool required;
	bool strict;
	uint8_t fallback;
};

struct pdftAlt {
	int32_t romIdx;       /* index into the sources array; < 0 for none */
	uint32_t offset;
	uint32_t size;
	uint8_t compression;  /* the loader implements 0 only */
};

struct pdftFile {
	uint32_t id;
	const char *name;     /* required, non-empty */
	const char *path;     /* NULL or "" for none */
	struct pdftAlt alt;
	bool romResident;     /* flag 0x1: bind g_RomFile + offset. Nothing sets this today */
	uint32_t offset;
	uint32_t size;
};

struct pdftTexMap {
	uint16_t localTexId;
	uint16_t slotIdx;     /* must be dense 0..count-1, see pdftWrite() */
};

struct pdftInput {
	const struct pdftFile *files;
	uint32_t numFiles;
	const struct pdftRomSource *sources;
	uint32_t numSources;
	const struct pdftTexMap *texmap;
	uint32_t numTexMap;
};

/**
 * Encodes a filetable. Returns a malloc'd buffer and its length, or NULL with
 * the reason in err.
 *
 * The version is chosen by content, not declared: 3 with a texmap, else 2 with
 * romSources or any alt-ROM entry, else 1.
 *
 * Refuses what the reader would mis-handle rather than what it would reject,
 * because most of what it mis-handles it accepts silently: an id at or above
 * PDFT_MAX_FILES (skipped without a word), more than PDFT_MAX_ROMSOURCES
 * (dropped with a warning), a name or path longer than a u16 can count, an id
 * or filename too long for the reader's fixed buffers, a compression mode the
 * loader does not implement, and texmap slot indices with holes in them — the
 * reader advances its global port base by the entry COUNT while mapping by
 * slot index, so a hole makes the next mod's texture ports overlap this one's.
 */
uint8_t *pdftWrite(const struct pdftInput *in, uint32_t *outLen, char *err, uint32_t errLen);

/** The version pdftWrite() would choose for this input. */
uint32_t pdftVersionFor(const struct pdftInput *in);

#endif
