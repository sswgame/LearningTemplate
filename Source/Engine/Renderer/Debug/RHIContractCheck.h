/**
 * @file RHIContractCheck.h
 * @brief RHI 공통 입구의 계약을 살아 있는 장치에서 확인하는 개발 전용 검사입니다(`RHIContractCheck`). 개발 명령 `gpu.rhicontract` 가 요청한다(`RenderDebugRegistry::requestRHIContractCheck`).
 * @details 같은 시나리오를 백엔드마다 돌려(`-scenario-backends=all`) 다섯 백엔드가 같은 답을 내는지 본다. 두 몫이다.
 *          - 거절: 틀린 텍스처 모양 · 배열 텍스처 등록 · UAV 로 만들지 않은 텍스처 · 페이지 밖 구간 · 내용 없는 정점 버퍼 · 이중 해제 · 루트 상수 넘침이 같은 로그 ID 로 남는다.
 *          - 그림: 5x1 대상에 그려 되읽는다 — 가위 뒤의 뷰포트가 가위를 되돌리는가, 넘친 루트 상수가 앞 값을 건드리지 않는가, 파이프라인을 바꾼 뒤 같은 번호를 다시 걸면 걸리는가.
 *          배포 구성(`SW_SHIPPING`)의 렌더러는 이것을 부르지 않는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIPipelineState.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Graphics/RHI/RHITextureSlot.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;
    class RenderDebugRegistry;

    /**
     * @class RHIContractCheck
     * @brief 파일 머리말 참고. 렌더 스레드에서만 쓴다.
     */
    class SW_API RHIContractCheck
    {
    public:
        /** @brief 도착한 그림을 견주어 로그로 남기고, 요청이 있으면 검사 하나를 @p cmd 에 기록합니다. 프레임 업로드 단계 바로 뒤(렌더 패스 · 병렬 기록 밖)에 부른다. */
        void service( IRHICommandList& cmd, IRHIDevice& device, RenderDebugRegistry& registry );
        /** @brief 자원을 놓습니다(렌더러가 장치 자원을 놓을 때). 여러 번 불러도 된다. */
        void release( IRHIDevice* pDevice );

    private:
        /** @brief 검사 하나의 진행입니다. */
        enum class Step : uint8
        {
            Idle,
            Recorded, ///< 그리기를 기록했다 — 그 프레임을 낸 뒤 되읽는다
            Reading,  ///< 되읽기가 도착하기를 기다린다
        };

        [[nodiscard]] bool ensureResources( IRHIDevice& device );
        /** @brief 공통 입구가 거절해야 하는 호출들을 부릅니다. */
        void expectRejections( IRHIDevice& device );
        void recordDraws( IRHICommandList& cmd );
        /** @brief 되읽은 픽셀(RGBA8)을 기대값과 견줍니다. */
        void comparePixels( const vector<uint8>& listByte );
        /** @brief 어긋난 항목이면 오류로 남기고 셉니다. */
        void expect( bool bPassed, const utf8* pWhat );

    private:
        RHITextureSlot                   _target{};
        RHIStructuredBuffer              _firstValues{};
        RHIStructuredBuffer              _secondValues{};
        RHIPipelineState                 _firstPipeline{};
        RHIPipelineState                 _secondPipeline{};
        RHITransferUtil::PendingReadback _readback{};
        uint64                           _recordedFrame{ 0 };
        uint32                           _expectCount{ 0 };
        uint32                           _failureCount{ 0 };
        Step                             _step{ Step::Idle };
    };
} // namespace sw
