/* Reader/writer for Hisoutensoku profile (.pf) files.
 *
 * Layout (verified on real profiles): a 0x6A-byte header (controller config, kept opaque),
 * then 20 characters x 4 slots of decks, each deck being a u8 card count followed by that
 * many little-endian u16 card IDs. Characters are in SokuLib order.
 */
#ifndef STS_PF_H
#define STS_PF_H

#include <stddef.h>
#include <stdint.h>
#include "deck.h"

#define STS_PF_HEADER_SIZE 0x6A
#define STS_NUM_CHARACTERS 20
#define STS_NUM_SLOTS 4

typedef struct {
    uint8_t header[STS_PF_HEADER_SIZE];
    sts_deck decks[STS_NUM_CHARACTERS][STS_NUM_SLOTS];
} sts_profile;

typedef enum {
    STS_PF_OK = 0,
    STS_PF_TRUNCATED,  /* data ended before all 80 decks were read */
    STS_PF_IO          /* file could not be opened, read or written */
} sts_pf_status;

/* Parses a profile from memory. *consumed (if non-NULL) is set to the number of bytes
 * used, so callers can tell whether trailing data exists. Card IDs are NOT validated. */
sts_pf_status sts_pf_parse(const uint8_t *data, size_t len, sts_profile *out, size_t *consumed);

/* Exact serialized size of a profile. */
size_t sts_pf_size(const sts_profile *profile);

/* Writes the profile with every deck sorted ascending (as the game writes them).
 * Returns bytes written, or 0 if cap is too small. */
size_t sts_pf_serialize(const sts_profile *profile, uint8_t *buf, size_t cap);

sts_pf_status sts_pf_load(const char *path, sts_profile *out, size_t *consumed);
sts_pf_status sts_pf_save(const char *path, const sts_profile *profile);

/* "Reimu" ... "Suwako", or "?" for an out-of-range index. */
const char *sts_character_name(int index);

#endif
