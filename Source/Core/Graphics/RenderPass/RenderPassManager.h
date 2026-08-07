#pragma once
/**
 * @file RenderPassManager.h
 * @brief 렌더 패스 등록·실행 관리
 */

#include "Core/CoreMinimal.h"

#include "RenderPassResource.h"
namespace sw
{

	class SW_API RenderPassManager
	{

	public:
		/** @brief 매니저를 초기화합니다. */
		bool initialize();
		/** @brief 캐시를 비우고 종료합니다. */
		void shutdown();

		/** @brief XML 에셋 경로에서 렌더 패스를 로드(또는 캐시 반환)합니다. */
		RenderPassResource* loadRenderPass( const std::string_view assetRelativePath );

		/** @brief 이름으로 이미 로드된 렌더 패스를 찾습니다. */
		RenderPassResource* findRenderPass( hashed_string name );

		/** @brief 로드된 렌더 패스 캐시를 비웁니다. */
		void clearCache();

	public:
		RenderPassManager()										 = default;
		~RenderPassManager()									 = default;
		RenderPassManager( const RenderPassManager& )			 = delete;
		RenderPassManager& operator=( const RenderPassManager& ) = delete;

		std::unordered_map<hashed_string, std::unique_ptr<RenderPassResource>> _renderPassMap;
	};
} // namespace sw
