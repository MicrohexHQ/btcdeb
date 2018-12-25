#include "catch.hpp"

#include "../compiler/tinyparser.h"

// inline std::vector<tiny::st_c> _list(tiny::st_t* list[]) {
//     std::vector<tiny::st_c> r;
//     for (size_t i = 0; list[i]; ++i) {
//         r.emplace_back(list[i]);
//     }
//     return r;
// }
// #define LIST(vals...) _list((tiny::st_t*[]){vals, nullptr})

inline tiny::bin_t* bin(tiny::st_t* lhs, char op, tiny::st_t* rhs) {
    switch (op) {
    case '+': return new tiny::bin_t(tiny::tok_plus, lhs, rhs);
    case '-': return new tiny::bin_t(tiny::tok_minus, lhs, rhs);
    case '*': return new tiny::bin_t(tiny::tok_mul, lhs, rhs);
    case '/': return new tiny::bin_t(tiny::tok_div, lhs, rhs);
    default: return new tiny::bin_t(tiny::tok_concat, lhs, rhs);
    }
}

#define RVAL(str, r)    new tiny::value_t(tiny::tok_number, str, r)
#define VAL(str)        RVAL(str, tiny::tok_undef)
#define VAR(name)       new tiny::var_t(name)
#define BIN(op,a,b)     new tiny::bin_t(op, a, b)
// #define CALL(fname, args) new tiny::call_t(fname, new tiny::list_t(LIST(args)))
// #define PCALL(r, args)  new tiny::pcall_t(r, new tiny::list_t(LIST(args)))
#define PREG(args, seq) new tiny::func_t(args, seq)
#define SET(varname, val) new tiny::set_t(varname, val)
// #define SEQ(vals...) new tiny::sequence_t(LIST(vals))

#define A VAR("a")
#define B VAR("b")
#define C VAR("c")
#define D VAR("d")

TEST_CASE("Simple Treeify", "[treeify-simple]") {
    SECTION("1 entry") {
        const char* inputs[] = {
            "0",
            "1",
            "arr",
            "\"hello world\"",
            "my_var",
            "0x",
            "0x1234",
            "0b1011",
            "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899",
            "aabbccddeeff00112233445566778899gaabbccddeeff0011223344556677889",
            nullptr,
        };
        tiny::st_t* expected[] = {
            VAL("0"),
            VAL("1"),
            VAR("arr"),
            VAL("hello world"),
            VAR("my_var"),
            RVAL("", tiny::tok_hex),
            RVAL("1234", tiny::tok_hex),
            RVAL("1011", tiny::tok_bin),
            VAR("aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899"),
            VAR("aabbccddeeff00112233445566778899gaabbccddeeff0011223344556677889"),
        };
        for (size_t i = 0; inputs[i]; ++i) {
            GIVEN(inputs[i]) {
                tiny::token_t* t = tiny::tokenize(inputs[i]);
                tiny::st_t* tree = tiny::treeify(t);
                REQUIRE(tree->to_string() == expected[i]->to_string());
                delete t;
                delete tree;
                delete expected[i];
            }
        }
    }

    SECTION("2 tokens") {
        const char* inputs[] = {
            "(0)",
            "(1)",
            "(arr)",
            "(\"hello world\")",
            "(my_var)",
            "(0x)",
            "(0x1234)",
            "(0b1011)",
            "(aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899)",
            "(aabbccddeeff00112233445566778899gaabbccddeeff0011223344556677889)",
            "!1",
            "!0",
            nullptr,
        };
        tiny::st_t* expected[] = {
            VAL("0"),
            VAL("1"),
            VAR("arr"),
            VAL("hello world"),
            VAR("my_var"),
            RVAL("", tiny::tok_hex),
            RVAL("1234", tiny::tok_hex),
            RVAL("1011", tiny::tok_bin),
            VAR("aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899"),
            VAR("aabbccddeeff00112233445566778899gaabbccddeeff0011223344556677889"),
            new tiny::unary_t(tiny::tok_not, VAL("1")),
            new tiny::unary_t(tiny::tok_not, VAL("0")),
        };
        for (size_t i = 0; inputs[i]; ++i) {
            GIVEN(inputs[i]) {
                tiny::token_t* t = tiny::tokenize(inputs[i]);
                tiny::st_t* tree = tiny::treeify(t);
                REQUIRE(tree->to_string() == expected[i]->to_string());
                delete t;
                delete tree;
                delete expected[i];
            }
        }
    }

    SECTION("3 tokens") {
        const char* inputs[] = {
            "1 + 1",
            "1 - 1",
            "a * a",
            "10 / 5",
            "\"hello\" ++ \"world\"",
            "0xab ++ 0xcd",
            "function()",
            "1 && 2",
            nullptr,
        };
        tiny::st_t* expected[] = {
            BIN(tiny::tok_plus, VAL("1"), VAL("1")),
            BIN(tiny::tok_minus, VAL("1"), VAL("1")),
            BIN(tiny::tok_mul, A, A),
            BIN(tiny::tok_div, VAL("10"), VAL("5")),
            BIN(tiny::tok_concat, VAL("hello"), VAL("world")),
            BIN(tiny::tok_concat, RVAL("ab", tiny::tok_hex), RVAL("cd", tiny::tok_hex)),
            new tiny::call_t("function", nullptr),
            BIN(tiny::tok_land, VAL("1"), VAL("2")),
        };
        for (size_t i = 0; inputs[i]; ++i) {
            GIVEN(inputs[i]) {
                tiny::token_t* t = tiny::tokenize(inputs[i]);
                tiny::st_t* tree = tiny::treeify(t);
                REQUIRE(tree->to_string() == expected[i]->to_string());
                delete t;
                delete tree;
                delete expected[i];
            }
        }
    }

    SECTION("4 tokens") {
        const char* inputs[] = {
            "a *= 5",
            // "() {}",
            "a ++= 11",
            "-1-1",
            nullptr,
        };
        tiny::st_t* expected[] = {
            SET("a", BIN(tiny::tok_mul, A, VAL("5"))),
            // PREG(std::vector<std::string>(), SEQ(nullptr)),
            SET("a", BIN(tiny::tok_concat, A, VAL("11"))),
            BIN(tiny::tok_minus, new tiny::unary_t(tiny::tok_minus, VAL("1")), VAL("1")),
        };
        for (size_t i = 0; inputs[i]; ++i) {
            GIVEN(inputs[i]) {
                tiny::token_t* t = tiny::tokenize(inputs[i]);
                tiny::st_t* tree = tiny::treeify(t);
                REQUIRE(tree->to_string() == expected[i]->to_string());
                delete t;
                delete tree;
                delete expected[i];
            }
        }
    }

    SECTION("5 tokens") {
        const char* inputs[] = {
            "2 + 3 * 5",
            "2 * 3 + 5",
            "2 ++ 3 * 5",
            "2 * 3 ++ 5",
            // "() { 10 }",
            "a = a * 5",
            "1 - 1 - 1",
            "(1 && 2)",
            nullptr,
        };
        tiny::st_t* expected[] = {
            BIN(tiny::tok_plus, VAL("2"), BIN(tiny::tok_mul, VAL("3"), VAL("5"))),
            BIN(tiny::tok_plus, BIN(tiny::tok_mul, VAL("2"), VAL("3")), VAL("5")),
            BIN(tiny::tok_concat, VAL("2"), BIN(tiny::tok_mul, VAL("3"), VAL("5"))),
            BIN(tiny::tok_concat, BIN(tiny::tok_mul, VAL("2"), VAL("3")), VAL("5")),
            // PREG(std::vector<std::string>(), SEQ(VAL("10"))),
            SET("a", BIN(tiny::tok_mul, A, VAL("5"))),
            BIN(tiny::tok_minus, BIN(tiny::tok_minus, VAL("1"), VAL("1")), VAL("1")),
            BIN(tiny::tok_land, VAL("1"), VAL("2")),
        };
        for (size_t i = 0; inputs[i]; ++i) {
            GIVEN(inputs[i]) {
                tiny::token_t* t = tiny::tokenize(inputs[i]);
                tiny::st_t* tree = tiny::treeify(t);
                REQUIRE(tree->to_string() == expected[i]->to_string());
                delete t;
                delete tree;
                delete expected[i];
            }
        }
    }

    SECTION("7 token binary arithmetic priorities") {
        const char* inputs[] = {
            "a * b + c * d",
            "a * b - c * d",
            "a * b - c - d",
            "a + b * c + d",
            "a + b - c - d",
            "a - b + c - d",
            "a - b - c + d",
            "a * b / c + d",
            "a + b / c * d",
            "a / b + c * d",
            "a / b * c + d",
            "a * b / c - d",
            "a - b / c * d",
            "a / b - c * d",
            "a / b * c - d",
            nullptr,
        };
        tiny::st_t* expected[] = {
            /* a*b + c*d */ bin(bin(A, '*', B), '+', bin(C, '*', D)),
            /* a*b - c*d */ bin(bin(A, '*', B), '-', bin(C, '*', D)),
            /* (a*b - c) - d */ bin(bin(bin(A, '*', B), '-', C), '-', D),
            /* a + (b*c + d)
            -OR-
               (a + b*c) + d
             */ bin(A, '+', bin(bin(B, '*', C), '+', D)),
            /* (a+b - c) - d
            -OR-
               a + ((b - c) - d)
             */ // case 1: not the case now bin(bin(bin(A, '+', B), '-', C), '-', D),
             bin(A, '+', bin(bin(B, '-', C), '-', D)),
            /* a-b + c-d */ bin(bin(A, '-', B), '+', bin(C, '-', D)),
            /* ((a - b) - c) + d */ bin(bin(bin(A, '-', B), '-', C), '+', D),
            /* (a*b / c) + d
            -OR-
               (a * (b/c)) + d
             */ bin(bin(bin(A, '*', B), '/', C), '+', D),
            /* a + (b/c * d)
            -OR-
               a + (b / (c * d))
             */ bin(A, '+', bin(bin(B, '/', C), '*', D)),
            /* a/b + c*d */ bin(bin(A, '/', B), '+', bin(C, '*', D)),
            /* (a/b * c) + d
            -OR-
               (a / (b*c)) + d
             */ bin(bin(bin(A, '/', B), '*', C), '+', D),
            /* (a*b / c) - d
            -OR-
               (a * (b/c)) - d
             */ bin(bin(bin(A, '*', B), '/', C), '-', D),
            /* a - (b/c * d)
            -OR-
               a - (b / c*d)
             */ bin(A, '-', bin(bin(B, '/', C), '*', D)),
            /* a/b - c*d */ bin(bin(A, '/', B), '-', bin(C, '*', D)),
            /* (a/b * c) - d
            -OR-
               (a / b*c) - d
             */ bin(bin(bin(A, '/', B), '*', C), '-', D),
        };
        for (size_t i = 0; inputs[i]; ++i) {
            GIVEN(inputs[i]) {
                tiny::token_t* t = tiny::tokenize(inputs[i]);
                tiny::st_t* tree = tiny::treeify(t);
                REQUIRE(tree->to_string() == expected[i]->to_string());
                delete t;
                delete tree;
                delete expected[i];
            }
        }
    }
}
