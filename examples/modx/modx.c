#include "py/persistnative.h"

#define QSTR_DEFINES \
    QDEF(VAL1, "VAL1") \
    QDEF(VAL2, "VAL2") \
    QDEF(VAL3, "VAL3") \
    QDEF(add1, "add1") \
    QDEF(make_list, "make_list") \

enum {
    #define QDEF(id, str) MP_LOCAL_QSTR_ ## id,
    QSTR_DEFINES
    #undef QDEF
    MP_LOCAL_QSTR_number_of,
};

__attribute__((section(".qstr")))
struct {
    #define QDEF(id, str) const char id[sizeof(str)];
    QSTR_DEFINES
    #undef QDEF
} QSTR_VALUE = {
    #define QDEF(id, str) str,
    QSTR_DEFINES
    #undef QDEF
};

STATIC mp_obj_t modx_add1(CONTEXT mp_obj_t x) {
    return RT(mp_binary_op)(MP_BINARY_OP_ADD, x, MP_OBJ_NEW_SMALL_INT(1));
}

STATIC mp_obj_t modx_make_list(CONTEXT mp_obj_t x, mp_obj_t y) {
    mp_obj_t list[6] = {
        MP_OBJ_NEW_SMALL_INT(1),
        MP_OBJ_NEW_SMALL_INT(2),
        CONST(_ellipsis),
        MP_OBJ_NEW_QSTR(QSTR(make_list)),
        x,
        y,
    };
    return RT(mp_obj_new_list)(6, list);
}

MP_PERSISTENT_NATIVE_HEADER

MP_PERSISTENT_NATIVE_INIT
void init(CONTEXT_ALONE) {
    // constants
    RT(mp_store_global)(QSTR(VAL1), CONST(_true));
    RT(mp_store_global)(QSTR(VAL2), MP_OBJ_NEW_SMALL_INT(123));
    RT(mp_store_global)(QSTR(VAL3), MP_OBJ_NEW_QSTR(QSTR(VAL3)));

    // functions
    RT(mp_store_global)(QSTR(add1), MAKE_FUN_1(modx_add1));
    RT(mp_store_global)(QSTR(make_list), MAKE_FUN_2(modx_make_list));
}
