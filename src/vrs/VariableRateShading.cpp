#include "VariableRateShading.h"

#include "Config.h"

namespace vr {
	void CheckResult(const std::string &operation, HRESULT result);

	void CheckResult(const std::string &operation, NvAPI_Status result) {
		if (result != NVAPI_OK) {
			Log() << "Failed (" << std::hex << result << std::dec << "): " << operation << std::endl;
			throw std::exception();
		}
	}

	void VariableRateShading::Init(ComPtr<ID3D11Device> device) {
		initialized = false;
		this->device = device;

		if (!nvapiLoaded) {
			NvAPI_Status result = NvAPI_Initialize();
			if (result != NVAPI_OK) {
				return;
			}
			nvapiLoaded = true;
		}

		NV_D3D1x_GRAPHICS_CAPS caps;
		memset(&caps, 0, sizeof(NV_D3D1x_GRAPHICS_CAPS));
		NvAPI_Status status = NvAPI_D3D1x_GetGraphicsCapabilities(device.Get(), NV_D3D1x_GRAPHICS_CAPS_VER, &caps);
		if (status != NVAPI_OK || !caps.bVariablePixelRateShadingSupported) {
			return;
		}

		initialized = true;
	}

	void VariableRateShading::Reset() {
		if (nvapiLoaded)
			NvAPI_Unload();
		nvapiLoaded = false;
		initialized = false;
		singleEyeVRSTex[0].Reset();
		singleEyeVRSTex[1].Reset();
		singleEyeVRSView[0].Reset();
		singleEyeVRSView[1].Reset();
		combinedVRSTex.Reset();
		combinedVRSView.Reset();
		arrayVRSTex.Reset();
		arrayVRSView.Reset();
	}

	void VariableRateShading::ApplyCombinedVRS( int width, int height, float leftProjX, float leftProjY, float rightProjX, float rightProjY ) {
		if (!initialized)
			return;

		SetupCombinedVRS( width, height, leftProjX, leftProjY, rightProjX, rightProjY );
	}

	void VariableRateShading::SetupCombinedVRS( int width, int height, float leftProjX, float leftProjY, float rightProjX, float rightProjY ) {
		if (!initialized || (combinedVRSTex && width == combinedWidth && height == combinedHeight)) {
			return;
		}
		combinedVRSTex.Reset();
		combinedVRSView.Reset();

		int vrsWidth = width / NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH;
		int vrsHeight = height / NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT;

		D3D11_TEXTURE2D_DESC td = {};
		td.Width = vrsWidth;
		td.Height = vrsHeight;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8_UINT;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = 0;
		td.CPUAccessFlags = 0;
		td.MiscFlags= 0;
		td.MipLevels = 1;
		auto data = CreateCombinedFixedFoveatedVRSPattern(vrsWidth, vrsHeight, leftProjX, leftProjY, rightProjX, rightProjY);
		D3D11_SUBRESOURCE_DATA srd;
		srd.pSysMem = data.data();
		srd.SysMemPitch = vrsWidth;
		srd.SysMemSlicePitch = 0;
		HRESULT result = device->CreateTexture2D( &td, &srd, combinedVRSTex.GetAddressOf() );
		if (FAILED(result)) {
			Reset();
			Log() << "Failed to create combined VRS pattern texture" << std::endl;
			return;
		}

		NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC vd = {};
		vd.version = NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;
		vd.Format = td.Format;
		vd.ViewDimension = NV_SRRV_DIMENSION_TEXTURE2D;
		vd.Texture2D.MipSlice = 0;
		NvAPI_Status status = NvAPI_D3D11_CreateShadingRateResourceView( device.Get(), combinedVRSTex.Get(), &vd, combinedVRSView.GetAddressOf() );
		if (status != NVAPI_OK) {
			Reset();
			Log() << "Failed to create combined VRS pattern view: " << status << std::endl;
			return;
		}
	}

	uint8_t DistanceToVRSLevel(float distance) {
		if (distance < Config::Instance().innerRadius) {
			return 0;
		}
		if (distance < Config::Instance().midRadius) {
			return 1;
		}
		if (distance < Config::Instance().outerRadius) {
			return 2;
		}
		return 3;
	}

	std::vector<uint8_t> VariableRateShading::CreateCombinedFixedFoveatedVRSPattern( int width, int height, float leftProjX, float leftProjY, float rightProjX, float rightProjY ) {
		std::vector<uint8_t> data (width * height);
		int halfWidth = width / 2;

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < halfWidth; ++x) {
				float fx = float(x) / halfWidth;
				float fy = float(y) / height;
				float distance = 2 * sqrtf((fx - leftProjX) * (fx - leftProjX) + (fy - leftProjY) * (fy - leftProjY));
				data[y * width + x] = DistanceToVRSLevel(distance);
			}
			for (int x = halfWidth; x < width; ++x) {
				float fx = float(x - halfWidth) / halfWidth;
				float fy = float(y) / height;
				float distance = 2 * sqrtf((fx - rightProjX) * (fx - rightProjX) + (fy - rightProjY) * (fy - rightProjY));
				data[y * width + x] = DistanceToVRSLevel(distance);
			}
		}

		return data;
	}

}
