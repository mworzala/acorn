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


// Parse functions

AstIndex expr_bp(self_t) {
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

        bool is_not_op = bp.lhs == 0; // We return 0, 0 if it was not a valid operator, and none of them return 0 for LHS.
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
    }

    return ast_index_empty;
}


// Pratt BP

BindingPower token_bp(Token token, bool is_prefix) {
    switch (token.type) {
        case TOK_PLUS:
        case TOK_MINUS:
            return !is_prefix ? (BindingPower) {AST_BINARY, 5, 6} :
                   (BindingPower) { 99, 9};
        case TOK_STAR:
        case TOK_SLASH:
            return (BindingPower) {7, 8};
        case TOK_BANG:
            return (BindingPower) {11, 100};
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






















