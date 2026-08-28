/**
 * @file RenderPassManager.h
 * @brief RenderPass(어태치먼트/RHI)와 RenderPipeline(프레임 그래프) XML 에셋 캐시
 * @note IRHIDevice가 소유합니다. GPU 오브젝트와 함께 죽으므로 ResourceManager에 넣지 않습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
	class RenderPassResource;
	class RenderPipelineResource;

	/// @brief 디바이스가 소유하는 렌더 패스/파이프라인 에셋 캐시
	class SW_API RenderPassManager
	{
	public:
		/** @brief 빈 매니저. */
		RenderPassManager();
		/** @brief 매니저를 해제합니다. */
		~RenderPassManager();

		/** @brief 복사를 금지합니다. */
		RenderPassManager( const RenderPassManager& ) = delete;
		/** @brief 대입을 금지합니다. */
		RenderPassManager& operator=( const RenderPassManager& ) = delete;

		/** @brief 매니저를 초기화합니다. */
		bool initialize();
		/** @brief 캐시를 비우고 종료합니다. */
		void shutdown();

		/** @brief XML 에셋 경로에서 렌더 패스를 로드(또는 캐시 반환)합니다. */
		RenderPassResource* loadRenderPass( string_view assetRelativePath );
		/** @brief XML에서 프레임 파이프라인을 로드(또는 캐시 반환)합니다. */
		RenderPipelineResource* loadPipeline( string_view assetRelativePath );
		/** @brief 로드된 렌더 패스·파이프라인 캐시를 비웁니다. */
		void clearCache();

		/** @brief 이름으로 이미 로드된 렌더 패스를 찾습니다. */
		RenderPassResource* findRenderPass( hashed_string name );
		/** @brief 이름으로 이미 로드된 파이프라인을 찾습니다. */
		RenderPipelineResource* findPipeline( hashed_string name );

	private:
		unordered_map<hashed_string, unique_ptr<RenderPassResource>>	 _mapRenderPass;
		unordered_map<hashed_string, unique_ptr<RenderPipelineResource>> _mapPipeline;
	};
} // namespace sw
