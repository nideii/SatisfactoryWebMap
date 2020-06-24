#pragma once

#include <stb_image.h>
#include <stb_image_resize.h>

#include <cstdint>
#include <string>
#include <stdexcept>

namespace {
inline void bitblt(void *dstp, size_t dst_stride, const void *srcp, size_t src_stride, size_t row_size, size_t height)
{
    if (height) {
        if (src_stride == dst_stride && src_stride == row_size) {
            memcpy(dstp, srcp, row_size * height);
        } else {
            const uint8_t *srcp8 = (const uint8_t *)srcp;
            uint8_t *dstp8 = (uint8_t *)dstp;
            for (size_t i = 0; i < height; i++) {
                memcpy(dstp8, srcp8, row_size);
                srcp8 += src_stride;
                dstp8 += dst_stride;
            }
        }
    }
}
}

struct Image
{
public:
    enum PixelOrder
    {
        BGR,
        RGB,
    };

    template<typename T>
    struct Pixel
    {
        T r, g, b, a;
    };

    static Image open(const std::string &fp)
    {
        FILE *f = fopen(fp.c_str(), "rb");
        if (f == nullptr) {
            return Image();
        }

        fseek(f, 0, SEEK_END);
        auto len = ftell(f);
        fseek(f, 0, SEEK_SET);

        std::vector<uint8_t> buf(len);
        fread(&buf[0], 1, len, f);
        fclose(f);

        return Image::from_bytes(&buf[0], len);
    }

    static Image from_bytes(const uint8_t *image_data, size_t len)
    {
        if (len == 0) {
            return Image();
        }

        int w, h, ch;
        uint8_t *data = stbi_load_from_memory(image_data, (int)len, &w, &h, &ch, 4);
        if (data == nullptr) {
            return Image();
        }

        // If it dose not have alpha channel, then it should be RGB; otherwise assume it is BGR

        return Image(data, w, h, 4, ch == 3 ? RGB : BGR);

    }

    static Image empty(int width, int height, int ch, Pixel<uint8_t> fill = {0, 0, 0, 0})
    {
        auto data = (uint8_t *)STBI_MALLOC(width * height * ch);
        if (fill.r == -1) {
            return Image(data, width, height, ch, RGB);
        }

        if (ch == 1 || fill.r == fill.g == fill.b) {
            if (ch == 1 || ch == 3 || fill.r == fill.a) {
                memset(data, fill.r, width * height * ch);
                return Image(data, width, height, ch, RGB);
            }
        }

        auto p = 0;
        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; ++w) {
                if (ch == 4) {
                    data[p++] = fill.r;
                    data[p++] = fill.g;
                    data[p++] = fill.b;
                    data[p++] = fill.a;
                } else if (ch == 3) {
                    data[p++] = fill.r;
                    data[p++] = fill.g;
                    data[p++] = fill.b;
                } else if (ch == 2) {
                    data[p++] = fill.r;
                    data[p++] = fill.g;
                }
            }
        }

        return Image(data, width, height, ch, RGB);
    }

    Image() :
        data(nullptr), width(0), height(0), ch(0), order(BGR)
    {}

    Image(uint8_t *_data, int _w, int _h, int _ch, PixelOrder _order) :
        data(_data), width(_w), height(_h), ch(_ch), order(_order)
    {}

    // don't assign copy image
    Image(const Image &other) = delete;
    Image &operator=(const Image &other) = delete;

    // use this
    Image copy()
    {
        uint8_t *clone = (uint8_t *)STBI_MALLOC(width * height * ch);
        if (clone == nullptr) {
            return Image();
        }

        memcpy(clone, data, width * height * ch);

        return Image(clone, width, height, ch, order);
    }

    Image(Image &&other) noexcept
    {
        if (other.data == nullptr) {
            return;
        }

        order = other.order;

        data = other.data;
        width = other.width;
        height = other.height;
        ch = other.ch;

        other.data = nullptr;
        return;
    }

    Image &operator=(Image &&other) noexcept
    {
        if (other.data == nullptr) {
            return *this;
        }

        order = other.order;

        data = other.data;
        width = other.width;
        height = other.height;
        ch = other.ch;

        other.data = nullptr;
        return *this;
    }

    uint8_t &operator[](size_t index)
    {
        assert(index < height * width * ch * sizeof(uint8_t));
        return data[index];
    }

    operator const uint8_t *() const
    {
        return data;
    }

    ~Image()
    {}

    bool opened() const
    {
        return data != nullptr;
    }

    operator bool() const
    {
        return opened();
    }

    Image resize(int w, int h) const
    {
        if (data == nullptr || w <= 0 || h <= 0) {
            return Image();
        }

        uint8_t *resized = (uint8_t *)STBI_MALLOC(w * h * ch);
        if (resized == nullptr) {
            return Image();
        }

        auto ret = stbir_resize_uint8(data, width, height, width * ch,
                                      resized, w, h, w * ch, ch);
        if (ret == 0) {
            STBI_FREE(resized);
            return Image();
        }

        return Image(resized, w, h, ch, order);
    }

    Image crop(int left, int top, int right, int bottom) const
    {
        if (data == nullptr) {
            return Image();
        }

        int w = width - left - right;
        int h = height - top - bottom;

        if (w <= 0 || h <= 0) {
            return Image();
        }

        uint8_t *resized = (uint8_t *)STBI_MALLOC(w * h * ch);
        if (resized == nullptr) {
            return Image();
        }

        bitblt(resized, w * ch,
               data + (width * top + left) * ch, height * ch,
               w * ch, h);

        return Image(resized, w, h, ch, order);
    }

    PixelOrder order;
    uint8_t *data;
    int width, height, ch;

private:
#ifdef USE_OPENCV
    cv::Mat image;
#endif
};
