#include <stdlib.h>
#include <string.h>
#include "ast_to_mir.h"

// Section: Scope

#define self_t AtmScope *self

void atm_scope_init(self_t, AtmScope *parent) {
    assert(self !=
           parent); // Basic check to ensure that we dont create a cycle, but needs to check the whole tree upwards in reality.
    self->size = 0;
    self->capacity = 0;
    self->names = NULL;
    self->data = NULL;
    self->types = NULL;
    self->parent = parent;
}

void atm_scope_free(self_t) {
    //todo this does free the names array, but it leaves the allocations of each string.
    ARRAY_FREE(char *, self->names);
    ARRAY_FREE(MirIndex, self->data);
    ARRAY_FREE(AtmScopeItemType, self->types);
    atm_scope_init(self, self->parent);
}

void atm_scope_set(self_t, const char *name, MirIndex value, AtmScopeItemType type) {
    if (self->capacity < self->size + 1) {
        self->capacity = ARRAY_GROW_CAPCITY(self->capacity);
        self->names = ARRAY_GROW(char *, self->names, self->capacity);
        self->data = ARRAY_GROW(MirIndex, self->data, self->capacity);
        self->types = ARRAY_GROW(AtmScopeItemType, self->types, self->capacity);
    }

    for (uint32_t i = 0; i < self->size; i++) {
        if (strcmp(self->names[i], name) == 0) {
            self->data[i] = value;
            self->types[i] = type;
            return;
        }
    }

    self->names[self->size] = strdup(name);
    self->data[self->size] = value;
    self->types[self->size] = type;
    self->size++;
}

MirIndex *atm_scope_get(self_t, const char *name) {
    for (uint32_t i = 0; i < self->size; i++) {
        if (strcmp(self->names[i], name) == 0) {
            return &self->data[i];
        }
    }

    if (self->parent != NULL) {
        return atm_scope_get(self->parent, name);
    }

    return NULL;
}

AtmScopeItemType *atm_scope_get_type(self_t, const char *name) {
    for (uint32_t i = 0; i < self->size; i++) {
        if (strcmp(self->names[i], name) == 0) {
            return &self->types[i];
        }
    }

    if (self->parent != NULL) {
        return atm_scope_get_type(self->parent, name);
    }

    return NULL;
}

#undef self_t


// Section: Ast-to-mir

#define self_t AstToMir *self

// Utilities
static inline MirIndex add_inst(self_t, MirInstTag tag, MirInstData data) {
    mir_inst_list_add(&self->instructions, (MirInst) {tag, data});
    return self->instructions.size - 1;
}

static inline MirIndex add_extra(self_t, MirIndex data) {
    index_list_add(&self->extra, data);
    return self->extra.size - 1;
}

static MirIndex reserve_inst(self_t) {
    return add_inst(self, MirReserved, (MirInstData) {});
}

static MirIndex fill_inst(self_t, MirIndex reserved, MirInstTag tag, MirInstData data) {
    MirInst *inst = mir_inst_list_get(&self->instructions, reserved);
    assert(inst->tag == MirReserved);
    inst->tag = tag;
    inst->data = data;
    return reserved;
}

// Returns a string containing the content of the token at the given index.
// The caller owns the string memory.
static char *get_token_content(self_t, TokenIndex token) {
    Token main_token = self->ast->tokens.data[token];
    size_t str_len = main_token.loc.end - main_token.loc.start;
    char *str = malloc(str_len + 1);
    memcpy(str, (const void *) main_token.loc.start, str_len);
    str[str_len] = '\0';
    return str;
}

static void push_scope(self_t) {
    AtmScope *scope = malloc(sizeof(AtmScope));
    atm_scope_init(scope, self->scope);
    self->scope = scope;
}

static void pop_scope(self_t) {
    AtmScope *scope = self->scope;
    self->scope = scope->parent;
    atm_scope_free(scope);
    free(scope);
}

static AstIndex find_named_fn(self_t, const char *name) {
    AstNode *module = ast_get_node_tagged(self->ast, ast_index_root, AST_MODULE);

    for (AstIndex i = module->data.lhs; i <= module->data.rhs; i++) {
        AstNode *decl = ast_get_node(self->ast, self->ast->extra_data.data[i]);
        if (decl->tag != AST_NAMED_FN)
            continue;

        char *fn_name = get_token_content(self, decl->main_token + 1);
        bool same = strcmp(fn_name, name) == 0;
        free(fn_name);

        if (same) return i;
    }

    return ast_index_empty;
}

// Public API
void ast_to_mir_init(self_t, Ast *ast) {
    self->ast = ast;

    mir_inst_list_init(&self->instructions);
    index_list_init(&self->extra);

    // Create global scope
    self->scope = malloc(sizeof(AtmScope));
    atm_scope_init(self->scope, NULL);
}

void ast_to_mir_free(self_t) {
    atm_scope_free(self->scope);
    free(self->scope);

    //todo other stuff
}

Mir lower_ast_fn(self_t, AstIndex fn_index) {
    AstNode *node = ast_get_node_tagged(self->ast, fn_index, AST_NAMED_FN);

    // New scope with params
    push_scope(self);
    AstNode *proto = ast_get_node_tagged(self->ast, node->data.lhs, AST_FN_PROTO);
    AstFnProto *proto_data = ((AstFnProto *) &self->ast->extra_data.data[proto->data.lhs]);

    MirIndex root_index = mir_lower_block(self, node->data.rhs, proto_data);
    assert(root_index == 0);

    // Cleanup
    pop_scope(self);

    return (Mir) {
        .instructions = self->instructions,
        .extra = self->extra,
    };
}

// Implementation

MirIndex mir_lower_stmt(self_t, AstIndex stmt_index) {
    AstNode *node = ast_get_node(self->ast, stmt_index);
    assert(node != NULL);

    switch (node->tag) {
        case AST_LET:
            return mir_lower_let(self, stmt_index);
        default:
            return mir_lower_expr(self, stmt_index);
    }
}

MirIndex mir_lower_let(self_t, AstIndex stmt_index) {
    AstNode *node = ast_get_node_tagged(self->ast, stmt_index, AST_LET);

    // Type
    Type type = {TypeI64};
    if (node->data.lhs != ast_index_empty) {
        // Type annotation has been provided, use it.
        AstNode *type_node = ast_get_node_tagged(self->ast, node->data.lhs, AST_TYPE);

        char *type_name = get_token_content(self, type_node->main_token);
        type = type_from_name(type_name);
        free(type_name);
    }

    // Alloc
    MirIndex alloc_index = add_inst(self, MirAlloc, (MirInstData) {
        .ty = type
    });
    // Insert the pointer to the scope
    char *name = get_token_content(self, node->main_token + 1);
    atm_scope_set(self->scope, name, alloc_index, AtmScopeItemTypeVar);
    free(name);

    // Initializer (must be present for now)
    assert(node->data.rhs != ast_index_empty);
    MirIndex init_index = mir_lower_expr(self, node->data.rhs);

    // Store
    MirIndex store_index = add_inst(self, MirStore, (MirInstData) {
        .bin_op = {
            .lhs = index_to_ref(alloc_index),
            .rhs = index_to_ref(init_index),
        }
    });

    return store_index;
}


MirIndex mir_lower_expr(self_t, AstIndex expr_index) {
    AstNode *node = ast_get_node(self->ast, expr_index);
    assert(node != NULL);

    switch (node->tag) {
        case AST_INTEGER:
            return mir_lower_int_const(self, expr_index);
        case AST_REF:
            return mir_lower_ref(self, expr_index);
        case AST_BINARY:
            return mir_lower_bin_op(self, expr_index);
        case AST_CALL:
            return mir_lower_call(self, expr_index);
//        case AST_BLOCK: //todo this should flatten the block while also entering a new scope (eg do not generate an MirBlock inst)
//            return mir_lower_block(self, expr_index);
        case AST_RETURN:
            return mir_lower_return(self, expr_index);
        default: {
            printf("Cannot lower %s as expr!\n", ast_tag_to_string(node->tag));
            assert(false);
        }
    }
}

MirIndex mir_lower_int_const(self_t, AstIndex expr_index) {
    AstNode *node = ast_get_node_tagged(self->ast, expr_index, AST_INTEGER);

    // Parse u32
    char *str = get_token_content(self, node->main_token);
    uint32_t value = strtol(str, NULL, 10);
    free(str);

    return add_inst(self, MirConstant, (MirInstData) {
        .ty_pl = {
            .payload = value, //todo this needs to point into values list
        }
    });
}

MirIndex mir_lower_ref(self_t, AstIndex expr_index) {
    AstNode *node = ast_get_node_tagged(self->ast, expr_index, AST_REF);

    // Lookup name in scope
    char *name = get_token_content(self, node->main_token);
    MirIndex *index = atm_scope_get(self->scope, name);

    if (index == NULL) {
        // Not found in scope, check if it is a named function
        AstIndex fn_index = find_named_fn(self, name);
        if (fn_index != ast_index_empty) {
            //todo adding the name here is really hacky and a memory leak currently.
            // Should reference the decl index in the module or something.
            return add_inst(self, MirFnPtr, (MirInstData) {
                .fn_ptr = name,
            });
        }

        // Not a named function, not sure what it is
        printf("Undefined reference %s!\n", name);
        free(name);
        assert(false);
    }

    // There is an element, generate item based on the type
    AtmScopeItemType type = *atm_scope_get_type(self->scope, name);
    switch (type) {
        case AtmScopeItemTypeVar: {
            free(name);
            return add_inst(self, MirLoad, (MirInstData) {
                .un_op = index_to_ref(*index)
            });
        }
        case AtmScopeItemTypeArg: {
            free(name);
            return *index;
//            return index_to_ref(*index);
//            return add_inst(self, MirLoad, (MirInstData) {
//                .un_op = index_to_ref(*index)
//            });
        }
        default: {
            //todo error is misleading
            printf("Cannot lower %s as ref!\n", ast_tag_to_string(node->tag));
            assert(false);
        }
    }
}

MirIndex mir_lower_bin_op(self_t, AstIndex expr_index) {
    AstNode *node = ast_get_node_tagged(self->ast, expr_index, AST_BINARY);

    // Lower lhs/rhs
    Ref lhs = index_to_ref(mir_lower_expr(self, node->data.lhs));
    Ref rhs = index_to_ref(mir_lower_expr(self, node->data.rhs));

    char *op_str = get_token_content(self, node->main_token);
    MirInstTag op_tag;
    if (strcmp(op_str, "+") == 0) {
        op_tag = MirAdd;
    } else if (strcmp(op_str, "-") == 0) {
        op_tag = MirSub;
    } else if (strcmp(op_str, "*") == 0) {
        op_tag = MirMul;
    } else if (strcmp(op_str, "/") == 0) {
        op_tag = MirDiv;
    } else if (strcmp(op_str, "==") == 0) {
        op_tag = MirEq;
    } else if (strcmp(op_str, "!=") == 0) {
        op_tag = MirNEq;
    } else if (strcmp(op_str, ">") == 0) {
        op_tag = MirGt;
    } else if (strcmp(op_str, ">=") == 0) {
        op_tag = MirGtEq;
    } else if (strcmp(op_str, "<") == 0) {
        op_tag = MirLt;
    } else if (strcmp(op_str, "<=") == 0) {
        op_tag = MirLtEq;
    } else {
        printf("Unsupported binary op %s!\n", op_str);
        assert(false);
    }

    free(op_str);

    return add_inst(self, op_tag, (MirInstData) {
        .bin_op = {lhs, rhs}
    });
}

MirIndex mir_lower_call(self_t, AstIndex expr_index) {
    AstNode *node = ast_get_node_tagged(self->ast, expr_index, AST_CALL);

    // Lower the operand
    Ref operand = index_to_ref(mir_lower_expr(self, node->data.lhs));
    //todo type checking here. How to ensure ref resolves to a function pointer?

    // Lower params
    AstCallData call_data = *((AstCallData *) &self->ast->extra_data.data[node->data.rhs]);

    IndexList arg_indices;
    index_list_init(&arg_indices);

    if (call_data.arg_start != ast_index_empty) {
        for (AstIndex arg_index = call_data.arg_start; arg_index <= call_data.arg_end; arg_index++) {
            Ref lowered_arg = index_to_ref(mir_lower_expr(self, self->ast->extra_data.data[arg_index]));
            index_list_add(&arg_indices, lowered_arg);
        }
    }

    // Add arg instruction pointers
    MirIndex data_index = add_extra(self, arg_indices.size);
    for (uint32_t i = 0; i < arg_indices.size; i++) {
        add_extra(self, *index_list_get(&arg_indices, i));
    }

    // Insert call
    return add_inst(self, MirCall, (MirInstData) {
        .pl_op = {
            .payload = data_index,
            .operand = operand,
        }
    });
}

MirIndex mir_lower_block(self_t, AstIndex block_index, AstFnProto *proto_data) {
    AstNode *block = ast_get_node_tagged(self->ast, block_index, AST_BLOCK);

    if (block->data.lhs == ast_index_empty) {
        MirIndex extra_index = add_extra(self, 0);
        return add_inst(self, MirBlock, (MirInstData) {
            .ty_pl = {.payload = extra_index},
        });
    }

    // Enter block scope
    // There is a slight duplication here in that we enter a new scope for a function and then again for its block.
    //todo if params are added here then we do not need a new scope at the function level
    push_scope(self);
    MirIndex out_index = reserve_inst(self);

    IndexList insts;
    index_list_init(&insts);

    // Append function parameters if present
    if (proto_data != NULL) {
        if (proto_data->param_start != ast_index_empty) {
            for (AstIndex i = proto_data->param_start; i <= proto_data->param_end; i++) {
                AstNode *param = ast_get_node_tagged(self->ast, self->ast->extra_data.data[i], AST_FN_PARAM);

                // Create the `arg` node.
                AstIndex arg_index = add_inst(self, MirArg, (MirInstData) {
                    .ty_pl = {.payload = i - proto_data->param_start}
                });

                char *param_name = get_token_content(self, param->main_token);
                //todo why am i not inserting as a ref?
                atm_scope_set(self->scope, param_name, arg_index, AtmScopeItemTypeArg);
                free(param_name);
            }
        }
    }

    // Append block instructions
    for (uint32_t index = block->data.lhs; index <= block->data.rhs; index++) {
        AstIndex ast_index = self->ast->extra_data.data[index];
        MirIndex inst_index = mir_lower_stmt(self, ast_index);

        index_list_add(&insts, inst_index);
    }

    // Copy instructions to extra data
    MirIndex data_index = add_extra(self, insts.size);
    for (uint32_t i = 0; i < insts.size; i++) {
        add_extra(self, *index_list_get(&insts, i));
    }

    // Cleanup
    pop_scope(self);

    return fill_inst(self, out_index, MirBlock, (MirInstData) {
        .ty_pl = {
            .payload = data_index,
        }
    });
}

MirIndex mir_lower_return(self_t, AstIndex ret_index) {
    AstNode *ret = ast_get_node_tagged(self->ast, ret_index, AST_RETURN);

    if (ret->data.lhs == ast_index_empty) {
        return add_inst(self, MirRet, (MirInstData) {
            .un_op = RefZero,
        });
    }

    MirIndex expr_index = mir_lower_expr(self, ret->data.lhs);

    return add_inst(self, MirRet, (MirInstData) {
        .un_op = index_to_ref(expr_index),
    });
}

#undef self_t