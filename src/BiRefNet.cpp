#define NOMINMAX
#include <windows.h>

#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "BiRefNet.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>


static std::filesystem::path get_execution_path() {
	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(NULL, exePath, MAX_PATH);
	std::wstring exePathStr(exePath);
	return std::filesystem::path(exePathStr).parent_path();
}

BiRefNet::BiRefNet(){}

BiRefNet::~BiRefNet() {
	Shutdown();
}
void BiRefNet::Shutdown() {
	m_ortSession.release();
	m_ortMemoryInfo.release();
	m_ortEnv.release();
}

bool BiRefNet::Initialize() {
	if (m_initialized) return true;
	try {
		// モデルファイルのパス
		std::wstring modelFilePath = (get_execution_path() / L"model.onnx").wstring();

		// ONNXランタイムの初期化
		m_ortEnv = Ort::Env();

		// GPUセッションの作成
		try {
			Ort::SessionOptions sessionOptions;
			sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
			sessionOptions.AddConfigEntry("memory.enable_memory_arena_shrinkage", "1");
			Ort::CUDAProviderOptions cudaOptions;
			cudaOptions.Update({
				{"arena_extend_strategy", "kSameAsRequested"},
			});
			sessionOptions.AppendExecutionProvider_CUDA_V2(*cudaOptions);
			m_ortSession = Ort::Session(m_ortEnv, modelFilePath.c_str(), sessionOptions);
			m_isEnableGPU = true;
		}
		catch (const std::exception& e) {
			OutputDebugStringA(e.what());
		}

		// CPUセッションの作成
		if (m_ortSession == nullptr) {
			Ort::SessionOptions sessionOptions;
			sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
			m_ortSession = Ort::Session(m_ortEnv, modelFilePath.c_str(), sessionOptions);
		}

		// メモリ情報の取得
		m_ortMemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

		// モデルの入力サイズを取得
		Ort::AllocatorWithDefaultOptions allocator;
		auto inputTypeInfo = m_ortSession.GetInputTypeInfo(0);
		auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
		auto inputShape = inputTensorInfo.GetShape();

		// 入力形状が動的な場合はデフォルト値を使用
		if (inputShape.size() >= 4) {
			m_inputHeight = inputShape[2] > 0 ? static_cast<int>(inputShape[2]) : 1024;
			m_inputWidth = inputShape[3] > 0 ? static_cast<int>(inputShape[3]) : 1024;
		}

		m_initialized = true;
	}
	catch (const Ort::Exception& e) {
		OutputDebugStringA(e.what());
		return false;
	}
	return true;
}

Image BiRefNet::Process(const Image& image, bool isTilingHighRes) {
	if (!m_initialized) return Image();
	if (image.channel != 3) return Image();
	return isTilingHighRes ? ProcessTiling(image) : ProcessStretch(image);
}

Image BiRefNet::ProcessStretch(const Image& image) {
	int originalWidth = static_cast<int>(image.width);
	int originalHeight = static_cast<int>(image.height);
	std::vector<float> modelInputData = PreProcess(image, originalWidth, originalHeight);
	std::vector<int64_t> inputShapes = { 1, 3, m_inputHeight, m_inputWidth };
	std::vector<float> modelOutputData;
	if (!RunInference(modelInputData, inputShapes, modelOutputData)) return Image();
	return PostProcess(modelOutputData, originalWidth, originalHeight);
}

Image BiRefNet::ProcessTiling(const Image& image) {
	const int originalWidth = static_cast<int>(image.width);
	const int originalHeight = static_cast<int>(image.height);
	Image mask(originalWidth, originalHeight, 1);
	uint8_t* maskData = mask.data();
	std::fill_n(maskData, originalWidth * originalHeight, 0);

	for (int ty = 0; ty < originalHeight; ty += m_inputHeight) {
		for (int tx = 0; tx < originalWidth; tx += m_inputWidth) {
			const int tileW = std::min(m_inputWidth, originalWidth - tx);
			const int tileH = std::min(m_inputHeight, originalHeight - ty);
			if (tileW <= 0 || tileH <= 0) continue;

			Image tile = CropImage(image, tx, ty, tileW, tileH);
			std::vector<float> modelInputData = PreProcess(tile, tileW, tileH);
			std::vector<int64_t> inputShapes = { 1, 3, m_inputHeight, m_inputWidth };
			std::vector<float> modelOutputData;
			if (!RunInference(modelInputData, inputShapes, modelOutputData)) return Image();

			Image tileMask = PostProcess(modelOutputData, tileW, tileH);
			const uint8_t* tileMaskData = tileMask.data();
			for (int dy = 0; dy < tileH; dy++) {
				for (int dx = 0; dx < tileW; dx++) {
					maskData[(ty + dy) * originalWidth + (tx + dx)] = tileMaskData[dy * tileW + dx];
				}
			}
		}
	}
	return mask;
}


bool BiRefNet::RunInference(const std::vector<float>& modelInputData,
	const std::vector<int64_t>& inputShapes,
	std::vector<float>& modelOutputData){
	// 出力形状を取得
	auto outputTypeInfo = m_ortSession.GetOutputTypeInfo(0);
	auto outputTensorInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
	auto outputShapes = outputTensorInfo.GetShape();

	// 出力サイズを計算
	size_t outputSize = 1;
	for (size_t i = 0; i < outputShapes.size(); i++) {
		if (outputShapes[i] > 0) {
			outputSize *= static_cast<size_t>(outputShapes[i]);
		}
	}
	modelOutputData.resize(outputSize);

	try {
		// 入出力用のテンソルを作成
		Ort::Value inputTensor = Ort::Value::CreateTensor<float>(m_ortMemoryInfo,
			const_cast<float*>(modelInputData.data()), modelInputData.size(),
			inputShapes.data(), inputShapes.size());

		std::vector<int64_t> outputTensorShapes(outputShapes.begin(), outputShapes.end());
		Ort::Value outputTensor = Ort::Value::CreateTensor<float>(m_ortMemoryInfo,
			modelOutputData.data(), modelOutputData.size(),
			outputTensorShapes.data(), outputTensorShapes.size());

		// 推論実行
		Ort::AllocatorWithDefaultOptions allocator;
		std::string inputName = m_ortSession.GetInputNameAllocated(0, allocator).get();
		std::string outputName = m_ortSession.GetOutputNameAllocated(0, allocator).get();

		const char* inputNames[] = { inputName.c_str() };
		const char* outputNames[] = { outputName.c_str() };

		// 推論実行（この処理が最も時間がかかる）
		m_ortSession.Run(Ort::RunOptions{ nullptr }, inputNames, &inputTensor, 1, outputNames, &outputTensor, 1);
	}
	catch (const Ort::Exception& e) {
		OutputDebugStringA(e.what());
		return false;
	}
	return true;
}

std::vector<float> BiRefNet::PreProcess(const Image& image, int originalWidth, int originalHeight){
	// リサイズ計算（アスペクト比を保持）
	if (originalWidth > originalHeight) {
		m_resizedWidth = m_inputWidth;
		m_resizedHeight = m_inputWidth * originalHeight / originalWidth;
	} else {
		m_resizedWidth = m_inputHeight * originalWidth / originalHeight;
		m_resizedHeight = m_inputHeight;
	}

	// stbでリサイズ
	std::vector<uint8_t> resizedData(3 * m_resizedWidth * m_resizedHeight);
	stbir_resize_uint8_linear(
		image.data(), originalWidth, originalHeight, originalWidth * 3,
		resizedData.data(), m_resizedWidth, m_resizedHeight, m_resizedWidth * 3,
		STBIR_RGB);

	// パディング（中央配置）
	std::vector<float> inputTensor(3 * m_inputHeight * m_inputWidth, 0.0f);
	m_offsetY = (m_inputHeight - m_resizedHeight) / 2;
	m_offsetX = (m_inputWidth - m_resizedWidth) / 2;

	for (int c = 0; c < 3; c++) {
		for (int y = 0; y < m_resizedHeight; y++) {
			for (int x = 0; x < m_resizedWidth; x++) {
				int inputIdx = c * m_inputHeight * m_inputWidth + (m_offsetY + y) * m_inputWidth + (m_offsetX + x);
				uint8_t pixelValue = resizedData[(y * m_resizedWidth + x) * 3 + c];
				inputTensor[inputIdx] = ((float)pixelValue - m_mean[c]) / m_std[c];
			}
		}
	}

	return inputTensor;
}

Image BiRefNet::PostProcess(const std::vector<float>& modelOutputData, int originalWidth, int originalHeight){
	// 出力形状を取得
	auto outputTypeInfo = m_ortSession.GetOutputTypeInfo(0);
	auto outputTensorInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
	auto outputShapes = outputTensorInfo.GetShape();

	// 出力形状から高さと幅を取得（[1,1,H,W] または [1,H,W] などの形式に対応）
	int outputHeight, outputWidth;
	if (outputShapes.size() == 4) {
		// [batch, channel, height, width] 形式
		outputHeight = static_cast<int>(outputShapes[2]);
		outputWidth = static_cast<int>(outputShapes[3]);
	} else if (outputShapes.size() == 3) {
		// [batch, height, width] 形式
		outputHeight = static_cast<int>(outputShapes[1]);
		outputWidth = static_cast<int>(outputShapes[2]);
	} else if (outputShapes.size() == 2) {
		// [height, width] 形式
		outputHeight = static_cast<int>(outputShapes[0]);
		outputWidth = static_cast<int>(outputShapes[1]);
	} else {
		outputHeight = m_inputHeight;
		outputWidth = m_inputWidth;
	}

	// マスクデータのオフセットを計算（バッチやチャンネル次元をスキップ）
	size_t maskOffset = 0;
	if (outputShapes.size() == 4 && outputShapes[1] > 1) {
		// 複数チャンネルの場合、最初のチャンネルを使用
		maskOffset = outputHeight * outputWidth;
	}

	// パディングを除いた有効領域を切り出し
	std::vector<float> croppedMask(m_resizedWidth * m_resizedHeight);
	for (int y = 0; y < m_resizedHeight; y++) {
		for (int x = 0; x < m_resizedWidth; x++) {
			int srcIdx = maskOffset + (m_offsetY + y) * outputWidth + (m_offsetX + x);
			croppedMask[y * m_resizedWidth + x] = modelOutputData[srcIdx];
		}
	}

	// stbで有効領域を元サイズにリサイズ
	std::vector<float> resizedMask(originalWidth * originalHeight);
	stbir_resize_float_linear(
		croppedMask.data(), m_resizedWidth, m_resizedHeight, m_resizedWidth * sizeof(float),
		resizedMask.data(), originalWidth, originalHeight, originalWidth * sizeof(float),
		STBIR_1CHANNEL);

	// マスクを作成
	Image mask(originalWidth, originalHeight, 1);
	uint8_t* maskData = mask.data();
	for (int y = 0; y < originalHeight; y++) {
		for (int x = 0; x < originalWidth; x++) {
			float maskValue = resizedMask[y * originalWidth + x];
			if (maskValue < 0.0f || maskValue > 1.0f) {
				maskValue = 1.0f / (1.0f + std::exp(-maskValue));
			}
			uint8_t alpha = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(maskValue * 255.0f))));
			maskData[y * originalWidth + x] = alpha;
		}
	}
	return mask;
}
