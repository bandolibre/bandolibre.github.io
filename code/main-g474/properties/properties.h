#ifndef PROPERTIES_H_
#define PROPERTIES_H_

/* Global property system.
 *
 * Every tunable value is declared once in property_table.def. From that single
 * list we generate a struct of live values (read directly via g_properties) and
 * an enumerable descriptor table that drives bound-checked writes and, later, a
 * console / MIDI 2.0 Property Exchange editor.
 *
 * Reads: g_properties->kb_press        (direct, no function call, read-only)
 * Writes / introspection: the index-keyed API below.
 *
 * The live values are loaded from their defaults by a constructor before main()
 * runs, so they are valid with no explicit init call. Storage is RAM only (lost
 * on power-off). The persistence serialization (pack/unpack with a checksum) is
 * implemented and tested, but the flash I/O that would back it is stubbed. */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROPERTY_TAG_NONE 0u   /* tag value marking a transient (non-persistent) property */

typedef enum {
  PROPERTY_TYPE_BOOL = 0,
  PROPERTY_TYPE_U16  = 1,
} property_type_t;

/* Map the C type token used in property_table.def to its property_type_t. */
#define PROPERTY_TYPE_ENUM_uint16_t PROPERTY_TYPE_U16
#define PROPERTY_TYPE_ENUM_bool     PROPERTY_TYPE_BOOL
#define PROPERTY_TYPE_ENUM(type) PROPERTY_TYPE_ENUM_##type

/* Live values. The type token in the table IS the C type, so each field
 * declares directly. Read through g_properties (the read-only view below). */
typedef struct {
#define PROPERTY(tag, type, name, ...) type name;
#include "property_table.def"
#undef PROPERTY
} properties_t;

typedef struct {
  property_type_t  type;
  const char      *name;          /* == field name (stringized) */
  const char      *description;
  uint16_t         tag;           /* 0 = transient; else unique permanent blob id */
  uint16_t         default_value;
  uint16_t         min;           /* inclusive */
  uint16_t         max;           /* inclusive */
  uint16_t         offset;        /* offsetof(properties_t, field) */
} property_desc_t;

/* Read-only view of the live values: g_properties->kb_press compiles, but
 * assigning through it does not. Writes go through the API below. */
extern const properties_t *const g_properties;

void property_reset_all(void);  /* restore every property to its default */

/* Reflexive / editor API, addressed by a dense index in [0, property_count()).
 * The index is NOT stable across software versions (removing a property
 * reindexes the rest): obtain it via property_at() or property_by_name(),
 * never hardcode or persist it. */
size_t                 property_count(void);
const property_desc_t *property_at(size_t index);          /* NULL if out of range */
bool                   property_by_name(const char *name, size_t *out_index);

bool property_get_u16(size_t index, uint16_t *out);        /* false on bad index/type */
bool property_set_u16(size_t index, uint16_t value);       /* clamps to [min,max] */
bool property_get_bool(size_t index, bool *out);
bool property_set_bool(size_t index, bool value);
bool property_reset(size_t index);                         /* one -> default */

/* Persistence serialization (pure, no flash). The blob is a small header
 * followed by tag-value pairs for persistent properties and a trailing
 * checksum. Identity is carried by the tag, not byte position. */
size_t property_blob_size(void);                           /* bytes a full blob needs */
size_t property_pack(uint8_t *buf, size_t cap);            /* bytes written; 0 if cap too small */
bool   property_unpack(const uint8_t *buf, size_t len);    /* verify + apply; false on bad
                                                            * magic/version/checksum. A value
                                                            * outside [min,max] loads as default. */

/* Flash I/O: thin stubs for now; will wrap property_pack/unpack + HAL flash. */
bool property_load_from_flash(void);   /* returns false (not implemented) */
bool property_save_to_flash(void);     /* returns false (not implemented) */

#ifdef __cplusplus
}
#endif

#endif /* PROPERTIES_H_ */
