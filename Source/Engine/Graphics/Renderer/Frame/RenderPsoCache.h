/**
 * @file RenderPsoCache.h
 * @brief FrameRenderer 가 만든 PSO 들의 저장소입니다: 엔진 패스 PSO · Present 포맷별 PSO · 머티리얼 퍼뮤테이션 변형과 그 바인딩 레이아웃.
 * @details **만드는 일은 하지 않습니다.** PSO 를 만드는 데는 파이프라인 XML · 디바이스 · 씬 배치가 필요하고 그것은 FrameRenderer 의
 *          일입니다(FrameRendererPso.cpp). 여기는 "만든 것을 어디에 두고, 누가 소유하고, 어떤 순서로 놓는가" 만 압니다.
 *          그 세 가지가 예전에 FrameRenderer 멤버 일곱 개와 뮤텍스 둘에 흩어져 있어, 해제 순서(변형 → 패스 PSO)가
 *          한 함수 안의 주석으로만 지켜졌습니다.
 *
 *          드로우 경로가 배치마다 조회하므로 조회는 락 하나로 짧게 끝납니다. 삽입은 기록 시작 전에만 일어납니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayoutCache.h"

namespace sw
{
    class IRHIDevice;
    class ShaderBindingLayout;

    /**
     * @class RenderPsoCache
     * @brief PSO 핸들과 레이아웃의 소유자입니다. 해제 순서와 뮤텍스가 여기 한 곳에 있습니다.
     */
    class SW_API RenderPsoCache
    {
    public:
        /**
         * @struct MaterialPsoEntry
         * @brief (패스, 머티리얼 퍼뮤테이션, 뷰 모드) 하나에 대응하는 PSO 입니다.
         * @details `_bOwned` 가 0 이면 값은 패스 PSO 그대로입니다. 퍼뮤테이션이 아무것도 안 바꾸는 흔한 경우라
         *          새로 만들지 않습니다. 그래서 파괴할 때 **이 PSO 는 건드리면 안 됩니다**(패스가 소유합니다).
         */
        struct MaterialPsoEntry
        {
            RHIPipelineStateHandle _pso{ 0 };
            uint8                  _bOwned{ 0 };
        };

        /** @brief 등록된 PSO 하나와 그 바인딩 레이아웃입니다. `collectLayouts` 가 반환합니다. */
        struct RegisteredLayout
        {
            RHIPipelineStateHandle     _pso{ 0 };
            const ShaderBindingLayout* _pLayout{ nullptr };
        };

        RenderPsoCache();
        ~RenderPsoCache() = default;

        RenderPsoCache( const RenderPsoCache& )            = delete;
        RenderPsoCache& operator=( const RenderPsoCache& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) 바인딩 레이아웃: PSO 를 만든 desc 로 리플렉션 레이아웃을 만들어 핸들에 매핑한다
        // ------------------------------------------------------------------------------
        /** @brief PSO 생성 desc 로 레이아웃을 만들고(캐시) 핸들에 매핑합니다. 0 핸들은 무시합니다. */
        void registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc, RHIBackend backend );
        /** @brief PSO 핸들의 바인딩 레이아웃입니다. 없으면 nullptr 입니다. */
        const ShaderBindingLayout* findLayout( RHIPipelineStateHandle pso ) const;
        /**
         * @brief PSO 를 만들 때 쓴 디스크립터를 돌려줍니다(셰이더 경로 · define · 렌더 상태). 모르는 PSO 면 false 입니다.
         * @details 어떤 퍼뮤테이션이 실제로 걸렸는지 밖에서 볼 수 있는 유일한 창입니다. 픽셀로는 안 보이는
         *          차이(알파 경로가 컴파일됐는가 같은)를 테스트가 여기서 확인합니다.
         */
        bool findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const;
        /** @brief 셰이더 핫 리로드: 그 경로로 만든 레이아웃 캐시를 버립니다(PSO 는 부르는 쪽이 다시 만듭니다). */
        void invalidateLayoutsByShaderPath( string_view shaderPath );
        /** @brief 등록된 (PSO, 레이아웃) 모두를 복사해 반환합니다. 셋업에서 폴백 버퍼 stride 를 모을 때 씁니다. */
        void collectLayouts( vector<RegisteredLayout>& outListLayout ) const;

        // ------------------------------------------------------------------------------
        // 2) 엔진 패스 PSO · Present 포맷별 PSO
        // ------------------------------------------------------------------------------
        /** @brief 패스 타입의 엔진 PSO 를 둡니다(있으면 덮어씁니다). */
        void setEnginePso( RenderPassType passType, RHIPipelineStateHandle pso );
        /** @brief 패스 타입의 엔진 PSO 입니다. 없으면 0 입니다. */
        RHIPipelineStateHandle findEnginePso( RenderPassType passType ) const;
        /** @brief 엔진 PSO 모두입니다. 머티리얼 변형을 만들 때 씬 메시 패스를 고르려고 훑습니다(기록 전). */
        const unordered_map<RenderPassType, RHIPipelineStateHandle>& getEnginePsos() const { return _mapEnginePso; }

        /** @brief Present 대상 포맷의 PSO 를 둡니다. 실패(0)도 기록합니다. 부르는 쪽이 blit 폴백으로 갑니다. */
        void setPresentPso( RHIFormat targetFormat, RHIPipelineStateHandle pso );
        /** @brief Present 대상 포맷의 PSO 가 **등록돼 있으면** true 와 함께 반환합니다(값이 0 이어도 등록된 것입니다). */
        bool findPresentPso( RHIFormat targetFormat, RHIPipelineStateHandle& outPso ) const;

        // ------------------------------------------------------------------------------
        // 3) 머티리얼 퍼뮤테이션 변형: 키는 (패스 PSO, 퍼뮤테이션 해시, 뷰 모드)
        // ------------------------------------------------------------------------------
        /**
         * @brief 변형 캐시 키입니다.
         * @details 뷰 모드가 키의 한 축입니다. 같은 머티리얼이라도 Lit 와 Wireframe 은 다른 PSO 이고,
         *          모드를 되돌리면 이미 만들어 둔 것이 다시 나옵니다(다시 컴파일하지 않습니다).
         */
        static uint64 materialPsoKey( RHIPipelineStateHandle passPso, uint64 permutationHash, RenderViewMode viewMode );
        /** @brief 그 키의 변형이 이미 있는지 반환합니다. */
        bool hasMaterialPso( uint64 key ) const;
        /** @brief 변형을 둡니다(있으면 덮어씁니다). */
        void setMaterialPso( uint64 key, const MaterialPsoEntry& entry );
        /** @brief 그 키의 변형 PSO 입니다. 없거나 0 이면 0 이고, 부르는 쪽은 패스 PSO 로 그립니다. */
        RHIPipelineStateHandle findMaterialPso( uint64 key ) const;

        // ------------------------------------------------------------------------------
        // 4) 수명
        // ------------------------------------------------------------------------------
        /**
         * @brief 모두 파괴하고 비웁니다. 디바이스가 **살아 있을 때** 부릅니다.
         * @details 순서가 규칙입니다: 머티리얼 변형(소유한 것만) → 엔진 PSO → Present PSO → 레이아웃 표.
         *          `_bOwned` 가 0 인 변형은 패스 PSO 를 그대로 담고 있을 뿐이라 먼저 걸러야 두 번 파괴하지 않습니다.
         */
        void releaseAll( IRHIDevice* pDevice );
        /** @brief 디바이스가 이미 사라졌을 때 부릅니다. 핸들만 잊습니다. */
        void forgetAll();

    private:
        ShaderBindingLayoutCache                                          _bindingLayoutCache;
        unordered_map<RHIPipelineStateHandle, const ShaderBindingLayout*> _mapPsoLayout;
        unordered_map<RHIPipelineStateHandle, RHIPipelineStateDesc>       _mapPsoDesc;
        mutable mutex                                                     _layoutMutex;
        /// @brief 엔진이 만들어 둔 패스별 PSO 입니다. 예전에는 string 키라 조회마다 string 을 만들었습니다.
        unordered_map<RenderPassType, RHIPipelineStateHandle> _mapEnginePso;
        /// @brief Present PSO 를 대상 렌더 타깃 포맷별로 둡니다. 백버퍼와 GameView RT 는 포맷이 다를 수 있습니다.
        unordered_map<RHIFormat, RHIPipelineStateHandle> _mapPresentPso;
        unordered_map<uint64, MaterialPsoEntry>          _mapMaterialPso;
        mutable mutex                                    _materialPsoMutex;
    };
} // namespace sw
