/**
 * @file FrameRendererInternal.h
 * @brief Shared helpers for FrameRenderer translation units.
 */
#pragma once
#include "Engine/Graphics/RHI/RHITypes.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

namespace sw
{
	namespace PassType
	{
		constexpr auto kShadow		  = "Shadow";
		constexpr auto kForwardOpaque = "ForwardOpaque";
		constexpr auto kTransparent	  = "Transparent";
		constexpr auto kGBuffer		  = "GBuffer";
		constexpr auto kGBufferAlbedo = "GBufferAlbedo";
		constexpr auto kGBufferNormal = "GBufferNormal";
		constexpr auto kLighting	  = "Lighting";
		constexpr auto kShading		  = "Shading";
		constexpr auto kPostBloom	  = "PostBloom";
		constexpr auto kOutline		  = "Outline";
		constexpr auto kPostOutline	  = "PostOutline";
		constexpr auto kPresent		  = "Present";
		constexpr auto kTaa			  = "TAA";
		constexpr auto kSsao		  = "SSAO";
		constexpr auto kHbao		  = "HBAO";
		constexpr auto kTonemap		  = "Tonemap";
		constexpr auto kDepthPrepass  = "DepthPrepass";
	} // namespace PassType

	namespace Attachment
	{
		constexpr auto kSwapchain		 = "Swapchain";
		constexpr auto kSceneColor		 = "SceneColor";
		constexpr auto kSceneDepth		 = "SceneDepth";
		constexpr auto kShadowMap		 = "ShadowMap";
		constexpr auto kGBufferAlbedo	 = "GBufferAlbedo";
		constexpr auto kGBufferNormal	 = "GBufferNormal";
		constexpr auto kLitColor		 = "LitColor";
		constexpr auto kBloomColor		 = "BloomColor";
		constexpr auto kBloomBright		 = "BloomBright";
		constexpr auto kOutlineColor	 = "OutlineColor";
		constexpr auto kTransparentColor = "TransparentColor";
		constexpr auto kTaaColor		 = "TaaColor";
	} // namespace Attachment

	namespace Entry
	{
		constexpr auto kVSMain = "VSMain";
		constexpr auto kPSMain = "PSMain";
		constexpr auto kCSMain = "CSMain";
	} // namespace Entry

	constexpr uint32  kDefaultTransientSize = 1280;
	constexpr auto	  kDefaultMainPassName	= "DefaultMainPass";
	constexpr float32 kBlackClear[4]		= { 0.0f, 0.0f, 0.0f, 1.0f };
	constexpr float32 kSceneClear[4]		= { 0.12f, 0.15f, 0.18f, 1.0f };
	constexpr float32 kDepthClear[4]		= { 1.0f, 0.0f, 0.0f, 0.0f };
	constexpr float32 kBloomClear[4]		= { 0.0f, 0.0f, 0.0f, 1.0f };
	constexpr float32 kNormalClear[4]		= { 0.5f, 0.5f, 1.0f, 1.0f };
	inline bool		  isDepthFormat( RHIFormat format ) { return format == RHIFormat::D24_UNORM_S8_UINT; }
	/** @brief 카메라 컴포넌트가 없을 때 GpuScene::buildFromScene에 전달되는 fallback 위치 */
	constexpr float32 kDefaultCameraPos[3] = { 0.0f, 1.2f, 3.2f };

	/** @brief 맵에 있는 첫 어태치먼트 이름. 없으면 nullptr. */
	inline const utf8* pickFirstExisting( const unordered_map<string, RHITextureHandle>& mapAttachments,
										  std::initializer_list<const utf8*>			 listNames )
	{
		for ( const utf8* pName : listNames )
		{
			if ( mapAttachments.find( pName ) != mapAttachments.end() )
				return pName;
		}
		return nullptr;
	}
} // namespace sw
