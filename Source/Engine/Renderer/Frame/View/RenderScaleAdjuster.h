/**
 * @file RenderScaleAdjuster.h
 * @brief GPU 프레임 시간이 예산을 깨면 3D 렌더 배율을 한 단계씩 내리고, 여유가 오래 이어지면 한 단계씩 올리는 자동 품질 조정기입니다(게임 스레드).
 * @details 입력은 GPU 프레임 시간(`GPUFrameClock` 의 표본)이다 — 벽시계는 vsync 에 묶여 여유가 서지 않아 내려간 배율이 돌아오지 않았다(언리얼 Dynamic Resolution 도 GPU 시간으로 판단한다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Config/RenderScaleAdjusterConfig.h"
#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Frame/Timeline/GPUFrameClock.h"

namespace sw
{
    /**
     * @class RenderScaleAdjuster
     * @brief 언리얼 Dynamic Resolution · 유니티 Adaptive Performance 의 렌더 배율 쪽에 해당하는 순수 계산입니다. 렌더러 없이 테스트합니다.
     * @details 배율은 1 에서 시작해 `_stepDown` 씩 `_minScale` 까지 내려가고 `_stepUp` 씩 1 까지 올라갑니다. 단계가 바뀐 뒤에는 연속 횟수를 다시 세고,
     *          `_discardSamples` 개의 표본을 버립니다(옛 배율로 그린 프레임의 시간이다). 같은 순번의 표본은 한 번만 셉니다(표본은 몇 프레임 늦고 겹친다).
     *          사용자 설정 배율(`gv_renderScale`)은 따로 곱해지는 값이고 이 조정기는 그것을 바꾸지 않습니다.
     */
    class SW_API RenderScaleAdjuster
    {
    public:
        RenderScaleAdjuster();

        /** @brief 설정이 올바른지 봅니다. 올바르면 빈 문자열, 틀리면 이유입니다. */
        [[nodiscard]] static string validate( const RenderScaleAdjusterConfig& config );
        /** @brief 설정을 넣고 배율을 1 로 되돌립니다. 설정이 틀리면 false 이고 상태는 그대로입니다. */
        [[nodiscard]] bool configure( const RenderScaleAdjusterConfig& config );
        /**
         * @brief 시계의 상태를 봅니다. 타임스탬프가 없는 장치(`Unsupported`)면 배율을 1 로 되돌리고 false 입니다 — 부르는 쪽은 자동 배율을 끈다(벽시계로 대신하지 않는다).
         */
        [[nodiscard]] bool acceptClockState( GPUFrameClockState state );
        /** @brief GPU 프레임 표본을 넣고 현재 배율을 돌려줍니다. 순번 0(아직 없음) · 이미 센 순번은 세지 않습니다. 설정 전에 부르면 1 입니다. */
        float32 update( const GPUFrameSample& sample );
        /** @brief 배율을 1 로 되돌리고 센 횟수를 지웁니다(끌 때, 씬이 바뀔 때). */
        void    reset();
        float32 getScale() const { return _scale; }

    private:
        RenderScaleAdjusterConfig _config;
        float32                   _scale;
        uint32                    _overCount;
        uint32                    _underCount;
        /// @brief 마지막으로 센 표본의 순번입니다(0 = 아직 없음).
        uint32 _lastSerial;
        /// @brief 단계를 바꾼 뒤 아직 버릴 표본 수입니다.
        uint32 _discardCount;
        uint8  _bConfigured : 1;
    };
} // namespace sw
