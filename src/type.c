#include <stdlib.h>
#include "type.h"

#include "string.h"
#include "hir.h"

char *type_tag_to_string(TypeTag tag) {
    switch (tag) {
        case TypeUnknown:
            return "!!!";
        case TY_VOID:
            return "void";
            //        case TypeU8:
//            return "u8";
        case TypeI8:
            return "i8";
//        case TypeU16:
//            return "u16";
        case TypeI16:
            return "i16";
//        case TypeU32:
//            return "u32";
        case TypeI32:
            return "i32";
//        case TypeU64:
//            return "u64";
        case TypeI64:
            return "i64";
//        case TypeU128:
//            return "u128";
        case TypeI128:
            return "i128";
//        case TypeUSize:
//            return "usize";
        case TypeISize:
            return "isize";
        case TypeF32:
            return "f32";
        case TypeF64:
            return "f64";
        case TypeBool:
            return "bool";

        case TypeIntUnsigned:
            return "int_unsigned";
        case TypeIntSigned:
            return "int_signed";

        case TypeRef:
            return "&";
        case TY_PTR:
            return "*";

        default:
            return "<?>";
    }
}

char *type_to_string(Type type) {
    TypeTag tag = type_tag(type);
    char *tag_str = type_tag_to_string(tag);
    if (!type_is_extended(type))
        return strdup(tag_str);

    if (tag == TY_PTR) {
        char *ext_str = type_to_string(type.extended->data.inner_type);
        char *str = malloc(strlen(tag_str) + strlen(ext_str) + 1);
        sprintf(str, "%s%s", tag_str, ext_str);
        free(ext_str);
        return str;
    }

    assert(false);
}

bool type_is_extended(Type type) {
    return type.tag >= __TYPE_LAST;
}

TypeTag type_tag(Type type) {
    if (type_is_extended(type)) {
        return type.extended->tag;
    } else {
        return type.tag;
    }
}

bool type_is_integer(Type type) {
    TypeTag tag = type_tag(type);
    return (tag >= TypeI8 && tag <= TypeISize) ||
           (tag >= TypeIntUnsigned && tag <= TypeIntSigned) ||
            tag == TY_PTR;
}

Type type_from_name(char *type_name) {
#define is_name(rhs) (strcmp(type_name, rhs) == 0)

//    if is_name("u8") {
//        return (Type) {.tag = TypeU8};
//    } else
    if is_name("i8") {
        return (Type) {.tag = TypeI8};
//    } else if is_name("u16") {
//        return (Type) {.tag = TypeU16};
    } else if is_name("i16") {
        return (Type) {.tag = TypeI16};
//    } else if is_name("u32") {
//        return (Type) {.tag = TypeU32};
    } else if is_name("i32") {
        return (Type) {.tag = TypeI32};
//    } else if is_name("u64") {
//        return (Type) {.tag = TypeU64};
    } else if is_name("i64") {
        return (Type) {.tag = TypeI64};
//    } else if is_name("u128") {
//        return (Type) {.tag = TypeU128};
    } else if is_name("i128") {
        return (Type) {.tag = TypeI128};
//    } else if is_name("usize") {
//        return (Type) {.tag = TypeUSize};
    } else if is_name("isize") {
        return (Type) {.tag = TypeISize};
    } else if is_name("f32") {
        return (Type) {.tag = TypeF32};
    } else if is_name("f64") {
        return (Type) {.tag = TypeF64};
    } else if is_name("bool") {
        return (Type) {.tag = TypeBool};
    }

    fprintf(stderr, "Unknown type name: %s\n", type_name);
    assert(false);

#undef is_name
}



Type type_from_hir_inst(Hir *hir, HirIndex index) {
    HirInst *type_inst = hir_get_inst_tagged(hir, index, HIR_TYPE);

    if (type_inst->data.ty.is_ptr) {
        ExtendedType *ext_ty = malloc(sizeof(ExtendedType));
        ext_ty->tag = TY_PTR;
        ext_ty->data.inner_type = type_from_hir_inst(hir, type_inst->data.ty.inner);
    }

    // Name is borrowed from the string set
    char *name = string_set_get(&hir->strings, type_inst->data.ty.inner);
    return type_from_name(name);
}
