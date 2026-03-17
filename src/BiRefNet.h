#pragma once
#include <onnxruntime_cxx_api.h>

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <urlmon.h>

#include "Image.h"

// 背景削除
class BiRefNet {
public:
    BiRefNet();
    ~BiRefNet();

	bool Initialize(std::function<void(const std::wstring& status)> callback);
	void Shutdown();

	bool IsEnableGPU()const{ return m_isEnableGPU; }
	Image Process(const Image& image, bool isTilingHighRes);

private:
    // ONNXランタイム関連
    Ort::Env m_ortEnv{nullptr};
    Ort::Session m_ortSession{nullptr};
    Ort::MemoryInfo m_ortMemoryInfo{nullptr};

    // 初期化済みフラグ
    bool m_initialized{false};
	bool m_isEnableGPU{false};

	// コールバック
	std::function<void(const std::wstring& status)> m_callback;

    // モデル入力サイズ
    int m_inputWidth{1024};
    int m_inputHeight{1024};

    // パディング情報（Preprocess→PostprocessResults間で共有）
    int m_resizedWidth{0};
    int m_resizedHeight{0};
    int m_offsetX{0};
    int m_offsetY{0};

    // 正規化パラメータ（birefnet.cppを参考）
    float m_mean[3] = { 123.675f, 116.28f, 103.53f };
    float m_std[3] = { 58.395f, 57.12f, 57.375f };

    // 推論実行
    bool RunInference(const std::vector<float>& modelInputData, const std::vector<int64_t>& inputShapes, std::vector<float>& modelOutputData);

    // 前処理
    std::vector<float> PreProcess(const Image& image, int originalWidth, int originalHeight);

    // 後処理
    Image PostProcess(const std::vector<float>& modelOutputData, int originalWidth, int originalHeight);

    // 処理モード別
    Image ProcessStretch(const Image& image);
    Image ProcessTiling(const Image& image);

	// ダウンロード
	bool DownloadFile(const std::wstring& url, const std::wstring& filePath);
};

