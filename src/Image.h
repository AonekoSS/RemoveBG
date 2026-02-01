#pragma once
#include <gdiplus.h>
#include <memory>
#include <cstdint>

class Image {
	std::shared_ptr<void> data_;
public:
	const uint32_t width;
	const uint32_t height;
	const uint32_t channel;
	uint8_t* data() const { return static_cast<uint8_t*>(data_.get()); }
	Image() noexcept : width{ 0 }, height{ 0 }, channel{ 0 } {}
	Image(uint32_t w, uint32_t h, uint32_t c) : width{ w }, height{ h }, channel{ c }, data_{ malloc(w * h * c), free } {}
	Image(int w, int h, int c) : Image{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), static_cast<uint32_t>(c) } {}
};

Image ImageFromBitmap(Gdiplus::Bitmap& bitmap);
Gdiplus::Bitmap* ImageToBitmap(Image& image);
bool BitmapToFile(Gdiplus::Bitmap* bitmap, const std::wstring& outputPath);

Image ConvertToRGB(const Image& image);
Image ConvertToARGB(const Image& image);
Image ConvertToARGB(const Image& image, const Image& mask);

/** stb_image_resize2 で Image をリサイズ（1ch / 3ch / 4ch 対応） */
Image ResizeImage(const Image& image, int newWidth, int newHeight);

/** 矩形領域を切り出し */
Image CropImage(const Image& image, int x, int y, int w, int h);

