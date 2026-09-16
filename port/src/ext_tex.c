#include <dirent.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#include "gbiex.h"
#include "types.h"

#include "system.h"
#include "fs.h"
#include "data.h"
#include "romdata.h"
#include "ext_tex.h"

#define EXT_TEX_DIRNAME "ext_tex"
#define FONT_OUTLINES_DIR "outlines"

static char extTexPath[FS_MAXPATH + 1];

// Indexed directly by texture number, so it has to span every id the display
// list can name. The widened G_NOOP texture-slot encoding carries 15 bits, so
// ids run 0..32767 and mod-assigned slots start at 4096; sizing to the whole
// range is what keeps a PNG override addressable at any slot a mod can get.
// The shipped set (mod_aio_characters, mod_fojo, mod_gex_characters) wants 633
// slots and lands at 4096..4728, so the range is far wider than today needs.
// It is sized to the encoding rather than to the current set, so no mod can be
// handed a slot this table cannot address.
//
// Cost is measured, not estimated. sizeof(struct ExtTexture) is 24 (22 bytes of
// payload, 2 of tail padding - already the minimum for an 8-byte-aligned
// pointer, so reordering the fields cannot shrink it), giving 32768 * 24 =
// 786432 bytes = 768 KiB. That is static BSS, not a heap allocation: it costs
// address space, and nothing at runtime until a page is touched.
//
// Three loops walk the whole table. The two in extTexInit run once at startup.
// extTexFree is the one that repeats - videoResetTextureCache reaches it on
// every menu-model swap - and the 4x growth measured at ~10.7us per sweep,
// 0.06% of a 60fps frame, which is why this stays a flat array instead of
// becoming a map.
//
// Tied to G_NOOP_TEXSLOT_MAX in src/include/gbiex.h, the single source of truth
// for the encoding's width, so widening the slot field resizes this table with
// it and the two cannot drift apart.
//
// Cast back to s32 because the macro is 0x7fffu and every texnum in this file
// is signed. Each `texnum >= MAX_EXT_TEX` test today is paired with a
// `texnum < 0` test that short-circuits ahead of it, so an unsigned macro
// would not change any current result; the cast is so that the next such test
// written without the companion still rejects a negative index instead of
// promoting it to a huge unsigned one.
#define MAX_EXT_TEX ((s32)(G_NOOP_TEXSLOT_MAX + 1))
#define NUM_FONTS 5
const u16 IDMASK_FONT_OUTLINE = MASK_FONT_OUTLINE << 8;


struct ExtTexture
{
	u8 *texdata;
	s32 texnum;
	u16 width;
	u16 height;
	char extension[5];
	s8 ownerMod;
};

struct ModelTextures
{
	s16 fileNum;
	s16 numTextures;
	struct ExtTexture *textures;
	char basePath[FS_MAXPATH + 1];
	char modelName[64];
};

static struct ExtTexture extTextures[MAX_EXT_TEX];
static s32 g_ExtTexCurrentModIndex = -1; // set during extTexScanDir for readModelTextures

static struct ModelTextures *modelTextures;
static s32 numModels;

#if VERSION == VERSION_PAL_FINAL
#define NCHARS 135
#else
#define NCHARS 94
#endif

static struct ExtTexture fontExtTextures[NUM_FONTS][NCHARS];
static struct ExtTexture fontOutlineExtTextures[NUM_FONTS][NCHARS];

#define FONT_HANDELGOTHICSM 0
#define FONT_HANDELGOTHICMD 1
#define FONT_HANDELGOTHICXS 2
#define FONT_HANDELGOTHICLG 3
#define FONT_NUMERIC 4

s32 fileInfo(const char *filename, s32 *texNum, char extension[5])
{
	char *ext = strrchr(filename, '.');

	// no extension
	if (!ext) return 1;

	++ext;
	strncpy(extension, ext, 5);

	// get the filename without extension
	char basename[16] = { 0 };
	memcpy(basename, filename, strlen(filename) - strlen(ext) - 1);

	*texNum = strtol(basename, NULL, 16);

	return 0;
}

/**
 * Which mod a model entry's overrides came from.
 *
 * ownerMod lives on each ExtTexture rather than on the entry, but a directory
 * scan stamps every texture it reads with the mod it was reading, so the whole
 * entry shares one owner. -1 means unknown, and an unknown owner matches
 * anything.
 */
static s8 modelEntryOwner(const struct ModelTextures *m)
{
	return (m && m->numTextures > 0 && m->textures) ? m->textures[0].ownerMod : -1;
}

/**
 * Find the model entry for a file number, preferring the mod being rendered.
 *
 * fileNum is a RAW file number - romdataFilePreprocess strips the owner out of
 * a tagged mod file id before the preprocess ever sees it - so two mods that
 * each ship overrides for their own file 0x1234 land on the same key here.
 * Taking the first match means whichever mod was scanned first answers for
 * both. The G_TEXTYPE_GENERAL path already filters on ownerMod against
 * g_TexModNum; this is the same guard for models.
 *
 * Falls back to the first name match when nothing matches the current mod, so
 * a lookup that works today keeps working.
 */
static struct ModelTextures *findModelEntry(u16 fileNum)
{
	extern s32 g_TexModNum;
	struct ModelTextures *first = NULL;
	int i;

	for (i = 0; i < numModels; ++i) {
		if (modelTextures[i].fileNum != (s16)fileNum) {
			continue;
		}

		if (!first) {
			first = &modelTextures[i];
		}

		s8 owner = modelEntryOwner(&modelTextures[i]);

		if (owner < 0 || g_TexModNum < 0 || owner == g_TexModNum) {
			return &modelTextures[i];
		}
	}

	return first;
}

struct ExtTexture *lookupModelTex(u16 fileNum, s32 texNum)
{
	if (fileNum > NUM_FILES) {
		sysLogPrintf(LOG_WARNING, "lookupModelTex: INVALID fileNum %04x > NUM_FILES %d, texNum: %04x", fileNum, NUM_FILES, texNum);
		return 0;
	}

	struct ModelTextures *modelTex = findModelEntry(fileNum);

	if (modelTex == NULL) {
		// sysLogPrintf(LOG_NOTE, "lookupModelTex: NO model entry for fileNum=%04x (texNum=%04x), numModels=%d", fileNum, texNum, numModels);
		if (numModels > 0) {
			for (int i = 0; i < numModels; ++i) {
				// sysLogPrintf(LOG_NOTE, "  modelTextures[%d]: fileNum=%04x modelName=%s basePath=%s numTex=%d",
				//	i, modelTextures[i].fileNum, modelTextures[i].modelName, modelTextures[i].basePath, modelTextures[i].numTextures);
			}
		}
		return NULL;
	}

	for (int i = 0; i < modelTex->numTextures; ++i) {
		if (modelTex->textures[i].texnum == texNum)
			return &modelTex->textures[i];
	}

	// sysLogPrintf(LOG_NOTE, "lookupModelTex: model fileNum=%04x (%s) found but texNum=%04x not in %d textures",
	//	fileNum, modelTex->modelName, texNum, modelTex->numTextures);
	return NULL;
}

struct ExtTexture *getExtTexture(u8 type, u16 id, s32 texnum)
{
	struct ExtTexture *texlist;
	switch (type) {
		case G_TEXTYPE_NONE:
			return NULL;
		case G_TEXTYPE_GENERAL:
			if (texnum < 0 || texnum >= MAX_EXT_TEX) {
				return NULL;
			}
			return &extTextures[texnum];
		case G_TEXTYPE_MODEL:
			return lookupModelTex(id, texnum);
		case G_TEXTYPE_FONT: {
			// Both indices arrive straight off a display-list packet, which
			// can name a 15-bit id and a 15-bit texnum, against a table that
			// is NUM_FONTS (5) rows of NCHARS (94 on NTSC, 135 on PAL). The
			// top bit of id is IDMASK_FONT_OUTLINE, so the row index is the
			// masked value and not the raw one. Same guard shape as
			// G_TEXTYPE_GENERAL above.
			u16 fontId = id & ~IDMASK_FONT_OUTLINE;

			if (fontId >= NUM_FONTS || texnum < 0 || texnum >= NCHARS) {
				return NULL;
			}

			if (id & IDMASK_FONT_OUTLINE) {
				return &fontOutlineExtTextures[fontId][texnum];
			}

			return &fontExtTextures[fontId][texnum];
		}
		default:
			sysLogPrintf(LOG_WARNING, "Invalid Texture type: %d, texnum: %04x", type, texnum);
			return NULL;
	}
}

u8 extTexExists(u8 type, u16 id, s32 texnum)
{
	struct ExtTexture *tex = getExtTexture(type, id, texnum);
	return tex && tex->texnum >= 0;
}

s8 extTexGetOwnerMod(u8 type, u16 id, s32 texnum)
{
	struct ExtTexture *tex = getExtTexture(type, id, texnum);
	if (tex && tex->texnum >= 0) {
		return tex->ownerMod;
	}
	return -1;
}

bool extTexModelHasEntryForTexid(s16 fileNum, s32 texNum)
{
	if (fileNum <= 0 || !modelTextures) return false;
	for (int i = 0; i < numModels; ++i) {
		if (modelTextures[i].fileNum != fileNum) continue;
		for (int j = 0; j < modelTextures[i].numTextures; ++j) {
			if (modelTextures[i].textures[j].texnum == texNum) return true;
		}
		return false;
	}
	return false;
}

s32 extTexModelGetTextureCount(s16 fileNum)
{
	if (fileNum <= 0 || !modelTextures) return 0;
	for (int i = 0; i < numModels; ++i) {
		if (modelTextures[i].fileNum == fileNum) return modelTextures[i].numTextures;
	}
	return 0;
}

s32 extTexModelGetTextureInfo(s16 fileNum, s32 index, s32 *texNum, s8 *ownerMod, u16 *width, u16 *height)
{
	if (fileNum <= 0 || !modelTextures || index < 0) return false;
	for (int i = 0; i < numModels; ++i) {
		if (modelTextures[i].fileNum != fileNum) continue;
		if (index >= modelTextures[i].numTextures) return false;
		struct ExtTexture *tex = &modelTextures[i].textures[index];
		if (texNum) *texNum = tex->texnum;
		if (ownerMod) *ownerMod = tex->ownerMod;
		if (width) *width = tex->width;
		if (height) *height = tex->height;
		return true;
	}
	return false;
}

const u8 *extTexModelLoadPixels(s16 fileNum, s32 texNum, u32 *width, u32 *height)
{
	struct ExtTexture *tex = lookupModelTex((u16)fileNum, texNum);
	struct ModelTextures *modelTex = NULL;
	char path[FS_MAXPATH + 1];

	if (!tex) return NULL;

	for (int i = 0; i < numModels; ++i) {
		if (modelTextures[i].fileNum == fileNum) {
			modelTex = &modelTextures[i];
			break;
		}
	}

	if (!modelTex) return NULL;

	if (!tex->texdata) {
		int loadedWidth = 0;
		int loadedHeight = 0;
		int channels = 0;
		snprintf(path, sizeof(path), "%s/%s/%04x.%s",
			modelTex->basePath, modelTex->modelName, texNum, tex->extension);
		stbi_set_flip_vertically_on_load(1);
		tex->texdata = stbi_load(path, &loadedWidth, &loadedHeight, &channels, 4);
		stbi_set_flip_vertically_on_load(0);
		if (!tex->texdata) return NULL;
		tex->width = loadedWidth;
		tex->height = loadedHeight;
	}

	if (width) *width = tex->width;
	if (height) *height = tex->height;
	return tex->texdata;
}

u8 extTexGetDimensions(u8 type, u16 id, s32 texnum, u16 *width, u16 *height)
{
	struct ExtTexture *tex = getExtTexture(type, id, texnum);
	if (tex && tex->texnum >= 0 && tex->width > 0 && tex->height > 0) {
		*width = tex->width;
		*height = tex->height;
		return 1;
	}
	return 0;
}

/**
 * Name a registration source for a log line.
 *
 * The mod directory's basename, so "$B/mods/mod_fojo" reads as "mod_fojo".
 * ownerMod -1 is the global ext_tex directory.
 */
static const char *extTexOwnerName(s8 ownerMod)
{
	if (ownerMod < 0 || (size_t)ownerMod >= sizeof(modDirs) / sizeof(modDirs[0])
			|| !modDirs[ownerMod][0]) {
		return "the global ext_tex dir";
	}

	const char *slash = strrchr(modDirs[ownerMod], '/');

	return slash ? slash + 1 : modDirs[ownerMod];
}

/**
 * The ext_tex directory a flat-table entry was registered from.
 *
 * struct ExtTexture carries no path and cannot afford one: it is 24 bytes x
 * MAX_EXT_TEX = 768 KiB of BSS, and FS_MAXPATH is 1024, so a path per entry
 * would be 32 MiB. It does not need one. ownerMod already is the index into
 * modDirs[] - setTex stamps it from g_ExtTexCurrentModIndex, which extTexInit
 * drives over that same 0-based array - so the directory is recoverable from
 * the byte that is already there.
 *
 * ownerMod -1 is the global basedir/ext_tex, which extTexInit resolved once
 * into extTexPath. Otherwise this rebuilds the mod's path the way extTexInit
 * built it for the scan, including the fsFullPath pass that expands the $B
 * placeholder modDirs[] entries are stored with.
 *
 * Writes into `buf` and returns it, or returns extTexPath when the owner names
 * no usable mod directory.
 */
static const char *extTexOwnerDir(s8 ownerMod, char *buf, size_t bufsize)
{
	char rel[FS_MAXPATH + 1];

	if (ownerMod < 0 || (size_t)ownerMod >= sizeof(modDirs) / sizeof(modDirs[0])
			|| !modDirs[ownerMod][0]) {
		return extTexPath;
	}

	snprintf(rel, sizeof(rel), "%s/" EXT_TEX_DIRNAME, modDirs[ownerMod]);
	strncpy(buf, fsFullPath(rel), bufsize - 1);
	buf[bufsize - 1] = '\0';

	return buf;
}

char *resolveFontname(const u8 fontId)
{
	switch (fontId) {
		case FONT_HANDELGOTHICSM: return "fonthandelgothicsm";
		case FONT_HANDELGOTHICMD: return "fonthandelgothicmd";
		case FONT_HANDELGOTHICXS: return "fonthandelgothicxs";
		case FONT_HANDELGOTHICLG: return "fonthandelgothiclg";
		case FONT_NUMERIC: return "fontnumeric";
		default: return "";
	}
}

u8 getTexPath(char *dst, u8 type, u16 id, s32 texnum)
{
	struct ExtTexture *tex;
	const char *name;

	switch (type) {
		case G_TEXTYPE_GENERAL: {
			if (texnum < 0 || texnum >= MAX_EXT_TEX) {
				return 1;
			}
			tex = &extTextures[texnum];
			// Prefer a per-model PNG override. When the caller supplied a
			// nonzero `id`, it identifies the model that owns the render
			// (see texWriteLoadToTmemAddr): use that to pick the model dir
			// so Foslerfer's /0daf.png and Mikado's /0daf.bin do not
			// pretend to be each other. When `id` is 0 (legacy callers
			// with no model context), fall back to owner-mod filtering.
			if (id != 0) {
				for (int i = 0; i < numModels; ++i) {
					if ((s16)id != modelTextures[i].fileNum) continue;
					for (int j = 0; j < modelTextures[i].numTextures; ++j) {
						if (modelTextures[i].textures[j].texnum == texnum) {
							snprintf(dst, FS_MAXPATH, "%s/%s/%04x.%s",
								modelTextures[i].basePath, modelTextures[i].modelName,
								texnum, modelTextures[i].textures[j].extension);
							return 0;
						}
					}
					// Model matched but has no PNG at this texid; do not fall
					// through to other models' dirs (that was the old bug).
					break;
				}
			} else {
				extern s32 g_TexModNum;
				for (int i = 0; i < numModels; ++i) {
					for (int j = 0; j < modelTextures[i].numTextures; ++j) {
						if (modelTextures[i].textures[j].texnum != texnum) continue;
						s8 owner = modelTextures[i].textures[j].ownerMod;
						if (owner >= 0 && g_TexModNum >= 0 && owner != g_TexModNum) continue;
						snprintf(dst, FS_MAXPATH, "%s/%s/%04x.%s",
							modelTextures[i].basePath, modelTextures[i].modelName,
							texnum, modelTextures[i].textures[j].extension);
						return 0;
					}
				}
			}
			// Last resort: a loose PNG registered straight into the flat
			// table. extTexScanDir registers those from every ext_tex
			// directory it walks, including each mod's own, and this used to
			// build the path from extTexPath unconditionally - so a mod
			// shipping mods/<mod>/ext_tex/1234.png got a path under
			// basedir/ext_tex and loaded nothing. On this install that global
			// directory does not even exist (pd.log: "extTexScanDir: FAILED to
			// open .../data/ext_tex"), so every such override missed.
			char ownerDir[FS_MAXPATH + 1];

			snprintf(dst, FS_MAXPATH, "%s/%04x.%s",
				extTexOwnerDir(tex->ownerMod, ownerDir, sizeof(ownerDir)),
				texnum, tex->extension);
			return 0;
		}
		case G_TEXTYPE_FONT: {
			// Packet-derived indices, bounded exactly as in getExtTexture's
			// font case; extTexLoad calls both with the same id and texnum,
			// so they have to agree on what is in range.
			u16 fontId = id & ~IDMASK_FONT_OUTLINE;

			if (fontId >= NUM_FONTS || texnum < 0 || texnum >= NCHARS) {
				return 1;
			}

			name = resolveFontname(fontId);

			if (id & IDMASK_FONT_OUTLINE) {
				tex = &fontOutlineExtTextures[fontId][texnum];
				snprintf(dst, FS_MAXPATH, "%s/%s/" FONT_OUTLINES_DIR "/%02x.%s", extTexPath, name, texnum, tex->extension);
				return 0;
			}

			tex = &fontExtTextures[fontId][texnum];
			snprintf(dst, FS_MAXPATH, "%s/%s/%02x.%s", extTexPath, name, texnum, tex->extension);
			return 0;
		}
		case G_TEXTYPE_MODEL: {
			tex = lookupModelTex(id, texnum);
			if (!tex) return 1;
			// Same entry lookupModelTex chose, or the basePath could come from
			// one mod while the texture came from another.
			struct ModelTextures *m = findModelEntry(id);
			if (m) {
				snprintf(dst, FS_MAXPATH, "%s/%s/%05x.%s", m->basePath, m->modelName, texnum, tex->extension);
				return 0;
			}
			return 1;
		}
		default: return 1;
	}
}

u8 *extTexLoad(u8 type, u16 id, s32 texnum, u32 *width, u32 *height)
{
	char path[FS_MAXPATH];
	u8 err = getTexPath(path, type, id, texnum);
	if (err) {
		sysLogPrintf(LOG_WARNING, "extTexLoad: getTexPath FAILED type=%d id=%04x texnum=%04x", type, id, texnum);
		return 0;
	}
	sysLogPrintf(LOG_NOTE, "extTexLoad: loading type=%d id=%04x texnum=%04x path='%s'", type, id, texnum, path);

	struct ExtTexture *tex = getExtTexture(type, id, texnum);

	if (!tex) {
		sysLogPrintf(LOG_WARNING, "Unable to load texture: %05x", texnum);
		return NULL;
	}

	u32 channels;
	stbi_set_flip_vertically_on_load(1);
	tex->texdata = stbi_load(path, width, height, &channels, 4);
	stbi_set_flip_vertically_on_load(0);
	return tex->texdata;
}

u8 extTexFontID(struct font *font) {
	if (font == g_FontHandelGothicSm)
		return FONT_HANDELGOTHICSM;
	else if (font == g_FontHandelGothicMd)
		return FONT_HANDELGOTHICMD;
	else if (font == g_FontHandelGothicXs)
		return FONT_HANDELGOTHICXS;
	else if (font == g_FontHandelGothicLg)
		return FONT_HANDELGOTHICLG;
	else if (font == g_FontNumeric)
		return FONT_NUMERIC;

	return 0xff;
}

u8 resolveFontID(const char *fontname)
{
	if (strcmp(fontname, "fonthandelgothicsm") == 0)
		return FONT_HANDELGOTHICSM;
	else if (strcmp(fontname, "fonthandelgothicmd") == 0)
		return FONT_HANDELGOTHICMD;
	else if (strcmp(fontname, "fonthandelgothicxs") == 0)
		return FONT_HANDELGOTHICXS;
	else if (strcmp(fontname, "fonthandelgothiclg") == 0)
		return FONT_HANDELGOTHICLG;
	else if (strcmp(fontname, "fontnumeric") == 0)
		return FONT_NUMERIC;

	return 0xff;
}

void setTex(struct ExtTexture *texlist, s32 index, s32 texNum, char extension[5])
{
	struct ExtTexture *tex = &texlist[index];
	tex->texnum = texNum;
	strcpy(tex->extension, extension);
	tex->ownerMod = (s8)g_ExtTexCurrentModIndex;
}

void setTexDimensions(struct ExtTexture *tex, const char *filepath)
{
	int w = 0, h = 0, comp = 0;
	if (stbi_info(filepath, &w, &h, &comp)) {
		tex->width = (u16)w;
		tex->height = (u16)h;
	}
}

void readModelTextures(const char *path, s16 fileNum, s32 *modelOffset, struct ModelTextures *modelTex)
{
	sysLogPrintf(LOG_NOTE, "readModelTextures: path=%s fileNum=%04x", path, (u16)fileNum);
	DIR *dr = opendir(path);
	struct dirent *de;

	s32 MAX_TEX = 16;
	modelTex->textures = sysMemAlloc(MAX_TEX * sizeof(struct ExtTexture));
	modelTex->numTextures = 0;
	modelTex->fileNum = fileNum;

	// Store the parent directory path for loading textures later
	char *lastSlash = strrchr(path, '/');
	if (lastSlash) {
		size_t dirLen = lastSlash - path;
		memcpy(modelTex->basePath, path, dirLen);
		modelTex->basePath[dirLen] = '\0';
		strncpy(modelTex->modelName, lastSlash + 1, sizeof(modelTex->modelName) - 1);
		modelTex->modelName[sizeof(modelTex->modelName) - 1] = '\0';
	} else {
		strncpy(modelTex->basePath, path, FS_MAXPATH);
		modelTex->modelName[0] = '\0';
	}

	char extension[5] = { 0 };

	while ((de = readdir(dr)) != NULL) {
		const char *name = de->d_name;
		// Skip . / .. and hidden files (macOS .DS_Store, AppleDouble ._*, etc.).
		if (name[0] == '.') continue;

		s32 texNum;
		s32 err = fileInfo(name, &texNum, extension);
		// no extension: skip
		if (err) continue;

		// Grow before write to avoid OOB at modelTex->textures[numTextures].
		if (modelTex->numTextures >= MAX_TEX) {
			MAX_TEX *= 2;
			modelTex->textures = sysMemRealloc(modelTex->textures, MAX_TEX * sizeof(struct ExtTexture));
		}

		setTex(modelTex->textures, modelTex->numTextures, texNum, extension);

		// Read PNG dimensions from file header
		char texFilePath[FS_MAXPATH + 1];
		snprintf(texFilePath, sizeof(texFilePath), "%s/%s", path, name);
		setTexDimensions(&modelTex->textures[modelTex->numTextures], texFilePath);

		modelTex->numTextures++;

		// Also register as a general texture so head models (which use
		// G_TEXTYPE_GENERAL via texWriteLoadToTmemAddr) can find them.
		// First writer wins, same as extTexScanDir's loose-PNG branch.
		if (texNum >= 0 && texNum < MAX_EXT_TEX) {
			if (extTextures[texNum].texnum < 0) {
				setTex(extTextures, texNum, texNum, extension);
				extTextures[texNum].width = modelTex->textures[modelTex->numTextures - 1].width;
				extTextures[texNum].height = modelTex->textures[modelTex->numTextures - 1].height;
				sysLogPrintf(LOG_NOTE, "readModelTextures: also registered texnum=%04x as GENERAL (%dx%d)", texNum,
					extTextures[texNum].width, extTextures[texNum].height);
			} else {
				// Only the GENERAL alias is contested - the per-model entry is
				// kept either way, and getTexPath's id != 0 branch serves this
				// model from its own directory. What the losing model gives up
				// is being findable by texid alone, by a caller with no model
				// context. Two shipped dirs hit this today: mod_fojo's
				// CheadCatherineZ and CheadFoslerferZ both carry 0db1 and 0db2.
				sysLogPrintf(LOG_WARNING,
					"readModelTextures: slot %04x contested - %s/%04x.%s from %s not aliased as GENERAL, already claimed by %s",
					texNum, modelTex->modelName, texNum, extension,
					extTexOwnerName((s8)g_ExtTexCurrentModIndex),
					extTexOwnerName(extTextures[texNum].ownerMod));
			}
		}
	}
	closedir(dr);

	// shrink the textures array to the actual number of textures found
	s32 numTex = modelTex->numTextures;

	if (numTex > 0)
		modelTex->textures = sysMemRealloc(modelTex->textures, numTex * sizeof(struct ExtTexture));

	for (int i = 0; i < modelTex->numTextures; ++i) {
		modelTex->textures[i].texdata = 0;
	}

	sysLogPrintf(LOG_NOTE, "readModelTextures: DONE path=%s fileNum=%04x basePath=%s modelName=%s numTextures=%d",
		path, (u16)fileNum, modelTex->basePath, modelTex->modelName, modelTex->numTextures);
	for (int i = 0; i < modelTex->numTextures; ++i) {
		sysLogPrintf(LOG_NOTE, "  tex[%d]: texnum=%04x ext=%s", i, modelTex->textures[i].texnum, modelTex->textures[i].extension);
	}
}

void readFontTextures(const char *path, const char *fontName)
{
	// extTexScanDir dispatches here on the directory's first character alone,
	// so any 'f...' directory under ext_tex/ reaches this. resolveFontID
	// answers 0xff for one that is not a font, which would index 250 rows past
	// fontExtTextures. Checked before opendir so nothing is left open.
	u8 fontID = resolveFontID(fontName);

	if (fontID >= NUM_FONTS) {
		sysLogPrintf(LOG_WARNING, "readFontTextures: '%s' is not a known font, skipping '%s'", fontName, path);
		return;
	}

	DIR *dr = opendir(path);
	struct dirent *de;

	char extension[5] = { 0 };

	char outlinesPath[FS_MAXPATH];
	sprintf(outlinesPath , "%s/" FONT_OUTLINES_DIR, path);
	u8 outlines = false;

	while (true) {
		de = readdir(dr);
		// after done processing the font folder, do the same for the outlines folder if any
		if (de == NULL) {
			if (outlines) break;

			outlines = true;
			closedir(dr);
			dr = opendir(outlinesPath);
			de = readdir(dr);

			if (de == NULL) break;
		}

		const char *name = de->d_name;
		// Skip . / .. and hidden files (macOS .DS_Store, AppleDouble ._*, etc.).
		if (name[0] == '.') continue;

		s32 texNum;
		s32 err = fileInfo(name, &texNum, extension);
		// no extension: skip
		if (err) continue;

		// texNum is strtol(basename, 16) off a filename, so a font directory
		// holding ff.png yields 255 against a row of NCHARS (94 on NTSC, 135
		// on PAL) - a write into the next font's row, or off the end of the
		// last one.
		if (texNum < 0 || texNum >= NCHARS) {
			sysLogPrintf(LOG_WARNING, "readFontTextures: REJECTED '%s' in %s%s - texNum %d out of range (0..%d)",
				name, fontName, outlines ? "/" FONT_OUTLINES_DIR : "", texNum, NCHARS - 1);
			continue;
		}

		if (outlines)
			setTex(fontOutlineExtTextures[fontID], texNum, texNum, extension);
		else
			setTex(fontExtTextures[fontID], texNum, texNum, extension);
	}

	closedir(dr);
}

void extTexFree()
{
	for (int i = 0; i < MAX_EXT_TEX; ++i) {
		if (extTextures[i].texdata)
			stbi_image_free(extTextures[i].texdata);

		extTextures[i].texdata = 0;
	}

	for (int i = 0; i < NUM_FONTS; ++i) {
		for (int j = 0; j < NCHARS; ++j) {
			if (fontExtTextures[i][j].texdata)
				stbi_image_free(fontExtTextures[i][j].texdata);

			if (fontOutlineExtTextures[i][j].texdata)
				stbi_image_free(fontOutlineExtTextures[i][j].texdata);

			fontExtTextures[i][j].texdata = 0;
			fontOutlineExtTextures[i][j].texdata = 0;
		}
	}

	for (int i = 0; i < numModels; ++i) {
		struct ModelTextures *modelTex = &modelTextures[i];
		for (int j = 0; j < modelTex->numTextures; ++j) {
			if (modelTex->textures[j].texdata)
				stbi_image_free(modelTex->textures[j].texdata);

			modelTex->textures[j].texdata = 0;
		}
	}
}

static void extTexScanDir(const char *dirPath, s32 *maxModels)
{
	sysLogPrintf(LOG_NOTE, "extTexScanDir: scanning '%s'", dirPath);
	char buf[FS_MAXPATH];
	strncpy(buf, fsFullPath(dirPath), FS_MAXPATH);
	DIR *dr = opendir(buf);
	if (!dr) {
		sysLogPrintf(LOG_NOTE, "extTexScanDir: FAILED to open '%s'", dirPath);
		return;
	}

	char filepath[FS_MAXPATH];
	struct dirent *de;
	s32 entryCount = 0;

	while ((de = readdir(dr)) != NULL) {
		const char *name = de->d_name;
		// Skip . / .. and hidden files (macOS .DS_Store, AppleDouble ._*, etc.).
		if (name[0] == '.') continue;

		entryCount++;
		struct stat stbuf;
		snprintf(filepath, sizeof(filepath), "%s/%s", dirPath, name);
		char *buf = sysMemAlloc(FS_MAXPATH);
		strncpy(buf, fsFullPath(filepath), FS_MAXPATH);
		strncpy(filepath, buf, FS_MAXPATH);
		if (stat(buf, &stbuf) == -1) {
			sysLogPrintf(LOG_WARNING, "extTexScanDir: stat failed: %s", filepath);
			continue;
		}

		if (S_ISDIR(stbuf.st_mode)) {
			char s = name[0];
			sysLogPrintf(LOG_NOTE, "extTexScanDir: found dir '%s' (first char='%c')", name, s);
			if (s == 'P' || s == 'C' || s == 'G') {
				s16 fileNum = (s16)romdataFileGetNumForNameAnyMod(name);
				sysLogPrintf(LOG_NOTE, "extTexScanDir: model dir '%s' => fileNum=%d (0x%04x)", name, fileNum, (u16)fileNum);
				if (fileNum < 0) {
					sysLogPrintf(LOG_WARNING, "extTexScanDir: REJECTED '%s' — not in any mod's file table", name);
					continue;
				}

				struct ModelTextures *modelTex = &modelTextures[numModels++];
				readModelTextures(filepath, fileNum, NULL, modelTex);

				if (numModels > *maxModels) {
					*maxModels *= 2;
					modelTextures = sysMemRealloc(modelTextures, *maxModels * sizeof(struct ModelTextures));
				}
			} else if (s == 'f') {
				sysLogPrintf(LOG_NOTE, "extTexScanDir: font dir '%s'", name);
				readFontTextures(filepath, name);
			} else {
				sysLogPrintf(LOG_NOTE, "extTexScanDir: skipping unknown dir '%s'", name);
			}
		} else {
			s32 texNum = 0;
			char extension[5] = { 0 };
			s32 err = fileInfo(name, &texNum, extension);
			if (err) continue;
			if (texNum < 0 || texNum >= MAX_EXT_TEX) {
				sysLogPrintf(LOG_WARNING, "extTexScanDir: REJECTED '%s' — texNum %d out of range (0..%d)", name, texNum, MAX_EXT_TEX - 1);
				continue;
			}
			if (extTextures[texNum].texnum >= 0) {
				// First writer wins, which is the policy readModelTextures
				// already had; this side used to be a bare setTex, so the two
				// registration paths disagreed about the same table.
				//
				// First writer, not last, because the scan order is fixed -
				// the global ext_tex directory, then modDirs[] in order - so
				// the winner is the same on every run and does not turn on
				// readdir order between directories. Last-writer-wins also
				// overwrote extension, width and height rather than just
				// ownerMod, so a late loser could leave a slot naming its own
				// file at the earlier entry's dimensions.
				//
				// Named on both sides at WARNING because this drops one mod's
				// texture outright, and doing that silently is the failure
				// that took longest to find here.
				sysLogPrintf(LOG_WARNING,
					"extTexScanDir: slot %04x contested - '%s' from %s dropped, already claimed by %s",
					texNum, name, extTexOwnerName((s8)g_ExtTexCurrentModIndex),
					extTexOwnerName(extTextures[texNum].ownerMod));
				continue;
			}

			setTex(extTextures, texNum, texNum, extension);
			setTexDimensions(&extTextures[texNum], filepath);
			sysLogPrintf(LOG_NOTE, "extTexScanDir: general texture '%s' => texNum=%04x (%dx%d)",
				name, texNum, extTextures[texNum].width, extTextures[texNum].height);
		}
	}

	closedir(dr);
	sysLogPrintf(LOG_NOTE, "extTexScanDir: DONE '%s' — %d entries processed, numModels now %d", dirPath, entryCount, numModels);
}

s32 extTexInit()
{
	const char *path = fsFullPath(EXT_TEX_DIRNAME);
	strcpy(extTexPath, path);

	sysLogPrintf(LOG_NOTE, "extTexInit: global extTexPath='%s'", extTexPath);
	sysLogPrintf(LOG_NOTE, "extTexInit: g_NumModDirs=%d", g_NumModDirs);
	for (u32 i = 0; i <= g_NumModDirs; ++i) {
		sysLogPrintf(LOG_NOTE, "extTexInit: modDirs[%d]='%s'", i, modDirs[i]);
	}

	for (int i = 0; i < MAX_EXT_TEX; ++i) {
		extTextures[i].texnum = -1;
		extTextures[i].texdata = 0;
	}

	for (int i = 0; i < NUM_FONTS; ++i) {
		for (int j = 0; j < NCHARS; ++j) {
			fontExtTextures[i][j].texnum = -1;
			fontExtTextures[i][j].texdata = 0;

			fontOutlineExtTextures[i][j].texnum = -1;
			fontOutlineExtTextures[i][j].texdata = 0;
		}
	}

	s32 MAX_MODELS = 16;
	numModels = 0;
	modelTextures = sysMemAlloc(MAX_MODELS * sizeof(struct ModelTextures));

	// Scan the global ext_tex directory (basedir/ext_tex/)
	sysLogPrintf(LOG_NOTE, "extTexInit: scanning global ext_tex dir...");
	g_ExtTexCurrentModIndex = -1;
	extTexScanDir(extTexPath, &MAX_MODELS);

	// Scan each mod's ext_tex directory (mods/mod_xxx/ext_tex/)
	sysLogPrintf(LOG_NOTE, "extTexInit: scanning mod ext_tex dirs...");
	for (u32 i = 0; i <= g_NumModDirs; ++i) {
		if (modDirs[i][0]) {
			char modExtTexPath[FS_MAXPATH + 1];
			snprintf(modExtTexPath, FS_MAXPATH, "%s/" EXT_TEX_DIRNAME, modDirs[i]);
			sysLogPrintf(LOG_NOTE, "extTexInit: mod[%d] ext_tex path='%s'", i, modExtTexPath);
			g_ExtTexCurrentModIndex = (s32)i;
			extTexScanDir(modExtTexPath, &MAX_MODELS);
		}
	}
	g_ExtTexCurrentModIndex = -1;

	sysLogPrintf(LOG_NOTE, "extTexInit: FINAL numModels=%d", numModels);
	s32 totalTextures = 0;
	for (int i = 0; i < numModels; ++i) {
		sysLogPrintf(LOG_NOTE, "  model[%d]: fileNum=%04x name=%s basePath=%s numTex=%d",
			i, (u16)modelTextures[i].fileNum, modelTextures[i].modelName, modelTextures[i].basePath, modelTextures[i].numTextures);
		totalTextures += modelTextures[i].numTextures;
	}

	// Count general textures
	for (int i = 0; i < MAX_EXT_TEX; ++i) {
		if (extTextures[i].texnum >= 0) {
			totalTextures++;
		}
	}

	// shrink this array to the actual number of model folders found
	if (numModels > 0)
		modelTextures = sysMemRealloc(modelTextures, numModels * sizeof(struct ModelTextures));

	sysLogPrintf(LOG_NOTE, "extTexInit: total ext textures found: %d", totalTextures);
	return totalTextures;
}
