#ifndef ACORNC_PARSER_INTERNAL_H
#define ACORNC_PARSER_INTERNAL_H

#include "common.h"
#include "lexer.h"
#include "ast.h"

typedef struct parser_s Parser;

#define self_t Parser *self

// Parser utilities
Token parse_peek_last(self_t);
Token parse_peek_curr(self_t);
Token parse_advance(self_t);

// Returns true and advances if the next token is the expected token
// Otherwise returns false and does not advance
bool parse_match(self_t, TokenType type);

// Assert that the next token is of the given type, or panic
//todo very unsafe mechanic, should enter recovery mode or at least skip until it finds that token
TokenIndex parse_assert(self_t, TokenType type);


// Pratt BP
typedef struct binding_power_s {
    uint8_t lhs;
    uint8_t rhs;
} BindingPower;

BindingPower token_bp(Token token, bool is_prefix);


// Statements
AstIndex int_stmt(self_t);
AstIndex stmt_let(self_t);


// Expressions
AstIndex int_expr(self_t);
AstIndex int_expr_bp(self_t);
// Return a literal ast node or `empty_ast_index` if the next token is not a literal.
AstIndex expr_literal(self_t);
AstIndex expr_block(self_t);


#undef self_t

typedef struct parse_frame_s {
    uint8_t min_bp;
    AstIndex lhs;
    TokenIndex op_idx;
} ParseFrame;

typedef struct parse_frame_stack_s {
    uint32_t size;
    uint32_t capacity;
    ParseFrame *data;
} ParseFrameStack;

#define self_t ParseFrameStack *self

void parse_frame_stack_init(self_t);
void parse_frame_stack_free(self_t);
void parse_frame_stack_push(self_t, ParseFrame frame);
ParseFrame parse_frame_stack_pop(self_t);
bool parse_frame_stack_empty(self_t);

#undef self_t

#endif //ACORNC_PARSER_INTERNAL_H
