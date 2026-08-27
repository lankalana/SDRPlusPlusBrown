#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <dsp/stream.h>
#include <mutex>
#include <vector>

#include <utils/opengl_include_code.h>

namespace ImGui {
    class ImageDisplay {
    public:
        ImageDisplay(int width, int height);
        void draw(const ImVec2& size_arg = ImVec2(0, 0));
        void swap();

        void* buffer;

    private:
        void updateTexture();

        std::mutex bufferMtx;
        std::vector<uint8_t> writeBuffer;
        std::vector<uint8_t> activeBuffer;

        int _width;
        int _height;

        GLuint textureId;

        bool newData = false;
    };
}
