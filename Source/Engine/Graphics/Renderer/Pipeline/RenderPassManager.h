/**
 * @file RenderPassManager.h
 * @brief RenderPass(어태치먼트 · RHI)와 RenderPipeline(프레임 그래프) XML 에셋 캐시입니다.
 * @note FrameRenderer 가 소유합니다(예전에는 IRHIDevice 가 소유했습니다). 렌더러 수명을 따르므로 ResourceManager 에 넣지 않습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    class RenderPassResource;
    class RenderPipelineResource;

    /// @brief FrameRenderer 가 소유하는 렌더 패스 · 파이프라인 에셋 캐시입니다.
    class SW_API RenderPassManager
    {
    public:
        /** @brief 빈 매니저로 만듭니다. */
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
        /** @brief XML 에서 프레임 파이프라인을 로드(또는 캐시 반환)합니다. */
        RenderPipelineResource* loadPipeline( string_view assetRelativePath );
        /** @brief 로드된 렌더 패스 · 파이프라인 캐시를 비웁니다. */
        void clearCache();

        /** @brief 이름으로 이미 로드된 렌더 패스를 찾습니다. */
        RenderPassResource* findRenderPass( hashed_string name );
        /** @brief 이름으로 이미 로드된 파이프라인을 찾습니다. */
        RenderPipelineResource* findPipeline( hashed_string name );

    private:
        unordered_map<hashed_string, unique_ptr<RenderPassResource>>     _mapRenderPass;
        unordered_map<hashed_string, unique_ptr<RenderPipelineResource>> _mapPipeline;
        /// @brief 파일 경로 → 이미 로드된 리소스입니다. 같은 파이프라인이 같은 RenderPass XML 을 여러 번
        /// 참조할 때, 이름 기준 캐시를 보기 전에 경로로 먼저 확인해 XML 을 매번 다시 파싱하지 않습니다.
        unordered_map<string, RenderPassResource*>     _mapPathToRenderPass;
        unordered_map<string, RenderPipelineResource*> _mapPathToPipeline;
    };
} // namespace sw
