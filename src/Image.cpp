#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#pragma comment(lib, "gdiplus.lib")

#include "Image.h"
#include <stb_image_resize2.h>

Image ImageFromBitmap(Gdiplus::Bitmap& bitmap) {
	UINT width = bitmap.GetWidth();
	UINT height = bitmap.GetHeight();

	Image image(static_cast<int>(width), static_cast<int>(height), 3);
	uint8_t* imageData = image.data();

	Gdiplus::BitmapData bitmapData;
	Gdiplus::Rect rect(0, 0, width, height);
	if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &bitmapData) == Gdiplus::Ok) {
		BYTE* src = static_cast<BYTE*>(bitmapData.Scan0);
		int stride = bitmapData.Stride;
		for (UINT y = 0; y < height; y++) {
			for (UINT x = 0; x < width; x++) {
				BYTE* srcPixel = src + y * stride + x * 3;
				int idx = (y * width + x) * 3;
				imageData[idx + 0] = srcPixel[2]; // R
				imageData[idx + 1] = srcPixel[1]; // G
				imageData[idx + 2] = srcPixel[0]; // B
			}
		}
		bitmap.UnlockBits(&bitmapData);
	}
	return image;
}

Gdiplus::Bitmap* ImageToBitmap(Image& image) {
	Gdiplus::BitmapData bitmapData;
	Gdiplus::Rect rect(0, 0, image.width, image.height);
	uint8_t* imageData = image.data();
	switch (image.channel) {
	case 1:
		if (Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(image.width, image.height, PixelFormat24bppRGB)) {
			if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &bitmapData) == Gdiplus::Ok) {
				BYTE* dst = static_cast<BYTE*>(bitmapData.Scan0);
				int stride = bitmapData.Stride;
				for (UINT y = 0; y < image.height; y++) {
					for (UINT x = 0; x < image.width; x++) {
						uint8_t v = imageData[y * image.width + x];
						BYTE* dstPixel = dst + y * stride + x * 3;
						dstPixel[0] = dstPixel[1] = dstPixel[2] = v; // B = G = R
					}
				}
				bitmap->UnlockBits(&bitmapData);
				return bitmap;
			}
		}
	case 3:
		if (Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(image.width, image.height, PixelFormat24bppRGB)) {
			if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &bitmapData) == Gdiplus::Ok) {
				BYTE* dst = static_cast<BYTE*>(bitmapData.Scan0);
				int stride = bitmapData.Stride;
				for (UINT y = 0; y < image.height; y++) {
					for (UINT x = 0; x < image.width; x++) {
						int idx = (y * image.width + x) * 3;
						BYTE* dstPixel = dst + y * stride + x * 3;
						dstPixel[0] = imageData[idx + 2]; // B
						dstPixel[1] = imageData[idx + 1]; // G
						dstPixel[2] = imageData[idx + 0]; // R
					}
				}
				bitmap->UnlockBits(&bitmapData);
				return bitmap;
			}
		}
		break;
	case 4:
		if (Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(image.width, image.height, PixelFormat32bppARGB)) {
			if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bitmapData) == Gdiplus::Ok) {
				BYTE* dst = static_cast<BYTE*>(bitmapData.Scan0);
				int stride = bitmapData.Stride;
				for (UINT y = 0; y < image.height; y++) {
					for (UINT x = 0; x < image.width; x++) {
						int idx = (y * image.width + x) * 4;
						BYTE* dstPixel = dst + y * stride + x * 4;
						dstPixel[0] = imageData[idx + 2]; // B
						dstPixel[1] = imageData[idx + 1]; // G
						dstPixel[2] = imageData[idx + 0]; // R
						dstPixel[3] = imageData[idx + 3]; // A
					}
				}
				bitmap->UnlockBits(&bitmapData);
				return bitmap;
			}
		}
		break;
	}
	return nullptr;
}


// GDI+エンコーダーCLSIDを取得するヘルパー関数
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
	UINT num = 0;
	UINT size = 0;
	Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;
	Gdiplus::GetImageEncodersSize(&num, &size);
	if (size == 0) return -1;
	pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if (pImageCodecInfo == NULL) return -1;
	Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
	for (UINT j = 0; j < num; ++j) {
		if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return j;
		}
	}
	free(pImageCodecInfo);
	return -1;
}

bool BitmapToFile(Gdiplus::Bitmap* bitmap, const std::wstring& outputPath) {
	CLSID clsid;
	if (GetEncoderClsid(L"image/png", &clsid) < 0) {
		OutputDebugStringW(L"PNGエンコーダーの取得に失敗");
		return false;
	}
	if (bitmap->Save(outputPath.c_str(), &clsid, NULL) != Gdiplus::Ok) {
		OutputDebugStringW(L"画像ファイルの保存に失敗");
		return false;
	}
	return true;
}

Image ConvertToRGB(const Image& image) {
	const uint8_t* src = image.data();
	const size_t w = image.width, h = image.height;
	Image out(static_cast<int>(w), static_cast<int>(h), 3);
	uint8_t* dst = out.data();

	if (image.channel == 1) {
		for (size_t i = 0; i < w * h; i++) {
			uint8_t v = src[i];
			dst[i * 3 + 0] = v;
			dst[i * 3 + 1] = v;
			dst[i * 3 + 2] = v;
		}
	} else if (image.channel == 3) {
		memcpy(dst, src, w * h * 3);
	} else if (image.channel == 4) {
		for (size_t i = 0; i < w * h; i++) {
			dst[i * 3 + 0] = src[i * 4 + 0];
			dst[i * 3 + 1] = src[i * 4 + 1];
			dst[i * 3 + 2] = src[i * 4 + 2];
		}
	} else {
		throw std::runtime_error("ConvertToRGB: unsupported channel count");
	}
	return out;
}

Image ConvertToARGB(const Image& image) {
	if (image.channel != 3)
		throw std::runtime_error("ConvertToARGB(image): input must be 3-channel");
	const uint8_t* src = image.data();
	const size_t w = image.width, h = image.height;
	Image out(static_cast<int>(w), static_cast<int>(h), 4);
	uint8_t* dst = out.data();
	for (size_t i = 0; i < w * h; i++) {
		dst[i * 4 + 0] = src[i * 3 + 0];
		dst[i * 4 + 1] = src[i * 3 + 1];
		dst[i * 4 + 2] = src[i * 3 + 2];
		dst[i * 4 + 3] = 255;
	}
	return out;
}

Image CropImage(const Image& image, int x, int y, int w, int h) {
	const int srcW = static_cast<int>(image.width);
	const int srcH = static_cast<int>(image.height);
	const int c = static_cast<int>(image.channel);
	x = std::max(0, std::min(x, srcW - 1));
	y = std::max(0, std::min(y, srcH - 1));
	w = std::min(w, srcW - x);
	h = std::min(h, srcH - y);
	if (w <= 0 || h <= 0) return Image(0, 0, c);
	Image out(w, h, c);
	const uint8_t* src = image.data();
	uint8_t* dst = out.data();
	for (int dy = 0; dy < h; dy++) {
		const uint8_t* row = src + ((y + dy) * srcW + x) * c;
		memcpy(dst + dy * w * c, row, w * c);
	}
	return out;
}

Image ConvertToARGB(const Image& image, const Image& mask) {
	if (image.channel != 3 || mask.channel != 1)
		throw std::runtime_error("ConvertToARGB(image,mask): image must be 3ch, mask 1ch");
	if (image.width != mask.width || image.height != mask.height)
		throw std::runtime_error("ConvertToARGB(image,mask): size mismatch");
	const uint8_t* rgb = image.data();
	const uint8_t* alpha = mask.data();
	const size_t w = image.width, h = image.height;
	Image out(static_cast<int>(w), static_cast<int>(h), 4);
	uint8_t* dst = out.data();
	for (size_t i = 0; i < w * h; i++) {
		dst[i * 4 + 0] = rgb[i * 3 + 0];
		dst[i * 4 + 1] = rgb[i * 3 + 1];
		dst[i * 4 + 2] = rgb[i * 3 + 2];
		dst[i * 4 + 3] = alpha[i];
	}
	return out;
}
