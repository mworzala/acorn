#include "parser_internal.h"

#include <assert.h>
#include <printf.h>

#include "parser.h"
#include "array_util.h"

// Empty values

static const AstNode ast_node_empty = {
    .tag = AST_EMPTY,
    .main_token = UINT32_MAX,
    .data = {
        .lhs = ast_index_empty,
        .rhs = ast_index_empty,
    },
};

static const AstData ast_data_empty = {
    .lhs = ast_index_empty,
    .rhs = ast_index_empty,
};

#define self_t Parser *self

Token parse_peek_last(self_t) {
    assert(self->tok_index > 0);
    return self->tokens.data[self->tok_index - 1];
}

Token parse_peek_curr(self_t) {
    return self->tokens.data[self->tok_index];
}

Token parse_advance(self_t) {
    if (self->tokens.size <= self->tok_index + 1) {
        // Always return last element, which is known to be EOF.
        return self->tokens.data[self->tokens.size - 1];
    }

    Token tok = parse_peek_curr(self);
    self->tok_index++;
    return tok;
}

bool parse_match(self_t, TokenType type) {
    return parse_peek_curr(self).type == type;
}

bool parse_match_advance(self_t, TokenType type) {
    Token tok = parse_peek_curr(self);
    if (tok.type != type) {
        return false;
    }

    parse_advance(self);
    return true;
}

TokenIndex parse_assert(self_t, TokenType type) {
    Token tok = parse_advance(self);
    if (tok.type != type) {
        printf("Expected token of type %d, got %d\n", type, tok.type);
        assert(false);
    }
    return self->tok_index - 1;
}


// Top level declarations

AstIndex int_top_level_decls(self_t) {
    if (parse_peek_curr(self).type == TOK_FN) {
        return tl_fn_decl(self);
    }

    return ast_index_empty;
}

AstIndex tl_fn_decl(self_t) {
    TokenIndex main_token = parse_assert(self, TOK_FN);

    // Parse prototype
    AstIndex prototype = fn_proto(self);

    // Parse body
    AstIndex body_block = expr_block(self);

    // Construct node
    ast_node_list_add(&self->nodes, (AstNode) {
        .tag = AST_NAMED_FN,
        .main_token = main_token,
        .data = {
            .lhs = prototype,
            .rhs = body_block,
        }
    });
    return self->nodes.size - 1;
}


// Statements

AstIndex int_stmt(self_t) {
    if (parse_peek_curr(self).type == TOK_LET) {
        return stmt_let(self);
    }

    return int_expr(self);
}

AstIndex stmt_let(self_t) {
    TokenIndex main_token = parse_assert(self, TOK_LET);

    // Ensure identifier is present.
    // The token stream stays around in the next phase, so this may be fetched in the future.
    parse_assert(self, TOK_IDENT);

    // Parse the type expression, if present
    AstIndex type_expr = ast_index_empty;
    //todo

    // Parse the initializer, if present
    AstIndex init_expr = ast_index_empty;
    if (parse_match_advance(self, TOK_EQ)) {
        init_expr = int_expr(self);
    }

    // Generate the AST node itself.
    ast_node_list_add(&self->nodes, (AstNode) {
        .tag = AST_LET,
        .main_token = main_token,
        .data = {
            .lhs = type_expr,
            .rhs = init_expr,
        },
    });
    return self->nodes.size - 1;
}


// Expressions

AstIndex int_expr(self_t) {
    if (parse_match(self, TOK_LBRACE)) {
        return expr_block(self);
    } else if (parse_match(self, TOK_RETURN)) {
        return expr_return(self);
    }

    return int_expr_bp(self);
}

AstIndex int_expr_bp(self_t) {
    ParseFrame top = {
        .min_bp = 0,
        .lhs = expr_literal(self),
        .op_idx = UINT32_MAX,
    };

    ParseFrameStack stack;
    // Freed in the single return below. Must be careful about returns here.
    parse_frame_stack_init(&stack);

    for (;;) {
        Token token = parse_peek_curr(self);
        BindingPower bp = token_bp(token, top.lhs == UINT32_MAX);

        bool is_not_op =
            bp.lhs == 0; // We return 0, 0 if it was not a valid operator, and none of them return 0 for LHS.
        bool is_low_bp = bp.lhs < top.min_bp; // Too low of binding power (precedence) to continue.
        if (is_not_op || is_low_bp) {
            ParseFrame res = top;
            if (parse_frame_stack_empty(&stack)) {
                parse_frame_stack_free(&stack);
                return res.lhs;
            }

            top = parse_frame_stack_pop(&stack);

            if (self->tokens.data[res.op_idx].type == TOK_LPAREN) {
                assert(parse_advance(self).type == TOK_RPAREN);
                top.lhs = res.lhs;
                continue;
            }

            // Account for special case of prefix operators.
            // AST unary operator is normalized to only have a data.lhs, and no RHS.
            // but prefix operators are the opposite, only RHS. So flip that.
            AstIndex lhs = top.lhs, rhs = res.lhs;
            if (lhs == ast_index_empty) {
                lhs = rhs;
                rhs = ast_index_empty;
            }

            // Write the node to the node array.
            ast_node_list_add(&self->nodes, (AstNode) {
                // Unary ops have an empty rhs, whereas binary ops have a value.
                .tag = rhs == ast_index_empty ? AST_UNARY : AST_BINARY,
                .main_token = res.op_idx,
                .data = {lhs, rhs},
            });
            top.lhs = self->nodes.size - 1;
            continue;
        }

        TokenIndex op_idx = self->tok_index;
        parse_advance(self); // Eat the operator symbol

        parse_frame_stack_push(&stack, top);
        top = (ParseFrame) {
            .min_bp = bp.rhs,
            .lhs = expr_literal(self),
            .op_idx = op_idx,
        };
    }
}

AstIndex expr_literal(self_t) {
    Token next = parse_peek_curr(self);
    if (next.type == TOK_NUMBER) {
        parse_advance(self);
        ast_node_list_add(&self->nodes, (AstNode) {
            .tag = AST_INTEGER,
            .main_token = self->tok_index - 1,
            .data = ast_data_empty,
        });
        return self->nodes.size - 1;
    } else if (next.type == TOK_TRUE || next.type == TOK_FALSE) {
        parse_advance(self);
        ast_node_list_add(&self->nodes, (AstNode) {
            .tag = AST_BOOL,
            .main_token = self->tok_index - 1,
            .data = ast_data_empty,
        });
        return self->nodes.size - 1;
    } else if (next.type == TOK_IDENT) {
        parse_advance(self);
        ast_node_list_add(&self->nodes, (AstNode) {
            .tag = AST_REF,
            .main_token = self->tok_index - 1,
            .data = ast_data_empty,
        });
        return self->nodes.size - 1;
    }

    return ast_index_empty;
}

AstIndex expr_block(self_t) {
    assert(parse_peek_curr(self).type == TOK_LBRACE);
    TokenIndex main_token = self->tok_index;

    AstIndexPair data = int_parse_list(self, int_stmt,
                                       TOK_LBRACE, TOK_RBRACE, TOK_SEMI);

    // Create the block node
    ast_node_list_add(&self->nodes, (AstNode) {
        .tag = AST_BLOCK,
        .main_token = main_token,
        .data = {data.first, data.second},
    });
    return self->nodes.size - 1;
}

AstIndex expr_return(self_t) {
    TokenIndex main_token = parse_assert(self, TOK_RETURN);

    AstIndex expr = ast_index_empty;
    TokenType next = parse_peek_curr(self).type;
    // Semicolon to manually terminate, rbrace if it is last in a block.
    if (next != TOK_SEMI && next != TOK_RBRACE) {
        expr = int_expr(self);
    }

    ast_node_list_add(&self->nodes, (AstNode) {
        .tag = AST_RETURN,
        .main_token = main_token,
        .data = {expr, ast_index_empty},
    });
    return self->nodes.size - 1;
}


// Special

AstIndex fn_proto(self_t) {
    TokenIndex main_token = parse_assert(self, TOK_IDENT);

    // Parse parameters
    AstIndexPair param_data = int_parse_list(self, fn_param,
                                             TOK_LPAREN, TOK_RPAREN, TOK_COMMA);

    // Parse type expression
    AstIndex type_expr = ast_index_empty;
    //todo

    AstIndex proto_data_idx = self->extra_data.size;
    FnProto proto_data = {
        .param_start = param_data.first,
        .param_end = param_data.second,
    };
    ast_index_list_add_sized(&self->extra_data, proto_data);
//    ast_index_list_add_multi(&self->extra_data, &proto_data, sizeof(proto_data) / sizeof(AstIndex));

    ast_node_list_add(&self->nodes, (AstNode) {
        .tag = AST_FN_PROTO,
        .main_token = main_token,
        .data = {proto_data_idx, type_expr},
    });
    return self->nodes.size - 1;
}

AstIndex fn_param(self_t) {
    TokenIndex main_token = parse_assert(self, TOK_IDENT);

    // Parse type expression
    AstIndex type_expr = ast_index_empty;
    //todo

    ast_node_list_add(&self->nodes, (AstNode) {
        .tag = AST_FN_PARAM,
        .main_token = main_token,
        .data = {ast_index_empty, type_expr},
    });
    return self->nodes.size - 1;
}


// Unmapped

AstIndexPair
int_parse_list(self_t, AstIndex (*parse_fn)(self_t), TokenType open, TokenType close, TokenType delimiter) {
    parse_assert(self, open);

    // Need to store the inner indices so that they can all be added at once to ensure
    //  they are continuous inside extra_data. Consider the case of {{x}}.
    AstIndexList inner_indices;
    ast_index_list_init(&inner_indices);

    // Parse inner expressions
    while (parse_peek_curr(self).type != close) {
        AstIndex idx = parse_fn(self);
        ast_index_list_add(&inner_indices, idx);

        if (idx == ast_index_empty) {
            assert(false);
        }

        if (parse_peek_curr(self).type == delimiter) {
            parse_advance(self);
        } else if (parse_peek_curr(self).type == close) {
            break;
        } else {
            assert(false);
//            parse_error(self, "Expected ';' or '}'");
        }
    }
    parse_assert(self, close);

    // First node
    AstIndex start = ast_index_empty;
    AstIndex end = ast_index_empty;

    if (inner_indices.size != 0) {
        start = self->extra_data.size;
        end = self->extra_data.size + inner_indices.size - 1;

        // Copy inner_indices to extra_data
        for (size_t i = 0; i < inner_indices.size; i++) {
            ast_index_list_add(&self->extra_data, inner_indices.data[i]);
        }
    }

    ast_index_list_free(&inner_indices);
    return (AstIndexPair) {start, end};
}


// Pratt BP

BindingPower token_bp(Token token, bool is_prefix) {
    switch (token.type) {
        case TOK_AMPAMP:
        case TOK_BARBAR:
            return (BindingPower) {3, 4};
        case TOK_EQEQ:
        case TOK_BANGEQ:
        case TOK_GT:
        case TOK_GTEQ:
        case TOK_LT:
        case TOK_LTEQ:
            return (BindingPower) {5, 6};
        case TOK_PLUS:
        case TOK_MINUS:
            return !is_prefix ? (BindingPower) {15, 16} :
                   (BindingPower) {99, 19};
        case TOK_STAR:
        case TOK_SLASH:
            return (BindingPower) {17, 18};
        case TOK_BANG:
            return (BindingPower) {21, 100};
        case TOK_LPAREN:
            return (BindingPower) {99, 0};
        default:
            return (BindingPower) {0, 0};
    }
}


#undef self_t


// Parse frame/stack

#define self_t ParseFrameStack *self

void parse_frame_stack_init(self_t) {
    self->size = 0;
    self->capacity = 0;
    self->data = NULL;
}

void parse_frame_stack_free(self_t) {
    ARRAY_FREE(ParseFrame, self->data);
    parse_frame_stack_init(self);
}

void parse_frame_stack_push(self_t, ParseFrame frame) {
    if (self->size == self->capacity) {
        self->capacity = ARRAY_GROW_CAPCITY(self->capacity);
        self->data = ARRAY_GROW(ParseFrame, self->data, self->capacity);
    }
    self->data[self->size] = frame;
    self->size++;
}

ParseFrame parse_frame_stack_pop(self_t) {
    assert(self->size > 0);
    self->size--;
    return self->data[self->size];
}

bool parse_frame_stack_empty(self_t) {
    return self->size == 0;
}

#undef self_t






















