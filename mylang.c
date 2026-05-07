#include "src/common/util.c"
#include "src/frontend/token.c"
#include "src/frontend/lexer.c"
#include "src/frontend/ast.c"
#include "src/frontend/parser.c"
#include "src/middle/symbols.c"
#include "src/backend/codegen.c"
#include "src/compiler.c"

int main(int argc, char** argv) {
    return mylang_main(argc, argv);
}

