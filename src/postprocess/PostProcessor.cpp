#include <fstream>
#include "PostProcessor.h"
#define no_init_all deprecated
#include <d3d11.h>
#include <d3dcommon.h>
#include <wrl/client.h>
#define A_CPU
#include "nis/NIS_Config.h"
#include "Config.h"
#include "shader_nis_sharpen.h"
#include "shader_rdm_fullscreen_tri.h"
#include "shader_rdm_mask.h"
#include "shader_rdm_reconstruction.h"
#include "VrHooks.h"

using Microsoft::WRL::ComPtr;

namespace vr {
	void CheckResult(const std::string &operation, HRESULT result) {
		if (FAILED(result)) {
			Log() << "Failed (" << std::hex << result << std::dec << "): " << operation << std::endl;
			throw std::exception();
		}
	}
	
	DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case DXGI_FORMAT_R32G32B32_TYPELESS:
			return DXGI_FORMAT_R32G32B32_FLOAT;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			return DXGI_FORMAT_R10G10B10A2_UINT;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		default:
			return format;
		}
	}

	DXGI_FORMAT TranslateTypelessDepthFormats(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_R16_TYPELESS:
			return DXGI_FORMAT_D16_UNORM;
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R32_TYPELESS:
			return DXGI_FORMAT_D32_FLOAT;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		default:
			return format;
		}
	}

	DXGI_FORMAT MakeSrgbFormatsTypeless(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			return DXGI_FORMAT_B8G8R8A8_TYPELESS;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return DXGI_FORMAT_B8G8R8X8_TYPELESS;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		default:
			return format;
		}
	}

	DXGI_FORMAT DetermineOutputFormat(DXGI_FORMAT inputFormat) {
		switch (inputFormat) {
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			// SteamVR applies a different color conversion for these formats that we can't match
			// with R8G8B8 textures, so we have to use a matching texture format for our own resources.
			// Otherwise we'll get darkened pictures (applies to Revive mostly)
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		default:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}

	bool IsConsideredSrgbByOpenVR(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return true;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			// OpenVR appears to treat submitted typeless textures as SRGB
			return true;
		default:
			return false;
		}
	}

	bool IsSrgbFormat(DXGI_FORMAT format) {
		switch (format) {
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return true;
		default:
			return false;
		}
	}

	void CalculateProjectionCenter(EVREye eye, float &x, float &y) {
		IVRSystem *vrSystem = (IVRSystem*) VR_GetGenericInterface(IVRSystem_Version, nullptr);
		float left, right, top, bottom;
		vrSystem->GetProjectionRaw(eye, &left, &right, &top, &bottom);
		Log() << "Raw projection for eye " << eye << ": l " << left << ", r " << right << ", t " << top << ", b " << bottom << "\n";

		// calculate canted angle between the eyes
		auto ml = vrSystem->GetEyeToHeadTransform(Eye_Left);
		auto mr = vrSystem->GetEyeToHeadTransform(Eye_Right);
		float dotForward = ml.m[2][0] * mr.m[2][0] + ml.m[2][1] * mr.m[2][1] + ml.m[2][2] * mr.m[2][2];
		float cantedAngle = std::abs(std::acosf(dotForward) / 2) * (eye == Eye_Right ? -1 : 1);
		Log() << "Display is canted by " << cantedAngle << " RAD\n";

		float canted = std::tanf(cantedAngle);
		x = 0.5f * (1.f + (right + left - 2*canted) / (left - right));
		y = 0.5f * (1.f + (bottom + top) / (top - bottom));
		Log() << "Projection center for eye " << eye << ": " << x << ", " << y << "\n";
	}

	void PostProcessor::Apply(EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags) {
		if (!enabled || pTexture == nullptr || pTexture->eType != TextureType_DirectX || pTexture->handle == nullptr) {
			return;
		}

		static VRTextureBounds_t defaultBounds { 0, 0, 1, 1 };
		if (pBounds == nullptr) {
			pBounds = &defaultBounds;
		}

		ID3D11Texture2D *texture = (ID3D11Texture2D*)pTexture->handle;

		if ( Config::Instance().ffrEnabled ) {
			if (initialized) {
				D3D11_TEXTURE2D_DESC td;
				texture->GetDesc(&td);
				if (td.Width != textureWidth || td.Height != textureHeight) {
					Log() << "Texture size changed, recreating resources...\n";
					Reset();
				}
			}
			if (!initialized) {
				try {
					textureContainsOnlyOneEye = std::abs(pBounds->uMax - pBounds->uMin) > .5f;
					PrepareResources(texture, pTexture->eColorSpace);
				} catch (...) {
					Log() << "Resource creation failed, disabling\n";
					enabled = false;
					return;
				}
			}

			// if a single shared texture is used for both eyes, only apply effects on the first Submit
			if (true || eyeCount == 0 || textureContainsOnlyOneEye || texture != lastSubmittedTexture) {
				ApplyPostProcess(eEye, texture, pBounds);
			}
			lastSubmittedTexture = texture;
			eyeCount = (eyeCount + 1) % 2;
			if (eyeCount == 0) {
				depthClearCount = 0;
			}
			const_cast<Texture_t*>(pTexture)->handle = outputTexture;
			const_cast<Texture_t*>(pTexture)->eColorSpace = inputIsSrgb ? ColorSpace_Gamma : ColorSpace_Auto;
		}
	}

	void PostProcessor::ApplyFixedFoveatedRendering( ID3D11DepthStencilView *depthStencilView, float depth, uint8_t stencil ) {
		if (!enabled || depthStencilView == nullptr) {
			return;
		}

		ComPtr<ID3D11Resource> resource;
		depthStencilView->GetResource(resource.GetAddressOf());
		if (resource.Get() == nullptr) {
			return;
		}
		D3D11_TEXTURE2D_DESC texDesc;
		((ID3D11Texture2D*)resource.Get())->GetDesc(&texDesc);

		if (texDesc.Width < textureWidth || texDesc.Height < textureHeight) {
			// smaller than submitted texture size, so not the correct render target
			return;
		}

		if (texDesc.Width == texDesc.Height) {
			// this is probably the shadow map or something similar
			return;
		}

		ApplyRadialDensityMask( (ID3D11Texture2D*)resource.Get(), depth, stencil);
	}

	void PostProcessor::Reset() {
		enabled = true;
		initialized = false;
		device.Reset();
		context.Reset();
		sampler.Reset();
		inputTextureViews.clear();
		copiedTexture.Reset();
		copiedTextureView.Reset();
		rdmFullTriVertexShader.Reset();
		rdmMaskingShader.Reset();
		rdmMaskingConstantsBuffer[0].Reset();
		rdmMaskingConstantsBuffer[1].Reset();
		rdmDepthStencilState.Reset();
		rdmRasterizerState.Reset();
		rdmReconstructShader.Reset();
		rdmReconstructedTexture.Reset();
		rdmReconstructedView.Reset();
		rdmReconstructedUav.Reset();
		rdmReconstructConstantsBuffer[0].Reset();
		rdmReconstructConstantsBuffer[1].Reset();
		sharpenShader.Reset();
		sharpenConstantsBuffer[0].Reset();
		sharpenConstantsBuffer[1].Reset();
		sharpenedTexture.Reset();
		sharpenedTextureUav.Reset();
		lastSubmittedTexture = nullptr;
		outputTexture = nullptr;
		eyeCount = 0;
		depthClearCount = 0;
		for (int i = 0; i < QUERY_COUNT; ++i) {
			profileQueries[i].queryStart.Reset();
			profileQueries[i].queryEnd.Reset();
			profileQueries[i].queryDisjoint.Reset();
		}
	}

	void PostProcessor::PrepareCopyResources( DXGI_FORMAT format ) {
		Log() << "Creating copy texture of size " << textureWidth << "x" << textureHeight << "\n";
		D3D11_TEXTURE2D_DESC td;
		td.Width = textureWidth;
		td.Height = textureHeight;
		td.MipLevels = 1;
		td.CPUAccessFlags = 0;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		td.Format = MakeSrgbFormatsTypeless(format);
		td.MiscFlags = 0;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.ArraySize = 1;
		CheckResult("Creating copy texture", device->CreateTexture2D( &td, nullptr, copiedTexture.GetAddressOf()));
		D3D11_SHADER_RESOURCE_VIEW_DESC srv;
		srv.Format = TranslateTypelessFormats(td.Format);
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		srv.Texture2D.MostDetailedMip = 0;
		CheckResult("Creating copy SRV", device->CreateShaderResourceView(copiedTexture.Get(), &srv, copiedTextureView.GetAddressOf()));
	}

	ID3D11ShaderResourceView * PostProcessor::GetInputView( ID3D11Texture2D *inputTexture, int eye ) {
		if (requiresCopy) {
			D3D11_TEXTURE2D_DESC td;
			inputTexture->GetDesc(&td);
			if (td.SampleDesc.Count > 1) {
				context->ResolveSubresource(copiedTexture.Get(), 0, inputTexture, 0, td.Format);
			} else {
				D3D11_BOX region;
				region.left = region.top = region.front = 0;
				region.right = td.Width;
				region.bottom = td.Height;
				region.back = 1;
				context->CopySubresourceRegion(copiedTexture.Get(), 0, 0, 0, 0, inputTexture, 0, &region);
			}
			return copiedTextureView.Get();
		}
		
		if (inputTextureViews.find(inputTexture) == inputTextureViews.end()) {
			Log() << "Creating shader resource view for input texture " << inputTexture << std::endl;
			// create resource view for input texture
			D3D11_TEXTURE2D_DESC std;
			inputTexture->GetDesc( &std );
			Log() << "Texture has size " << std.Width << "x" << std.Height << " and format " << std.Format << "\n";
			D3D11_SHADER_RESOURCE_VIEW_DESC svd;
			svd.Format = TranslateTypelessFormats(std.Format);
			svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			svd.Texture2D.MostDetailedMip = 0;
			svd.Texture2D.MipLevels = 1;
			EyeViews &views = inputTextureViews[inputTexture];
			HRESULT result = device->CreateShaderResourceView( inputTexture, &svd, views.view[0].GetAddressOf() );
			if (FAILED(result)) {
				Log() << "Failed to create resource view: " << std::hex << (unsigned long)result << std::dec << std::endl;
				inputTextureViews.erase( inputTexture );
				return nullptr;
			}
			if (std.ArraySize > 1) {
				// if an array texture was submitted, the right eye will be placed in the second entry, so we need
				// a separate view for that eye
				Log() << "Texture is an array texture, using separate subview for right eye\n";
				svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				svd.Texture2DArray.ArraySize = 1;
				svd.Texture2DArray.FirstArraySlice = D3D11CalcSubresource( 0, 1, 1 );
				svd.Texture2DArray.MostDetailedMip = 0;
				svd.Texture2DArray.MipLevels = 1;
				result = device->CreateShaderResourceView( inputTexture, &svd, views.view[1].GetAddressOf() );
				if (FAILED(result)) {
					Log() << "Failed to create secondary resource view: " << std::hex << (unsigned long)result << std::dec << std::endl;
					inputTextureViews.erase( inputTexture );
					return nullptr;
				}
			} else {
				views.view[1] = views.view[0];
			}
		}
		return inputTextureViews[inputTexture].view[eye].Get();
	}

	struct RdmMaskingConstants {
		float depthOut;
		float radius[3];
		float invClusterResolution[2];
		float projectionCenter[2];
		float yFix[2];
		float unused[2];
	};

	struct RdmReconstructConstants {
		int offset[2];
		float projectionCenter[2];
		float invClusterResolution[2];
		float invResolution[2];
		float radius[3];
		int quality;
	};

	void PostProcessor::PrepareRdmResources( DXGI_FORMAT format ) {
		CheckResult("Creating RDM fullscreen tri vertex shader", device->CreateVertexShader( g_RDMFullscreenTriShader, sizeof( g_RDMFullscreenTriShader ), nullptr, rdmFullTriVertexShader.GetAddressOf() ));
		CheckResult("Creating RDM masking shader", device->CreatePixelShader( g_RDMMaskShader, sizeof( g_RDMMaskShader ), nullptr, rdmMaskingShader.GetAddressOf() ));
		CheckResult("Creating RDM reconstruction shader", device->CreateComputeShader( g_RDMReconstructShader, sizeof( g_RDMReconstructShader ), nullptr, rdmReconstructShader.GetAddressOf() ));

		// create output texture
		D3D11_TEXTURE2D_DESC td;
		td.Width = textureWidth;
		td.Height = textureHeight;
		td.MipLevels = 1;
		td.CPUAccessFlags = 0;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
		td.Format = format;
		td.MiscFlags = 0;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.ArraySize = 1;
		CheckResult("Creating RDM reconstructed texture", device->CreateTexture2D( &td, nullptr, rdmReconstructedTexture.GetAddressOf() ));
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
		uav.Format = td.Format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uav.Texture2D.MipSlice = 0;
		CheckResult("Creating RDM reconstructed UAV", device->CreateUnorderedAccessView( rdmReconstructedTexture.Get(), &uav, rdmReconstructedUav.GetAddressOf() ));
		D3D11_SHADER_RESOURCE_VIEW_DESC svd;
		svd.Format = TranslateTypelessFormats(format);
		svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		svd.Texture2D.MostDetailedMip = 0;
		svd.Texture2D.MipLevels = 1;
		CheckResult("Creating RDM reconstructed view", device->CreateShaderResourceView( rdmReconstructedTexture.Get(), &svd, rdmReconstructedView.GetAddressOf() ));

		D3D11_DEPTH_STENCIL_DESC dsd;
		dsd.DepthEnable = TRUE;
		dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
		dsd.StencilEnable = TRUE;
		dsd.StencilReadMask = 255;
		dsd.StencilWriteMask = 255;
		dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		dsd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		dsd.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		CheckResult("Creating RDM depth stencil state", device->CreateDepthStencilState(&dsd, rdmDepthStencilState.GetAddressOf()));

		D3D11_RASTERIZER_DESC rsd;
		rsd.FillMode = D3D11_FILL_SOLID;
		rsd.CullMode = D3D11_CULL_NONE;
		rsd.FrontCounterClockwise = FALSE;
		rsd.DepthBias = 0;
		rsd.SlopeScaledDepthBias = 0;
		rsd.DepthBiasClamp = 0;
		rsd.DepthClipEnable = TRUE;
		rsd.ScissorEnable = FALSE;
		rsd.MultisampleEnable = FALSE;
		rsd.AntialiasedLineEnable = FALSE;
		CheckResult("Creating RDM rasterizer state", device->CreateRasterizerState(&rsd, rdmRasterizerState.GetAddressOf()));

		D3D11_BUFFER_DESC bd;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = 0;
		bd.StructureByteStride = 0;
		bd.ByteWidth = sizeof(RdmMaskingConstants);
		CheckResult("Creating RDM masking constants buffer", device->CreateBuffer( &bd, nullptr, rdmMaskingConstantsBuffer[0].GetAddressOf() ));
		CheckResult("Creating RDM masking constants buffer", device->CreateBuffer( &bd, nullptr, rdmMaskingConstantsBuffer[1].GetAddressOf() ));
		bd.ByteWidth = sizeof(RdmReconstructConstants);
		CheckResult("Creating RDM reconstruct constants buffer", device->CreateBuffer( &bd, nullptr, rdmReconstructConstantsBuffer[0].GetAddressOf() ));
		CheckResult("Creating RDM reconstruct constants buffer", device->CreateBuffer( &bd, nullptr, rdmReconstructConstantsBuffer[1].GetAddressOf() ));
	}

	ID3D11DepthStencilView * PostProcessor::GetDepthStencilView( ID3D11Texture2D *depthStencilTex, EVREye eye ) {
		if ( depthStencilViews.find( depthStencilTex ) == depthStencilViews.end() ) {
			Log() << "Creating depth stencil views for " << std::hex << depthStencilTex << std::dec << "\n";
			D3D11_TEXTURE2D_DESC td;
			depthStencilTex->GetDesc( &td );
			bool isArray = td.ArraySize == 2;
			bool isMS = td.SampleDesc.Count > 1;
			Log() << "Texture format " << td.Format << ", array size " << td.ArraySize << ", sample count " << td.SampleDesc.Count << "\n";
			D3D11_DEPTH_STENCIL_VIEW_DESC dvd;
			dvd.Format = TranslateTypelessDepthFormats( td.Format );
			dvd.ViewDimension = isMS ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
			dvd.Flags = 0;
			dvd.Texture2D.MipSlice = 0;
			auto &views = depthStencilViews[depthStencilTex];
			HRESULT result = device->CreateDepthStencilView( depthStencilTex, &dvd, views.view[0].GetAddressOf() );
			if (FAILED(result)) {
				Log() << "Error creating depth stencil view: " << std::hex << result << std::dec << std::endl;
				return nullptr;
			}
			if (isArray) {
				Log() << "Depth stencil texture is an array, using separate slice per eye\n";
				if (isMS) {
					dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
					dvd.Texture2DMSArray.ArraySize = 1;
					dvd.Texture2DMSArray.FirstArraySlice = D3D11CalcSubresource( 0, 1, 1 );
				} else {
					dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
					dvd.Texture2DArray.MipSlice = 0;
					dvd.Texture2DArray.ArraySize = 1;
					dvd.Texture2DArray.FirstArraySlice = D3D11CalcSubresource( 0, 1, 1 );
				}
				result = device->CreateDepthStencilView( depthStencilTex, &dvd, views.view[1].GetAddressOf() );
				if (FAILED(result)) {
					Log() << "Error creating depth stencil view array slice: " << std::hex << result << std::dec << std::endl;
					return nullptr;
				}
			} else {
				views.view[1] = views.view[0];
			}
		}

		return depthStencilViews[depthStencilTex].view[eye].Get();
	}

	bool HasBlacklistedTextureName(ID3D11Texture2D *tex) {
		// used to ignore certain depth textures that we know are not relevant for us
		// currently found in some older Unity games
		char debugName[255] = { 0 };
		UINT bufferSize = 255;
		tex->GetPrivateData( WKPDID_D3DDebugObjectName, &bufferSize, debugName );
		if (strncmp( debugName, "Camera DepthTexture", 255 ) == 0) {
			return true;
		}
		return false;
	}

	void PostProcessor::ApplyRadialDensityMask( ID3D11Texture2D *depthStencilTex, float depth, uint8_t stencil ) {
		if (HasBlacklistedTextureName(depthStencilTex)) {
			return;
		}

		D3D11_TEXTURE2D_DESC td;
		depthStencilTex->GetDesc( &td );
		bool sideBySide = !textureContainsOnlyOneEye || td.Width >= 2 * textureWidth;
		bool arrayTex = td.ArraySize == 2;
		vr::EVREye currentEye = vr::Eye_Left;
		if (!sideBySide && !arrayTex && depthClearCount > 0) {
			currentEye = vr::Eye_Right;
		}
		uint32_t renderWidth = td.Width * (sideBySide ? 0.5 : 1);
		uint32_t renderHeight = td.Height;
		++depthClearCount;

		// store current D3D11 state before drawing RDM mask
		ComPtr<ID3D11VertexShader> prevVS;
		context->VSGetShader(prevVS.GetAddressOf(), nullptr, nullptr);
		ComPtr<ID3D11PixelShader> prevPS;
		context->PSGetShader(prevPS.GetAddressOf(), nullptr, nullptr);
		ComPtr<ID3D11InputLayout> inputLayout;
		context->IAGetInputLayout( inputLayout.GetAddressOf() );
		D3D11_PRIMITIVE_TOPOLOGY topology;
		context->IAGetPrimitiveTopology( &topology );
		ID3D11Buffer *vertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		context->IAGetVertexBuffers( 0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertexBuffers, strides, offsets );
		ComPtr<ID3D11Buffer> indexBuffer;
		DXGI_FORMAT format;
		UINT offset;
		context->IAGetIndexBuffer(indexBuffer.GetAddressOf(), &format, &offset);
		ID3D11RenderTargetView *renderTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
		ComPtr<ID3D11DepthStencilView> depthStencil;
		context->OMGetRenderTargets( D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, renderTargets, depthStencil.GetAddressOf() );
		ComPtr<ID3D11RasterizerState> rasterizerState;
		context->RSGetState( rasterizerState.GetAddressOf() );
		ComPtr<ID3D11DepthStencilState> depthStencilState;
		UINT stencilRef;
		context->OMGetDepthStencilState( depthStencilState.GetAddressOf(), &stencilRef );
		D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		UINT numViewports = 0;
		context->RSGetViewports( &numViewports, nullptr );
		context->RSGetViewports( &numViewports, viewports );
		ComPtr<ID3D11Buffer> vsConstantBuffer;
		context->VSGetConstantBuffers( 0, 1, vsConstantBuffer.GetAddressOf() );
		ComPtr<ID3D11Buffer> psConstantBuffer;
		context->PSGetConstantBuffers( 0, 1, psConstantBuffer.GetAddressOf() );

		context->VSSetShader( rdmFullTriVertexShader.Get(), nullptr, 0 );
		context->PSSetShader( rdmMaskingShader.Get(), nullptr, 0 );
		context->IASetInputLayout( nullptr );
		context->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		context->IASetVertexBuffers( 0, 0, nullptr, nullptr, nullptr );
		context->IASetIndexBuffer( nullptr, DXGI_FORMAT_UNKNOWN, 0 );
		context->OMSetRenderTargets( 0, nullptr, GetDepthStencilView(depthStencilTex, currentEye) );
		context->RSSetState(rdmRasterizerState.Get());
		context->OMSetDepthStencilState(rdmDepthStencilState.Get(), ~stencil);

		RdmMaskingConstants constants;
		constants.depthOut = 1.f - depth;
		constants.radius[0] = Config::Instance().innerRadius;
		constants.radius[1] = Config::Instance().midRadius;
		constants.radius[2] = Config::Instance().outerRadius;
		constants.invClusterResolution[0] = 8.f / renderWidth;
		constants.invClusterResolution[1] = 8.f / renderHeight;
		constants.projectionCenter[0] = projX[currentEye];
		constants.projectionCenter[1] = projY[currentEye];
		// new Unity engine with array textures renders heads down and then flips the texture before submitting.
		// so we also need to construct the RDM heads-down in that case.
		constants.yFix[0] = arrayTex ? -1 : 1;
		constants.yFix[1] = arrayTex ? renderHeight : 0;
		D3D11_MAPPED_SUBRESOURCE mapped { nullptr, 0, 0 };
		context->Map( rdmMaskingConstantsBuffer[currentEye].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
		memcpy(mapped.pData, &constants, sizeof(constants));
		context->Unmap( rdmMaskingConstantsBuffer[currentEye].Get(), 0 );
		context->VSSetConstantBuffers( 0, 1, rdmMaskingConstantsBuffer[currentEye].GetAddressOf() );
		context->PSSetConstantBuffers( 0, 1, rdmMaskingConstantsBuffer[currentEye].GetAddressOf() );

		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.MinDepth = 0;
		vp.MaxDepth = 1;
		vp.Width = renderWidth;
		vp.Height = renderHeight;
		context->RSSetViewports( 1, &vp );

		context->Draw( 3, 0 );

		if (sideBySide || arrayTex) {
			constants.projectionCenter[0] = projX[Eye_Right] + (sideBySide ? 1.f : 0.f);
			constants.projectionCenter[1] = projY[Eye_Right];
			context->Map( rdmMaskingConstantsBuffer[Eye_Right].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			memcpy(mapped.pData, &constants, sizeof(constants));
			context->Unmap( rdmMaskingConstantsBuffer[Eye_Right].Get(), 0 );
			context->VSSetConstantBuffers( 0, 1, rdmMaskingConstantsBuffer[1].GetAddressOf() );
			context->PSSetConstantBuffers( 0, 1, rdmMaskingConstantsBuffer[1].GetAddressOf() );
			context->OMSetRenderTargets( 0, nullptr, GetDepthStencilView(depthStencilTex, Eye_Right) );
			if (sideBySide) {
				vp.TopLeftX = renderWidth;
			}
			context->RSSetViewports( 1, &vp );
			context->Draw( 3, 0 );
		}

		// restore previous state
		context->VSSetShader(prevVS.Get(), nullptr, 0);
		context->PSSetShader(prevPS.Get(), nullptr, 0);
		context->IASetInputLayout( inputLayout.Get() );
		context->IASetPrimitiveTopology( topology );
		context->IASetVertexBuffers( 0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertexBuffers, strides, offsets );
		for (int i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i) {
			if (vertexBuffers[i])
				vertexBuffers[i]->Release();
		}
		context->IASetIndexBuffer( indexBuffer.Get(), format, offset );
		context->OMSetRenderTargets( D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, renderTargets, depthStencil.Get() );
		for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
			if (renderTargets[i]) {
				renderTargets[i]->Release();
			}
		}
		context->RSSetState( rasterizerState.Get() );
		context->OMSetDepthStencilState( depthStencilState.Get(), stencilRef );
		context->RSSetViewports( numViewports, viewports );
		context->VSSetConstantBuffers( 0, 1, vsConstantBuffer.GetAddressOf() );
		context->PSSetConstantBuffers( 0, 1, psConstantBuffer.GetAddressOf() );
	}

	void PostProcessor::ReconstructRdmRender( vr::EVREye eye, ID3D11ShaderResourceView *inputView, int x, int y, int width, int height ) {
		context->CSSetShader( rdmReconstructShader.Get(), nullptr, 0 );
		ID3D11Buffer *emptyBind[] = {nullptr};
		context->CSSetConstantBuffers( 0, 1, emptyBind );

		RdmReconstructConstants constants;
		constants.offset[0] = x;
		constants.offset[1] = y;
		constants.projectionCenter[0] = projX[eye];
		constants.projectionCenter[1] = projY[eye];
		constants.invResolution[0] = 1.f / textureWidth;
		constants.invResolution[1] = 1.f / textureHeight;
		constants.invClusterResolution[0] = 8.f / width;
		constants.invClusterResolution[1] = 8.f / height;
		constants.radius[0] = Config::Instance().innerRadius;
		constants.radius[1] = Config::Instance().midRadius;
		constants.radius[2] = Config::Instance().outerRadius;
		constants.quality = 2;
		if (!textureContainsOnlyOneEye && eye == Eye_Right)
			constants.projectionCenter[0] += 1.f;
		D3D11_MAPPED_SUBRESOURCE mapped { nullptr, 0, 0 };
		context->Map( rdmReconstructConstantsBuffer[eye].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
		memcpy(mapped.pData, &constants, sizeof(constants));
		context->Unmap( rdmReconstructConstantsBuffer[eye].Get(), 0 );
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews( 0, 1, rdmReconstructedUav.GetAddressOf(), &uavCount );
		context->CSSetConstantBuffers( 0, 1, rdmReconstructConstantsBuffer[eye].GetAddressOf() );
		ID3D11ShaderResourceView *srvs[1] = {inputView};
		context->CSSetShaderResources( 0, 1, srvs );
		context->CSSetSamplers(0, 1, sampler.GetAddressOf());
		context->Dispatch( (width+7)/8, (height+7)/8, 1 );
	}


	void PostProcessor::PrepareSharpeningResources(DXGI_FORMAT format) {
		CheckResult("Creating NIS sharpening shader", device->CreateComputeShader( g_NISSharpenShader, sizeof(g_NISSharpenShader), nullptr, sharpenShader.GetAddressOf()));

		float proj[4];
		CalculateProjectionCenter(Eye_Left, proj[0], proj[1]);
		CalculateProjectionCenter(Eye_Right, proj[2], proj[3]);

		D3D11_BUFFER_DESC bd;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = 0;
		bd.StructureByteStride = 0;
		bd.ByteWidth = sizeof(NISConfig);
		CheckResult("Creating sharpen constants buffer", device->CreateBuffer( &bd, nullptr, sharpenConstantsBuffer[0].GetAddressOf()));
		CheckResult("Creating sharpen constants buffer", device->CreateBuffer( &bd, nullptr, sharpenConstantsBuffer[1].GetAddressOf()));
		
		Log() << "Creating sharpened texture of size " << textureWidth << "x" << textureHeight << "\n";
		D3D11_TEXTURE2D_DESC td;
		td.Width = textureWidth;
		td.Height = textureHeight;
		td.MipLevels = 1;
		td.CPUAccessFlags = 0;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
		td.Format = format;
		td.MiscFlags = 0;
		td.SampleDesc.Count = 1;
		td.SampleDesc.Quality = 0;
		td.ArraySize = 1;
		CheckResult("Creating sharpened texture", device->CreateTexture2D( &td, nullptr, sharpenedTexture.GetAddressOf()));
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
		uav.Format = format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uav.Texture2D.MipSlice = 0;
		CheckResult("Creating sharpened UAV", device->CreateUnorderedAccessView( sharpenedTexture.Get(), &uav, sharpenedTextureUav.GetAddressOf()));
	}

	void PostProcessor::ApplySharpening( EVREye eEye, ID3D11ShaderResourceView *inputView, int x, int y, int width, int height ) {
		NISConfig nisConfig;
		NVSharpenUpdateConfig( nisConfig, Config::Instance().sharpness, x, y, width, height, textureWidth, textureHeight, x, y );
		nisConfig.imageCentre[0] = x + width * projX[eEye];
		nisConfig.imageCentre[1] = y + height * projY[eEye];
		nisConfig.radius[0] = 0.5f * Config::Instance().sharpenRadius * height;
		nisConfig.radius[1] = nisConfig.radius[0] * nisConfig.radius[0];
		nisConfig.radius[2] = textureWidth;
		nisConfig.radius[3] = textureHeight;
		nisConfig.reserved1 = Config::Instance().debugMode ? 1.f : 0.f;
		D3D11_MAPPED_SUBRESOURCE mapped { nullptr, 0, 0 };
		context->Map( sharpenConstantsBuffer[eEye].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
		memcpy( mapped.pData, &nisConfig, sizeof(nisConfig) );
		context->Unmap( sharpenConstantsBuffer[eEye].Get(), 0 );
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews( 0, 1, sharpenedTextureUav.GetAddressOf(), &uavCount );
		context->CSSetConstantBuffers( 0, 1, sharpenConstantsBuffer[eEye].GetAddressOf() );
		ID3D11ShaderResourceView *srvs[1] = {inputView};
		context->CSSetShaderResources( 0, 1, srvs );
		context->CSSetSamplers( 0, 1, sampler.GetAddressOf() );
		context->CSSetShader( sharpenShader.Get(), nullptr, 0 );
		context->Dispatch( (UINT)std::ceil(width / 32.f), (UINT)std::ceil(height / 32.f), 1 );
	}

	void PostProcessor::PrepareResources( ID3D11Texture2D *inputTexture, EColorSpace colorSpace ) {
		Log() << "Creating post-processing resources\n";
		inputTexture->GetDevice( device.GetAddressOf() );
		device->GetImmediateContext( context.GetAddressOf() );

		CalculateProjectionCenter( vr::Eye_Left, projX[0], projY[0] );
		CalculateProjectionCenter( vr::Eye_Right, projX[1], projY[1] );

		D3D11_TEXTURE2D_DESC std;
		inputTexture->GetDesc( &std );
		inputIsSrgb = colorSpace == ColorSpace_Gamma || (colorSpace == ColorSpace_Auto && IsConsideredSrgbByOpenVR(std.Format));
		if (inputIsSrgb) {
			Log() << "Input texture is in SRGB color space\n";
		}

		textureWidth = std.Width;
		textureHeight = std.Height;

		D3D11_SAMPLER_DESC sd;
		sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MipLODBias = 0;
		sd.MaxAnisotropy = 1;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sd.MinLOD = 0;
		sd.MaxLOD = 0;
		device->CreateSamplerState(&sd, sampler.GetAddressOf());

		if (!(std.BindFlags & D3D11_BIND_SHADER_RESOURCE) || std.SampleDesc.Count > 1 || IsSrgbFormat(std.Format)) {
			Log() << "Input texture can't be bound directly, need to copy\n";
			requiresCopy = true;
			PrepareCopyResources(std.Format);
		}

		if (Config::Instance().ffrEnabled) {
			DXGI_FORMAT textureFormat = DetermineOutputFormat(std.Format);
			Log() << "Creating output textures in format " << textureFormat << "\n";
			PrepareRdmResources(textureFormat);
			if (Config::Instance().useSharpening) {
				PrepareSharpeningResources(textureFormat);
			}

			HookD3D11Context( context.Get(), device.Get() );

			if (Config::Instance().debugMode) {
				for (int i = 0; i < QUERY_COUNT; ++i) {
					D3D11_QUERY_DESC qd;
					qd.Query = D3D11_QUERY_TIMESTAMP;
					qd.MiscFlags = 0;
					device->CreateQuery(&qd, profileQueries[i].queryStart.GetAddressOf());
					device->CreateQuery(&qd, profileQueries[i].queryEnd.GetAddressOf());
					qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
					device->CreateQuery(&qd, profileQueries[i].queryDisjoint.GetAddressOf());
				}
			}
		}

		initialized = true;
	}


	void PostProcessor::ApplyPostProcess( EVREye eEye, ID3D11Texture2D *inputTexture, const VRTextureBounds_t *bounds ) {
		ID3D11Buffer* currentConstBuffs[1];
		ID3D11ShaderResourceView* currentSRVs[3];
		ID3D11UnorderedAccessView* currentUAVs[1];

		context->CSGetShaderResources(0, 3, currentSRVs);
		context->CSGetUnorderedAccessViews(0, 1, currentUAVs);
		context->CSGetConstantBuffers(0, 1, currentConstBuffs);

		outputTexture = inputTexture;

		ID3D11ShaderResourceView *inputView = GetInputView(inputTexture, eEye);
		if (inputView == nullptr) {
			return;
		}

		if (Config::Instance().debugMode) {
			context->Begin(profileQueries[currentQuery].queryDisjoint.Get());
			context->End(profileQueries[currentQuery].queryStart.Get());
		}

		context->OMSetRenderTargets(0, nullptr, nullptr);
		uint32_t offsetX = textureWidth * min(bounds->uMin, bounds->uMax);
		uint32_t offsetY = textureHeight * min(bounds->vMin, bounds->vMax);
		uint32_t width = textureWidth * fabsf(bounds->uMax - bounds->uMin);
		uint32_t height = textureHeight * fabsf(bounds->vMax - bounds->vMin);

		if (Config::Instance().ffrEnabled) {
			ReconstructRdmRender( eEye, inputView, offsetX, offsetY, width, height );
			inputView = rdmReconstructedView.Get();
			outputTexture = rdmReconstructedTexture.Get();
		}

		if (Config::Instance().ffrEnabled && Config::Instance().useSharpening) {
			ApplySharpening(eEye, inputView, offsetX, offsetY, width, height);
			outputTexture = sharpenedTexture.Get();
		}

		context->CSSetShaderResources(0, 3, currentSRVs);
		UINT uavCount = -1;
		context->CSSetUnorderedAccessViews(0, 1, currentUAVs, &uavCount);
		context->CSSetConstantBuffers(0, 1, currentConstBuffs);

		if (Config::Instance().debugMode) {
			context->End(profileQueries[currentQuery].queryEnd.Get());
			context->End(profileQueries[currentQuery].queryDisjoint.Get());

			currentQuery = (currentQuery + 1) % QUERY_COUNT;
			while (context->GetData(profileQueries[currentQuery].queryDisjoint.Get(), nullptr, 0, 0) == S_FALSE) {
				Sleep(1);
			}
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
			HRESULT result = context->GetData(profileQueries[currentQuery].queryDisjoint.Get(), &disjoint, sizeof(disjoint), 0);
			if (result == S_OK && !disjoint.Disjoint) {
				UINT64 begin, end;
				context->GetData(profileQueries[currentQuery].queryStart.Get(), &begin, sizeof(UINT64), 0);
				context->GetData(profileQueries[currentQuery].queryEnd.Get(), &end, sizeof(UINT64), 0);
				float duration = (end - begin) / float(disjoint.Frequency);
				summedGpuTime += duration;
				++countedQueries;

				if (countedQueries >= 500) {
					float avgTimeMs = 1000.f / countedQueries * summedGpuTime;
					avgTimeMs *= 2; // because it's only for one eye, and we are interested in a time for both
					Log() << "Average GPU processing time for upscale: " << avgTimeMs << " ms\n";
					countedQueries = 0;
					summedGpuTime = 0.f;
				}
			}
		}
	}
}
