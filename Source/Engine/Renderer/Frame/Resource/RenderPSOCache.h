/**
 * @file RenderPSOCache.h
 * @brief FrameRenderer 가 만든 PSO 의 저장소입니다: 엔진 패스 PSO, 출력 포맷별 PSO(Present, Canvas), 머티리얼 퍼뮤테이션 변형과 그 바인딩 레이아웃.
 * @details **만드는 일은 하지 않습니다.** PSO 를 만드는 데는 파이프라인 XML, 디바이스, 씬 배치가 필요하고 그것은 FrameRenderer 의
 *          일입니다(`FramePSOManager`). 여기는 "만든 것을 어디에 두고, 누가 소유하고, 어떤 순서로 해제하는가"만 압니다.
 *          해제 순서(변형 → 패스 PSO)도 이 타입이 지킵니다.
 *
 *          삽입과 조회는 기록 시작 전에만 일어납니다 — 드로우는 기록 전에 푼 표(`FramePSOManager::buildBatchPSOTable`)를 잠금 없이 읽습니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Mesh/MeshVertex.h"
#include "Engine/Graphics/RHI/RHIPipelineState.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayoutCache.h"
#include "Engine/Graphics/View/RenderViewMode.h"
#include "Engine/Renderer/Frame/Resource/MaterialPSOBuilder.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"

namespace sw
{
    class IRHIDevice;
    class ShaderBindingLayout;
    struct ShaderBindingSlot;

    /**
     * @class RenderPSOCache
     * @brief PSO 와 레이아웃의 소유자입니다. 해제 순서와 뮤텍스가 여기 한 곳에 있습니다.
     */
    class SW_API RenderPSOCache
    {
    public:
        /**
         * @enum MaterialPSOState
         * @brief 머티리얼 변형 PSO 하나의 준비 상태입니다(RHIAsyncUploads 2.4).
         * @details "아직 없음" · "만드는 중" · "됐음" · "실패" 를 핸들 0 이나 항목 없음으로 흉내 내지 않고 데이터로 둔다. 드로우 경로는 `Ready` 의 핸들만 쓰고,
         *          `Building` 은 그 배치를 이번 프레임에 그리지 않으며(`RT.PSO.pendingBatches`), `Failed` 는 만들 때 오류를 한 번 남기고 그리지 않는다.
         */
        enum class MaterialPSOState : uint8
        {
            Absent = 0, ///< 항목이 없다(키를 처음 본다). 저장하지 않는 값이다 — 조회가 항목 없음을 이렇게 돌려준다
            Building,   ///< 만드는 중이다(`beginMaterialPSO` 가 올렸다). 핸들이 아직 없다
            Ready,      ///< 만들었고 핸들이 유효하다(`_owned` 가 비면 패스 PSO 그대로)
            Failed,     ///< 만들지 못했다(오류를 남겼다). 핸들이 없고 다시 만들지 않는다
        };

        /**
         * @struct MaterialPSOEntry
         * @brief (패스, 머티리얼 퍼뮤테이션, 뷰 모드) 하나에 대응하는 PSO 와 그 준비 상태입니다. 값은 `building` · `ready` · `failed` 로만 만듭니다.
         * @details `_owned` 가 비고 `_pso` 만 있으면 패스 PSO 그대로입니다. 퍼뮤테이션이 아무것도 안 바꾸는 흔한 경우라
         *          새로 만들지 않습니다 — 소유자가 패스라 이 항목은 놓을 것이 없습니다.
         */
        struct MaterialPSOEntry
        {
            RHIPipelineStateHandle _pso{ 0 }; ///< 그릴 PSO(`_owned` 또는 패스 PSO)
            RHIPipelineState       _owned{};  ///< 이 변형을 위해 만든 PSO. 패스 PSO 를 그대로 가리키면 비어 있다
            MaterialPSOState       _state{ MaterialPSOState::Absent };

            /** @brief 만드는 중인 항목입니다. */
            [[nodiscard]] static MaterialPSOEntry building() { return MaterialPSOEntry{ 0, {}, MaterialPSOState::Building }; }
            /** @brief 만든 항목입니다. @p pso 가 비면 만들지 못한 것이라 `failed()` 입니다. */
            [[nodiscard]] static MaterialPSOEntry ready( RHIPipelineState&& pso )
            {
                const RHIPipelineStateHandle handle = pso._handle;
                return handle != 0 ? MaterialPSOEntry{ handle, std::move( pso ), MaterialPSOState::Ready } : failed();
            }
            /** @brief 패스 PSO 를 그대로 가리키는 항목입니다(소유하지 않는다). 0 이면 `failed()` 입니다. */
            [[nodiscard]] static MaterialPSOEntry makeFromPass( RHIPipelineStateHandle passPSO )
            {
                return passPSO != 0 ? MaterialPSOEntry{ passPSO, {}, MaterialPSOState::Ready } : failed();
            }
            /** @brief 만들지 못한 항목입니다. */
            [[nodiscard]] static MaterialPSOEntry failed() { return MaterialPSOEntry{ 0, {}, MaterialPSOState::Failed }; }
        };

        /** @brief 등록된 PSO 하나와 그 바인딩 레이아웃입니다. `collectLayouts` 가 반환합니다. */
        struct RegisteredLayout
        {
            RHIPipelineStateHandle     _pso{ 0 };
            const ShaderBindingLayout* _pLayout{ nullptr };
        };

        RenderPSOCache();
        ~RenderPSOCache() = default;

        RenderPSOCache( const RenderPSOCache& )            = delete;
        RenderPSOCache& operator=( const RenderPSOCache& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) 바인딩 레이아웃: PSO 를 만든 desc 로 리플렉션 레이아웃을 만들어 핸들에 연결한다
        // ------------------------------------------------------------------------------
        /** @brief PSO 생성 desc 로 레이아웃을 만들고(캐시) 핸들에 연결합니다. 0 핸들은 무시합니다. */
        void registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat );
        /** @brief PSO 핸들의 바인딩 레이아웃입니다. 없으면 nullptr 입니다. */
        const ShaderBindingLayout* findLayout( RHIPipelineStateHandle pso ) const;
        /**
         * @brief PSO 를 만들 때 쓴 디스크립터를 돌려줍니다(셰이더 경로, define, 렌더 상태). 모르는 PSO 면 false 입니다.
         * @details 어떤 퍼뮤테이션이 실제로 걸렸는지 밖에서 볼 수 있는 유일한 방법입니다. 픽셀로는 안 보이는
         *          차이(알파 경로가 컴파일됐는가 같은)를 테스트가 여기서 확인합니다.
         */
        [[nodiscard]] bool findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const;
        /** @brief 레이아웃 없이 desc 만 핸들에 적습니다 — 컴퓨트 PSO(`createComputePipelineState`)가 어느 셰이더로 만들었는지 `releasePSOsUsingShader` 가 알게 한다. 0 핸들은 무시합니다. */
        void registerDesc( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc );
        /**
         * @brief 컴퓨트 PSO 의 @p slot(@p bUAV 면 u 슬롯, 아니면 t 슬롯)이 선언한 구조 버퍼 슬롯입니다. 모르는 PSO · 구조 버퍼가 아닌 슬롯이면 nullptr 입니다(버퍼 보기가 원소 형식을 읽는다).
         * @details 컴퓨트 PSO 는 레이아웃을 등록하지 않으므로(`registerDesc`) 만든 서술로 리플렉션 레이아웃을 짓는다(캐시).
         */
        const ShaderBindingSlot* findComputeBufferSlot( RHIPipelineStateHandle pso, uint32 slot, bool bUAV, ShaderTargetFormat targetFormat );
        /** @brief 등록된 (PSO, 레이아웃) 모두를 복사해 반환합니다. 셋업에서 폴백 버퍼 stride 를 모을 때 씁니다. */
        void collectLayouts( vector<RegisteredLayout>& outListLayout ) const;

        // ------------------------------------------------------------------------------
        // 2) 엔진 패스 PSO 와 출력 포맷별 PSO. Present 와 Canvas 처럼 주 출력에 그리는 패스는 대상 포맷마다 PSO 가 다르다
        // ------------------------------------------------------------------------------
        /** @brief 패스 타입의 엔진 PSO 를 둡니다. 다른 핸들을 덮어쓰면 옛 핸들은 @p pDevice 로 해제한다(`IRHIDevice::releaseHandle` — 쓰던 프레임 뒤로 미룬다). */
        void setEnginePSO( IRHIDevice* pDevice, RenderPassKind passType, RHIPipelineState&& pso );
        /** @brief 패스 타입의 엔진 PSO 입니다. 없으면 0 입니다. */
        RHIPipelineStateHandle findEnginePSO( RenderPassKind passType ) const;
        /** @brief 엔진 PSO 모두입니다. 머티리얼 변형을 만들 때 씬 메시 패스를 고르려고 훑습니다(기록 전). */
        const unordered_map<RenderPassKind, RHIPipelineState>& getEnginePSOs() const { return _mapEnginePSO; }

        /** @brief 주 출력에 그리는 패스(Present, Canvas)의 대상 포맷별 PSO 를 둡니다. 실패(0)도 기록합니다. 부르는 쪽이 폴백으로 갑니다. 덮어쓴 옛 핸들은 `setEnginePSO` 와 같이 해제한다. */
        void setOutputPSO( IRHIDevice* pDevice, RenderPassKind passType, RHIFormat targetFormat, RHIPipelineState&& pso );
        /** @brief 그 패스와 대상 포맷의 PSO 가 **등록돼 있으면** true 와 함께 반환합니다(값이 0 이어도 등록된 것입니다). */
        [[nodiscard]] bool findOutputPSO( RenderPassKind passType, RHIFormat targetFormat, RHIPipelineStateHandle& outPipelineState ) const;

        /** @brief 모듈 확장 패스 프로그램(`ExtensionProgramDesc`) 하나의 PSO 를 프로그램 키(셰이더 · 상태 · 타깃 포맷을 이은 글)로 둡니다. 실패(0)도 기록합니다(만들기를 되풀이하지 않는다). */
        void setExtensionProgramPSO( IRHIDevice* pDevice, const hashed_string& programKey, RHIPipelineState&& pso );
        /** @brief 모듈 프로그램의 PSO 가 **등록돼 있으면** true 와 함께 반환합니다(값이 0 이어도 등록된 것입니다). */
        [[nodiscard]] bool findExtensionProgramPSO( const hashed_string& programKey, RHIPipelineStateHandle& outPipelineState ) const;

        // ------------------------------------------------------------------------------
        // 3) 머티리얼 퍼뮤테이션 변형: 키는 `MaterialPSOKey`(패스 PSO, 퍼뮤테이션 해시, 뷰 모드, 정점 형식, 컬 반전)
        // ------------------------------------------------------------------------------
        /** @brief 그 키의 항목이 어떤 상태로든(`Building` · `Ready` · `Failed`) 이미 있는지 반환합니다. */
        bool hasMaterialPSO( const MaterialPSOKey& key ) const;
        /**
         * @brief 그 키가 처음이면 `Building` 항목을 올리고 true 를 반환합니다. 이미 있으면(만드는 중이든 끝났든) 아무것도 하지 않고 false 입니다.
         * @details 만드는 쪽이 "내가 만든다" 를 한 번에 정하는 자리다 — `hasMaterialPSO` 로 묻고 `setMaterialPSO` 로 넣는 두 걸음은 만드는 스레드가 둘이 되면 같은 키를 두 번 만든다.
         *          true 를 받은 쪽은 `setMaterialPSO( key, ready/failed )` 로 끝낸다.
         */
        [[nodiscard]] bool beginMaterialPSO( const MaterialPSOKey& key );
        /** @brief 변형을 둡니다(있으면 덮어씁니다). `Absent` 항목은 둘 수 없습니다(assert) — 만드는 중이면 `building()`, 끝났으면 `ready` · `failed` 를 넘깁니다. */
        void setMaterialPSO( const MaterialPSOKey& key, MaterialPSOEntry&& entry );
        /** @brief 그 키의 준비 상태입니다. 항목이 없으면 `Absent` 입니다. */
        MaterialPSOState findMaterialPSOState( const MaterialPSOKey& key ) const;
        /**
         * @brief 그 키의 변형 PSO 입니다. `Ready` 가 아니면(없음 · 만드는 중 · 실패) 0 이고, 부르는 쪽은 그 배치를 그리지 않습니다(패스 PSO 로 바꿔 그리면 머티리얼 · 보기 모드가 다른 그림이 된다).
         * @details 0 이 "없음", "만드는 중", "실패" 중 무엇인지는 `findMaterialPSOState` 가 답합니다(`Building` 이면 `RT.PSO.pendingBatches` 로 센다).
         */
        RHIPipelineStateHandle findMaterialPSO( const MaterialPSOKey& key ) const;
        /**
         * @brief `Building` 키 하나의 생성을 빌더 줄에 넣습니다(`beginMaterialPSO` 가 true 를 준 쪽이 부릅니다). 렌더 스레드만 부릅니다.
         * @param bUrgent 이번 프레임 배치가 필요한 키면 true, 미리 만들기면 false 입니다.
         * @param bAsync true 이면 빌더 스레드가, false 이면 `pollMaterialPSOBuilds` 를 부른 스레드가 만듭니다.
         */
        void requestMaterialPSO( IRHIDevice* pDevice, const MaterialPSOKey& key, RHIPipelineStateDesc&& desc, bool bUrgent, bool bAsync );
        /**
         * @brief 빌더가 끝낸 결과를 캐시에 합칩니다(`Building` → `Ready` · `Failed`, 레이아웃 등록 포함). 렌더 스레드가 셋업 구간(기록 전)에만 부릅니다.
         * @param bAsync false 이면 먼저 줄의 작업을 @p budgetMicroseconds 안에서 이 스레드가 만듭니다(0 이면 전부).
         * @return 이번에 `Ready` 로 합친 항목 수입니다(0 보다 크면 호출자가 폴백 버퍼를 다시 점검합니다).
         */
        uint32 pollMaterialPSOBuilds( IRHIDevice* pDevice, bool bAsync, uint32 budgetMicroseconds );
        /** @brief 빌더 줄에 있거나 만드는 중인 작업 수입니다(계측). */
        [[nodiscard]] uint32 getPendingMaterialPSOBuilds() const;

        // ------------------------------------------------------------------------------
        // 4) 수명
        // ------------------------------------------------------------------------------
        /**
         * @brief 모두 파괴하고 비웁니다. 디바이스가 **살아 있을 때** 부릅니다.
         * @details 순서가 규칙입니다: 머티리얼 변형(소유한 것만) → 엔진 PSO → 출력 포맷별 PSO → 레이아웃 테이블.
         *          `_owned` 가 빈 변형은 패스 PSO 를 그대로 가리킬 뿐이라 놓을 것이 없습니다.
         */
        void releaseAll( IRHIDevice* pDevice );
        /** @brief 디바이스가 이미 사라졌을 때 부릅니다. 핸들만 잊습니다. */
        void clearAll();
        /**
         * @brief 셰이더 핫 리로드: 그 셰이더로 만든 PSO 만 파괴하고 비웁니다(레이아웃 캐시도 그 셰이더 것만). 빈 자리는 `FramePSOManager::createPassPSOs` · 지연 생성이 다시 채웁니다.
         * @details 언리얼 셰이더 핫 리로드처럼 바뀐 셰이더를 쓰는 PSO 만 다시 만든다 — 패스 자원(상수 버퍼 · 풀 · 그리기 캐시)은 그대로다.
         *          엔진 패스 PSO 를 하나라도 놓으면 머티리얼 변형을 모두 놓는다: 변형 키에 패스 PSO 핸들이 들어 있어 남기면 쓰이지 않는 채로 쌓이고, `_owned` 가 빈 변형은 놓은 패스 PSO 를 가리킨다.
         *          `Failed` 변형과 실패(0)로 적힌 모듈 프로그램도 지운다 — 고친 셰이더로 다시 만들어 본다.
         *          만드는 중(`Building`)인 변형도 지우고 빌더의 작업을 무효로 한다(`MaterialPSOBuilder::discardPending`) — 옛 바이트코드로 만든 PSO 를 받지 않는다.
         * @return 파괴한 PSO 수입니다.
         */
        uint32 releasePSOsUsingShader( IRHIDevice* pDevice, string_view shaderPath );

    private:
        friend struct RenderPSOCacheTestAccess; ///< 테스트만 안을 본다 — 배포 공개 API 에 진단 조회를 두지 않는다
        /**
         * @brief 덮어쓸 엔진 · 출력 · 모듈 PSO 를 놓습니다 — 비지 않았으면 장치로 해제하고, 그 핸들의 레이아웃 · desc 와 그 핸들을 그대로 가리키던 머티리얼 항목을 잊는다.
         * @details 예전에는 덮어쓰기가 옛 핸들을 잊기만 해 PSO 가 샜다(해제 경로는 `releasePSOsUsingShader` · 장치 종료뿐이었다).
         */
        void releaseReplacedPSO( IRHIDevice* pDevice, RHIPipelineState& previous );
        /** @brief (패스 << 32 | 대상 포맷) 키입니다. */
        static uint64 makeOutputPSOKey( RenderPassKind passType, RHIFormat targetFormat )
        {
            return ( static_cast<uint64>( passType ) << 32 ) | static_cast<uint64>( targetFormat );
        }

    private:
        ShaderBindingLayoutCache                                                        _bindingLayoutCache;
        unordered_map<RHIPipelineStateHandle, const ShaderBindingLayout*> _mapPSOLayout SW_GUARDED_BY( _layoutMutex );
        unordered_map<RHIPipelineStateHandle, RHIPipelineStateDesc> _mapPSODesc         SW_GUARDED_BY( _layoutMutex );
        mutable shared_mutex                                                            _layoutMutex; ///< 기록 워커들이 배치마다 조회만 한다 — 공유 잠금(언리얼 PSO 캐시의 `FRWLock`)
        /// @brief 엔진이 만들어 둔 패스별 PSO 입니다. 키가 enum 이라 조회에 문자열을 만들지 않습니다.
        unordered_map<RenderPassKind, RHIPipelineState> _mapEnginePSO;
        /// @brief 주 출력에 그리는 패스의 PSO 를 대상 렌더 타깃 포맷별로 둡니다(키 = makeOutputPSOKey). 백버퍼와 에디터 뷰 RT 는 포맷이 다를 수 있습니다.
        unordered_map<uint64, RHIPipelineState> _mapOutputPSO;
        /// @brief 모듈 프로그램의 PSO 입니다(키 = 프로그램 키). 모듈 패스는 셰이더가 패스마다 달라 패스 종류 하나로 둘 수 없습니다.
        unordered_map<hashed_string, RHIPipelineState>                                      _mapExtensionProgramPSO;
        unordered_map<MaterialPSOKey, MaterialPSOEntry, MaterialPSOKeyHash> _mapMaterialPSO SW_GUARDED_BY( _materialPSOMutex );
        mutable shared_mutex                                                                _materialPSOMutex; ///< `_layoutMutex` 와 같은 까닭의 공유 잠금
        /// @brief 머티리얼 변형의 생성 줄과 전용 스레드입니다. 캐시가 소유해 미리보기 렌더러와 함께 씁니다(해제는 `releaseAll` 이 스레드부터 합류합니다).
        MaterialPSOBuilder _materialPSOBuilder;
    };
} // namespace sw
