#include <gui/widgets/image.h>
#include <algorithm>

namespace ImGui {
    ImageDisplay::ImageDisplay(int width, int height)
        : writeBuffer(width * height * 4, 0), activeBuffer(width * height * 4, 0) {
        _width = width;
        _height = height;
        buffer = writeBuffer.data();

        glGenTextures(1, &textureId);
    }

    void ImageDisplay::draw(const ImVec2& size_arg) {
        std::lock_guard<std::mutex> lck(bufferMtx);

        ImGuiWindow* window = GetCurrentWindow();
        ImGuiStyle& style = GetStyle();
        ImVec2 min = window->DC.CursorPos;

        // Calculate scale
        float width = CalcItemWidth();
        float height = roundf((width / (float)_width) * (float)_height);

        ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), height);
        ImRect bb(min, ImVec2(min.x + size.x, min.y + size.y));

        ItemSize(size, style.FramePadding.y);
        if (!ItemAdd(bb, 0)) {
            return;
        }

        if (newData) {
            newData = false;
            updateTexture();
        }

        window->DrawList->AddImage((void*)(intptr_t)textureId, min, ImVec2(min.x + width, min.y + height));
    }

    void ImageDisplay::swap() {
        std::lock_guard<std::mutex> lck(bufferMtx);
        writeBuffer.swap(activeBuffer);
        buffer = writeBuffer.data();
        newData = true;
        std::ranges::fill(writeBuffer, uint8_t{});
    }

    void ImageDisplay::updateTexture() {
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE, activeBuffer.data());
    }

}
