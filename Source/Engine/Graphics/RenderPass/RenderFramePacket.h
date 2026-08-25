/**
 * @file RenderFramePacket.h
 * @brief Game Thread → Render Thread frame submission data.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RenderPass/GpuScene.h"

namespace sw
{
	class Material;
	class IRHIDevice;
	struct RenderFramePacket;

	SW_DECLARE_DELEGATE( void, PresentHookDelegate, IRHIDevice&, RenderFramePacket& );

	/// @brief 게임 스레드 → 렌더 스레드로 넘기는 한 프레임 스냅샷
	struct RenderFramePacket
	{
		GpuScene			   _gpuScene;
		float32				   _clearColor[4]{ 0.12f, 0.15f, 0.18f, 1.0f };
		float32				   _cameraPos[3]{ 0.0f, 1.2f, 3.2f };
		float32				   _viewProj[16]{};
		float32				   _lightViewProj[16]{};
		Material*			   _pSceneMaterial{ nullptr };
		RHITextureHandle	   _gameRenderTarget{ 0 }; ///< 0 = backbuffer path
		uint32				   _viewportWidth{ 0 };
		uint32				   _viewportHeight{ 0 };
		uint64				   _frameIndex{ 0 };
		uint8				   _bHasViewProj  : 1;
		uint8				   _bEnableEditor : 1;
		uint8				   _bValid		  : 1;
		[[maybe_unused]] uint8 _reserved	  : 5;

		RenderFramePacket()
			: _bHasViewProj{ 0 }
			, _bEnableEditor{ 0 }
			, _bValid{ 0 }
			, _reserved{ 0 } {}
	};
} // namespace sw
