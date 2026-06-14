#include "properties_console.h"
#include "properties.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *type_name(property_type_t t)
{
  return t == PROPERTY_TYPE_BOOL ? "bool" : "u16";
}

/* Current value of a property regardless of type (bool -> 0/1). */
static uint16_t current_value(size_t index)
{
  const property_desc_t *d = property_at(index);
  if (d->type == PROPERTY_TYPE_BOOL)
  {
    bool b = false;
    property_get_bool(index, &b);
    return b ? 1u : 0u;
  }
  uint16_t v = 0;
  property_get_u16(index, &v);
  return v;
}

/* "name = value  (min M, max X, default D)" */
static void print_one(size_t index)
{
  const property_desc_t *d = property_at(index);
  printf("%s = %u  (min %u, max %u, default %u)\r\n",
         d->name, current_value(index), d->min, d->max, d->default_value);
}

bool property_cmd_get(int argc, const char *const *argv)
{
  if (argc < 2) { printf("usage: get <name>\r\n"); return false; }
  size_t i;
  if (!property_by_name(argv[1], &i)) { printf("unknown property: %s\r\n", argv[1]); return false; }
  print_one(i);
  return true;
}

bool property_cmd_set(int argc, const char *const *argv)
{
  if (argc < 3) { printf("usage: set <name> <value>\r\n"); return false; }
  size_t i;
  if (!property_by_name(argv[1], &i)) { printf("unknown property: %s\r\n", argv[1]); return false; }
  char *end;
  unsigned long v = strtoul(argv[2], &end, 0);
  if (argv[2][0] == '\0' || *end != '\0') { printf("not a number: %s\r\n", argv[2]); return false; }
  if (property_at(i)->type == PROPERTY_TYPE_BOOL) property_set_bool(i, v != 0);
  else property_set_u16(i, v > 0xFFFFu ? 0xFFFFu : (uint16_t)v);
  print_one(i);
  return true;
}

bool property_cmd_reset(int argc, const char *const *argv)
{
  if (argc < 2) { printf("usage: reset <name|all>\r\n"); return false; }
  if (strcmp(argv[1], "all") == 0) { property_reset_all(); printf("reset all properties\r\n"); return true; }
  size_t i;
  if (!property_by_name(argv[1], &i)) { printf("unknown property: %s\r\n", argv[1]); return false; }
  property_reset(i);
  print_one(i);
  return true;
}

void property_cmd_show(void)
{
  printf("%-18s %6s %6s %6s %8s\r\n", "name", "value", "min", "max", "default");
  for (size_t i = 0; i < property_count(); i++)
  {
    const property_desc_t *d = property_at(i);
    printf("%-18s %6u %6u %6u %8u\r\n", d->name, current_value(i), d->min, d->max, d->default_value);
  }
}

void property_cmd_help(void)
{
  printf("Property commands:\r\n");
  printf("  show                 list all properties with current value\r\n");
  printf("  get <name>           show value, min, max and default of one property\r\n");
  printf("  set <name> <value>   set a property (clamped to [min,max])\r\n");
  printf("  reset <name|all>     restore default(s)\r\n");
  printf("  help                 this help\r\n");
  printf("\r\nProperties:\r\n");
  printf("%-18s %5s %6s %6s  %s\r\n", "name", "type", "min", "max", "description");
  for (size_t i = 0; i < property_count(); i++)
  {
    const property_desc_t *d = property_at(i);
    printf("%-18s %5s %6u %6u  %s\r\n", d->name, type_name(d->type), d->min, d->max, d->description);
  }
}

size_t property_complete(const char *prefix, const char **out, size_t cap)
{
  size_t n = 0;
  size_t plen = prefix ? strlen(prefix) : 0;
  for (size_t i = 0; i < property_count() && n < cap; i++)
  {
    const char *name = property_at(i)->name;
    if (strncmp(name, prefix ? prefix : "", plen) == 0) out[n++] = name;
  }
  return n;
}
