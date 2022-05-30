#include <string.h>
#include <assert.h>
#include "ast.h"


//todo there should probably be an AST_PAREN to account for them in the ast.
//     I dont think the AST should be in charge of removing syntax sugar, that should be when lowering it to HIR.
const char *ast_tag_to_string(AstTag tag) {
    switch (tag) {
        case AST_INTEGER:
            return "int";
        case AST_BOOL:
            return "bool";
        case AST_REF:
            return "ref";
        case AST_BINARY:
            return "binary";
        case AST_UNARY:
            return "unary";
        case AST_BLOCK:
            return "block";
        case AST_RETURN:
            return "ret";
        case AST_IF:
            return "if";
        case AST_WHILE:
            return "while";
        case AST_DOT:
            return "dot";
        case AST_CALL:
            return "call";

        case AST_LET:
            return "let";

        case AST_NAMED_FN:
            return "named_fn";
        case AST_STRUCT:
            return "struct";
        case AST_ENUM:
            return "enum";

        case AST_FN_PROTO:
            return "fn_proto";
        case AST_FN_PARAM:
            return "fn_param";
        case AST_FIELD:
            return "field";
        case AST_ENUM_CASE:
            return "enum_case";

        case AST_MODULE:
            return "module";
        case AST_EMPTY:
            return "<empty>";
        default:
            return "<?>";
    }
}

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

#define self_t Ast *self

AstNode *ast_get_node(self_t, AstIndex index) {
    return &self->nodes.data[index];
}

AstNode *ast_get_node_tagged(self_t, AstIndex index, AstTag tag) {
    AstNode *node = ast_get_node(self, index);
    assert(tag == node->tag);
    return node;
}

#undef self_t
