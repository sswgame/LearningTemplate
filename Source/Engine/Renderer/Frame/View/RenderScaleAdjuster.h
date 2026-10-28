/**
 * @file RenderScaleAdjuster.h
 * @brief 프레임 시간이 예산을 깨면 3D 렌더 배율을 한 단계씩 내리고, 여유가 오래 이어지면 한 단계씩 올리는 자동 품질 조정기입니다(게임 스레드).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    /**
     * @struct RenderScaleAdjusterConfig
     * @brief 조정기의 설정입니다. 맞지 않으면 `RenderScaleAdjuster::configure` 가 false 를 돌려줍니다(기본값으로 대신하지 않는다).
     */
    struct RenderScaleAdjusterConfig
    {
        float32 _budgetMs{ 16.7f };     ///< 프레임 시간 예산(ms)
        float32 _minScale{ 0.5f };      ///< 배율의 아래 한계(0 초과 1 이하)
        float32 _stepDown{ 0.1f };      ///< 예산을 깰 때 한 번에 내리는 배율(0 초과)
        float32 _stepUp{ 0.05f };       ///< 여유가 이어질 때 한 번에 올리는 배율(0 초과). 내림보다 작아야 진동하지 않는다
        float32 _overRatio{ 1.05f };    ///< 프레임 시간이 예산의 이 배수를 넘으면 "깼다"(수직동기 프레임 시간의 흔들림 여유, 1 이상)
        float32 _headroomRatio{ 0.8f }; ///< 프레임 시간이 예산의 이 배수 아래이면 "여유"(0 초과 1 미만)
        uint32  _downFrames{ 6 };       ///< 연속으로 깬 프레임이 이 수가 되면 내린다(1 이상)
        uint32  _upFrames{ 120 };       ///< 연속으로 여유인 프레임이 이 수가 되면 올린다(1 이상)
    };
} // namespace sw

namespace sw
{
    /**
     * @class RenderScaleAdjuster
     * @brief 언리얼 Dynamic Resolution · 유니티 Adaptive Performance 의 렌더 배율 쪽에 해당하는 순수 계산입니다. 렌더러 없이 테스트합니다.
     * @details 배율은 1 에서 시작해 `_stepDown` 씩 `_minScale` 까지 내려가고 `_stepUp` 씩 1 까지 올라갑니다. 단계가 바뀐 뒤에는 연속 횟수를 다시 셉니다.
     *          사용자 설정 배율(`gv_renderScale`)은 따로 곱해지는 값이고 이 조정기는 그것을 바꾸지 않습니다.
     */
    class SW_API RenderScaleAdjuster
    {
    public:
        /** @brief 설정이 올바른지 봅니다. 올바르면 빈 문자열, 틀리면 이유입니다. */
        [[nodiscard]] static string validate( const RenderScaleAdjusterConfig& config );
        /** @brief 설정을 넣고 배율을 1 로 되돌립니다. 설정이 틀리면 false 이고 상태는 그대로입니다. */
        [[nodiscard]] bool configure( const RenderScaleAdjusterConfig& config );
        /** @brief 이번 프레임의 시간(ms)을 넣고 현재 배율을 돌려줍니다. 설정 전에 부르면 1 입니다. */
        float32 update( float32 frameMs );
        /** @brief 배율을 1 로 되돌리고 센 횟수를 지웁니다(끌 때, 씬이 바뀔 때). */
        void    reset();
        float32 getScale() const { return _scale; }

    private:
        RenderScaleAdjusterConfig _config{};
        float32                   _scale{ 1.0f };
        uint32                    _overCount{ 0 };
        uint32                    _underCount{ 0 };
        bool                      _bConfigured{ false };
    };
} // namespace sw
