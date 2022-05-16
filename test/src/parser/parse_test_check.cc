#include "parse_test_check.h"

extern "C" {
#include "debug/ast_debug.h"
}

testing::AssertionResult parse_check_generic(ParseFn parse, const char *expr, const char *expected) {
    Parser parser;
    parser_init(&parser, (uint8_t *) expr);

    AstIndex root = parse(&parser);
    Ast ast = (Ast) {
        .source = parser.source,
        .tokens = parser.tokens,
        .nodes = parser.nodes,
        .extra_data = parser.extra_data,
        .root = root,
    };
    char *actual = ast_debug_print(&ast);
    size_t actual_len = strlen(actual);
    // If there are two newlines at the end, remove one of them.
    // Module does this at the moment, should fix it inside module then this is not necessary.
    if (actual[actual_len - 1] == '\n' && actual[actual_len - 2] == '\n') {
        actual[actual_len - 1] = '\0';
        actual_len -= 1;
    }

    bool result = actual_len == strlen(expected) && strcmp(actual, expected) == 0;
    if (!result) {
        printf("Expected:\n%s\n", expected);
        printf("Actual:\n%s\n", actual);
        return testing::AssertionFailure() << "Expected: " << expected << "\nActual: " << actual;
    }

    free(actual);


    return testing::AssertionSuccess();
}
