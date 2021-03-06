#include "module.h"

#include <string.h>
#include <stdlib.h>

#include "array_util.h"
#include "parser.h"
#include "ast_to_mir.h"
#include "ast_lowering.h"

// SECTION: Declaration

#define self_t Decl *self

void decl_init_from_ast(self_t, Ast *ast, AstIndex ast_index) {
    AstNode *node = ast_get_node_tagged(ast, ast_index, AST_NAMED_FN);
    Token name_token = ast->tokens.data[node->main_token + 1];

    size_t str_len = name_token.loc.end - name_token.loc.start;
    char *name = malloc(str_len + 1);
    memcpy(name, (const void *) name_token.loc.start, str_len);
    name[str_len] = '\0';

    self->name = name;
    self->state = DeclStateUnused;
    self->ast_index = ast_index;
    self->mir = NULL;
    self->llvm_value = NULL;
}

void decl_free(self_t) {
    free(self->name);
    self->name = NULL;
//    mir_free(self->mir); //todo
    self->mir = NULL;
}

Mir *decl_get_mir_in_module(self_t, Module *module) {
    if (self->mir == NULL) {
        AstToMir lowering;
        ast_to_mir_init(&lowering, module->ast);
        Mir mir = lower_ast_fn(&lowering, self->ast_index);

        self->mir = malloc(sizeof(Mir));
        *self->mir = mir;
    }

    return self->mir;
}

#undef self_t

#define self_t DeclList *self

void decl_list_init(self_t) {
    self->size = 0;
    self->capacity = 0;
    self->data = NULL;
}

void decl_list_free(self_t) {
    ARRAY_FREE(Decl, self->data);
    decl_list_init(self);
}

void decl_list_add(self_t, Decl inst) {

    if (self->capacity < self->size + 1) {
        self->capacity = ARRAY_GROW_CAPCITY(self->capacity);
        self->data = ARRAY_GROW(Decl, self->data, self->capacity);
    }

    self->data[self->size] = inst;
    self->size++;
}

Decl *decl_list_get(self_t, DeclIndex index) {
    if (index < self->size)
        return &self->data[index];
    return NULL;
}

#undef self_t


// SECTION: Module definition

#define self_t Module *self

void module_init(self_t, char *path) {
    self->path = path;
    self->name = strrchr(path, '/') + 1;

    self->ast = NULL;
    self->hir = NULL;
    decl_list_init(&self->decls);
    self->codegen = NULL;
}

void module_free(self_t) {
    if (self->codegen != NULL) {
        codegen_free(self->codegen);
        free(self->codegen);
        self->codegen = NULL;
    }
    decl_list_free(&self->decls);
    if (self->ast) {
//        ast_free(self->ast); //todo
        free(self->codegen);
        self->ast = NULL;
    }

    self->name = NULL;
    self->path = NULL;

}


static uint8_t *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file: %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    uint8_t *buffer = malloc(file_size + 1); //todo must be freed in all cases
    if (buffer == NULL) {
        fprintf(stderr, "Could not allocate memory for file: %s\n", path);
        return NULL;
    }

    size_t bytes_read = fread(buffer, sizeof(uint8_t), file_size, file);
    if (bytes_read < file_size) {
        fprintf(stderr, "Could not read file: %s\n", path);
        return NULL;
    }

    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}


bool module_parse(self_t) {
    assert(self->ast == NULL);

    // Read source
    // `source` ownership is given to the ast.
    uint8_t *source = read_file(self->path);
    if (source == NULL) {
        return false;
    }

    // Lex
    Parser parser;
    parser_init(&parser, source);

    // Parse
    self->ast = malloc(sizeof(Ast));
    *self->ast = parser_parse(&parser);
    return true;
}

static Decl decl_from_hir(self_t, HirIndex index) {
    HirInst *inst = hir_get_inst(self->hir, index);

    if (inst->tag == HIR_CONST_DECL) {
        StringKey name = inst->data.pl_op.payload;
        HirInst *fn_decl = hir_get_inst(self->hir, inst->data.pl_op.operand);
        if (fn_decl->tag != HIR_FN_DECL)
            assert(false);

        HirFnDecl *fn_data = index_list_get_sized(&self->hir->extra, HirFnDecl, fn_decl->data.extra);
        HirIndex param_start = fn_decl->data.extra + (sizeof(HirFnDecl) / sizeof(HirInst));

        Type *param_types = malloc(sizeof(Type) * fn_data->param_len);
        for (size_t i = 0; i < fn_data->param_len; i++) {
            param_types[i] = type_from_hir_inst(self->hir, self->hir->extra.data[param_start + i]);
        }

        DeclFnData *decl_fn = malloc(sizeof(DeclFnData));
        *decl_fn = (DeclFnData) {
            .ret_type = type_from_hir_inst(self->hir, fn_data->ret_ty),
            .param_count = fn_data->param_len,
            .param_types = param_types,
        };

        return (Decl) {
            name,
            (fn_data->flags & HIR_FN_DECL_FLAGS_FOREIGN) ? DeclStateGenerated : DeclStateUnused,
            .data = decl_fn,
            .mir = NULL,
        };
    } else {
        assert(false);
    }
}

static void extract_decls_from_hir(self_t) {
    HirInst *module_inst = hir_get_inst_tagged(self->hir, 0, HIR_MODULE);
    HirModule *module_data = index_list_get_sized(&self->hir->extra, HirModule, module_inst->data.extra);
    assert(module_data != NULL);

    HirIndex decl_start = module_inst->data.extra + (sizeof(HirModule) / sizeof(HirIndex));
    for (HirIndex i = 0; i < module_data->decl_count; i++) {
        HirIndex decl_index = decl_start + i;
        Decl decl = decl_from_hir(self, decl_index);
        decl_list_add(&self->decls, decl);
    }
}

bool module_lower_ast(self_t) {
    assert(self->ast != NULL);
    assert(self->hir == NULL);

    self->hir = malloc(sizeof(Hir));
    *self->hir = ast_lower(self->ast);

    //todo check for errors

    // Extract Decls from HIR
    extract_decls_from_hir(self);

    // We are now done with AST
//    ast_free(self->ast);
    free(self->ast);
    self->ast = NULL;

    return true;
}

bool module_lower_main(self_t) {
    assert(self->hir != NULL);

    // Extract declarations from module
    AstNode *root_node = ast_get_node_tagged(self->ast, ast_index_root, AST_MODULE);
    if (root_node->data.lhs == ast_index_empty) {
        fprintf(stderr, "Module has no main function\n");
        return false;
    }

    for (AstIndex index = root_node->data.lhs; index <= root_node->data.rhs; index++) {
        AstIndex node_index = self->ast->extra_data.data[index];
        Decl decl;
        decl_init_from_ast(&decl, self->ast, node_index);
        decl_list_add(&self->decls, decl);
    }

    // Initialize codegen
    self->codegen = malloc(sizeof(Codegen));
    codegen_init(self->codegen, self);

    // Compile the "main" decl
    Decl *main = module_find_decl(self, "main");
    assert(main != NULL);
    codegen_lower_decl(self->codegen, main);

    // Lower all other decls that have been referenced
    //todo this only does one pass, need to replace this with a stack of decls pending generation.
    for (DeclIndex i = 0; i < self->decls.size; i++) {
        Decl *decl = decl_list_get(&self->decls, i);

        if (decl->state == DeclStateReferenced)
            codegen_lower_decl(self->codegen, decl);
    }

    return true;
}

bool module_emit_llvm(self_t) {
    assert(self->codegen != NULL);

    char *llvm_path = malloc(strlen(self->path) + 3);
    strcpy(llvm_path, self->path);
    strcat(llvm_path, ".ll");

    bool result = codegen_write_to_file(self->codegen, llvm_path);
    free(llvm_path);
    if (!result) {
        return false;
    }

    char *obj_path = malloc(strlen(self->path) + 2);
    strcpy(obj_path, self->path);
    strcat(obj_path, ".o");

    result = codegen_write_to_obj_file(self->codegen, obj_path);
    free(obj_path);

    return result;
}


Decl *module_find_decl(self_t, char *name) {
    for (DeclIndex index = 0; index < self->decls.size; index++) {
        Decl *decl = decl_list_get(&self->decls, index);
        if (strcmp(decl->name, name) == 0) {
            return decl;
        }
    }

    return NULL;
}

#undef self_t


