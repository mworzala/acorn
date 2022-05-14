#include "ast.h"

#include "array_util.h"

//(-3)!+2*1


const char *ast_tag_to_string(AstTag tag) {
    switch (tag) {
        case AST_INTEGER:
            return "integer";
        case AST_BINARY:
            return "binary";
        case AST_UNARY:
            return "unary";
        case AST_EMPTY:
            return "<empty>";
        default:
            return "<?>";
    }
}


#define self_t AstIndexList *self

void ast_index_list_init(self_t) {
    self->size = 0;
    self->capacity = 0;
    self->data = NULL;
}

void ast_index_list_free(self_t) {
    ARRAY_FREE(AstNode, self->data);
    ast_index_list_init(self);
}

void ast_index_list_add(self_t, AstIndex index) {
    if (self->capacity < self->size + 1) {
        self->capacity = ARRAY_GROW_CAPCITY(self->capacity);
        self->data = ARRAY_GROW(AstIndex, self->data, self->capacity);
    }

    self->data[self->size] = index;
    self->size++;
}

#undef self_t

#define self_t AstNodeList *self

void ast_node_list_init(self_t) {
    self->size = 0;
    self->capacity = 0;
    self->data = NULL;
}

void ast_node_list_free(self_t) {
    ARRAY_FREE(AstNode, self->data);
    ast_node_list_init(self);
}

void ast_node_list_add(self_t, AstNode node) {
    if (self->capacity < self->size + 1) {
        self->capacity = ARRAY_GROW_CAPCITY(self->capacity);
        self->data = ARRAY_GROW(AstNode, self->data, self->capacity);
    }

    self->data[self->size] = node;
    self->size++;
}

#undef self_t
