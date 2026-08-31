#include "protocol.h"
#include "mmcss.h"
#include <iostream>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    MMCSSScopedTask mmcss(L"Games");
    std::cout << "Eidolon Client Core initialized.\n";
    return 0;
}