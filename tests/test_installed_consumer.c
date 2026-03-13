#include <stdint.h>

#if __has_include(<debounce/debounce.h>)
#include <debounce/debounce.h>
#else
#include "debounce.h"
#endif

#if __has_include(<debounce/debounce_version.h>)
#include <debounce/debounce_version.h>
#else
#define DEBOUNCE_VERSION_MINOR 0
#endif

int
main(void)
{
        struct debounce db;

        if (!debounce_init(&db, (uint16_t)(DEBOUNCE_VERSION_MINOR + 1))) {
                return 1;
        }

        return debounce_get_trip(&db) == (uint16_t)(DEBOUNCE_VERSION_MINOR + 1)
                   ? 0
                   : 2;
}
