#include "ast_to_llvm.h"
#include "array_util.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

//region Scope

#define self_t AcgScope *self

void acg_scope_init(self_t, AcgScope *parent) {
    assert(self != parent);
    self->size = 0;
    self->capacity = 0;
    self->names = NULL;
    self->data = NULL;
    self->parent = parent;
}

void acg_scope_free(self_t) {
    ARRAY_FREE(char *, self->names);
    ARRAY_FREE(LLVMValueRef, self->data);
    acg_scope_init(self, self->parent);
}

void acg_scope_set(self_t, const char *name, LLVMValueRef value) {
    if (self->capacity < self->size + 1) {
        self->capacity = ARRAY_GROW_CAPCITY(self->capacity);
        self->names = ARRAY_GROW(char *, self->names, self->capacity);
        self->data = ARRAY_GROW(LLVMValueRef, self->data, self->capacity);
    }

    for (uint32_t i = 0; i < self->size; i++) {
        if (strcmp(self->names[i], name) == 0) {
            self->data[i] = value;
            return;
        }
    }

    self->names[self->size] = strdup(name);
    self->data[self->size] = value;
    self->size++;
}

LLVMValueRef *acg_scope_get(self_t, const char *name) {
    for (uint32_t i = 0; i < self->size; i++) {
        if (strcmp(self->names[i], name) == 0) {
            return &self->data[i];
        }
    }

    if (self->parent != NULL) {
        return acg_scope_get(self->parent, name);
    }

    return NULL;
}

#undef self_t

//endregion

#define llvm_int LLVMInt64Type()

void compile_ast_to_file(Ast *ast, const char *filename) {
    AstCodeGen codegen;
    ast_codegen_init(&codegen, ast);

    AstNode *root = &ast->nodes.data[ast_index_root];
    if (root->tag == AST_MODULE) {
        ast_codegen_module(&codegen, root);
        ast_codegen_write(&codegen, filename);
    } else {
        printf("Error: root node is not a module\n");
    }

    ast_codegen_free(&codegen);
}

#define self_t AstCodeGen *self

//region Boring init stuff

void ast_codegen_init(self_t, Ast *ast) {
    self->ast = ast;
    acg_scope_init(&self->root_scope, NULL);
    self->context = LLVMContextCreate();
    self->module = LLVMModuleCreateWithNameInContext("unnamed_module", self->context);
    self->builder = LLVMCreateBuilderInContext(self->context);
}

void ast_codegen_free(self_t) {
    self->ast = NULL;
    acg_scope_free(&self->root_scope);
    LLVMDisposeBuilder(self->builder);
    LLVMDisposeModule(self->module);
    LLVMContextDispose(self->context);
}

void ast_codegen_write(self_t, const char *filename) {
    char *error = NULL;
    bool result = LLVMPrintModuleToFile(self->module, filename, &error);
    if (result) {
        printf("Error: %s\n", error);
        LLVMDisposeMessage(error);
    } else {
        printf("Wrote to %s\n", filename);
    }
    LLVMDisposeMessage(error);
}

//endregion

//region Special

void ast_codegen_module(self_t, AstNode *node) {
    if (node->data.lhs == ast_index_empty) {
        return; // Empty module
    }

    // Add function prototypes to scope
    for (AstIndex idx = node->data.lhs; idx <= node->data.rhs; idx++) {
        AstNode *child = ast_codegen_node_from_extra_data(self, idx);
        switch (child->tag) {
            case AST_NAMED_FN: {
                AstNode *proto_node = ast_codegen_node(self, child->data.lhs);
                LLVMValueRef fn = ast_codegen_fn_proto(self, proto_node);
                char *fn_name = ast_codegen_get_token_content(self, child->main_token + 1);
                acg_scope_set(&self->root_scope, fn_name, fn);
                free(fn_name);
                break;
            }
            default:
                printf("Error: Unexpected ast node in module: %s\n", ast_tag_to_string(child->tag));
                assert(false);
        }
    }

    // Generate functions
    for (AstIndex idx = node->data.lhs; idx <= node->data.rhs; idx++) {
        AstNode *child = ast_codegen_node_from_extra_data(self, idx);
        switch (child->tag) {
            case AST_NAMED_FN:
                ast_codegen_named_fn(self, child);
                break;
            default:
                printf("Error: Unexpected ast node in module: %s\n", ast_tag_to_string(child->tag));
                assert(false);
        }
    }
}

//endregion

//region Module declarations

LLVMValueRef ast_codegen_fn_proto(self_t, AstNode *node) {
    AstFnProto proto = *((AstFnProto *) &self->ast->extra_data.data[node->data.lhs]);

    size_t param_count = 0;
    LLVMTypeRef *params = NULL;
    if (proto.param_start != ast_index_empty) {
        param_count = proto.param_end - proto.param_start + 1;
        params = malloc(sizeof(LLVMTypeRef) * param_count);
        for (int32_t i = 0; i < param_count; i++)
            params[i] = llvm_int;
    }

    LLVMTypeRef fn_type = LLVMFunctionType(llvm_int, params, param_count, false);

    if (params != NULL) {
        free(params);
    }

    char *fn_name = ast_codegen_get_token_content(self, node->main_token);
    LLVMValueRef fn = LLVMAddFunction(self->module, fn_name, fn_type);
    free(fn_name);

    return fn;
}

void ast_codegen_named_fn(self_t, AstNode *node) {
    // Fn has already been defined in scope
    char *fn_name = ast_codegen_get_token_content(self, node->main_token + 1);
    LLVMValueRef *fn = acg_scope_get(&self->root_scope, fn_name);
    assert(fn != NULL);
    free(fn_name);

    // Create body scope
    AstNode *proto_node = ast_codegen_node(self, node->data.lhs);
    AstFnProto proto = *((AstFnProto *) &self->ast->extra_data.data[proto_node->data.lhs]);
    AcgScope body_scope;
    acg_scope_init(&body_scope, &self->root_scope);
    if (proto.param_start != ast_index_empty) {
        for (int32_t i = 0; i < proto.param_end - proto.param_start + 1; i++) {
            AstNode *param = ast_codegen_node_from_extra_data(self, proto.param_start + i);
            char *param_name = ast_codegen_get_token_content(self, param->main_token);
            acg_scope_set(&body_scope, param_name, LLVMGetParam(*fn, i));
            free(param_name);
        }
    }

    // Generate function body
    AstNode *body = ast_codegen_node(self, node->data.rhs);
    ast_codegen_block(self, body, *fn, &body_scope);

    acg_scope_free(&body_scope);
}

//endregion

//region Statements

//endregion

//region Expressions

LLVMValueRef ast_codegen_expr(self_t, AstNode *node, LLVMValueRef fn, AcgScope *scope) {
    printf("eval: %s\n", ast_tag_to_string(node->tag));
    switch (node->tag) {
        case AST_INTEGER:
            return ast_codegen_const_int(self, node);
        case AST_REF:
            return ast_codegen_ref(self, node, scope);
        case AST_BINARY:
            return ast_codegen_binary_op(self, node, fn, scope);
        case AST_BLOCK: {
            // Create outer scope and generate block
            AcgScope block_scope;
            acg_scope_init(&block_scope, scope);
            ast_codegen_block(self, node, fn, &block_scope);
            acg_scope_free(&block_scope);
            return NULL;
        }
        case AST_RETURN:
            ast_codegen_return(self, node, fn, scope);
            return NULL;
        case AST_CALL:
            return ast_codegen_call(self, node, fn, scope);
        default:
            printf("Error: Unexpected ast node in expr: %s\n", ast_tag_to_string(node->tag));
            assert(false);
    }
}

LLVMValueRef ast_codegen_const_int(self_t, AstNode *node) {
    char *str = ast_codegen_get_token_content(self, node->main_token);
    uint64_t value = atoll(str);
    free(str);
    return LLVMConstInt(llvm_int, value, false);
}

LLVMValueRef ast_codegen_ref(self_t, AstNode *node, AcgScope *scope) {
    char *str = ast_codegen_get_token_content(self, node->main_token);
    LLVMValueRef *value = acg_scope_get(scope, str);
    free(str);
    assert(value != NULL);
    return *value;
}

LLVMValueRef ast_codegen_binary_op(self_t, AstNode *node, LLVMValueRef fn, AcgScope *scope) {
    LLVMValueRef lhs = ast_codegen_expr(self, ast_codegen_node(self, node->data.lhs), fn, scope);
    LLVMValueRef rhs = ast_codegen_expr(self, ast_codegen_node(self, node->data.rhs), fn, scope);

    char *op = ast_codegen_get_token_content(self, node->main_token);
    switch (op[0]) {
        case '+':
            return LLVMBuildAdd(self->builder, lhs, rhs, "add-tmp");
        default:
            assert(false);
    }
}

void ast_codegen_block(self_t, AstNode *node, LLVMValueRef fn, AcgScope *scope) {
    LLVMBasicBlockRef ll_block = LLVMAppendBasicBlock(fn, "entry");
    LLVMPositionBuilderAtEnd(self->builder, ll_block);

    bool did_return = false;
    if (node->data.lhs != ast_index_empty) {
        for (AstIndex idx = node->data.lhs; idx <= node->data.rhs; idx++) {
            AstNode *child = ast_codegen_node_from_extra_data(self, idx);
            LLVMValueRef implicit_return = ast_codegen_expr(self, child, fn, scope);
            if (child->tag == AST_RETURN) {
                // Do nothing
                did_return = true;
            } else if (implicit_return != NULL) {
                LLVMBuildRet(self->builder, implicit_return);
                did_return = true;
            }
        }
    }

    // Implicit null return
    if (!did_return) {
        LLVMBuildRet(self->builder, LLVMConstNull(llvm_int));
    }

}

void ast_codegen_return(self_t, AstNode *node, LLVMValueRef fn, AcgScope *scope) {
    LLVMValueRef ret = ast_codegen_expr(self, ast_codegen_node(self, node->data.lhs), fn, scope);
    LLVMBuildRet(self->builder, ret);
}

LLVMValueRef ast_codegen_call(self_t, AstNode *node, LLVMValueRef fn, AcgScope *scope) {
    AstNode *callee_node = ast_codegen_node(self, node->data.lhs);
    LLVMValueRef fn_target = ast_codegen_expr(self, callee_node, fn, scope);
    assert(LLVMIsAFunction(fn_target));

    // Build args
    AstCallData call_data = *((AstCallData *) &self->ast->extra_data.data[node->data.rhs]);
    uint32_t arg_count = 0;
    LLVMValueRef *args = NULL;
    if (call_data.arg_start != ast_index_empty) {
        arg_count = call_data.arg_end - call_data.arg_start + 1;

        args = malloc(arg_count * sizeof(LLVMValueRef));
        for (int i = 0; i < arg_count; i++) {
            AstNode *arg_node = ast_codegen_node_from_extra_data(self, call_data.arg_start + i);
            args[i] = ast_codegen_expr(self, arg_node, fn, scope);
        }
    }

    // Make call (todo LLVMBuildCall is deprecated, use LLVMBuildCall2)
    LLVMValueRef call = LLVMBuildCall(self->builder, fn_target, args, arg_count, "call_tmp");
    if (args != NULL) {
        free(args);
    }

    return call;
}

//endregion

AstNode *ast_codegen_node(self_t, AstIndex node_index) {
    return &self->ast->nodes.data[node_index];
}

AstNode *ast_codegen_node_from_extra_data(self_t, AstIndex extra_data_index) {
    AstIndex node_index = self->ast->extra_data.data[extra_data_index];
    return &self->ast->nodes.data[node_index];
}

char *ast_codegen_get_token_content(self_t, TokenIndex token_index) {
    Token token = self->ast->tokens.data[token_index];
    size_t len = token.loc.end - token.loc.start;
    char *content = malloc(len + 1);
    memcpy(content, (const void *) token.loc.start, len);
    content[len] = '\0';
    return content;
}

#undef self_t
