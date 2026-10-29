/**
 * @file FramePassContext.h
 * @brief 패스 하나를 기록하는 동안 쓰는 로컬 상태(`FramePassContext`)입니다. 렌더러와 그 협력 객체(패스 실행 · 후처리 · 확장 패스)가 함께 씁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Frame/Resource/FrameResourceRegistry.h"
#include "Engine/Renderer/Frame/Resource/PassConstantValues.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"

namespace sw
{
    class IRHICommandList;
    class ShaderBindingLayout;

    /**
     * @brief 패스 하나를 기록하는 동안 쓰는 로컬 상태입니다.
     * @details 병렬 기록에서는 패스마다 하나씩 존재한다. 주의: 이 값들을 FrameRenderer 멤버로 두고
     *          저장하고 복원하면 "한 번에 한 패스만 돈다" 는 전제가 생기고, 병렬 기록에서 패스끼리 값을 덮어쓴다.
     *          상수 버퍼도 패스마다 따로 있어야 한다. 하나를 공유하면 기록은 지연되고
     *          버퍼 쓰기는 즉시 일어나므로, replay 시점에는 마지막으로 쓴 값만 남는다.
     */
    struct FramePassContext
    {
        IRHICommandList* _pCmd{ nullptr };
        /** @brief 엔진 PassCB 값입니다(이름 기반). ShaderParameterBinder 가 리플렉션 오프셋에 기록합니다. */
        PassConstantValues _passValues{};
        /** @brief `g_World` 입니다. 인스턴스 버퍼가 없는 드로우(풀스크린, 픽스처)의 월드 행렬이며, 씬 메시는 인스턴스 버퍼에서 읽습니다. */
        float4x4           _world{};
        RHIBufferHandle    _passCb{ 0 };
        RHIDescriptorIndex _passCbIndex{ kInvalidDescriptorIndex };
        /** @brief 패스 스코프 이름에서 리소스를 찾는 레지스트리입니다. 패스를 시작할 때마다 reset 합니다.
         *         병렬 기록에서는 패스마다 독립이어야 하므로 FrameRenderer 의 공유 멤버가 아니라 여기에 둡니다. */
        FrameResourceRegistry _resourceRegistry{};
        /// @brief 엔진 상수 버퍼를 마지막으로 올린 시점의 버퍼, 값 버전, 레지스트리 버전입니다.
        ///        셋이 그대로면 그 드로우는 버퍼를 다시 올릴 필요가 없습니다.
        RHIBufferHandle _lastCbBuffer{ 0 };
        uint32          _lastCbValuesVersion{ 0 };
        uint32          _lastCbRegistryVersion{ 0 };
        /**
         * @brief 마지막으로 리소스를 **실제로 바인딩한** PSO 입니다.
         * @details 슬롯 상태는 PSO 단위다. `setPipelineState` 는 이전 PSO 가 바인딩한 t/u 슬롯이 다음 PSO 로
         *          새지 않게 슬롯 상태를 통째로 비운다. 그래서 값과 레지스트리가 그대로여도 PSO 가 바뀌었으면
         *          다시 바인딩해야 한다. 이를 빼먹으면 배치가 퍼뮤테이션 PSO 로 바뀐 순간 t5(g_SwMaterials)
         *          가 **바인딩되지 않은 채** 드로우가 나가고, Vulkan 은 초기화되지 않은 디스크립터를 읽어
         *          디바이스를 잃는다(GPU-AV: "binding 25 Descriptor index 0 is uninitialized").
         */
        RHIPipelineStateHandle _lastBindPSO{ 0 };
        /**
         * @brief 이 패스가 어느 컬링 슬롯의 결과를 쓸지 정합니다. 0은 주 시점, 1은 그림자이고, `kFirstExtraCullView` 부터는 추가 뷰입니다.
         * @details 그림자 패스만 그림자 슬롯을 쓰고, 나머지는 지금 그리는 뷰의 슬롯을 쓴다. 컬링 결과는 절두체에 종속되므로
         *          뷰를 잘못 고르면 그림자 캐스터가 사라지거나 화면 밖 물체를 그린다.
         */
        uint32 _cullViewIndex{ 0 };
        /// @brief 지금 기록하는 패스의 종류입니다. `executePass` 가 정하며, 드로우 루프가 이 패스에서 그리지 않는 머티리얼의 배치를 거를 때 봅니다.
        RenderPassType _passType{ RenderPassType::Invalid };
        /// @brief 지금 기록하는 패스의 파이프라인 이름입니다(진단 로그용 — bindless 인덱스 검사가 어느 패스인지 적는다). 패스 밖이면 비어 있습니다.
        hashed_string _passName;
        /// @brief 이 드로우 그룹의 루트 상수 값(머티리얼 원소 수)입니다. 배치마다 다른 값(인스턴스 시작, 모프 풀, 정점 풀)은
        ///        배치 테이블(g_SwBatches)과 인스턴스 슬롯 스트림이 제공하므로 그룹 안에서 루트 상수를 다시 바인딩하지 않습니다.
        ///        PassCB 에 넣으면 한 패스의 드로우들이 서로를 덮어씁니다(binding.hlsli "드로우별 루트 상수" 절 참고).
        uint32 _drawMaterialCount{ 0 };
        /// @brief bindForDraw 가 마지막으로 조회한 PSO 와 그 레이아웃입니다. 같은 PSO 로 연속해서 드로우할 때
        ///        layoutForPSO() 의 뮤텍스와 해시맵 조회를 건너뛰는 패스 로컬 캐시입니다(1항목).
        RHIPipelineStateHandle     _lastLayoutPSO{ 0 };
        const ShaderBindingLayout* _pLastLayout{ nullptr };

        /**
         * @brief "마지막으로 바인딩한 것" 캐시를 모두 지웁니다. PSO 와 버퍼 핸들이 무효가 되는 시점(디바이스 교체)에 부릅니다.
         * @details 핸들 값은 **디바이스 안에서만** 식별자 역할을 한다. 새 디바이스의 PSO 가 옛 디바이스의 PSO 와 같은 값을
         *          받을 수 있고(둘 다 첫 PSO 가 같은 번호), 그러면 `_lastLayoutPSO == pso` 가 참이 되어 이미 파괴된
         *          레이아웃(`_pLastLayout`)을 쓰고 리소스 재바인딩도 건너뛴다. 결과적으로 백엔드를 교체한 뒤
         *          아무것도 그려지지 않는다. 파괴할 때 캐시도 함께 지워야 "같은 값은 같은 대상" 이 성립한다.
         */
        void resetBindingCache()
        {
            _lastCbBuffer          = 0;
            _lastCbValuesVersion   = 0;
            _lastCbRegistryVersion = 0;
            _lastBindPSO           = 0;
            _lastLayoutPSO         = 0;
            _pLastLayout           = nullptr;
        }
    };
} // namespace sw
