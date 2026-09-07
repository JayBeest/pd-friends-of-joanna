/**
 * mkfiletable — build a mod's filetable.dat from its JSON manifest.
 *
 * Replaces `pdt build-mod-filetable`. Humans write and maintain the JSON; this
 * writes the binary, and the .dat is always generated, never edited.
 *
 *   mkfiletable <mod-name> [--workspace <dir>] [--output <dir>]
 *
 * Reads <workspace>/<mod-name>_filetable.json and, if present,
 * <workspace>/<mod-name>_texmap.json; writes <output>/filetable.dat and
 * rewrites the texmap.
 *
 * The encoding itself is pdft_write.c. This file owns everything that needs a
 * filesystem or a decision: reading the manifest, allocating ids, and keeping
 * texture port slots stable across rebuilds.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include "pdft_write.h"
#include "vendor/parson/parson.h"

#define VANILLA_MAX_ID   2017  /* the first mod-local id is the one after this */
#define FIRST_LOCAL_ID   2018

/* Composed paths are checked, not assumed: a truncated path would write the
 * filetable somewhere other than where it was asked to. */
#define PATHMAX 4096
#define JOIN(dst, ...) do { \
		if (snprintf((dst), sizeof(dst), __VA_ARGS__) >= (int)sizeof(dst)) { \
			die("path is longer than %d bytes", (int)sizeof(dst)); \
		} \
	} while (0)

struct texEntry {
	uint32_t localTexId;
	uint32_t slotIdx;
};

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "mkfiletable: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

/**
 * A manifest number may be a JSON number or a string like "0x0db6", because
 * the ids people write by hand are hex. Mirrors int(x, 0).
 */
static bool numberOf(const JSON_Value *v, long *out)
{
	if (!v) {
		return false;
	}

	if (json_value_get_type(v) == JSONNumber) {
		*out = (long)json_value_get_number(v);
		return true;
	}

	if (json_value_get_type(v) == JSONString) {
		const char *s = json_value_get_string(v);
		char *end = NULL;
		long n;

		if (!s || !s[0]) {
			return false;
		}

		n = strtol(s, &end, 0);

		if (end && *end) {
			return false;
		}

		*out = n;
		return true;
	}

	return false;
}


/* -- texmap persistence ---------------------------------------------------
 *
 * The texmap is what makes a texture's port stable across rebuilds: a local
 * texture id keeps whatever slot it was first given. Slots must stay dense —
 * the engine advances its global port base by the ENTRY COUNT while mapping
 * each entry by its slot index, so a hole makes the next mod's ports overlap
 * this one's. The Python never pruned, which is how two shipped mods ended up
 * with holes and duplicates; here, retired ids are dropped and the survivors
 * are compacted, in first-assigned order.
 */
static int cmpTexLocal(const void *a, const void *b)
{
	const struct texEntry *x = a, *y = b;
	return x->localTexId < y->localTexId ? -1 : x->localTexId > y->localTexId ? 1 : 0;
}

static int cmpTexSlot(const void *a, const void *b)
{
	const struct texEntry *x = a, *y = b;
	return x->slotIdx < y->slotIdx ? -1 : x->slotIdx > y->slotIdx ? 1 : 0;
}


/* -- the vanilla name -> id map -------------------------------------------
 *
 * What `replaces:` resolves against: a snapshot of one ROM's own file names,
 * taken by `pdt snapshot-vanilla-names` and checked in under
 * docker-caroll/share/pd-<romid>/files.json. It is a plain {name: id} object.
 */
struct vanillaMap {
	JSON_Value *val;
	JSON_Object *files;
};

static bool vanillaLoad(struct vanillaMap *vm, const char *path)
{
	JSON_Object *root;

	vm->val = json_parse_file(path);

	if (!vm->val) {
		return false;
	}

	root = json_value_get_object(vm->val);
	vm->files = root ? json_object_get_object(root, "files") : NULL;

	return vm->files != NULL;
}

static long vanillaLookup(const struct vanillaMap *vm, const char *name)
{
	JSON_Value *v;

	if (!vm->files) {
		return -1;
	}

	v = json_object_get_value(vm->files, name);

	if (!v || json_value_get_type(v) != JSONNumber) {
		return -1;
	}

	return (long)json_value_get_number(v);
}

/* -- alt ROMs -------------------------------------------------------------
 *
 * A romSource is another ROM this mod pulls bytes out of. The manifest says
 * which file and, for textures, where that ROM's texture list lives; the
 * entries say what to take. `byOffset` says it outright. `byId` names a
 * texture and we work the offset out from the ROM's own tlist. `byName` names
 * a file, which needs that ROM's file table and is not done yet.
 */
struct altRom {
	char id[PDFT_ROMSOURCE_ID];
	char filename[PDFT_ROMSOURCE_FILE];
	uint32_t expectedSize;
	bool required;
	bool strict;
	uint8_t fallback;
	uint32_t tlistOffset;
	uint32_t tlistCount;
	uint32_t texdataOffset;
	FILE *fp;          /* opened lazily, only when something needs bytes */
	bool tried;
	char path[PATHMAX];
};

static bool altOpen(struct altRom *rom, const char *const *dirs, int numDirs)
{
	int i;

	if (rom->tried) {
		return rom->fp != NULL;
	}

	rom->tried = true;

	for (i = 0; i < numDirs; ++i) {
		char candidate[PATHMAX];

		if (!dirs[i]) {
			continue;
		}

		if (snprintf(candidate, sizeof(candidate), "%s/%s", dirs[i], rom->filename) >= (int)sizeof(candidate)) {
			continue;
		}

		rom->fp = fopen(candidate, "rb");

		if (rom->fp) {
			snprintf(rom->path, sizeof(rom->path), "%s", candidate);
			return true;
		}
	}

	return false;
}

/**
 * One record of a ROM's texture list is eight bytes, of which bytes 1..3 are
 * the big endian offset of that texture's data from texdataOffset. A texture
 * runs to wherever the next one starts.
 */
static bool altTextureExtent(struct altRom *rom, uint32_t texId, uint32_t *outOfs, uint32_t *outSize)
{
	uint8_t rec[16];
	uint32_t cur, next;

	if (!rom->fp || texId + 1 >= rom->tlistCount) {
		return false;
	}

	if (fseek(rom->fp, (long)(rom->tlistOffset + texId * 8), SEEK_SET) != 0) {
		return false;
	}

	if (fread(rec, 1, sizeof(rec), rom->fp) != sizeof(rec)) {
		return false;
	}

	cur = ((uint32_t)rec[1] << 16) | ((uint32_t)rec[2] << 8) | rec[3];
	next = ((uint32_t)rec[9] << 16) | ((uint32_t)rec[10] << 8) | rec[11];

	if (next <= cur || next - cur > 0x10000) {
		return false;
	}

	*outOfs = rom->texdataOffset + cur;
	*outSize = next - cur;
	return true;
}

/**
 * Non-textures before textures; replacers before the rest; then by name, and
 * textures among themselves by texture id. Ids are handed out in this order,
 * so this is what keeps a rebuild from renumbering the world.
 */
static int cmpEntry(const void *a, const void *b)
{
	const struct entry { JSON_Object *obj; const char *name; const char *path;
			long texId; bool isTexture; bool replaces; long fixedId;
			int altRom; uint32_t altOfs, altSize; bool drop; } *x = a, *y = b;

	if (x->isTexture != y->isTexture) {
		return x->isTexture ? 1 : -1;
	}

	if (x->isTexture) {
		return x->texId < y->texId ? -1 : x->texId > y->texId ? 1 : 0;
	}

	if (x->replaces != y->replaces) {
		return x->replaces ? -1 : 1;
	}

	return strcmp(x->name, y->name);
}

int main(int argc, char **argv)
{
	const char *modName = NULL, *workspace = ".", *output = NULL, *vanillaPath = NULL;
	const char *romDirs[8] = { 0 };
	bool allowOrphans = false;
	int numRomDirs = 0;
	struct vanillaMap vanilla = { NULL, NULL };
	struct altRom *roms = NULL;
	struct pdftRomSource *sources = NULL;
	uint32_t numRoms = 0;
	char manifestPath[PATHMAX], texmapPath[PATHMAX], outPath[PATHMAX];
	char outTexmapPath[PATHMAX];
	JSON_Value *manifestVal, *texmapVal = NULL;
	JSON_Object *manifest;
	JSON_Array *files;
	struct pdftFile *out = NULL;
	struct pdftTexMap *texmap = NULL;
	struct texEntry *tex = NULL;
	uint32_t numOut = 0, numTex = 0;
	uint32_t nextLocalId = FIRST_LOCAL_ID;
	size_t i, n;
	uint8_t *blob;
	uint32_t blobLen = 0;
	char err[256];
	FILE *f;
	int arg;

	for (arg = 1; arg < argc; ++arg) {
		if (!strcmp(argv[arg], "--workspace") && arg + 1 < argc) {
			workspace = argv[++arg];
		} else if (!strcmp(argv[arg], "--output") && arg + 1 < argc) {
			output = argv[++arg];
		} else if (!strcmp(argv[arg], "--allow-orphans")) {
			allowOrphans = true;
		} else if (!strcmp(argv[arg], "--vanilla") && arg + 1 < argc) {
			vanillaPath = argv[++arg];
		} else if (!strcmp(argv[arg], "--rom-dir") && arg + 1 < argc) {
			if (numRomDirs < (int)(sizeof(romDirs) / sizeof(romDirs[0])) - 2) {
				romDirs[numRomDirs++] = argv[++arg];
			} else {
				die("too many --rom-dir");
			}
		} else if (argv[arg][0] == '-') {
			die("unknown argument %s", argv[arg]);
		} else if (!modName) {
			modName = argv[arg];
		} else {
			die("unexpected argument %s", argv[arg]);
		}
	}

	if (!modName) {
		fprintf(stderr, "usage: mkfiletable <mod-name> [--workspace <dir>] [--output <dir>]\n"
				"                   [--vanilla <files.json>] [--rom-dir <dir>]... [--allow-orphans]\n");
		return 2;
	}

	JOIN(manifestPath, "%s/%s_filetable.json", workspace, modName);
	JOIN(texmapPath, "%s/%s_texmap.json", workspace, modName);

	if (!output) {
		static char def[PATHMAX];
		JOIN(def, "%s/%s", workspace, modName);
		output = def;
	}

	JOIN(outPath, "%s/filetable.dat", output);
	JOIN(outTexmapPath, "%s", texmapPath);

	manifestVal = json_parse_file_with_comments(manifestPath);

	if (!manifestVal) {
		die("could not read or parse %s", manifestPath);
	}

	manifest = json_value_get_object(manifestVal);

	if (!manifest) {
		die("%s: top level is not an object", manifestPath);
	}

	{
		JSON_Array *rs = json_object_get_array(manifest, "romSources");
		size_t count = rs ? json_array_get_count(rs) : 0;

		if (count > PDFT_MAX_ROMSOURCES) {
			die("%s declares %zu romSources; the reader keeps %d",
					manifestPath, count, PDFT_MAX_ROMSOURCES);
		}

		roms = calloc(count ? count : 1, sizeof(*roms));
		sources = calloc(count ? count : 1, sizeof(*sources));

		for (i = 0; i < count; ++i) {
			JSON_Object *o = json_array_get_object(rs, i);
			const char *id = o ? json_object_get_string(o, "id") : NULL;
			const char *fn = o ? json_object_get_string(o, "filename") : NULL;
			const char *fb = o ? json_object_get_string(o, "fallbackBehavior") : NULL;
			JSON_Object *tx = o ? json_object_get_object(o, "textures") : NULL;
			long v = 0;

			if (!id || !fn) {
				die("%s: romSource %zu needs an id and a filename", manifestPath, i);
			}

			snprintf(roms[i].id, sizeof(roms[i].id), "%s", id);
			snprintf(roms[i].filename, sizeof(roms[i].filename), "%s", fn);

			if (strcmp(roms[i].id, id) || strcmp(roms[i].filename, fn)) {
				die("%s: romSource '%s': id or filename is longer than the reader's buffer", manifestPath, id);
			}

			roms[i].expectedSize = numberOf(json_object_get_value(o, "expectedSize"), &v) ? (uint32_t)v : 0;
			roms[i].required = json_object_get_boolean(o, "required") == 1;
			roms[i].strict = json_object_get_boolean(o, "strict") == 1;
			roms[i].fallback = fb && !strcmp(fb, "vanilla") ? PDFT_FALLBACK_VANILLA
					: fb && !strcmp(fb, "error") ? PDFT_FALLBACK_ERROR : PDFT_FALLBACK_SKIP;

			if (tx) {
				roms[i].tlistOffset = numberOf(json_object_get_value(tx, "tlistOffset"), &v) ? (uint32_t)v : 0;
				roms[i].tlistCount = numberOf(json_object_get_value(tx, "tlistCount"), &v) ? (uint32_t)v : 0;
				roms[i].texdataOffset = numberOf(json_object_get_value(tx, "texdataOffset"), &v) ? (uint32_t)v : 0;
			}

			sources[i].id = roms[i].id;
			sources[i].filename = roms[i].filename;
			sources[i].expectedSize = roms[i].expectedSize;
			sources[i].required = roms[i].required;
			sources[i].strict = roms[i].strict;
			sources[i].fallback = roms[i].fallback;
			++numRoms;
		}

		romDirs[numRomDirs++] = output;
		romDirs[numRomDirs++] = workspace;
	}

	if (vanillaPath && !vanillaLoad(&vanilla, vanillaPath)) {
		die("could not read the vanilla name map at %s", vanillaPath);
	}

	files = json_object_get_array(manifest, "files");
	n = files ? json_array_get_count(files) : 0;

	if (!n) {
		die("%s has no files", manifestPath);
	}

	/* the texmap as it stands, so ports stay put */
	texmapVal = json_parse_file(texmapPath);

	if (texmapVal) {
		JSON_Object *root = json_value_get_object(texmapVal);
		JSON_Object *entries = root ? json_object_get_object(root, "entries") : NULL;
		size_t count = entries ? json_object_get_count(entries) : 0;

		tex = calloc(count + n, sizeof(*tex));

		for (i = 0; i < count; ++i) {
			const char *key = json_object_get_name(entries, i);
			long local = strtol(key, NULL, 0);
			long slot = 0;

			if (!numberOf(json_object_get_value_at(entries, i), &slot)) {
				die("%s: entry '%s' is not a number", texmapPath, key);
			}

			tex[numTex].localTexId = (uint32_t)local;
			tex[numTex].slotIdx = (uint32_t)slot;
			++numTex;
		}
	} else {
		tex = calloc(n, sizeof(*tex));
	}

	out = calloc(n, sizeof(*out));

	/* The order the entries go out in decides the ids, so it is part of the
	 * format as far as anyone downstream is concerned: replacers first by
	 * name, then the rest by name, then textures by their texture id. */
	{
		struct entry {
			JSON_Object *obj;
			const char *name;
			const char *path;
			long texId;
			bool isTexture;
			bool replaces;
			long fixedId;      /* from the vanilla map, for a replacer */
			int altRom;        /* index into roms[], or -1 */
			uint32_t altOfs, altSize;
			bool drop;         /* an orphan, kept out of the output */
		};
		struct entry *ents = calloc(n, sizeof(*ents));
		size_t numPlain = 0, numTexEnt = 0;

		for (i = 0; i < n; ++i) {
			JSON_Object *e = json_array_get_object(files, i);
			const char *name = e ? json_object_get_string(e, "name") : NULL;
			const char *type = e ? json_object_get_string(e, "type") : NULL;

			if (!name || !name[0]) {
				die("%s: file entry %zu has no name", manifestPath, i);
			}

			const char *replaces = json_object_get_string(e, "replaces");
			JSON_Object *src = json_object_get_object(e, "source");

			ents[i].obj = e;
			ents[i].name = name;
			ents[i].path = json_object_get_string(e, "path");
			ents[i].isTexture = type && !strcmp(type, "texture");
			ents[i].replaces = replaces != NULL;
			ents[i].fixedId = -1;
			ents[i].altRom = -1;

			if (replaces) {
				if (!vanilla.files) {
					die("%s: '%s' replaces '%s', but no vanilla name map was given "
							"(pass --vanilla <share/pd-<romid>/files.json>)", manifestPath, name, replaces);
				}

				ents[i].fixedId = vanillaLookup(&vanilla, replaces);

				if (ents[i].fixedId < 0) {
					if (!allowOrphans) {
						die("%s: '%s' replaces '%s', which is not in the vanilla name map. "
								"Fix the manifest, or pass --allow-orphans to drop the entry",
								manifestPath, name, replaces);
					}

					/* The Python dropped these silently under the same flag, which
					 * is how a manifest row can do nothing for a year without
					 * anyone noticing. Dropped here too, but said out loud. */
					fprintf(stderr, "mkfiletable: dropping '%s': it replaces '%s', which no "
							"vanilla name map has\n", name, replaces);
					ents[i].drop = true;
				}
			}

			if (src) {
				const char *romId = json_object_get_string(src, "rom");
				const char *lookup = json_object_get_string(src, "lookup");
				uint32_t k;
				long v = 0;

				if (!romId || !lookup) {
					die("%s: '%s' has a source with no rom or no lookup", manifestPath, name);
				}

				for (k = 0; k < numRoms; ++k) {
					if (!strcmp(roms[k].id, romId)) {
						ents[i].altRom = (int)k;
						break;
					}
				}

				if (ents[i].altRom < 0) {
					die("%s: '%s' names romSource '%s', which the manifest does not declare",
							manifestPath, name, romId);
				}

				if (!strcmp(lookup, "byOffset")) {
					if (!numberOf(json_object_get_value(src, "offset"), &v)) {
						die("%s: '%s' is byOffset with no usable offset", manifestPath, name);
					}

					ents[i].altOfs = (uint32_t)v;

					if (!numberOf(json_object_get_value(src, "size"), &v)) {
						die("%s: '%s' is byOffset with no usable size", manifestPath, name);
					}

					ents[i].altSize = (uint32_t)v;
				} else if (!strcmp(lookup, "byId")) {
					struct altRom *rom = &roms[ents[i].altRom];

					if (!numberOf(json_object_get_value(src, "id"), &v)) {
						die("%s: '%s' is byId with no usable id", manifestPath, name);
					}

					if (!altOpen(rom, romDirs, numRomDirs)) {
						die("could not find '%s' for romSource '%s'; pass --rom-dir",
								rom->filename, rom->id);
					}

					if (!altTextureExtent(rom, (uint32_t)v, &ents[i].altOfs, &ents[i].altSize)) {
						die("%s: '%s': texture 0x%lx is not readable from %s",
								manifestPath, name, v, rom->path);
					}
				} else if (!strcmp(lookup, "byName")) {
					die("%s: '%s' is byName; resolving a name against a ROM's own file "
							"table is not implemented yet", manifestPath, name);
				} else {
					die("%s: '%s' has lookup '%s', which is not one of byOffset, byId, byName",
							manifestPath, name, lookup);
				}
			}

			if (ents[i].isTexture) {
				if (!numberOf(json_object_get_value(e, "textureId"), &ents[i].texId)) {
					die("%s: texture '%s' has no usable textureId", manifestPath, name);
				}

				++numTexEnt;
			} else {
				++numPlain;
			}
		}

		(void)numPlain;
		(void)numTexEnt;
		qsort(ents, n, sizeof(*ents), cmpEntry);

		for (i = 0; i < n; ++i) {
			if (ents[i].drop) {
				continue;
			}

			out[numOut].id = ents[i].fixedId >= 0 ? (uint32_t)ents[i].fixedId : nextLocalId++;
			out[numOut].name = ents[i].name;
			out[numOut].path = ents[i].path;
			out[numOut].alt.romIdx = ents[i].altRom;
			out[numOut].alt.offset = ents[i].altOfs;
			out[numOut].alt.size = ents[i].altSize;
			out[numOut].alt.compression = 0;
			++numOut;

			if (ents[i].isTexture) {
				bool known = false;
				uint32_t k;

				for (k = 0; k < numTex; ++k) {
					if (tex[k].localTexId == (uint32_t)ents[i].texId) {
						known = true;
						break;
					}
				}

				if (!known) {
					tex[numTex].localTexId = (uint32_t)ents[i].texId;
					tex[numTex].slotIdx = UINT_MAX;
					++numTex;
				}
			}
		}

		free(ents);
	}

	/* Drop texmap entries whose texture is gone, then compact the slots. */
	{
		uint32_t k, live = 0;

		for (k = 0; k < numTex; ++k) {
			bool stillThere = false;
			size_t j;

			for (j = 0; j < n; ++j) {
				JSON_Object *e = json_array_get_object(files, j);
				const char *type = json_object_get_string(e, "type");
				long texId = 0;

				if (!type || strcmp(type, "texture")) {
					continue;
				}

				if (numberOf(json_object_get_value(e, "textureId"), &texId)
						&& (uint32_t)texId == tex[k].localTexId) {
					stillThere = true;
					break;
				}
			}

			if (stillThere) {
				tex[live++] = tex[k];
			} else {
				printf("  retiring texture 0x%04x, its slot is reclaimed\n", tex[k].localTexId);
			}
		}

		numTex = live;

		/* Keep the order the slots were first handed out in, so a texture that
		 * already had a port keeps it wherever it can. */
		qsort(tex, numTex, sizeof(*tex), cmpTexSlot);

		for (k = 0; k < numTex; ++k) {
			tex[k].slotIdx = k;
		}
	}

	texmap = calloc(numTex ? numTex : 1, sizeof(*texmap));

	{
		struct texEntry *sorted = calloc(numTex ? numTex : 1, sizeof(*sorted));
		uint32_t k;

		memcpy(sorted, tex, numTex * sizeof(*tex));
		qsort(sorted, numTex, sizeof(*sorted), cmpTexLocal);

		for (k = 0; k < numTex; ++k) {
			texmap[k].localTexId = (uint16_t)sorted[k].localTexId;
			texmap[k].slotIdx = (uint16_t)sorted[k].slotIdx;
		}

		free(sorted);
	}

	{
		struct pdftInput in;

		in.files = out;
		in.numFiles = numOut;
		in.sources = sources;
		in.numSources = numRoms;
		in.texmap = texmap;
		in.numTexMap = numTex;

		blob = pdftWrite(&in, &blobLen, err, sizeof(err));

		if (!blob) {
			die("%s", err);
		}

		f = fopen(outPath, "wb");

		if (!f) {
			die("could not write %s", outPath);
		}

		fwrite(blob, 1, blobLen, f);
		fclose(f);
		printf("wrote %s: v%u, %u files, %u romSources, %u textures, %u bytes\n",
				outPath, pdftVersionFor(&in), numOut, numRoms, numTex, blobLen);
	}

	/* Write the texmap back so the next build keeps these ports. */
	{
		JSON_Value *rootVal = json_value_init_object();
		JSON_Object *root = json_value_get_object(rootVal);
		JSON_Value *entriesVal = json_value_init_object();
		JSON_Object *entries = json_value_get_object(entriesVal);
		uint32_t k;

		json_object_set_string(root, "modName", modName);

		for (k = 0; k < numTex; ++k) {
			char key[16], val[16];

			snprintf(key, sizeof(key), "0x%04x", texmap[k].localTexId);
			snprintf(val, sizeof(val), "0x%04x", texmap[k].slotIdx);
			json_object_set_string(entries, key, val);
		}

		json_object_set_value(root, "entries", entriesVal);

		if (json_serialize_to_file_pretty(rootVal, outTexmapPath) != JSONSuccess) {
			die("could not write %s", outTexmapPath);
		}

		json_value_free(rootVal);
	}

	json_value_free(manifestVal);

	if (texmapVal) {
		json_value_free(texmapVal);
	}

	return 0;
}
