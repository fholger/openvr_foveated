#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <unordered_map>
#include "openvr.h"

namespace vr {
	using Microsoft::WRL::ComPtr;

	class PostProcessor {
	public:
		void Apply(EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags);
		void ApplyFixedFoveatedRendering(ID3D11DepthStencilView *depthStencilView, float depth, uint8_t stencil);
		void OnRenderTargetChange(UINT numViews, ID3D11RenderTargetView * const *renderTargetViews);
		void Reset();

	private:
		bool enabled = true;
		bool initialized = false;
		bool useVariableRateShading = false;
		uint32_t textureWidth = 0;
		uint32_t textureHeight = 0;
		bool textureContainsOnlyOneEye = true;
		bool requiresCopy = false;
		bool inputIsSrgb = false;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11SamplerState> sampler;
		float projX[2];
		float projY[2];

		struct EyeViews {
			ComPtr<ID3D11ShaderResourceView> view[2];
		};
		std::unordered_map<ID3D11Texture2D*, EyeViews> inputTextureViews;
		// in case the incoming texture can't be bound as an SRV, we'll need to prepare a copy
		ComPtr<ID3D11Texture2D> copiedTexture;
		ComPtr<ID3D11ShaderResourceView> copiedTextureView;

		void PrepareCopyResources(DXGI_FORMAT format);
		ID3D11ShaderResourceView *GetInputView(ID3D11Texture2D *inputTexture, int eye);

		// resources for radial density masking
		ComPtr<ID3D11VertexShader> rdmFullTriVertexShader;
		ComPtr<ID3D11PixelShader> rdmMaskingShader;
		ComPtr<ID3D11Buffer> rdmMaskingConstantsBuffer[2];
		ComPtr<ID3D11ComputeShader> rdmReconstructShader;
		ComPtr<ID3D11Buffer> rdmReconstructConstantsBuffer[2];
		ComPtr<ID3D11Texture2D> rdmReconstructedTexture;
		ComPtr<ID3D11ShaderResourceView> rdmReconstructedView;
		ComPtr<ID3D11UnorderedAccessView> rdmReconstructedUav;
		ComPtr<ID3D11DepthStencilState> rdmDepthStencilState;
		ComPtr<ID3D11RasterizerState> rdmRasterizerState;
		int depthClearCount = 0;
		struct DepthStencilViews {
			ComPtr<ID3D11DepthStencilView> view[2];
		};
		std::unordered_map<ID3D11Texture2D*, DepthStencilViews> depthStencilViews;

		void CalculateSavedPixelCount();
		void PrepareRdmResources(DXGI_FORMAT format);
		ID3D11DepthStencilView *GetDepthStencilView( ID3D11Texture2D *depthStencilTex, EVREye eye );
		void ApplyRadialDensityMask(ID3D11Texture2D *depthStencilTex, float depth, uint8_t stencil);
		void ReconstructRdmRender(vr::EVREye eye, ID3D11ShaderResourceView *inputView, int x, int y, int width, int height);

		// NIS specific lookup textures
		ComPtr<ID3D11Texture2D> scalerCoeffTexture;
		ComPtr<ID3D11Texture2D> usmCoeffTexture;
		ComPtr<ID3D11ShaderResourceView> scalerCoeffView;
		ComPtr<ID3D11ShaderResourceView> usmCoeffView;

		// sharpening resources
		ComPtr<ID3D11ComputeShader> sharpenShader;
		ComPtr<ID3D11Buffer> sharpenConstantsBuffer[2];
		ComPtr<ID3D11Texture2D> sharpenedTexture;
		ComPtr<ID3D11UnorderedAccessView> sharpenedTextureUav;

		void PrepareSharpeningResources(DXGI_FORMAT format);
		void ApplySharpening(EVREye eEye, ID3D11ShaderResourceView *inputView, int x, int y, int width, int height);

		ID3D11Texture2D *lastSubmittedTexture = nullptr;
		ID3D11Texture2D *outputTexture = nullptr;
		int eyeCount = 0;

		void PrepareResources(ID3D11Texture2D *inputTexture, EColorSpace colorSpace);
		void ApplyPostProcess(EVREye eEye, ID3D11Texture2D *inputTexture, const VRTextureBounds_t *bounds);

		void CheckHotkeys();
		bool IsHotkeyActive(int keyCode);

		std::unordered_map<int, bool> wasKeyPressedBefore;
		bool takeCapture = false;

		struct ProfileQuery {
			ComPtr<ID3D11Query> queryDisjoint;
			ComPtr<ID3D11Query> queryStart;
			ComPtr<ID3D11Query> queryEnd;
		};

		static const int QUERY_COUNT = 6;
		ProfileQuery profileQueries[QUERY_COUNT];
		int currentQuery = 0;
		float summedGpuTime = 0.0f;
		int countedQueries = 0;
	};
}
