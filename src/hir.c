#include "hir.h"

char *hir_tag_to_string(HirInstTag tag) {
    switch (tag) {
        case HIR_RESERVED:      return "HIR_RESERVED";

        case HIR_INT:           return "HIR_INT";
        case HIR_STRING:        return "HIR_STRING";
        case HIR_BOOL:          return "HIR_BOOL";

        case HIR_ADD:           return "HIR_ADD";
        case HIR_SUB:           return "HIR_SUB";
        case HIR_MUL:           return "HIR_MUL";
        case HIR_DIV:           return "HIR_DIV";

        case HIR_BLOCK_INLINE:  return "HIR_BLOCK_INLINE";
        case HIR_BLOCK:         return "HIR_BLOCK";

        case HIR_CONST_DECL:    return "HIR_CONST_DECL";
        case HIR_FN_DECL:       return "HIR_FN_DECL";

        case HIR_MODULE:        return "HIR_MODULE";
        case HIR_AS_TYPE:       return "HIR_AS_TYPE";
        case HIR_TYPE:          return "HIR_TYPE";
        default:                return "<?>";
    }
}

#define self_t HirInstList *self

void hir_inst_list_init(self_t) {
    self->size = 0;
    self->capacity = 0;
    self->data = NULL;
}

void hir_inst_list_free(self_t) {
    ARRAY_FREE(MirInst, self->data);
    hir_inst_list_init(self);
}

void hir_inst_list_add(self_t, HirInst inst) {
    if (self->capacity < self->size + 1) {
        self->capacity = ARRAY_GROW_CAPCITY(self->capacity);
        self->data = ARRAY_GROW(HirInst, self->data, self->capacity);
    }

    self->data[self->size] = inst;
    self->size++;
}

HirInst *hir_inst_list_get(self_t, uint32_t index) {
    if (index < self->size)
        return &self->data[index];
    return NULL;
}

#undef self_t

#define self_t Hir *self

HirInst *hir_get_inst(self_t, HirIndex index) {
    return &self->instructions.data[index];
}

HirInst *hir_get_inst_tagged(self_t, HirIndex index, HirInstTag tag) {
    HirInst *inst = hir_get_inst(self, index);
    assert(tag == inst->tag);
    return inst;
}

#undef self_t
