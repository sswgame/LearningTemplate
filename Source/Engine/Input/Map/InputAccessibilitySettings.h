/**
 * @file InputAccessibilitySettings.h
 * @brief 플레이어마다의 입력 설정(감도 · 축 반전 · 스틱 데드존 · 대각선 정규화 · 토글 액션)을 선언합니다.
 * @details 맵 정의(바인딩 · 레이어)와 수명이 달라 맵을 비우거나 다시 읽어도 남는다. 사용자 설정 처리기(`UserSettingsEngineHandlers`)가 채우고 `InputMap` 이 평가에 읽는다.
 *          언리얼 `UEnhancedInputUserSettings` 의 플레이어 설정 자리이고, 토글은 언리얼 `UInputTriggerToggle` 처럼 바인딩이 아니라 액션에 건다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    enum class DeadzoneShape : uint8
    {
        Radial = 0, ///< 360도 원형 거리 기반 데드존(자유 이동).
        Axial       ///< X/Y 독립 십자형 데드존(4방향 그리드, 플랫포머).
    };

    enum class DigitalNormalization : uint8
    {
        Circular = 0,   ///< 대각선 입력의 반경을 1.0 으로 정규화한다(표준 3D/FPS 액션).
        IndependentAxes ///< X/Y 를 각각 독립적으로 1.0 까지 유지한다(2D 그리드, 탑다운).
    };
} // namespace sw

namespace sw
{
    /**
     * @class InputAccessibilitySettings
     * @brief 플레이어마다의 입력 설정입니다. 값 칸은 그대로 읽고 쓰고, 토글 액션 목록만 함수로 바꿉니다.
     */
    class SW_API InputAccessibilitySettings
    {
    public:
        float2               _mouseSensitivity{ 1.0f, 1.0f };
        float2               _gamepadSensitivity{ 1.0f, 1.0f };
        float32              _stickDeadzoneOverride{ -1.0f }; ///< 게임패드 스틱 바인딩의 안쪽 데드존을 덮어쓴다. 0 미만이면 바인딩마다의 값(`ActionBinding::_deadzone`)
        DeadzoneShape        _deadzoneShape{ DeadzoneShape::Radial };
        DigitalNormalization _digitalNormalization{ DigitalNormalization::Circular };
        bool                 _bInvertX{ false };
        bool                 _bInvertY{ false };

    public:
        /**
         * @brief 액션 @p action 을 토글 액션(한 번 눌러 켜고 다시 눌러 끈다 — 조준 · 달리기)으로 두거나 뺍니다.
         * @details 액션이 아직 없어도 된다. 맵이 그 이름의 액션을 만들면 그때부터 토글로 평가한다(`InputMap::isActionToggled`).
         */
        void setToggleAction( const hashed_string& action, bool bToggle );
        bool isToggleAction( const hashed_string& action ) const;

    private:
        vector<hashed_string> _listToggleAction; ///< 토글 액션 이름. 몇 개뿐이라 선형으로 찾는다
    };
} // namespace sw
