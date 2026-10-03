#pragma once
#include <string>

namespace sourcemenu {
    void init();
    void selectSource(std::string name);
    void draw(void* ctx);
}
