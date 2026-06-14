#include "properties.h"

#include <string.h>

/* Mutable live values. The default is a named initializer in the table, so the
 * preprocessor can't build a static initializer for this struct; it is filled
 * from the descriptor defaults by property__load_defaults() (a constructor that
 * runs before main()). Published read-only through g_properties. */
static properties_t s_props;
const properties_t *const g_properties = &s_props;

/* Descriptor table, generated from the single source of truth. Sequential
 * array: the array index is the public "index" (definition order). The named
 * .default_value/.min/.max/.description initializers ride in via __VA_ARGS__. */
static const property_desc_t g_prop_desc[] = {
/* Parameter names must avoid the designators .type/.name/.tag below, or the
 * preprocessor would rewrite e.g. ".type" into ".uint16_t". */
#define PROPERTY(ptag, ctype, pname, ...)                                      \
  { .type = PROPERTY_TYPE_ENUM(ctype), .name = #pname, .tag = (ptag),          \
    .offset = (uint16_t)offsetof(properties_t, pname), __VA_ARGS__ },
#include "property_table.def"
#undef PROPERTY
};

#define PROPERTY_COUNT (sizeof(g_prop_desc) / sizeof(g_prop_desc[0]))

/* Compile-time uniqueness of nonzero tags. Duplicate case labels are a
 * constraint violation, so two properties sharing a nonzero tag fail to
 * compile. A zero tag (transient) maps to a unique negative placeholder
 * (-1 - __COUNTER__) so multiple transient properties never collide. This
 * function is never called; it exists only to be compiled. */
__attribute__((unused))
static void property__check_unique_tags(int v)
{
  switch (v)
  {
#define PROPERTY(tg, ct, nm, ...) case ((tg) ? (tg) : (-1 - __COUNTER__)):
#include "property_table.def"
#undef PROPERTY
    default: break;
  }
}

/* ---- generic field access -------------------------------------------------*/

static uint16_t read_value(const property_desc_t *d)
{
  void *field = (char *)&s_props + d->offset;
  if (d->type == PROPERTY_TYPE_BOOL) return *(bool *)field ? 1u : 0u;
  return *(uint16_t *)field;
}

static void write_value(const property_desc_t *d, uint16_t v)
{
  void *field = (char *)&s_props + d->offset;
  if (d->type == PROPERTY_TYPE_BOOL) *(bool *)field = (v != 0);
  else *(uint16_t *)field = v;
}

static uint16_t clamp(const property_desc_t *d, uint16_t v)
{
  if (v < d->min) return d->min;
  if (v > d->max) return d->max;
  return v;
}

/* ---- lifecycle ------------------------------------------------------------*/

void property_reset_all(void)
{
  for (size_t i = 0; i < PROPERTY_COUNT; i++)
    write_value(&g_prop_desc[i], g_prop_desc[i].default_value);
}

/* Populate the live values before main(): the reset handler calls
 * __libc_init_array ahead of main, which runs constructors, so g_properties is
 * valid with no explicit init call. */
__attribute__((constructor))
static void property__load_defaults(void)
{
  property_reset_all();
}

/* ---- introspection --------------------------------------------------------*/

size_t property_count(void)
{
  return PROPERTY_COUNT;
}

const property_desc_t *property_at(size_t index)
{
  return index < PROPERTY_COUNT ? &g_prop_desc[index] : NULL;
}

bool property_by_name(const char *name, size_t *out_index)
{
  if (!name) return false;
  for (size_t i = 0; i < PROPERTY_COUNT; i++)
  {
    if (strcmp(g_prop_desc[i].name, name) == 0)
    {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

/* ---- typed get / set ------------------------------------------------------*/

bool property_get_u16(size_t index, uint16_t *out)
{
  if (index >= PROPERTY_COUNT || g_prop_desc[index].type != PROPERTY_TYPE_U16) return false;
  if (out) *out = read_value(&g_prop_desc[index]);
  return true;
}

bool property_set_u16(size_t index, uint16_t value)
{
  if (index >= PROPERTY_COUNT || g_prop_desc[index].type != PROPERTY_TYPE_U16) return false;
  write_value(&g_prop_desc[index], clamp(&g_prop_desc[index], value));
  return true;
}

bool property_get_bool(size_t index, bool *out)
{
  if (index >= PROPERTY_COUNT || g_prop_desc[index].type != PROPERTY_TYPE_BOOL) return false;
  if (out) *out = read_value(&g_prop_desc[index]) != 0;
  return true;
}

bool property_set_bool(size_t index, bool value)
{
  if (index >= PROPERTY_COUNT || g_prop_desc[index].type != PROPERTY_TYPE_BOOL) return false;
  write_value(&g_prop_desc[index], value ? 1u : 0u);
  return true;
}

bool property_reset(size_t index)
{
  if (index >= PROPERTY_COUNT) return false;
  write_value(&g_prop_desc[index], g_prop_desc[index].default_value);
  return true;
}

/* ---- persistence serialization (pure, no flash) ---------------------------*/

/* Blob layout (all little-endian, treated as a sequence of 16-bit words):
 *   magic   (2 words, 0x42414e44 "BAND")
 *   version (1 word)
 *   count   (1 word, number of {tag,value} pairs that follow)
 *   payload (count * 2 words: tag, value)
 *   checksum(1 word, XOR-fold of every preceding word)
 * The tag carries each value's identity, so byte position is irrelevant across
 * versions; a missing tag keeps its default, an unknown tag is skipped. */
#define BLOB_MAGIC   0x42414e44u
#define BLOB_VERSION 1u
#define BLOB_HEADER_WORDS 4u   /* magic(2) + version + count */

static uint16_t persistent_count(void)
{
  uint16_t n = 0;
  for (size_t i = 0; i < PROPERTY_COUNT; i++)
    if (g_prop_desc[i].tag != PROPERTY_TAG_NONE) n++;
  return n;
}

static void put_le16(uint8_t *b, size_t *off, uint16_t v)
{
  b[(*off)++] = (uint8_t)(v & 0xff);
  b[(*off)++] = (uint8_t)(v >> 8);
}

static uint16_t get_le16(const uint8_t *b, size_t *off)
{
  uint16_t v = (uint16_t)(b[*off] | (b[*off + 1] << 8));
  *off += 2;
  return v;
}

size_t property_blob_size(void)
{
  /* header + payload pairs (2 words each) + checksum, in bytes */
  return (size_t)(BLOB_HEADER_WORDS + persistent_count() * 2u + 1u) * 2u;
}

size_t property_pack(uint8_t *buf, size_t cap)
{
  size_t need = property_blob_size();
  if (cap < need) return 0;

  size_t off = 0;
  uint16_t cs = 0;
  uint16_t w;

  w = (uint16_t)(BLOB_MAGIC & 0xffff); put_le16(buf, &off, w); cs ^= w;
  w = (uint16_t)(BLOB_MAGIC >> 16);    put_le16(buf, &off, w); cs ^= w;
  w = BLOB_VERSION;                    put_le16(buf, &off, w); cs ^= w;
  w = persistent_count();              put_le16(buf, &off, w); cs ^= w;

  for (size_t i = 0; i < PROPERTY_COUNT; i++)
  {
    const property_desc_t *d = &g_prop_desc[i];
    if (d->tag == PROPERTY_TAG_NONE) continue;
    w = d->tag;          put_le16(buf, &off, w); cs ^= w;
    w = read_value(d);   put_le16(buf, &off, w); cs ^= w;
  }

  put_le16(buf, &off, cs);
  return off;
}

static const property_desc_t *desc_by_tag(uint16_t tag)
{
  for (size_t i = 0; i < PROPERTY_COUNT; i++)
    if (g_prop_desc[i].tag == tag) return &g_prop_desc[i];
  return NULL;
}

bool property_unpack(const uint8_t *buf, size_t len)
{
  /* Smallest valid blob is header + checksum (count == 0). */
  if (!buf || len < (BLOB_HEADER_WORDS + 1u) * 2u || (len % 2u) != 0u) return false;

  /* Verify the whole blob before mutating anything: the trailing checksum is
   * the XOR of all preceding words, so XOR-folding every word must yield 0. */
  uint16_t cs = 0;
  for (size_t o = 0; o < len; o += 2)
    cs ^= (uint16_t)(buf[o] | (buf[o + 1] << 8));
  if (cs != 0) return false;

  size_t off = 0;
  uint32_t magic = get_le16(buf, &off);
  magic |= (uint32_t)get_le16(buf, &off) << 16;
  if (magic != BLOB_MAGIC) return false;

  uint16_t version = get_le16(buf, &off);
  if (version != BLOB_VERSION) return false;

  uint16_t count = get_le16(buf, &off);
  if (len != (size_t)(BLOB_HEADER_WORDS + count * 2u + 1u) * 2u) return false;

  for (uint16_t i = 0; i < count; i++)
  {
    uint16_t tag = get_le16(buf, &off);
    uint16_t value = get_le16(buf, &off);
    const property_desc_t *d = desc_by_tag(tag);
    if (!d) continue;  /* unknown tag from another version: skip */
    /* Out-of-range (corrupt/stale) values fall back to default, not clamped. */
    if (value < d->min || value > d->max) value = d->default_value;
    write_value(d, value);
  }
  return true;
}

/* ---- flash I/O (stubs) ----------------------------------------------------*/

bool property_load_from_flash(void)
{
  return false;  /* not implemented: storage is volatile for now */
}

bool property_save_to_flash(void)
{
  return false;  /* not implemented: storage is volatile for now */
}
