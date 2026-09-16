/*
 * pdheadedit - PD head model inspector/editor CLI
 *
 * Reads Rare-compressed (.Z) PD head model files, parses the N64
 * big-endian binary format, and outputs JSON to stdout.
 *
 * Commands:
 *   inspect <file.Z>              - dump full model tree as JSON
 *   validate <file.Z>             - validation report as JSON
 *   edit <file.Z> <output.Z>      - apply edits from stdin JSON, write copy
 *
 * Edit ops always produce a copy, never modify the original.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <zlib.h>

/* ── Endian helpers ─────────────────────────────────────────────── */

static inline uint16_t be16(const void *p) {
	const uint8_t *b = (const uint8_t *)p;
	return (uint16_t)(b[0] << 8 | b[1]);
}

static inline uint32_t be32(const void *p) {
	const uint8_t *b = (const uint8_t *)p;
	return (uint32_t)(b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]);
}

static inline void put_be16(void *p, uint16_t v) {
	uint8_t *b = (uint8_t *)p;
	b[0] = (v >> 8) & 0xff;
	b[1] = v & 0xff;
}

static inline void put_be32(void *p, uint32_t v) {
	uint8_t *b = (uint8_t *)p;
	b[0] = (v >> 24) & 0xff;
	b[1] = (v >> 16) & 0xff;
	b[2] = (v >> 8) & 0xff;
	b[3] = v & 0xff;
}

static inline float be32_to_float(uint32_t bits) {
	float f;
	memcpy(&f, &bits, 4);
	return f;
}

static inline uint32_t float_to_be32(float f) {
	uint32_t bits;
	memcpy(&bits, &f, 4);
	return bits;
}

/* ── Constants ──────────────────────────────────────────────────── */

#define SEGMENT         0x05000000
#define SEGMENT_MASK    0x00ffffff

#define SKEL_HEAD       0x0d

/* Model node types */
#define NT_CHRINFO      0x01
#define NT_POSITION     0x02
#define NT_GUNDL        0x04
#define NT_05           0x05
#define NT_DISTANCE     0x08
#define NT_REORDER      0x09
#define NT_BBOX         0x0a
#define NT_0B           0x0b
#define NT_CHRGUNFIRE   0x0c
#define NT_TOGGLE       0x12
#define NT_POSITIONHELD 0x15
#define NT_STARGUNFIRE  0x16
#define NT_HEADSPOT     0x17
#define NT_DL           0x18
#define NT_19           0x19

/* Head part numbers */
#define PART_SUNGLASSES 0x0000
#define PART_HAT        0x0001
#define PART_EYESOPEN   0x0002
#define PART_EYESCLOSED 0x0003
#define PART_HUDPIECE   0x0004

/* ── In-memory model ────────────────────────────────────────────── */

#define MAX_NODES 256
#define MAX_TEXCONFIGS 64
#define MAX_PARTS 32

struct texconfig {
	int index;             /* index in texconfigs array */
	uint32_t ptr_raw;      /* original pointer (0x05... = embedded, else external) */
	bool embedded;
	uint8_t width, height, level, format, depth, s, t, unk0b;
	/* embedded texture data */
	uint8_t *texdata;
	uint32_t texdata_len;
};

struct rodata_bbox {
	int32_t hitpart;
	float xmin, xmax, ymin, ymax, zmin, zmax;
};

struct rodata_toggle {
	int target_node_id;    /* index into model->nodes[] */
	uint16_t rwdataindex;
};

struct rodata_dl {
	uint16_t rwdataindex;
	int16_t numvertices;
	int16_t mcount;
	uint16_t numcolours;
	/* raw GDL and vertex/colour data preserved for round-trip */
	uint8_t *opagdl;   uint32_t opagdl_len;
	uint8_t *xlugdl;   uint32_t xlugdl_len;
	uint8_t *vertices;  uint32_t vertices_len;
	uint8_t *colours;   uint32_t colours_len;
};

struct rodata_chrinfo {
	uint16_t animpart;
	int16_t mtxindex;
	uint32_t unk04;
	uint16_t rwdataindex;
};

struct rodata_position {
	float pos[3];
	uint16_t part;
	int16_t mtxindexes[3];
	float drawdist;
};

struct rodata_distance {
	float near, far;
	int target_node_id;
	uint16_t rwdataindex;
};

struct rodata_reorder {
	uint32_t unk00, unk04, unk08;
	uint32_t unk0c[3];
	int node_unk18_id;
	int node_unk1c_id;
	int16_t side;
	uint16_t rwdataindex;
};

struct rodata_headspot {
	uint16_t rwdataindex;
};

/* Generic raw rodata for types we don't need to edit */
struct rodata_raw {
	uint8_t *data;
	uint32_t len;
};

struct model_node {
	int id;
	uint16_t type;          /* NT_* */
	int parent_id;          /* -1 if root */
	int next_id;            /* -1 if none */
	int prev_id;            /* -1 if none */
	int child_id;           /* -1 if none */

	/* original source offset for pointer resolution */
	uint32_t src_offset;

	/* rodata by type */
	union {
		struct rodata_bbox bbox;
		struct rodata_toggle toggle;
		struct rodata_dl dl;
		struct rodata_chrinfo chrinfo;
		struct rodata_position position;
		struct rodata_distance distance;
		struct rodata_reorder reorder;
		struct rodata_headspot headspot;
		struct rodata_raw raw;
	} rd;
};

struct model_part {
	int node_id;
	int16_t part_num;
};

struct model {
	/* modeldef fields */
	uint32_t skel;
	float scale;
	int16_t nummatrices;
	uint16_t rwdatalen;

	/* nodes */
	struct model_node nodes[MAX_NODES];
	int num_nodes;
	int root_id;

	/* parts */
	struct model_part parts[MAX_PARTS];
	int num_parts;

	/* texconfigs */
	struct texconfig texconfigs[MAX_TEXCONFIGS];
	int num_texconfigs;
};

/* ── Rare compression/decompression ─────────────────────────────── */

static uint8_t *rare_decompress(const uint8_t *input, uint32_t input_len, uint32_t *out_len) {
	if (input_len < 5) return NULL;
	uint16_t magic = be16(input);
	if (magic != 0x1173) {
		fprintf(stderr, "error: not a PD compressed file (magic=0x%04x)\n", magic);
		return NULL;
	}
	uint32_t decompressed_size = (input[2] << 16) | (input[3] << 8) | input[4];

	/* Allocate with some extra room */
	uint32_t alloc_size = decompressed_size + 4096;
	uint8_t *output = malloc(alloc_size);
	if (!output) return NULL;

	z_stream strm = {0};
	strm.next_in = (Bytef *)(input + 5);
	strm.avail_in = input_len - 5;
	strm.next_out = output;
	strm.avail_out = alloc_size;

	if (inflateInit2(&strm, -15) != Z_OK) {
		free(output);
		return NULL;
	}

	int ret = inflate(&strm, Z_FINISH);
	inflateEnd(&strm);

	if (ret != Z_STREAM_END) {
		free(output);
		return NULL;
	}

	*out_len = strm.total_out;
	return output;
}

static uint8_t *rare_compress(const uint8_t *input, uint32_t input_len, uint32_t *out_len) {
	uint32_t bound = compressBound(input_len) + 16;
	uint8_t *output = malloc(bound);
	if (!output) return NULL;

	/* Header: 0x1173 + 3-byte decompressed size */
	output[0] = 0x11;
	output[1] = 0x73;
	output[2] = (input_len >> 16) & 0xff;
	output[3] = (input_len >> 8) & 0xff;
	output[4] = input_len & 0xff;

	z_stream strm = {0};
	strm.next_in = (Bytef *)input;
	strm.avail_in = input_len;
	strm.next_out = output + 5;
	strm.avail_out = bound - 5;

	if (deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY) != Z_OK) {
		free(output);
		return NULL;
	}

	int ret = deflate(&strm, Z_FINISH);
	deflateEnd(&strm);

	if (ret != Z_STREAM_END) {
		free(output);
		return NULL;
	}

	*out_len = strm.total_out + 5;
	return output;
}

/* ── N64 Binary Parser ──────────────────────────────────────────── */

/*
 * We walk the binary following the same logic as filemodel.c's
 * populateMarkers(), but build our in-memory model directly.
 */

/* Offset lookup: find node by src_offset, return id or -1 */
static int find_node_by_offset(struct model *m, uint32_t offset) {
	if (offset == 0) return -1;
	offset &= SEGMENT_MASK;
	for (int i = 0; i < m->num_nodes; i++) {
		if (m->nodes[i].src_offset == offset) return i;
	}
	return -1;
}

/* Forward-declare for recursive parsing */
static int parse_node(struct model *m, const uint8_t *data, uint32_t file_len, uint32_t offset);

/* Compute the end offset of a section (next known structure or EOF).
 * For simplicity, since we parse linearly, we pass the file length
 * and let the caller determine section boundaries. */

static int parse_node(struct model *m, const uint8_t *data, uint32_t file_len, uint32_t offset) {
	offset &= SEGMENT_MASK;

	/* Already parsed? */
	int existing = find_node_by_offset(m, offset);
	if (existing >= 0) return existing;

	if (m->num_nodes >= MAX_NODES) {
		fprintf(stderr, "error: too many nodes (max %d)\n", MAX_NODES);
		return -1;
	}

	int id = m->num_nodes++;
	struct model_node *node = &m->nodes[id];
	memset(node, 0, sizeof(*node));
	node->id = id;
	node->src_offset = offset;
	node->parent_id = -1;
	node->next_id = -1;
	node->prev_id = -1;
	node->child_id = -1;

	/* N64 modelnode: u16 type, (2 pad), u32 rodata, u32 parent, u32 next, u32 prev, u32 child = 24 bytes */
	if (offset + 24 > file_len) {
		fprintf(stderr, "error: node at 0x%x extends past EOF\n", offset);
		return -1;
	}

	node->type = be16(data + offset) & 0xff;
	uint32_t ptr_rodata = be32(data + offset + 4);
	uint32_t ptr_parent = be32(data + offset + 8);
	uint32_t ptr_next   = be32(data + offset + 12);
	uint32_t ptr_prev   = be32(data + offset + 16);
	uint32_t ptr_child  = be32(data + offset + 20);

	/* Parse children first so we can resolve IDs */
	if (ptr_child) node->child_id = parse_node(m, data, file_len, ptr_child);
	if (ptr_next) node->next_id = parse_node(m, data, file_len, ptr_next);
	if (ptr_prev) node->prev_id = find_node_by_offset(m, ptr_prev);
	if (ptr_parent) node->parent_id = find_node_by_offset(m, ptr_parent);

	/* Parse rodata based on type */
	if (ptr_rodata) {
		uint32_t rd_off = ptr_rodata & SEGMENT_MASK;

		switch (node->type) {
		case NT_BBOX: {
			node->rd.bbox.hitpart = (int32_t)be32(data + rd_off);
			node->rd.bbox.xmin = be32_to_float(be32(data + rd_off + 4));
			node->rd.bbox.xmax = be32_to_float(be32(data + rd_off + 8));
			node->rd.bbox.ymin = be32_to_float(be32(data + rd_off + 12));
			node->rd.bbox.ymax = be32_to_float(be32(data + rd_off + 16));
			node->rd.bbox.zmin = be32_to_float(be32(data + rd_off + 20));
			node->rd.bbox.zmax = be32_to_float(be32(data + rd_off + 24));
			break;
		}
		case NT_TOGGLE: {
			uint32_t tgt = be32(data + rd_off);
			node->rd.toggle.rwdataindex = be16(data + rd_off + 4);
			/* target node will be resolved later if not yet parsed */
			if (tgt) {
				int tgt_id = find_node_by_offset(m, tgt);
				if (tgt_id < 0) tgt_id = parse_node(m, data, file_len, tgt);
				node->rd.toggle.target_node_id = tgt_id;
			} else {
				node->rd.toggle.target_node_id = -1;
			}
			break;
		}
		case NT_DL: {
			uint32_t ptr_opagdl  = be32(data + rd_off + 0);
			uint32_t ptr_xlugdl  = be32(data + rd_off + 4);
			uint32_t ptr_colours = be32(data + rd_off + 8);
			uint32_t ptr_verts   = be32(data + rd_off + 12);
			node->rd.dl.numvertices = (int16_t)be16(data + rd_off + 16);
			node->rd.dl.mcount      = (int16_t)be16(data + rd_off + 18);
			node->rd.dl.rwdataindex = be16(data + rd_off + 20);
			node->rd.dl.numcolours  = be16(data + rd_off + 22);

			/* Store raw vertex data: each vertex is 16 bytes (s16 x,y,z,pad, s16 s,t, u8 r,g,b,a) */
			/* Actually N64 Vtx is 16 bytes: s16 x,y,z, u16 flag, s16 tc[2], u8 cn[4] */
			if (ptr_verts) {
				uint32_t v_off = ptr_verts & SEGMENT_MASK;
				uint32_t v_len = node->rd.dl.numvertices * 16;
				if (v_off + v_len <= file_len) {
					node->rd.dl.vertices = malloc(v_len);
					memcpy(node->rd.dl.vertices, data + v_off, v_len);
					node->rd.dl.vertices_len = v_len;
				}
			}

			/* Store raw colour data */
			if (ptr_colours && node->rd.dl.numcolours > 0) {
				uint32_t c_off = ptr_colours & SEGMENT_MASK;
				uint32_t c_len = node->rd.dl.numcolours * 4;
				if (c_off + c_len <= file_len) {
					node->rd.dl.colours = malloc(c_len);
					memcpy(node->rd.dl.colours, data + c_off, c_len);
					node->rd.dl.colours_len = c_len;
				}
			}

			/* Store raw GDL data (opaque display list) */
			if (ptr_opagdl) {
				uint32_t g_off = ptr_opagdl & SEGMENT_MASK;
				/* GDL ends with 0xB8000000 (G_ENDDL). Scan for it. */
				uint32_t g_end = g_off;
				while (g_end + 8 <= file_len) {
					uint32_t cmd = be32(data + g_end);
					g_end += 8;
					if ((cmd >> 24) == 0xB8) break;  /* G_ENDDL */
				}
				uint32_t g_len = g_end - g_off;
				node->rd.dl.opagdl = malloc(g_len);
				memcpy(node->rd.dl.opagdl, data + g_off, g_len);
				node->rd.dl.opagdl_len = g_len;
			}

			/* Store raw GDL data (translucent display list) */
			if (ptr_xlugdl) {
				uint32_t g_off = ptr_xlugdl & SEGMENT_MASK;
				uint32_t g_end = g_off;
				while (g_end + 8 <= file_len) {
					uint32_t cmd = be32(data + g_end);
					g_end += 8;
					if ((cmd >> 24) == 0xB8) break;
				}
				uint32_t g_len = g_end - g_off;
				node->rd.dl.xlugdl = malloc(g_len);
				memcpy(node->rd.dl.xlugdl, data + g_off, g_len);
				node->rd.dl.xlugdl_len = g_len;
			}
			break;
		}
		case NT_HEADSPOT: {
			node->rd.headspot.rwdataindex = be16(data + rd_off);
			break;
		}
		case NT_CHRINFO: {
			node->rd.chrinfo.animpart = be16(data + rd_off);
			node->rd.chrinfo.mtxindex = (int16_t)be16(data + rd_off + 2);
			node->rd.chrinfo.unk04 = be32(data + rd_off + 4);
			node->rd.chrinfo.rwdataindex = be16(data + rd_off + 8);
			break;
		}
		case NT_DISTANCE: {
			node->rd.distance.near = be32_to_float(be32(data + rd_off));
			node->rd.distance.far = be32_to_float(be32(data + rd_off + 4));
			uint32_t tgt = be32(data + rd_off + 8);
			if (tgt) {
				int tgt_id = find_node_by_offset(m, tgt);
				if (tgt_id < 0) tgt_id = parse_node(m, data, file_len, tgt);
				node->rd.distance.target_node_id = tgt_id;
			} else {
				node->rd.distance.target_node_id = -1;
			}
			node->rd.distance.rwdataindex = be16(data + rd_off + 12);
			break;
		}
		default: {
			/* Store raw rodata for unknown types - we'll preserve them on round-trip.
			 * We don't know the exact size, so store a conservative amount. */
			uint32_t raw_len = 64;
			if (rd_off + raw_len > file_len) raw_len = file_len - rd_off;
			node->rd.raw.data = malloc(raw_len);
			memcpy(node->rd.raw.data, data + rd_off, raw_len);
			node->rd.raw.len = raw_len;
			break;
		}
		}
	}

	return id;
}

static bool parse_model(struct model *m, const uint8_t *data, uint32_t file_len) {
	memset(m, 0, sizeof(*m));

	/* N64 modeldef: rootnode(4) skel(4) parts(4) numparts(2) nummatrices(2) scale(4) rwdatalen(2) numtexconfigs(2) texconfigs(4) = 28 bytes */
	if (file_len < 28) {
		fprintf(stderr, "error: file too small for modeldef (%u bytes)\n", file_len);
		return false;
	}

	uint32_t ptr_rootnode = be32(data + 0);
	uint32_t ptr_skel     = be32(data + 4);
	uint32_t ptr_parts    = be32(data + 8);
	int16_t numparts      = (int16_t)be16(data + 12);
	int16_t nummatrices   = (int16_t)be16(data + 14);
	uint32_t scale_bits   = be32(data + 16);
	uint16_t rwdatalen    = be16(data + 20);
	uint16_t numtexconfigs = be16(data + 22);
	uint32_t ptr_texconfigs = be32(data + 24);

	/* The skel pointer is a raw value, not a segment pointer. For heads it should be SKEL_HEAD. */
	m->skel = ptr_skel;
	m->scale = be32_to_float(scale_bits);
	m->nummatrices = nummatrices;
	m->rwdatalen = rwdatalen;

	/* Parse node tree */
	if (ptr_rootnode) {
		m->root_id = parse_node(m, data, file_len, ptr_rootnode);
	} else {
		m->root_id = -1;
	}

	/* Parse parts array */
	if (ptr_parts && numparts > 0) {
		uint32_t parts_off = ptr_parts & SEGMENT_MASK;
		/* parts array: numparts × u32 node pointers, then numparts × s16 part numbers */
		for (int i = 0; i < numparts && i < MAX_PARTS; i++) {
			uint32_t node_ptr = be32(data + parts_off + i * 4);
			int16_t part_num = (int16_t)be16(data + parts_off + numparts * 4 + i * 2);
			m->parts[i].node_id = find_node_by_offset(m, node_ptr);
			m->parts[i].part_num = part_num;
		}
		m->num_parts = numparts < MAX_PARTS ? numparts : MAX_PARTS;
	}

	/* Parse texconfigs */
	if (ptr_texconfigs && numtexconfigs > 0) {
		uint32_t tc_off = ptr_texconfigs & SEGMENT_MASK;
		for (int i = 0; i < numtexconfigs && i < MAX_TEXCONFIGS; i++) {
			uint32_t base = tc_off + i * 12;
			if (base + 12 > file_len) break;
			struct texconfig *tc = &m->texconfigs[i];
			tc->index = i;
			tc->ptr_raw = be32(data + base);
			tc->width   = data[base + 4];
			tc->height  = data[base + 5];
			tc->level   = data[base + 6];
			tc->format  = data[base + 7];
			tc->depth   = data[base + 8];
			tc->s       = data[base + 9];
			tc->t       = data[base + 10];
			tc->unk0b   = data[base + 11];
			tc->embedded = (tc->ptr_raw & 0xff000000) == SEGMENT;

			/* Copy embedded texture data */
			if (tc->embedded) {
				uint32_t tex_off = tc->ptr_raw & SEGMENT_MASK;
				/* Estimate size from dimensions and depth */
				uint32_t bpp = tc->depth;
				uint32_t tex_size;
				if (bpp == 0) bpp = 16; /* default RGBA16 */
				tex_size = (uint32_t)tc->width * tc->height * bpp / 8;
				if (tex_size == 0) tex_size = 64;
				if (tex_off + tex_size > file_len) tex_size = file_len - tex_off;
				tc->texdata = malloc(tex_size);
				memcpy(tc->texdata, data + tex_off, tex_size);
				tc->texdata_len = tex_size;
			}
			m->num_texconfigs++;
		}
	}

	return true;
}

/* ── JSON Output ────────────────────────────────────────────────── */

static const char *node_type_name(uint16_t type) {
	switch (type) {
	case NT_CHRINFO:      return "CHRINFO";
	case NT_POSITION:     return "POSITION";
	case NT_GUNDL:        return "GUNDL";
	case NT_DISTANCE:     return "DISTANCE";
	case NT_REORDER:      return "REORDER";
	case NT_BBOX:         return "BBOX";
	case NT_CHRGUNFIRE:   return "CHRGUNFIRE";
	case NT_TOGGLE:       return "TOGGLE";
	case NT_POSITIONHELD: return "POSITIONHELD";
	case NT_STARGUNFIRE:  return "STARGUNFIRE";
	case NT_HEADSPOT:     return "HEADSPOT";
	case NT_DL:           return "DL";
	case NT_19:           return "TYPE19";
	default:              return "UNKNOWN";
	}
}

static const char *part_name(int16_t num) {
	switch (num) {
	case PART_SUNGLASSES: return "SUNGLASSES";
	case PART_HAT:        return "HAT";
	case PART_EYESOPEN:   return "EYESOPEN";
	case PART_EYESCLOSED: return "EYESCLOSED";
	case PART_HUDPIECE:   return "HUDPIECE";
	default:              return NULL;
	}
}

static const char *format_name(uint8_t format, uint8_t depth) {
	/* N64 texture format encoding */
	switch (format) {
	case 0:
		switch (depth) {
		case 16: return "RGBA16";
		case 32: return "RGBA32";
		default: return "RGBA";
		}
	case 2:
		switch (depth) {
		case 4:  return "CI4";
		case 8:  return "CI8";
		default: return "CI";
		}
	case 3:
		switch (depth) {
		case 4:  return "IA4";
		case 8:  return "IA8";
		case 16: return "IA16";
		default: return "IA";
		}
	case 4:
		switch (depth) {
		case 4:  return "I4";
		case 8:  return "I8";
		default: return "I";
		}
	default: return "UNKNOWN";
	}
}

static bool add_unique_u16(uint16_t *arr, int *count, int max, uint16_t value) {
	for (int i = 0; i < *count; i++) {
		if (arr[i] == value) return true;
	}
	if (*count >= max) return false;
	arr[(*count)++] = value;
	return true;
}

static void collect_ids_from_gdl(const uint8_t *gdl, uint32_t gdl_len,
		uint16_t *ids, int *num_ids, int max_ids)
{
	for (uint32_t off = 0; off + 8 <= gdl_len; off += 8) {
		uint32_t w0 = be32(gdl + off);
		uint32_t w1 = be32(gdl + off + 4);
		uint8_t cmd = (uint8_t)((w0 >> 24) & 0xff);

		if (cmd == 0xc0) {
			uint16_t t0 = (uint16_t)(w1 & 0x0fff);
			add_unique_u16(ids, num_ids, max_ids, t0);

			/* Repurposed G_NOOP dual-texture form: only subcmd==1 has t1.
			 * subcmd is 3 bits (w0 bits 2..0), not 8. Masking 0xff folded in
			 * w0 bits 7..3, which are the bottom of the `flags` field; that
			 * read the same as 3 bits only because every 0xc0 command in
			 * vanilla ROM data has flags of 0 or 64, leaving bits 8..3 clear. */
			uint8_t subcmd = (uint8_t)(w0 & 0x7);
			if (subcmd == 1) {
				uint16_t t1 = (uint16_t)((w1 >> 12) & 0x0fff);
				add_unique_u16(ids, num_ids, max_ids, t1);
			}
		}

		if (cmd == 0xb8) {
			break;
		}
	}
}

static int cmp_u16(const void *a, const void *b) {
	uint16_t ua = *(const uint16_t *)a;
	uint16_t ub = *(const uint16_t *)b;
	if (ua < ub) return -1;
	if (ua > ub) return 1;
	return 0;
}

static int collect_model_gdl_texture_ids(struct model *m, uint16_t *ids, int max_ids) {
	int num_ids = 0;

	for (int i = 0; i < m->num_nodes; i++) {
		struct model_node *n = &m->nodes[i];
		if (n->type != NT_DL) continue;

		if (n->rd.dl.opagdl && n->rd.dl.opagdl_len > 0) {
			collect_ids_from_gdl(n->rd.dl.opagdl, n->rd.dl.opagdl_len, ids, &num_ids, max_ids);
		}
		if (n->rd.dl.xlugdl && n->rd.dl.xlugdl_len > 0) {
			collect_ids_from_gdl(n->rd.dl.xlugdl, n->rd.dl.xlugdl_len, ids, &num_ids, max_ids);
		}
	}

	qsort(ids, num_ids, sizeof(ids[0]), cmp_u16);
	return num_ids;
}

/* Collect children of a node into an array */
static int get_children(struct model *m, int node_id, int *out, int max) {
	int count = 0;
	int cid = m->nodes[node_id].child_id;
	while (cid >= 0 && count < max) {
		out[count++] = cid;
		cid = m->nodes[cid].next_id;
	}
	return count;
}

static void json_emit_node(struct model *m, int node_id, int indent);

static void json_indent(int n) {
	for (int i = 0; i < n; i++) printf("  ");
}

static void json_emit_node(struct model *m, int node_id, int indent) {
	struct model_node *n = &m->nodes[node_id];

	json_indent(indent);
	printf("{\n");

	json_indent(indent + 1); printf("\"id\": %d,\n", n->id);
	json_indent(indent + 1); printf("\"type\": \"%s\",\n", node_type_name(n->type));
	json_indent(indent + 1); printf("\"type_num\": %d,\n", n->type);

	/* Check if this node is a registered part */
	int part_idx = -1;
	for (int i = 0; i < m->num_parts; i++) {
		if (m->parts[i].node_id == node_id) {
			part_idx = i;
			break;
		}
	}
	if (part_idx >= 0) {
		const char *pn = part_name(m->parts[part_idx].part_num);
		json_indent(indent + 1);
		printf("\"part_num\": %d,\n", m->parts[part_idx].part_num);
		if (pn) {
			json_indent(indent + 1);
			printf("\"part_name\": \"%s\",\n", pn);
		}
	}

	/* Type-specific rodata */
	json_indent(indent + 1); printf("\"rodata\": ");
	switch (n->type) {
	case NT_BBOX:
		printf("{\n");
		json_indent(indent + 2); printf("\"hitpart\": %d,\n", n->rd.bbox.hitpart);
		json_indent(indent + 2); printf("\"xmin\": %.4f, \"xmax\": %.4f,\n", n->rd.bbox.xmin, n->rd.bbox.xmax);
		json_indent(indent + 2); printf("\"ymin\": %.4f, \"ymax\": %.4f,\n", n->rd.bbox.ymin, n->rd.bbox.ymax);
		json_indent(indent + 2); printf("\"zmin\": %.4f, \"zmax\": %.4f\n", n->rd.bbox.zmin, n->rd.bbox.zmax);
		json_indent(indent + 1); printf("}");
		break;
	case NT_TOGGLE:
		printf("{\n");
		json_indent(indent + 2); printf("\"target_node_id\": %d,\n", n->rd.toggle.target_node_id);
		json_indent(indent + 2); printf("\"rwdataindex\": %d\n", n->rd.toggle.rwdataindex);
		json_indent(indent + 1); printf("}");
		break;
	case NT_DL:
		printf("{\n");
		json_indent(indent + 2); printf("\"numvertices\": %d,\n", n->rd.dl.numvertices);
		json_indent(indent + 2); printf("\"numcolours\": %d,\n", n->rd.dl.numcolours);
		json_indent(indent + 2); printf("\"mcount\": %d,\n", n->rd.dl.mcount);
		json_indent(indent + 2); printf("\"rwdataindex\": %d,\n", n->rd.dl.rwdataindex);
		json_indent(indent + 2); printf("\"opagdl_len\": %u,\n", n->rd.dl.opagdl_len);
		json_indent(indent + 2); printf("\"xlugdl_len\": %u\n", n->rd.dl.xlugdl_len);
		json_indent(indent + 1); printf("}");
		break;
	case NT_HEADSPOT:
		printf("{\n");
		json_indent(indent + 2); printf("\"rwdataindex\": %d\n", n->rd.headspot.rwdataindex);
		json_indent(indent + 1); printf("}");
		break;
	case NT_CHRINFO:
		printf("{\n");
		json_indent(indent + 2); printf("\"animpart\": %d,\n", n->rd.chrinfo.animpart);
		json_indent(indent + 2); printf("\"mtxindex\": %d,\n", n->rd.chrinfo.mtxindex);
		json_indent(indent + 2); printf("\"rwdataindex\": %d\n", n->rd.chrinfo.rwdataindex);
		json_indent(indent + 1); printf("}");
		break;
	case NT_DISTANCE:
		printf("{\n");
		json_indent(indent + 2); printf("\"near\": %.4f,\n", n->rd.distance.near);
		json_indent(indent + 2); printf("\"far\": %.4f,\n", n->rd.distance.far);
		json_indent(indent + 2); printf("\"target_node_id\": %d,\n", n->rd.distance.target_node_id);
		json_indent(indent + 2); printf("\"rwdataindex\": %d\n", n->rd.distance.rwdataindex);
		json_indent(indent + 1); printf("}");
		break;
	default:
		printf("null");
		break;
	}
	printf(",\n");

	/* Children */
	int children[64];
	int nchild = get_children(m, node_id, children, 64);
	json_indent(indent + 1); printf("\"children\": [\n");
	for (int i = 0; i < nchild; i++) {
		json_emit_node(m, children[i], indent + 2);
		if (i < nchild - 1) printf(",");
		printf("\n");
	}
	json_indent(indent + 1); printf("]\n");

	json_indent(indent);
	printf("}");
}

static void json_emit_model(struct model *m, const char *filename) {
	printf("{\n");
	printf("  \"file\": \"%s\",\n", filename);

	uint16_t gdl_tex_ids[512];
	int num_gdl_tex_ids = collect_model_gdl_texture_ids(m, gdl_tex_ids, 512);

	/* modeldef */
	printf("  \"modeldef\": {\n");
	printf("    \"skel\": %u,\n", m->skel);
	printf("    \"skel_name\": \"%s\",\n", m->skel == SKEL_HEAD ? "SKEL_HEAD" : "OTHER");
	printf("    \"scale\": %.10g,\n", m->scale);
	printf("    \"numparts\": %d,\n", m->num_parts);
	printf("    \"nummatrices\": %d,\n", m->nummatrices);
	printf("    \"rwdatalen\": %u,\n", m->rwdatalen);
	printf("    \"numtexconfigs\": %d\n", m->num_texconfigs);
	printf("  },\n");

	/* node tree */
	printf("  \"root\": ");
	if (m->root_id >= 0) {
		json_emit_node(m, m->root_id, 1);
	} else {
		printf("null");
	}
	printf(",\n");

	/* texconfigs */
	printf("  \"texconfigs\": [\n");
	for (int i = 0; i < m->num_texconfigs; i++) {
		struct texconfig *tc = &m->texconfigs[i];
		printf("    {\n");
		printf("      \"index\": %d,\n", tc->index);
		printf("      \"width\": %u,\n", tc->width);
		printf("      \"height\": %u,\n", tc->height);
		printf("      \"format\": \"%s\",\n", format_name(tc->format, tc->depth));
		printf("      \"format_num\": %u,\n", tc->format);
		printf("      \"depth\": %u,\n", tc->depth);
		printf("      \"embedded\": %s,\n", tc->embedded ? "true" : "false");
		printf("      \"ptr_raw\": %u,\n", tc->ptr_raw);
		printf("      \"texdata_len\": %u\n", tc->texdata_len);
		printf("    }%s\n", i < m->num_texconfigs - 1 ? "," : "");
	}
	printf("  ],\n");

	/* Actual texture IDs referenced by G_NOOP texture-binding commands in GDL. */
	printf("  \"gdl_texture_ids\": [\n");
	for (int i = 0; i < num_gdl_tex_ids; i++) {
		printf("    { \"id\": %u, \"hex\": \"0x%04x\" }%s\n",
			(unsigned)gdl_tex_ids[i], (unsigned)gdl_tex_ids[i],
			i < num_gdl_tex_ids - 1 ? "," : "");
	}
	printf("  ],\n");

	/* parts list */
	printf("  \"parts\": [\n");
	for (int i = 0; i < m->num_parts; i++) {
		const char *pn = part_name(m->parts[i].part_num);
		printf("    { \"part_num\": %d, \"part_name\": %s%s%s, \"node_id\": %d }%s\n",
			m->parts[i].part_num,
			pn ? "\"" : "", pn ? pn : "null", pn ? "\"" : "",
			m->parts[i].node_id,
			i < m->num_parts - 1 ? "," : "");
	}
	printf("  ]\n");

	printf("}\n");
}

/* ── Validation ─────────────────────────────────────────────────── */

struct validation_result {
	bool skel_head;
	bool has_sunglasses;
	bool has_hat;
	bool has_eyesopen;
	bool has_eyesclosed;
	bool has_hudpiece;
	int sunglasses_node;
	int hat_node;
	int eyesopen_node;
	int eyesclosed_node;
	int hudpiece_node;
	int num_dl_nodes;
	int num_toggle_nodes;
	int total_vertices;
};

static struct validation_result validate_model(struct model *m) {
	struct validation_result v = {0};
	v.sunglasses_node = -1;
	v.hat_node = -1;
	v.eyesopen_node = -1;
	v.eyesclosed_node = -1;
	v.hudpiece_node = -1;

	v.skel_head = (m->skel == SKEL_HEAD);

	for (int i = 0; i < m->num_parts; i++) {
		switch (m->parts[i].part_num) {
		case PART_SUNGLASSES:
			v.has_sunglasses = true;
			v.sunglasses_node = m->parts[i].node_id;
			break;
		case PART_HAT:
			v.has_hat = true;
			v.hat_node = m->parts[i].node_id;
			break;
		case PART_EYESOPEN:
			v.has_eyesopen = true;
			v.eyesopen_node = m->parts[i].node_id;
			break;
		case PART_EYESCLOSED:
			v.has_eyesclosed = true;
			v.eyesclosed_node = m->parts[i].node_id;
			break;
		case PART_HUDPIECE:
			v.has_hudpiece = true;
			v.hudpiece_node = m->parts[i].node_id;
			break;
		}
	}

	for (int i = 0; i < m->num_nodes; i++) {
		if (m->nodes[i].type == NT_DL) {
			v.num_dl_nodes++;
			v.total_vertices += m->nodes[i].rd.dl.numvertices;
		}
		if (m->nodes[i].type == NT_TOGGLE) {
			v.num_toggle_nodes++;
		}
	}

	return v;
}

static void json_emit_validation(struct model *m, const char *filename) {
	struct validation_result v = validate_model(m);

	printf("{\n");
	printf("  \"file\": \"%s\",\n", filename);
	printf("  \"skel_head\": %s,\n", v.skel_head ? "true" : "false");
	printf("  \"num_nodes\": %d,\n", m->num_nodes);
	printf("  \"num_dl_nodes\": %d,\n", v.num_dl_nodes);
	printf("  \"num_toggle_nodes\": %d,\n", v.num_toggle_nodes);
	printf("  \"total_vertices\": %d,\n", v.total_vertices);
	printf("  \"num_texconfigs\": %d,\n", m->num_texconfigs);
	printf("  \"parts\": {\n");
	printf("    \"SUNGLASSES\": { \"present\": %s, \"node_id\": %d },\n",
		v.has_sunglasses ? "true" : "false", v.sunglasses_node);
	printf("    \"HAT\": { \"present\": %s, \"node_id\": %d },\n",
		v.has_hat ? "true" : "false", v.hat_node);
	printf("    \"EYESOPEN\": { \"present\": %s, \"node_id\": %d },\n",
		v.has_eyesopen ? "true" : "false", v.eyesopen_node);
	printf("    \"EYESCLOSED\": { \"present\": %s, \"node_id\": %d },\n",
		v.has_eyesclosed ? "true" : "false", v.eyesclosed_node);
	printf("    \"HUDPIECE\": { \"present\": %s, \"node_id\": %d }\n",
		v.has_hudpiece ? "true" : "false", v.hudpiece_node);
	printf("  }\n");
	printf("}\n");
}

/* ── Edit Operations ────────────────────────────────────────────── */

/*
 * Minimal JSON parser for edit ops. We only need to handle:
 *   { "ops": [ { "op": "...", ... }, ... ] }
 *
 * For robustness we use a simple token-based approach.
 */

/* Skip whitespace */
static const char *skip_ws(const char *p) {
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	return p;
}

/* Read a quoted string, return pointer past closing quote. out receives string content. */
static const char *read_string(const char *p, char *out, int max) {
	if (*p != '"') return NULL;
	p++;
	int i = 0;
	while (*p && *p != '"' && i < max - 1) {
		out[i++] = *p++;
	}
	out[i] = '\0';
	if (*p == '"') p++;
	return p;
}

/* Read an integer */
static const char *read_int(const char *p, int *out) {
	char *end;
	*out = (int)strtol(p, &end, 10);
	return end;
}

/* Find a key in current object scope (very basic) */
static const char *find_key(const char *p, const char *key) {
	char buf[128];
	int depth = 0;

	while (*p) {
		p = skip_ws(p);
		if (*p == '{' || *p == '[') { depth++; p++; continue; }
		if (*p == '}' || *p == ']') { depth--; p++; continue; }
		if (*p == '"' && depth == 1) {
			p = read_string(p, buf, sizeof(buf));
			p = skip_ws(p);
			if (*p == ':') p++;
			p = skip_ws(p);
			if (strcmp(buf, key) == 0) return p;
			/* Skip value */
			if (*p == '"') {
				p = read_string(p, buf, sizeof(buf));
			} else if (*p == '{' || *p == '[') {
				int vd = 1; p++;
				while (*p && vd > 0) {
					if (*p == '{' || *p == '[') vd++;
					else if (*p == '}' || *p == ']') vd--;
					p++;
				}
			} else {
				while (*p && *p != ',' && *p != '}' && *p != ']') p++;
			}
		} else {
			p++;
		}
	}
	return NULL;
}

struct edit_op {
	char type[32];
	char part_name[32];
	int child_node_id;
	int node_id;
	int texconfig_index;
};

static int parse_edit_ops(const char *json, struct edit_op *ops, int max_ops) {
	int count = 0;

	/* Find "ops" array */
	const char *p = find_key(json, "ops");
	if (!p) return -1;
	p = skip_ws(p);
	if (*p != '[') return -1;
	p++;

	while (count < max_ops) {
		p = skip_ws(p);
		if (*p == ']') break;
		if (*p == ',') { p++; continue; }
		if (*p != '{') break;

		/* Parse a single op object */
		struct edit_op *op = &ops[count];
		memset(op, 0, sizeof(*op));
		op->child_node_id = -1;
		op->node_id = -1;
		op->texconfig_index = -1;

		int depth = 1;
		p++;
		while (*p && depth > 0) {
			p = skip_ws(p);
			if (*p == '}') { depth--; p++; break; }
			if (*p == ',') { p++; continue; }

			char key[64] = "";
			if (*p == '"') {
				p = read_string(p, key, sizeof(key));
				p = skip_ws(p);
				if (*p == ':') p++;
				p = skip_ws(p);

				if (strcmp(key, "op") == 0) {
					p = read_string(p, op->type, sizeof(op->type));
				} else if (strcmp(key, "part_name") == 0) {
					p = read_string(p, op->part_name, sizeof(op->part_name));
				} else if (strcmp(key, "child_node_id") == 0) {
					p = read_int(p, &op->child_node_id);
				} else if (strcmp(key, "node_id") == 0) {
					p = read_int(p, &op->node_id);
				} else if (strcmp(key, "texconfig_index") == 0) {
					p = read_int(p, &op->texconfig_index);
				} else {
					/* skip unknown value */
					if (*p == '"') { char tmp[256]; p = read_string(p, tmp, sizeof(tmp)); }
					else { while (*p && *p != ',' && *p != '}') p++; }
				}
			} else {
				p++;
			}
		}
		count++;
	}

	return count;
}

static int16_t part_num_from_name(const char *name) {
	if (strcmp(name, "SUNGLASSES") == 0) return PART_SUNGLASSES;
	if (strcmp(name, "HAT") == 0) return PART_HAT;
	if (strcmp(name, "EYESOPEN") == 0) return PART_EYESOPEN;
	if (strcmp(name, "EYESCLOSED") == 0) return PART_EYESCLOSED;
	if (strcmp(name, "HUDPIECE") == 0) return PART_HUDPIECE;
	return -1;
}

/* Add a TOGGLE node wrapping an existing child node and register it as a part */
static bool op_add_toggle(struct model *m, const char *pname, int child_node_id,
                          char *errbuf, int errbuf_len) {
	int16_t pnum = part_num_from_name(pname);
	if (pnum < 0) {
		snprintf(errbuf, errbuf_len, "add_toggle: unknown part name '%s'", pname);
		return false;
	}

	/* Check part doesn't already exist */
	for (int i = 0; i < m->num_parts; i++) {
		if (m->parts[i].part_num == pnum) {
			snprintf(errbuf, errbuf_len, "add_toggle: part %s already exists (node %d)",
				pname, m->parts[i].node_id);
			return false;
		}
	}

	if (child_node_id < 0 || child_node_id >= m->num_nodes) {
		snprintf(errbuf, errbuf_len, "add_toggle: invalid child_node_id %d", child_node_id);
		return false;
	}

	if (m->num_nodes >= MAX_NODES) {
		snprintf(errbuf, errbuf_len, "add_toggle: too many nodes");
		return false;
	}

	if (m->num_parts >= MAX_PARTS) {
		snprintf(errbuf, errbuf_len, "add_toggle: too many parts");
		return false;
	}

	/* Create new TOGGLE node */
	int new_id = m->num_nodes++;
	struct model_node *toggle = &m->nodes[new_id];
	memset(toggle, 0, sizeof(*toggle));
	toggle->id = new_id;
	toggle->type = NT_TOGGLE;
	toggle->parent_id = -1;
	toggle->next_id = -1;
	toggle->prev_id = -1;
	toggle->child_id = -1;
	toggle->rd.toggle.target_node_id = child_node_id;
	toggle->rd.toggle.rwdataindex = m->rwdatalen;
	m->rwdatalen += 1; /* toggle rwdata is 1 word */

	/* Find the child's current parent and insert toggle in its place */
	struct model_node *child = &m->nodes[child_node_id];

	if (child->parent_id >= 0) {
		struct model_node *parent = &m->nodes[child->parent_id];
		toggle->parent_id = child->parent_id;

		/* Replace child in parent's child list */
		if (parent->child_id == child_node_id) {
			parent->child_id = new_id;
		}
	}

	/* Take child's position in sibling chain */
	toggle->next_id = child->next_id;
	toggle->prev_id = child->prev_id;

	if (child->prev_id >= 0) {
		m->nodes[child->prev_id].next_id = new_id;
	}
	if (child->next_id >= 0) {
		m->nodes[child->next_id].prev_id = new_id;
	}

	/* Detach child from sibling chain and reparent under toggle */
	child->parent_id = new_id;
	child->prev_id = -1;
	child->next_id = -1;
	toggle->child_id = child_node_id;

	/* Register as a part */
	m->parts[m->num_parts].node_id = new_id;
	m->parts[m->num_parts].part_num = pnum;
	m->num_parts++;

	return true;
}

/* Rebind a DL node's display list to reference a different texture config.
 * This patches the GDL commands that load textures. For now we do a simpler
 * version: just record the intent. The actual GDL patching would require
 * parsing GBI commands — for the prototype, we note this as a limitation. */
static bool op_rebind_texture(struct model *m, int node_id, int texconfig_idx,
                              char *errbuf, int errbuf_len) {
	if (node_id < 0 || node_id >= m->num_nodes) {
		snprintf(errbuf, errbuf_len, "rebind_texture: invalid node_id %d", node_id);
		return false;
	}
	if (m->nodes[node_id].type != NT_DL) {
		snprintf(errbuf, errbuf_len, "rebind_texture: node %d is %s, not DL",
			node_id, node_type_name(m->nodes[node_id].type));
		return false;
	}
	if (texconfig_idx < 0 || texconfig_idx >= m->num_texconfigs) {
		snprintf(errbuf, errbuf_len, "rebind_texture: invalid texconfig_index %d", texconfig_idx);
		return false;
	}

	/* Scan the opaque GDL for G_SETTIMG commands (0xFD) and patch the segment address.
	 * G_SETTIMG format: FD tt0000 AAAAAAAA
	 * We replace the address (word 2) with the texconfig's pointer offset.
	 * This is a simplified approach that works for single-texture DL nodes. */
	struct model_node *n = &m->nodes[node_id];
	if (!n->rd.dl.opagdl || n->rd.dl.opagdl_len < 8) {
		snprintf(errbuf, errbuf_len, "rebind_texture: node %d has no opaque display list", node_id);
		return false;
	}

	bool patched = false;
	for (uint32_t off = 0; off + 8 <= n->rd.dl.opagdl_len; off += 8) {
		uint8_t cmd = n->rd.dl.opagdl[off];
		if (cmd == 0xFD) { /* G_SETTIMG */
			/* Patch the address in second word.
			 * We set it to 0x05000000 | texconfig offset.
			 * The actual texture offset will be resolved during serialization. */
			uint32_t tex_marker = SEGMENT | (0x00F00000 + texconfig_idx);
			put_be32(n->rd.dl.opagdl + off + 4, tex_marker);
			patched = true;
			break; /* Only patch first G_SETTIMG for now */
		}
	}

	if (!patched) {
		snprintf(errbuf, errbuf_len, "rebind_texture: no G_SETTIMG found in node %d GDL", node_id);
		return false;
	}

	return true;
}

static bool apply_edits(struct model *m, const char *json, char *errbuf, int errbuf_len) {
	struct edit_op ops[64];
	int num_ops = parse_edit_ops(json, ops, 64);

	if (num_ops < 0) {
		snprintf(errbuf, errbuf_len, "failed to parse edit ops JSON");
		return false;
	}

	if (num_ops == 0) {
		snprintf(errbuf, errbuf_len, "no ops found in input");
		return false;
	}

	/* Validate all ops before applying */
	for (int i = 0; i < num_ops; i++) {
		struct edit_op *op = &ops[i];

		if (strcmp(op->type, "add_toggle") == 0) {
			if (op->part_name[0] == '\0') {
				snprintf(errbuf, errbuf_len, "op %d: add_toggle requires part_name", i);
				return false;
			}
			if (op->child_node_id < 0) {
				snprintf(errbuf, errbuf_len, "op %d: add_toggle requires child_node_id", i);
				return false;
			}
		} else if (strcmp(op->type, "rebind_texture") == 0) {
			if (op->node_id < 0) {
				snprintf(errbuf, errbuf_len, "op %d: rebind_texture requires node_id", i);
				return false;
			}
			if (op->texconfig_index < 0) {
				snprintf(errbuf, errbuf_len, "op %d: rebind_texture requires texconfig_index", i);
				return false;
			}
		} else {
			snprintf(errbuf, errbuf_len, "op %d: unknown op type '%s'", i, op->type);
			return false;
		}
	}

	/* Apply all ops — if any single op fails, we report which one. */
	for (int i = 0; i < num_ops; i++) {
		struct edit_op *op = &ops[i];
		bool ok;

		if (strcmp(op->type, "add_toggle") == 0) {
			ok = op_add_toggle(m, op->part_name, op->child_node_id, errbuf, errbuf_len);
		} else if (strcmp(op->type, "rebind_texture") == 0) {
			ok = op_rebind_texture(m, op->node_id, op->texconfig_index, errbuf, errbuf_len);
		} else {
			ok = false;
		}

		if (!ok) {
			char tmp[512];
			snprintf(tmp, sizeof(tmp), "op %d failed: %s", i, errbuf);
			strncpy(errbuf, tmp, errbuf_len);
			errbuf[errbuf_len - 1] = '\0';
			return false;
		}
	}

	return true;
}

/* ── Serializer (full rebuild) ──────────────────────────────────── */

/*
 * Rebuild a fresh N64 big-endian binary from the in-memory model.
 * Layout matches what the PD engine expects:
 *
 *   [modeldef] [node tree] [rodata sections] [vertices/colours] [GDL] [texconfigs] [texdata] [parts]
 *
 * All pointers are segment-relative (0x05xxxxxx).
 */

/* Alignment helper */
static uint32_t align_to(uint32_t pos, uint32_t alignment) {
	return (pos + alignment - 1) & ~(alignment - 1);
}

/* Growable buffer for building the output binary */
struct outbuf {
	uint8_t *data;
	uint32_t len;
	uint32_t cap;
};

static void outbuf_init(struct outbuf *b) {
	b->cap = 65536;
	b->data = calloc(1, b->cap);
	b->len = 0;
}

static void outbuf_ensure(struct outbuf *b, uint32_t needed) {
	while (b->len + needed > b->cap) {
		b->cap *= 2;
		b->data = realloc(b->data, b->cap);
		memset(b->data + b->cap / 2, 0, b->cap / 2);
	}
}

static uint32_t outbuf_alloc(struct outbuf *b, uint32_t size, uint32_t alignment) {
	b->len = align_to(b->len, alignment);
	uint32_t pos = b->len;
	outbuf_ensure(b, size);
	b->len += size;
	return pos;
}

static void outbuf_write(struct outbuf *b, uint32_t pos, const void *data, uint32_t len) {
	outbuf_ensure(b, (pos + len > b->len) ? pos + len - b->len + 1 : 0);
	memcpy(b->data + pos, data, len);
	if (pos + len > b->len) b->len = pos + len;
}

/*
 * Serialization proceeds in two passes:
 * 1. Allocate positions for all structures (assign offsets)
 * 2. Write data and fill in pointers
 */

struct serial_ctx {
	struct outbuf buf;
	uint32_t node_offsets[MAX_NODES];      /* offset of each modelnode in output */
	uint32_t rodata_offsets[MAX_NODES];    /* offset of each node's rodata */
	uint32_t vtx_offsets[MAX_NODES];       /* DL vertex data offsets */
	uint32_t col_offsets[MAX_NODES];       /* DL colour data offsets */
	uint32_t opagdl_offsets[MAX_NODES];    /* DL opaque GDL offsets */
	uint32_t xlugdl_offsets[MAX_NODES];    /* DL translucent GDL offsets */
	uint32_t texconfig_offset;
	uint32_t texdata_offsets[MAX_TEXCONFIGS];
	uint32_t parts_offset;
};

/* Recursively count all nodes in tree order for allocation */
static void collect_nodes_dfs(struct model *m, int node_id, int *order, int *count) {
	if (node_id < 0) return;
	order[(*count)++] = node_id;
	/* child first, then siblings */
	int cid = m->nodes[node_id].child_id;
	while (cid >= 0) {
		collect_nodes_dfs(m, cid, order, count);
		cid = m->nodes[cid].next_id;
	}
}

static uint32_t rodata_size_for_type(uint16_t type) {
	switch (type) {
	case NT_BBOX:         return 28;
	case NT_TOGGLE:       return 6;
	case NT_DL:           return 24; /* 6 * 4 bytes */
	case NT_HEADSPOT:     return 2;
	case NT_CHRINFO:      return 10;
	case NT_DISTANCE:     return 14;
	case NT_POSITION:     return 24;
	default:              return 0; /* unknown: use raw length */
	}
}

static bool serialize_model(struct model *m, struct outbuf *out) {
	struct serial_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	outbuf_init(&ctx.buf);

	/* Collect nodes in DFS order */
	int order[MAX_NODES];
	int node_count = 0;
	if (m->root_id >= 0) {
		collect_nodes_dfs(m, m->root_id, order, &node_count);
	}

	/* Pass 1: allocate positions */

	/* modeldef header: 28 bytes, 16-byte aligned */
	uint32_t modeldef_pos = outbuf_alloc(&ctx.buf, 28, 16);
	(void)modeldef_pos; /* always 0 */

	/* Nodes: 24 bytes each (u16 type + 2 pad + 5×u32), 4-byte aligned */
	for (int i = 0; i < node_count; i++) {
		ctx.node_offsets[order[i]] = outbuf_alloc(&ctx.buf, 24, 4);
	}

	/* Rodata for each node */
	for (int i = 0; i < node_count; i++) {
		int nid = order[i];
		struct model_node *n = &m->nodes[nid];
		uint32_t rd_size = rodata_size_for_type(n->type);
		if (rd_size == 0 && n->type != NT_TOGGLE) {
			/* Unknown type with raw data */
			if (n->rd.raw.data && n->rd.raw.len > 0) {
				rd_size = n->rd.raw.len;
			}
		}
		if (rd_size > 0) {
			int align = (n->type == NT_DL) ? 8 : 4;
			ctx.rodata_offsets[nid] = outbuf_alloc(&ctx.buf, rd_size, align);
		}
	}

	/* Vertex + colour data for DL nodes */
	for (int i = 0; i < node_count; i++) {
		int nid = order[i];
		struct model_node *n = &m->nodes[nid];
		if (n->type == NT_DL) {
			if (n->rd.dl.vertices && n->rd.dl.vertices_len > 0) {
				ctx.vtx_offsets[nid] = outbuf_alloc(&ctx.buf, n->rd.dl.vertices_len, 8);
			}
			if (n->rd.dl.colours && n->rd.dl.colours_len > 0) {
				ctx.col_offsets[nid] = outbuf_alloc(&ctx.buf, n->rd.dl.colours_len, 8);
			}
		}
	}

	/* GDL data for DL nodes */
	for (int i = 0; i < node_count; i++) {
		int nid = order[i];
		struct model_node *n = &m->nodes[nid];
		if (n->type == NT_DL) {
			if (n->rd.dl.opagdl && n->rd.dl.opagdl_len > 0) {
				ctx.opagdl_offsets[nid] = outbuf_alloc(&ctx.buf, n->rd.dl.opagdl_len, 8);
			}
			if (n->rd.dl.xlugdl && n->rd.dl.xlugdl_len > 0) {
				ctx.xlugdl_offsets[nid] = outbuf_alloc(&ctx.buf, n->rd.dl.xlugdl_len, 8);
			}
		}
	}

	/* Texconfigs */
	if (m->num_texconfigs > 0) {
		ctx.texconfig_offset = outbuf_alloc(&ctx.buf, m->num_texconfigs * 12, 4);
	}

	/* Embedded texture data */
	for (int i = 0; i < m->num_texconfigs; i++) {
		if (m->texconfigs[i].embedded && m->texconfigs[i].texdata) {
			ctx.texdata_offsets[i] = outbuf_alloc(&ctx.buf, m->texconfigs[i].texdata_len, 8);
		}
	}

	/* Parts array: numparts × u32 + numparts × s16 */
	if (m->num_parts > 0) {
		uint32_t parts_size = m->num_parts * 4 + m->num_parts * 2;
		ctx.parts_offset = outbuf_alloc(&ctx.buf, parts_size, 4);
	}

	/* Pass 2: write data */

	/* Modeldef */
	uint32_t root_seg = (m->root_id >= 0) ? (SEGMENT | ctx.node_offsets[m->root_id]) : 0;
	put_be32(ctx.buf.data + 0, root_seg);
	put_be32(ctx.buf.data + 4, m->skel);
	put_be32(ctx.buf.data + 8, m->num_parts > 0 ? (SEGMENT | ctx.parts_offset) : 0);
	put_be16(ctx.buf.data + 12, (uint16_t)m->num_parts);
	put_be16(ctx.buf.data + 14, (uint16_t)m->nummatrices);
	put_be32(ctx.buf.data + 16, float_to_be32(m->scale));
	put_be16(ctx.buf.data + 20, m->rwdatalen);
	put_be16(ctx.buf.data + 22, (uint16_t)m->num_texconfigs);
	put_be32(ctx.buf.data + 24, m->num_texconfigs > 0 ? (SEGMENT | ctx.texconfig_offset) : 0);

	/* Nodes */
	for (int i = 0; i < node_count; i++) {
		int nid = order[i];
		struct model_node *n = &m->nodes[nid];
		uint32_t off = ctx.node_offsets[nid];

		put_be16(ctx.buf.data + off + 0, n->type);
		put_be16(ctx.buf.data + off + 2, 0); /* padding */
		put_be32(ctx.buf.data + off + 4, ctx.rodata_offsets[nid] ? (SEGMENT | ctx.rodata_offsets[nid]) : 0);
		put_be32(ctx.buf.data + off + 8, n->parent_id >= 0 ? (SEGMENT | ctx.node_offsets[n->parent_id]) : 0);
		put_be32(ctx.buf.data + off + 12, n->next_id >= 0 ? (SEGMENT | ctx.node_offsets[n->next_id]) : 0);
		put_be32(ctx.buf.data + off + 16, n->prev_id >= 0 ? (SEGMENT | ctx.node_offsets[n->prev_id]) : 0);
		put_be32(ctx.buf.data + off + 20, n->child_id >= 0 ? (SEGMENT | ctx.node_offsets[n->child_id]) : 0);
	}

	/* Rodata */
	for (int i = 0; i < node_count; i++) {
		int nid = order[i];
		struct model_node *n = &m->nodes[nid];
		uint32_t off = ctx.rodata_offsets[nid];
		if (off == 0 && nid != 0) continue;

		switch (n->type) {
		case NT_BBOX:
			put_be32(ctx.buf.data + off + 0, (uint32_t)n->rd.bbox.hitpart);
			put_be32(ctx.buf.data + off + 4, float_to_be32(n->rd.bbox.xmin));
			put_be32(ctx.buf.data + off + 8, float_to_be32(n->rd.bbox.xmax));
			put_be32(ctx.buf.data + off + 12, float_to_be32(n->rd.bbox.ymin));
			put_be32(ctx.buf.data + off + 16, float_to_be32(n->rd.bbox.ymax));
			put_be32(ctx.buf.data + off + 20, float_to_be32(n->rd.bbox.zmin));
			put_be32(ctx.buf.data + off + 24, float_to_be32(n->rd.bbox.zmax));
			break;
		case NT_TOGGLE:
			put_be32(ctx.buf.data + off + 0,
				n->rd.toggle.target_node_id >= 0 ?
				(SEGMENT | ctx.node_offsets[n->rd.toggle.target_node_id]) : 0);
			put_be16(ctx.buf.data + off + 4, n->rd.toggle.rwdataindex);
			break;
		case NT_DL:
			put_be32(ctx.buf.data + off + 0,
				ctx.opagdl_offsets[nid] ? (SEGMENT | ctx.opagdl_offsets[nid]) : 0);
			put_be32(ctx.buf.data + off + 4,
				ctx.xlugdl_offsets[nid] ? (SEGMENT | ctx.xlugdl_offsets[nid]) : 0);
			put_be32(ctx.buf.data + off + 8,
				ctx.col_offsets[nid] ? (SEGMENT | ctx.col_offsets[nid]) : 0);
			put_be32(ctx.buf.data + off + 12,
				ctx.vtx_offsets[nid] ? (SEGMENT | ctx.vtx_offsets[nid]) : 0);
			put_be16(ctx.buf.data + off + 16, (uint16_t)n->rd.dl.numvertices);
			put_be16(ctx.buf.data + off + 18, (uint16_t)n->rd.dl.mcount);
			put_be16(ctx.buf.data + off + 20, n->rd.dl.rwdataindex);
			put_be16(ctx.buf.data + off + 22, n->rd.dl.numcolours);
			break;
		case NT_HEADSPOT:
			put_be16(ctx.buf.data + off + 0, n->rd.headspot.rwdataindex);
			break;
		case NT_CHRINFO:
			put_be16(ctx.buf.data + off + 0, n->rd.chrinfo.animpart);
			put_be16(ctx.buf.data + off + 2, (uint16_t)n->rd.chrinfo.mtxindex);
			put_be32(ctx.buf.data + off + 4, n->rd.chrinfo.unk04);
			put_be16(ctx.buf.data + off + 8, n->rd.chrinfo.rwdataindex);
			break;
		case NT_DISTANCE:
			put_be32(ctx.buf.data + off + 0, float_to_be32(n->rd.distance.near));
			put_be32(ctx.buf.data + off + 4, float_to_be32(n->rd.distance.far));
			put_be32(ctx.buf.data + off + 8,
				n->rd.distance.target_node_id >= 0 ?
				(SEGMENT | ctx.node_offsets[n->rd.distance.target_node_id]) : 0);
			put_be16(ctx.buf.data + off + 12, n->rd.distance.rwdataindex);
			break;
		default:
			if (n->rd.raw.data && n->rd.raw.len > 0) {
				outbuf_write(&ctx.buf, off, n->rd.raw.data, n->rd.raw.len);
			}
			break;
		}
	}

	/* Vertex + colour data */
	for (int i = 0; i < node_count; i++) {
		int nid = order[i];
		struct model_node *n = &m->nodes[nid];
		if (n->type == NT_DL) {
			if (n->rd.dl.vertices && ctx.vtx_offsets[nid]) {
				outbuf_write(&ctx.buf, ctx.vtx_offsets[nid], n->rd.dl.vertices, n->rd.dl.vertices_len);
			}
			if (n->rd.dl.colours && ctx.col_offsets[nid]) {
				outbuf_write(&ctx.buf, ctx.col_offsets[nid], n->rd.dl.colours, n->rd.dl.colours_len);
			}
		}
	}

	/* GDL data — resolve texture rebind markers before writing */
	for (int i = 0; i < node_count; i++) {
		int nid = order[i];
		struct model_node *n = &m->nodes[nid];
		if (n->type == NT_DL) {
			/* Resolve texture markers in GDL */
			if (n->rd.dl.opagdl) {
				for (uint32_t off2 = 0; off2 + 8 <= n->rd.dl.opagdl_len; off2 += 8) {
					uint32_t addr = be32(n->rd.dl.opagdl + off2 + 4);
					if ((addr & 0xFFF00000) == (SEGMENT | 0x00F00000)) {
						int tc_idx = addr & 0xFFFF;
						if (tc_idx < m->num_texconfigs && m->texconfigs[tc_idx].embedded && ctx.texdata_offsets[tc_idx]) {
							put_be32(n->rd.dl.opagdl + off2 + 4, SEGMENT | ctx.texdata_offsets[tc_idx]);
						}
					}
					/* Also fix vertex references: segment 0x05 pointing to old vertex offsets */
				}
			}

			/* Fix GDL vertex load commands (G_VTX = 0x01 on F3DEX2) to point to new vertex offsets */
			if (n->rd.dl.opagdl && ctx.vtx_offsets[nid]) {
				for (uint32_t off2 = 0; off2 + 8 <= n->rd.dl.opagdl_len; off2 += 8) {
					uint8_t cmd = n->rd.dl.opagdl[off2];
					if (cmd == 0x01) { /* G_VTX */
						put_be32(n->rd.dl.opagdl + off2 + 4, SEGMENT | ctx.vtx_offsets[nid]);
					}
				}
			}

			if (n->rd.dl.opagdl && ctx.opagdl_offsets[nid]) {
				outbuf_write(&ctx.buf, ctx.opagdl_offsets[nid], n->rd.dl.opagdl, n->rd.dl.opagdl_len);
			}
			if (n->rd.dl.xlugdl && ctx.xlugdl_offsets[nid]) {
				/* Same vertex fixup for xlu GDL */
				if (ctx.vtx_offsets[nid]) {
					for (uint32_t off2 = 0; off2 + 8 <= n->rd.dl.xlugdl_len; off2 += 8) {
						uint8_t cmd = n->rd.dl.xlugdl[off2];
						if (cmd == 0x01) {
							put_be32(n->rd.dl.xlugdl + off2 + 4, SEGMENT | ctx.vtx_offsets[nid]);
						}
					}
				}
				outbuf_write(&ctx.buf, ctx.xlugdl_offsets[nid], n->rd.dl.xlugdl, n->rd.dl.xlugdl_len);
			}
		}
	}

	/* Texconfigs */
	for (int i = 0; i < m->num_texconfigs; i++) {
		struct texconfig *tc = &m->texconfigs[i];
		uint32_t tc_off = ctx.texconfig_offset + i * 12;

		uint32_t tex_ptr;
		if (tc->embedded && ctx.texdata_offsets[i]) {
			tex_ptr = SEGMENT | ctx.texdata_offsets[i];
		} else {
			tex_ptr = tc->ptr_raw;
		}

		put_be32(ctx.buf.data + tc_off + 0, tex_ptr);
		ctx.buf.data[tc_off + 4] = tc->width;
		ctx.buf.data[tc_off + 5] = tc->height;
		ctx.buf.data[tc_off + 6] = tc->level;
		ctx.buf.data[tc_off + 7] = tc->format;
		ctx.buf.data[tc_off + 8] = tc->depth;
		ctx.buf.data[tc_off + 9] = tc->s;
		ctx.buf.data[tc_off + 10] = tc->t;
		ctx.buf.data[tc_off + 11] = tc->unk0b;
	}

	/* Embedded texture data */
	for (int i = 0; i < m->num_texconfigs; i++) {
		if (m->texconfigs[i].embedded && m->texconfigs[i].texdata && ctx.texdata_offsets[i]) {
			outbuf_write(&ctx.buf, ctx.texdata_offsets[i],
				m->texconfigs[i].texdata, m->texconfigs[i].texdata_len);
		}
	}

	/* Parts array */
	if (m->num_parts > 0) {
		/* Sort parts by part number first (engine requires sorted for binary search) */
		for (int i = 0; i < m->num_parts - 1; i++) {
			for (int j = i + 1; j < m->num_parts; j++) {
				if (m->parts[j].part_num < m->parts[i].part_num) {
					struct model_part tmp = m->parts[i];
					m->parts[i] = m->parts[j];
					m->parts[j] = tmp;
				}
			}
		}

		for (int i = 0; i < m->num_parts; i++) {
			uint32_t node_seg = (m->parts[i].node_id >= 0) ?
				(SEGMENT | ctx.node_offsets[m->parts[i].node_id]) : 0;
			put_be32(ctx.buf.data + ctx.parts_offset + i * 4, node_seg);
		}
		uint32_t nums_off = ctx.parts_offset + m->num_parts * 4;
		for (int i = 0; i < m->num_parts; i++) {
			put_be16(ctx.buf.data + nums_off + i * 2, (uint16_t)m->parts[i].part_num);
		}
	}

	*out = ctx.buf;
	return true;
}

/* ── Cleanup ────────────────────────────────────────────────────── */

static void free_model(struct model *m) {
	for (int i = 0; i < m->num_nodes; i++) {
		struct model_node *n = &m->nodes[i];
		if (n->type == NT_DL) {
			free(n->rd.dl.vertices);
			free(n->rd.dl.colours);
			free(n->rd.dl.opagdl);
			free(n->rd.dl.xlugdl);
		} else if (n->type != NT_BBOX && n->type != NT_TOGGLE &&
		           n->type != NT_HEADSPOT && n->type != NT_CHRINFO &&
		           n->type != NT_DISTANCE) {
			free(n->rd.raw.data);
		}
	}
	for (int i = 0; i < m->num_texconfigs; i++) {
		free(m->texconfigs[i].texdata);
	}
}

/* ── File I/O ───────────────────────────────────────────────────── */

static uint8_t *read_file_contents(const char *path, uint32_t *out_len) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (len <= 0) { fclose(f); return NULL; }

	uint8_t *buf = malloc(len);
	if (fread(buf, 1, len, f) != (size_t)len) {
		free(buf);
		fclose(f);
		return NULL;
	}

	fclose(f);
	*out_len = (uint32_t)len;
	return buf;
}

static bool write_file_contents(const char *path, const uint8_t *data, uint32_t len) {
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	bool ok = fwrite(data, 1, len, f) == len;
	fclose(f);
	return ok;
}

/* ── Main ───────────────────────────────────────────────────────── */

static void usage(void) {
	fprintf(stderr, "usage: pdheadedit <command> [args...]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "commands:\n");
	fprintf(stderr, "  inspect <file.Z>              dump model tree as JSON\n");
	fprintf(stderr, "  validate <file.Z>             validation report as JSON\n");
	fprintf(stderr, "  edit <file.Z> <output.Z>      apply edits from stdin, write copy\n");
	fprintf(stderr, "\n");
}

int main(int argc, char **argv) {
	if (argc < 3) {
		usage();
		return 1;
	}

	const char *cmd = argv[1];
	const char *input_path = argv[2];

	/* Read and decompress */
	uint32_t compressed_len;
	uint8_t *compressed = read_file_contents(input_path, &compressed_len);
	if (!compressed) {
		fprintf(stderr, "error: could not read '%s'\n", input_path);
		return 1;
	}

	uint32_t raw_len;
	uint8_t *raw = rare_decompress(compressed, compressed_len, &raw_len);
	free(compressed);
	if (!raw) {
		fprintf(stderr, "error: decompression failed\n");
		return 1;
	}

	/* Parse */
	struct model m;
	if (!parse_model(&m, raw, raw_len)) {
		free(raw);
		return 1;
	}
	free(raw);

	/* Extract just the filename for JSON output */
	const char *filename = strrchr(input_path, '/');
	filename = filename ? filename + 1 : input_path;

	if (strcmp(cmd, "inspect") == 0) {
		json_emit_model(&m, filename);
		free_model(&m);
		return 0;
	}

	if (strcmp(cmd, "validate") == 0) {
		json_emit_validation(&m, filename);
		free_model(&m);
		return 0;
	}

	if (strcmp(cmd, "edit") == 0) {
		if (argc < 4) {
			fprintf(stderr, "error: edit requires output path\n");
			free_model(&m);
			return 1;
		}
		const char *output_path = argv[3];

		/* Refuse to overwrite input */
		if (strcmp(input_path, output_path) == 0) {
			fprintf(stderr, "error: output path must differ from input (safety)\n");
			free_model(&m);
			return 1;
		}

		/* Read edit JSON from stdin */
		char json_buf[65536] = "";
		size_t json_len = fread(json_buf, 1, sizeof(json_buf) - 1, stdin);
		json_buf[json_len] = '\0';

		char errbuf[512] = "";
		if (!apply_edits(&m, json_buf, errbuf, sizeof(errbuf))) {
			printf("{\"status\": \"error\", \"error\": \"%s\"}\n", errbuf);
			free_model(&m);
			return 1;
		}

		/* Serialize */
		struct outbuf out;
		if (!serialize_model(&m, &out)) {
			printf("{\"status\": \"error\", \"error\": \"serialization failed\"}\n");
			free_model(&m);
			return 1;
		}

		/* Compress */
		uint32_t comp_len;
		uint8_t *comp = rare_compress(out.data, out.len, &comp_len);
		free(out.data);

		if (!comp) {
			printf("{\"status\": \"error\", \"error\": \"compression failed\"}\n");
			free_model(&m);
			return 1;
		}

		if (!write_file_contents(output_path, comp, comp_len)) {
			printf("{\"status\": \"error\", \"error\": \"could not write output file\"}\n");
			free(comp);
			free_model(&m);
			return 1;
		}
		free(comp);

		printf("{\"status\": \"ok\", \"output_file\": \"%s\", ", output_path);
		printf("\"model\": ");
		json_emit_model(&m, filename);
		printf("}\n");

		free_model(&m);
		return 0;
	}

	fprintf(stderr, "error: unknown command '%s'\n", cmd);
	usage();
	free_model(&m);
	return 1;
}
