/* Data files built into the DLL as resources (see embedded.rc.in): the card table and the
 * translations. Files of the same name beside the DLL are ignored, because an install updated
 * over an older release may still have them, out of date. */
#include "internal.h"

const char *embedded_data(const char *name, size_t *length)
{
    HMODULE module = NULL;
    HRSRC found;
    HGLOBAL loaded;

    *length = 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&embedded_data, &module))
        return NULL;
    found = FindResourceA(module, name, MAKEINTRESOURCEA(10));   /* 10 is RT_RCDATA */
    if (!found)
        return NULL;
    loaded = LoadResource(module, found);
    if (!loaded)
        return NULL;
    *length = SizeofResource(module, found);
    return (const char *)LockResource(loaded);
}
