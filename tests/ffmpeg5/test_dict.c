#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include "ijkplayer/ijkavutil/ijkdict.h"
int main(void)
{
    IjkAVDictionary *dict = NULL;
    uintptr_t values[] = { 0, 1, UINTPTR_MAX, UINTPTR_MAX / 2 + 1, (uintptr_t)&dict };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        assert(ijk_av_dict_set_intptr(&dict, "callback", values[i], 0) == 0);
        assert(ijk_av_dict_get_intptr(dict, "callback") == values[i]);
    }
    assert(ijk_av_dict_get_intptr(dict, "missing") == 0);
    const char *bad[] = { "", "0x", "0xjunk", "0x1junk", "0x10000000000000000" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(ijk_av_dict_set(&dict, "bad", bad[i], 0) == 0);
        assert(ijk_av_dict_get_intptr(dict, "bad") == 0);
    }
    assert(ijk_av_dict_set(&dict, "hex", "0X10", 0) == 0);
    assert(ijk_av_dict_get_intptr(dict, "hex") == 16);
    ijk_av_dict_free(&dict);
    assert(!dict);
    puts("PASS IjkAVDictionary: own dictionary API, pointer high bits, missing/malformed/range checks");
    return 0;
}
