#pragma once
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>
#include "nvapi/nvapi.h"
#include "openvr.h"

namespace vr {
	using Microsoft::WRL::ComPtr;

	class VariableRateShading {
	public:
		~VariableRateShading() { Reset(); }

		static VariableRateShading& Instance();

		void Init(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context);
		void Reset();

		bool SupportsVariableRateShading() const { return initialized; }

		void ApplyCombinedVRS(int width, int height, float leftProjX, float leftProjY, float rightProjX, float rightProjY);
		void ApplyArrayVRS(int width, int height, float leftProjX, float leftProjY, float rightProjX, float rightProjY);
		void ApplySingleEyeVRS(EVREye eye, int width, int height, float projX, float projY);
		void DisableVRS();

	private:
		VariableRateShading() {}

		bool nvapiLoaded = false;
		bool initialized = false;

		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		int singleWidth[2] = { 0, 0 };
		int singleHeight[2] = { 0, 0 };
		ComPtr<ID3D11Texture2D> singleEyeVRSTex[2];
		ComPtr<ID3D11NvShadingRateResourceView> singleEyeVRSView[2];
		int combinedWidth = 0;
		int combinedHeight = 0;
		ComPtr<ID3D11Texture2D> combinedVRSTex;
		ComPtr<ID3D11NvShadingRateResourceView> combinedVRSView;
		int arrayWidth = 0;
		int arrayHeight = 0;
		ComPtr<ID3D11Texture2D> arrayVRSTex;
		ComPtr<ID3D11NvShadingRateResourceView> arrayVRSView;

		void EnableVRS();

		void SetupSingleEyeVRS(EVREye eye, int width, int height, float projX, float projY);
		void SetupCombinedVRS(int width, int height, float leftProjX, float leftProjY, float rightProjX, float rightProjY);
		void SetupArrayVRS(int width, int height, float leftProjX, float leftProjY, float rightProjX, float rightProjY);
	};
}
