#ifndef PROPERTIES_CONSOLE_H_
#define PROPERTIES_CONSOLE_H_

/* Console / microrl command layer over the property system. Kept separate from
 * properties.c so that core stays printf/stdlib-free; these helpers print via
 * printf (\r\n line endings) and parse argv tokens. */

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* argv is the usual token array with argv[0] = the command word. get/show print
 * value, min, max and default; help lists every property's type/min/max plus
 * its description. Each command returns false on a usage/lookup error (after
 * printing it). */
bool   property_cmd_get(int argc, const char *const *argv);   /* get <name>          */
bool   property_cmd_set(int argc, const char *const *argv);   /* set <name> <value>  */
bool   property_cmd_reset(int argc, const char *const *argv); /* reset <name|all>    */
void   property_cmd_show(void);                               /* show: table of all  */
void   property_cmd_help(void);                               /* help: usage + table */

/* Fill out[] with up to cap property names starting with prefix (prefix may be
 * NULL/"" to match all); returns the count. Backs a completion callback. */
size_t property_complete(const char *prefix, const char **out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* PROPERTIES_CONSOLE_H_ */
