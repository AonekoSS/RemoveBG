
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

#include <filesystem>

#include "RemoveBG.h"
#include "BiRefNet.h"
#include "Image.h"

// エントリポイント
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(pCmdLine);
	RemoveBG app;
	if (!app.Initialize(hInstance)) return FALSE;
	return app.Run();
}

RemoveBG::RemoveBG() : m_hwnd(nullptr), m_hStatusBar(nullptr), m_hComboOutputFormat(nullptr), m_hComboHighRes(nullptr), m_hOriginalBitmap(nullptr), m_hProcessedBitmap(nullptr), m_GDIPlusToken(0) {
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	GdiplusStartup(&m_GDIPlusToken, &gdiplusStartupInput, nullptr);
}

RemoveBG::~RemoveBG() {
	if (m_hOriginalBitmap) DeleteObject(m_hOriginalBitmap);
	if (m_hProcessedBitmap) DeleteObject(m_hProcessedBitmap);
	if (m_GDIPlusToken) Gdiplus::GdiplusShutdown(m_GDIPlusToken);
}

// ウィンドウの初期化
bool RemoveBG::Initialize(HINSTANCE hInstance) {
	// ウィンドウクラスの登録
	WNDCLASSEX wcex{
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WindowProc,
		.hInstance = hInstance,
		.hIcon = LoadIcon(NULL, IDI_APPLICATION),
		.hCursor = LoadCursor(nullptr, IDC_ARROW),
		.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
		.lpszClassName = L"RemoveBGClass",
		.hIconSm = LoadIcon(NULL, IDI_APPLICATION)
	};
	if (!RegisterClassEx(&wcex)) {
		return false;
	}

	const int width = 600;
	const int height = 400;
	const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
	const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
	// メインウィンドウの作成
	m_hwnd = CreateWindowEx(
		0,
		L"RemoveBGClass",
		L"RemoveBG",
		WS_OVERLAPPEDWINDOW,
		x, y, width, height,
		NULL,
		NULL,
		hInstance,
		this
	);

	if (!m_hwnd) {
		return false;
	}

	// コモンコントロール初期化
	INITCOMMONCONTROLSEX icex{ sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
	InitCommonControlsEx(&icex);

	// オプションバー（画面上部）
	{
		const int margin = 8;
		const int labelW1 = 120, comboW1 = 140;
		const int labelW2 = 120, comboW2 = 160;
		const int rowH = 24;
		const int comboTop = (OPTION_BAR_HEIGHT - rowH) / 2;
		const int comboH = 200;

		CreateWindowExW(0, L"STATIC", L"出力形式:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
			margin, comboTop, labelW1, rowH, m_hwnd, (HMENU)IDC_STATIC_OUTPUT, hInstance, NULL);
		m_hComboOutputFormat = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
			margin + labelW1 + 4, comboTop - 2, comboW1, comboH, m_hwnd, (HMENU)IDC_COMBO_OUTPUT, hInstance, NULL);
		SendMessageW(m_hComboOutputFormat, CB_ADDSTRING, 0, (LPARAM)L"オブジェクト");
		SendMessageW(m_hComboOutputFormat, CB_ADDSTRING, 0, (LPARAM)L"マスク画像");
		SendMessage(m_hComboOutputFormat, CB_SETCURSEL, 0, 0);

		const int col2X = margin + labelW1 + 4 + comboW1 + 20;
		CreateWindowExW(0, L"STATIC", L"高解像度対応:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
			col2X, comboTop, labelW2, rowH, m_hwnd, (HMENU)IDC_STATIC_HIGHRES, hInstance, NULL);
		m_hComboHighRes = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
			col2X + labelW2 + 4, comboTop - 2, comboW2, comboH, m_hwnd, (HMENU)IDC_COMBO_HIGHRES, hInstance, NULL);
		SendMessageW(m_hComboHighRes, CB_ADDSTRING, 0, (LPARAM)L"ストレッチ処理");
		SendMessageW(m_hComboHighRes, CB_ADDSTRING, 0, (LPARAM)L"タイリング処理");
		SendMessage(m_hComboHighRes, CB_SETCURSEL, 0, 0);
	}

	// ステータスバー（画面下部）
	m_hStatusBar = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, m_hwnd, (HMENU)IDC_STATUS_BAR, hInstance, NULL);

	// ドラッグ＆ドロップを有効化
	DragAcceptFiles(m_hwnd, TRUE);

	RECT rc;
	GetClientRect(m_hwnd, &rc);
	LayoutControls(rc.right - rc.left, rc.bottom - rc.top);

	ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);
	return true;
}

int RemoveBG::Run() {
	// AIの初期化を別スレッドで実行
	m_bProcessing = true;
	PostMessage(m_hwnd, WM_UPDATED, 0, 0);
	Status(L"Initializing...");
	std::thread t([this]() {
		if (!m_BiRefNet.Initialize()) {
			// 初期化失敗時は処理をロックしたまま
			Status(L"BiRefNet Initialize failed...");
			return;
		}
		std::wstring gpuStatus = m_BiRefNet.IsEnableGPU() ? L"GPU 有効" : L"CPU";
		Status(L"Initialized. (" + gpuStatus + L")");
		m_bProcessing = false;
		PostMessage(m_hwnd, WM_UPDATED, 0, 0);
	});
	t.detach();

	// メインループ
	MSG msg{};
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	m_BiRefNet.Shutdown();
	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK RemoveBG::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	RemoveBG* pThis = NULL;
	if (uMsg == WM_NCCREATE) {
		pThis = static_cast<RemoveBG*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
	} else {
		pThis = reinterpret_cast<RemoveBG*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	}
	if (!pThis) return 0;

	switch (uMsg) {
	case WM_DROPFILES:
		pThis->OnDropFiles(hwnd, wParam);
		break;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT clientRect;
		GetClientRect(hwnd, &clientRect);
		pThis->DrawImage(hdc, clientRect);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_SIZE:
	{
		RECT rc;
		GetClientRect(hwnd, &rc);
		pThis->LayoutControls(rc.right - rc.left, rc.bottom - rc.top);
	}
	return 0;
	case WM_UPDATED:
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void RemoveBG::OnDropFiles(HWND, WPARAM wParam) {
	if (m_bProcessing) return;
	HDROP hDrop = reinterpret_cast<HDROP>(wParam);
	UINT fileCount = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
	std::vector<std::wstring> filePaths;
	filePaths.reserve(fileCount);
	for (UINT i = 0; i < fileCount; ++i) {
		wchar_t szFile[MAX_PATH];
		if (DragQueryFile(hDrop, i, szFile, MAX_PATH)) {
			filePaths.push_back(szFile);
		}
	}
	DragFinish(hDrop);
	if (!filePaths.empty()) {
		ProcessImageFiles(filePaths);
	}
}

void RemoveBG::ProcessImageFiles(const std::vector<std::wstring>& filePaths) {
	m_bProcessing = true;
	std::thread t([this, filePaths]() {
		bool isObjectOutput = (GetOutputFormatOption() == OutputFormatOption::Object);
		bool isTilingHighRes = (GetHighResOption() == HighResOption::Tiling);

		for (const auto& filePath : filePaths) {
			std::wstring fileName = filePath.substr(filePath.find_last_of(L"\\/") + 1);
			std::wstring outputPath = filePath.substr(0, filePath.find_last_of(L".")) + L"_.png";
			std::wstring outputName = outputPath.substr(outputPath.find_last_of(L"\\/") + 1);
			Status(fileName + L" -> " + outputName);

			Gdiplus::Bitmap sourceBitmap(filePath.c_str());
			if (sourceBitmap.GetLastStatus() != Gdiplus::Ok) {
				Status(L"Failed to load " + fileName + L"...");
				Sleep(500);
				continue;
			}
			UpdateDisplay(&sourceBitmap, false);

			Image image = ImageFromBitmap(sourceBitmap);
			Image mask = m_BiRefNet.Process(image, isTilingHighRes);

			Gdiplus::Bitmap* outputBitmap = nullptr;
			if (isObjectOutput) {
				Image output = ConvertToARGB(image, mask);
				outputBitmap = ImageToBitmap(output);
			} else {
				outputBitmap = ImageToBitmap(mask);
			}
			if (outputBitmap) {
				UpdateDisplay(outputBitmap, true);
				BitmapToFile(outputBitmap, outputPath);
				delete outputBitmap;
			}
			Sleep(500);
		}
		m_bProcessing = false;
		Status(L"Completed.");
		});
	t.detach();
}

void RemoveBG::Status(const std::wstring& message) {
	if (m_hStatusBar) {
		SendMessageW(m_hStatusBar, SB_SETTEXTW, 0, (LPARAM)message.c_str());
	}
}

void RemoveBG::UpdateDisplay(Gdiplus::Bitmap* bitmap, bool isProcessed) {
	{
		std::lock_guard<std::mutex> lock(m_UpdateMutex);
		HBITMAP* target = isProcessed ? &m_hProcessedBitmap : &m_hOriginalBitmap;
		if (!isProcessed && m_hProcessedBitmap) {
			DeleteObject(m_hProcessedBitmap);
			m_hProcessedBitmap = NULL;
		}
		if (*target) {
			DeleteObject(*target);
			*target = NULL;
		}
		if (bitmap) {
			bitmap->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), target);
			PostMessage(m_hwnd, WM_UPDATED, 0, 0);
		}
	}
	Sleep(0);
}

void RemoveBG::LayoutControls(int clientW, int clientH) {
	// ステータスバー（下部）のみ可変レイアウト
	if (m_hStatusBar) {
		MoveWindow(m_hStatusBar, 0, clientH - STATUS_BAR_HEIGHT, clientW, STATUS_BAR_HEIGHT, TRUE);
	}
}

void RemoveBG::DrawImage(HDC hdc, RECT& clientRect) {
	int w = clientRect.right - clientRect.left;
	int h = clientRect.bottom - clientRect.top;

	// 描画領域はオプションバーとステータスバーを除いた中央
	RECT contentRect = { 0, OPTION_BAR_HEIGHT, w, h - STATUS_BAR_HEIGHT };
	FillRect(hdc, &contentRect, (HBRUSH)(COLOR_WINDOW + 1));

	std::lock_guard<std::mutex> lock(m_UpdateMutex);

	int halfW = w / 2;
	int gap = 10;
	int areaW = halfW - gap;
	int areaH = (h - OPTION_BAR_HEIGHT - STATUS_BAR_HEIGHT) - 10;
	int areaTop = OPTION_BAR_HEIGHT + 5;

	if (!m_bProcessing && !m_hOriginalBitmap && !m_hProcessedBitmap) {
		SetTextColor(hdc, RGB(128, 128, 128));
		SetBkMode(hdc, TRANSPARENT);
		DrawText(hdc, L"画像ファイルをドロップしてください", -1, &contentRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}

	if (m_hOriginalBitmap || m_hProcessedBitmap) {
		HDC memDC = CreateCompatibleDC(hdc);
		SetStretchBltMode(hdc, HALFTONE);

		// 元画像（左側）
		if (m_hOriginalBitmap) {
			BITMAP bmOrig;
			GetObject(m_hOriginalBitmap, sizeof(BITMAP), &bmOrig);
			double scaleOrig = std::min(static_cast<double>(areaW) / bmOrig.bmWidth, static_cast<double>(areaH) / bmOrig.bmHeight);
			int drawOrigW = static_cast<int>(bmOrig.bmWidth * scaleOrig);
			int drawOrigH = static_cast<int>(bmOrig.bmHeight * scaleOrig);
			HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, m_hOriginalBitmap);
			StretchBlt(hdc, (areaW - drawOrigW) / 2, areaTop + (areaH - drawOrigH) / 2, drawOrigW, drawOrigH, memDC, 0, 0, bmOrig.bmWidth, bmOrig.bmHeight, SRCCOPY);
			SelectObject(memDC, oldBitmap);
		}

		// 処理後画像（右側）
		if (m_hProcessedBitmap) {
			BITMAP bmProc;
			GetObject(m_hProcessedBitmap, sizeof(BITMAP), &bmProc);
			double scaleProc = std::min(static_cast<double>(areaW) / bmProc.bmWidth, static_cast<double>(areaH) / bmProc.bmHeight);
			int drawProcW = static_cast<int>(bmProc.bmWidth * scaleProc);
			int drawProcH = static_cast<int>(bmProc.bmHeight * scaleProc);
			HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, m_hProcessedBitmap);
			StretchBlt(hdc, halfW + gap + (areaW - drawProcW) / 2, areaTop + (areaH - drawProcH) / 2, drawProcW, drawProcH, memDC, 0, 0, bmProc.bmWidth, bmProc.bmHeight, SRCCOPY);
			SelectObject(memDC, oldBitmap);
		}
		DeleteDC(memDC);
	}
}

OutputFormatOption RemoveBG::GetOutputFormatOption() const {
	int sel = (int)SendMessage(m_hComboOutputFormat, CB_GETCURSEL, 0, 0);
	return (sel == 1) ? OutputFormatOption::MaskImage : OutputFormatOption::Object;
}

HighResOption RemoveBG::GetHighResOption() const {
	int sel = (int)SendMessage(m_hComboHighRes, CB_GETCURSEL, 0, 0);
	return (sel == 1) ? HighResOption::Tiling : HighResOption::Stretch;
}
