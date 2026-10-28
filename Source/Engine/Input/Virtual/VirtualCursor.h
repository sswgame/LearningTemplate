/**
 * @file VirtualCursor.h
 * @brief 게임패드 스틱으로 마우스 커서를 움직이는 가상 커서입니다(언리얼 CommonUI 의 아날로그 커서, Steam Input 의 마우스 에뮬레이션).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/InputViewRect.h"
#include "Engine/Input/RawInputEvent.h"

namespace sw
{
    class GamepadDevice;

    /** @brief 가상 커서의 설정입니다. */
    struct VirtualCursorSettings
    {
        bool          _bRightStick{ true };                 ///< true 면 오른쪽 스틱, false 면 왼쪽 스틱으로 커서를 움직인다.
        uint8         _padIndex{ 0 };                       ///< 읽는 게임패드 번호
        float32       _speed{ 900.0f };                     ///< 스틱을 끝까지 밀었을 때의 속도(픽셀/초)
        float32       _deadzone{ 0.2f };                    ///< 스틱 안쪽 데드존(반경)
        float32       _exponent{ 2.0f };                    ///< 응답 곡선 지수. 1 보다 크면 스틱을 조금 밀었을 때 정밀하게 움직인다.
        GamepadButton _primaryButton{ GamepadButton::A };   ///< 마우스 왼쪽 버튼으로 바꿀 패드 버튼
        GamepadButton _secondaryButton{ GamepadButton::B }; ///< 마우스 오른쪽 버튼으로 바꿀 패드 버튼
    };
} // namespace sw

namespace sw
{
    /**
     * @class VirtualCursor
     * @brief 패드 스틱과 버튼을 마우스 이동 · 버튼 이벤트로 바꿔 `InputManager` 가 OS 이벤트와 같은 경로로 적용하게 합니다.
     * @details 명령형 게임(RTS · 도시 건설)이 패드만으로 커서를 쓰게 한다. UI 호버와 월드 피킹은 마우스 위치만 읽으므로 따로 고칠 곳이 없다.
     *          꺼 둔 채 시작하고(`setEnabled`), 켠 게임이 패드 A · B 를 커서 버튼으로 쓴다. 실제 마우스가 움직이면 그 위치에서 이어 움직인다.
     */
    class SW_API VirtualCursor
    {
    public:
        void                         setEnabled( bool bEnabled );
        bool                         isEnabled() const { return _bEnabled; }
        void                         setSettings( const VirtualCursorSettings& settings ) { _settings = settings; }
        const VirtualCursorSettings& getSettings() const { return _settings; }

        /**
         * @brief 스틱 한 프레임 분의 이동 속도(픽셀/초)입니다. 데드존 밖을 0~1 로 다시 펴고 응답 곡선을 건다. 화면 아래가 +y 입니다.
         * @param stick 스틱 원시값. 위가 +y 입니다.
         */
        float2 computeVelocity( float2 stick ) const;

        /**
         * @brief 이번 프레임의 마우스 이동 · 버튼 이벤트를 @p outListEvent 끝에 더합니다. 꺼져 있거나 패드가 없거나 연결되지 않았으면 아무것도 하지 않습니다.
         * @param pPad 스틱과 버튼을 읽을 패드입니다.
         * @param mousePosition 지금 마우스 위치입니다. 지난 프레임에 낸 위치와 다르면 실제 마우스가 움직인 것이라 거기서 이어 움직입니다.
         * @param view 커서가 머무는 영역 — 게임 화면의 창 안 사각형입니다(`InputManager::getGameViewRect`, 창 전체가 아니다).
         * @param deltaSeconds 프레임 시간입니다.
         */
        void emitFrame( const GamepadDevice* pPad, int2 mousePosition, const InputViewRect& view, float32 deltaSeconds, vector<RawInputEvent>& outListEvent );

        /** @brief 가장 최근 `emitFrame` 이 이벤트를 냈는지 묻습니다. 패드가 낸 마우스 이벤트를 "마우스를 만졌다" 로 읽지 않게 하는 데 씁니다. */
        bool didEmitLastFrame() const { return _bEmittedLastFrame; }

    private:
        VirtualCursorSettings _settings{};
        float2                _position{ 0.0f, 0.0f };
        int2                  _lastEmitted{ 0, 0 };
        bool                  _bEnabled{ false };
        bool                  _bHasEmitted{ false };
        bool                  _bPrimaryDown{ false };
        bool                  _bSecondaryDown{ false };
        bool                  _bEmittedLastFrame{ false };
    };
} // namespace sw
