/**
 * @file FrameRendererUtil.h
 * @brief FrameRenderer 번역 단위 공유 상수·헬퍼
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	/** @brief FrameRenderer TU 공유 패스/어태치먼트 이름과 헬퍼 */
	struct FrameRendererUtil
	{
		struct PassType
		{
			static constexpr auto kShadow		 = "Shadow";
			static constexpr auto kForwardOpaque = "ForwardOpaque";
			static constexpr auto kTransparent	 = "Transparent";
			static constexpr auto kGBuffer		 = "GBuffer";
			static constexpr auto kGBufferAlbedo = "GBufferAlbedo";
			static constexpr auto kGBufferNormal = "GBufferNormal";
			static constexpr auto kLighting		 = "Lighting";
			static constexpr auto kShading		 = "Shading";
			static constexpr auto kPostBloom	 = "PostBloom";
			static constexpr auto kOutline		 = "Outline";
			static constexpr auto kPostOutline	 = "PostOutline";
			static constexpr auto kPresent		 = "Present";
			static constexpr auto kTaa			 = "TAA";
			static constexpr auto kSsao			 = "SSAO";
			static constexpr auto kHbao			 = "HBAO";
			static constexpr auto kTonemap		 = "Tonemap";
			static constexpr auto kDepthPrepass	 = "DepthPrepass";
		};

		struct Attachment
		{
			static constexpr auto kSwapchain		= "Swapchain";
			static constexpr auto kSceneColor		= "SceneColor";
			static constexpr auto kSceneDepth		= "SceneDepth";
			static constexpr auto kShadowMap		= "ShadowMap";
			static constexpr auto kGBufferAlbedo	= "GBufferAlbedo";
			static constexpr auto kGBufferNormal	= "GBufferNormal";
			static constexpr auto kLitColor			= "LitColor";
			static constexpr auto kBloomColor		= "BloomColor";
			static constexpr auto kBloomBright		= "BloomBright";
			static constexpr auto kOutlineColor		= "OutlineColor";
			static constexpr auto kTransparentColor = "TransparentColor";
			static constexpr auto kTaaColor			= "TaaColor";
		};

		struct Entry
		{
			static constexpr auto kVSMain = "VSMain";
			static constexpr auto kPSMain = "PSMain";
			static constexpr auto kCSMain = "CSMain";
		};

		static constexpr uint32	 kDefaultTransientSize = 1280;
		static constexpr auto	 kDefaultMainPassName  = "DefaultMainPass";
		static constexpr float32 kBlackClear[4]		   = { 0.0f, 0.0f, 0.0f, 1.0f };
		static constexpr float32 kSceneClear[4]		   = { 0.12f, 0.15f, 0.18f, 1.0f };
		static constexpr float32 kDepthClear[4]		   = { 1.0f, 0.0f, 0.0f, 0.0f };
		static constexpr float32 kBloomClear[4]		   = { 0.0f, 0.0f, 0.0f, 1.0f };
		static constexpr float32 kNormalClear[4]	   = { 0.5f, 0.5f, 1.0f, 1.0f };
		static constexpr float32 kDefaultCameraPos[3]  = { 0.0f, 1.2f, 3.2f };

		static bool isDepthFormat( RHIFormat format ) { return format == RHIFormat::D24_UNORM_S8_UINT; }

		static const utf8* pickFirstExisting( const unordered_map<string, RHITextureHandle>& mapAttachment,
											  std::initializer_list<const utf8*>			 listName )
		{
			for ( const utf8* pName : listName )
			{
				if ( mapAttachment.find( pName ) != mapAttachment.end() )
					return pName;
			}
			return nullptr;
		}
	};
} // namespace sw
