#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

#include "BiRefNet.h"

// コントロールID
#define IDC_STATUS_BAR        1000
#define IDC_STATIC_OUTPUT     1001
#define IDC_COMBO_OUTPUT      1002
#define IDC_STATIC_HIGHRES    1003
#define IDC_COMBO_HIGHRES     1004

// オプション値
enum class OutputFormatOption { Object, MaskImage };
enum class HighResOption { Stretch, Tiling };

class RemoveBG {
public:
	RemoveBG();
	~RemoveBG();
	bool Initialize(HINSTANCE hInstance);
	int Run();
private:
	HWND m_hwnd;
	HWND m_hStatusBar;
	HWND m_hComboOutputFormat;
	HWND m_hComboHighRes;
	ULONG_PTR m_GDIPlusToken;
	std::mutex m_UpdateMutex;
	HBITMAP m_hOriginalBitmap;
	HBITMAP m_hProcessedBitmap;
	std::atomic<bool> m_bProcessing{ false };

	BiRefNet m_BiRefNet;

	static const int STATUS_BAR_HEIGHT = 22;
	static const int OPTION_BAR_HEIGHT = 36;

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static const UINT WM_UPDATED = WM_USER + 1;

	void OnDropFiles(HWND hwnd, WPARAM wParam);
	void ProcessImageFiles(const std::vector<std::wstring>& filePaths);
	void ProcessSingleImage(const std::wstring& filePath);
	void Status(const std::wstring& message);
	void UpdateDisplay(Gdiplus::Bitmap* bitmap, bool isProcessed);
	void DrawImage(HDC hdc, RECT& clientRect);
	void LayoutControls(int clientW, int clientH);
	OutputFormatOption GetOutputFormatOption() const;
	HighResOption GetHighResOption() const;
};
