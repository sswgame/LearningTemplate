/**
 * @file MouseDevice.h
 * @brief 표준 마우스 입력 장치입니다(버튼, 좌표, 1:1 원시 델타, 휠, 커서 모드).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
    /** @brief 마우스 커서 잠금과 클리핑 모드입니다. */
    enum class MouseLockMode : uint8
    {
        None = 0,
        ConfinedToWindow,
        LockedInCenter
    };

    /**
     * @class MouseDevice
     * @brief 마우스 버튼 · 좌표 · 센서 델타 · 커서 상태를 맡는 IInputDevice 구현입니다.
     */
    class SW_API MouseDevice : public IInputDevice
    {
    public:
        MouseDevice();
        virtual ~MouseDevice() override = default;

        MouseDevice( const MouseDevice& )            = delete;
        MouseDevice& operator=( const MouseDevice& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) IInputDevice 수명주기
        // ------------------------------------------------------------------------------
        InputDeviceKind getDeviceKind() const override { return InputDeviceKind::Mouse; }
        string_view     getDeviceName() const override { return "Mouse"; }
        bool            isConnected() const override { return true; }

        void poll( float32 deltaTime ) override;
        void onFrameBegin( float32 deltaTime ) override;
        void onFrameEnd() override;
        void resetState() override;

        bool    isControlDown( uint16 controlIndex ) const override;
        bool    wasControlPressed( uint16 controlIndex ) const override;
        bool    wasControlReleased( uint16 controlIndex ) const override;
        float32 getControlValue( uint16 controlIndex ) const override;

        // ------------------------------------------------------------------------------
        // 2) 마우스 전용 쿼리
        // ------------------------------------------------------------------------------
        bool isButtonDown( MouseButton button ) const;
        bool wasButtonPressed( MouseButton button ) const;
        bool wasButtonReleased( MouseButton button ) const;
        bool wasAnyButtonPressed() const { return _pressedMask != 0; }

        int2    getPosition() const { return _mouse; }
        int32   getPositionX() const { return _mouse._x; }
        int32   getPositionY() const { return _mouse._y; }
        int2    getDelta() const { return _delta; }
        float2  getRawDelta() const { return _rawDelta; }
        float2  getSmoothDelta() const { return _smoothDelta; }
        float32 getSmoothing() const { return _smoothingFactor; }
        void    setSmoothing( float32 factor ) { _smoothingFactor = factor < 0.0f ? 0.0f : ( factor > 0.99f ? 0.99f : factor ); }
        float32 getAcceleration() const { return _accelerationPower; }
        void    setAcceleration( float32 power ) { _accelerationPower = power < 1.0f ? 1.0f : power; }

        float32 getMouseWheel() const { return _mouseWheelDelta; }
        float32 getMouseWheelHorizontal() const { return _mouseWheelHorizontalDelta; }

        bool isPointerInside() const { return _bPointerInside == SW_TRUE; }
        bool wasPointerEntered() const { return _bPointerEntered == SW_TRUE; }
        bool wasPointerLeft() const { return _bPointerLeft == SW_TRUE; }

        MouseLockMode getLockMode() const { return _lockMode; }
        void          setLockMode( MouseLockMode mode ) { _lockMode = mode; }
        bool          isCursorVisible() const { return _bCursorVisible == SW_TRUE; }
        void          setCursorVisible( bool bVisible ) { _bCursorVisible = bVisible ? SW_TRUE : SW_FALSE; }

        void setClipSubRect( int32 left, int32 top, int32 right, int32 bottom )
        {
            _clipSubRectLeft   = left;
            _clipSubRectTop    = top;
            _clipSubRectRight  = right;
            _clipSubRectBottom = bottom;
            _bHasSubRect       = SW_TRUE;
        }
        void clearClipSubRect()
        {
            _clipSubRectLeft   = 0;
            _clipSubRectTop    = 0;
            _clipSubRectRight  = 0;
            _clipSubRectBottom = 0;
            _bHasSubRect       = SW_FALSE;
        }
        bool hasClipSubRect() const { return _bHasSubRect == SW_TRUE; }
        bool getClipSubRect( int32& outLeft, int32& outTop, int32& outRight, int32& outBottom ) const
        {
            outLeft   = _clipSubRectLeft;
            outTop    = _clipSubRectTop;
            outRight  = _clipSubRectRight;
            outBottom = _clipSubRectBottom;
            return _bHasSubRect == SW_TRUE;
        }

        // ------------------------------------------------------------------------------
        // 3) OS 이벤트 처리기
        // ------------------------------------------------------------------------------
        void setButtonDown( MouseButton button, bool bDown );
        void setPosition( int32 x, int32 y );
        void addRawDelta( float32 dx, float32 dy );
        void addWheelDelta( float32 delta );
        void addHorizontalWheelDelta( float32 delta );
        void setPointerInsideState( bool bInside );

    private:
        /**
         * @brief 델타에 **가속 곡선과 EMA 스무딩**을 적용해 `_smoothDelta` 를 갱신합니다.
         * @details 이 계산이 `poll()` 에도 **글자까지 같은 사본**으로 들어 있었습니다. 마우스 감각을
         *          조정하는 사람이 한쪽만 고치면 **입력 경로에 따라 감각이 달라집니다.** 원시 입력이
         *          오는 기계와 안 오는 기계가 서로 다르게 움직이고, 테스트는 부호만 보므로 잡히지 않습니다.
         *
         * @note **아직 정하지 못한 것: 한 프레임에 여러 번 적용됩니다.** `addRawDelta` · `setPosition` 이
         *       입력 이벤트마다 이것을 부르고, `poll()` 이 프레임당 한 번 더 부릅니다(그때는 프레임 시작
         *       시점의 위치 차이 `_delta` 로). 즉 EMA 가 프레임당 "이벤트 수 + 1" 번 돌아서 **스무딩 양이
         *       마우스 폴링 레이트에 따라 달라집니다.** 1000Hz 와 125Hz 가 다른 감각이 된다는 뜻입니다.
         *       여기서는 **동작을 바꾸지 않았습니다.** 감각을 재려면 실제로 마우스를 움직여 봐야 하고
         *       그것은 자동 검증이 안 됩니다. 손에 마우스를 쥔 사람이 정할 일입니다.
         */
        void updateSmoothDelta( float32 dx, float32 dy );

        static constexpr size_t kButtonCount = static_cast<size_t>( MouseButton::Count );

        int2                   _mouse;                     /**< 현재 프레임의 마우스 화면 좌표(창 클라이언트 기준). */
        int2                   _prevMouse;                 /**< 직전 프레임의 마우스 좌표. 델타 계산에 씀. */
        int2                   _delta;                     /**< 이번 프레임의 좌표 이동량(_mouse - _prevMouse). 화면 경계에 막히면 실제 이동보다 작음. */
        float2                 _rawDelta;                  /**< OS 원시(Raw Input) 델타 누적값. 화면 경계에 막히지 않는 실제 이동량(FPS 카메라 룩에 알맞음). */
        float2                 _smoothDelta;               /**< 가속 · 스무딩(EMA)을 적용한 최종 델타. getSmoothDelta() 가 반환하는 값. */
        float32                _smoothingFactor;           /**< EMA 스무딩 계수 [0.0, 0.99]. 0 이면 스무딩 없이 델타를 그대로 씀. */
        float32                _accelerationPower;         /**< 마우스 가속 지수. 1.0 이면 가속 없음. 클수록 빠르게 움직일 때 델타가 더 커짐. */
        float32                _mouseWheelDelta;           /**< 이번 프레임 세로 휠 회전량. getMouseWheel() 이 반환하는 값. */
        float32                _mouseWheelHorizontalDelta; /**< 이번 프레임 가로 휠(틸트) 회전량. */
        int32                  _clipSubRectLeft;           /**< 마우스 클리핑 서브 영역(클라이언트 좌표 기준, setClipSubRect 로 설정). */
        int32                  _clipSubRectTop;
        int32                  _clipSubRectRight;
        int32                  _clipSubRectBottom;
        MouseLockMode          _lockMode;     /**< 커서 잠금 모드(None/ConfinedToWindow/LockedInCenter). 실제 OS 클리핑은 InputManager::applyMouseLockMode() 가 적용. */
        uint8                  _buttonMask;   /**< 이번 프레임의 버튼 눌림 비트마스크(MouseButton 인덱스로 비트 조회). */
        uint8                  _pressedMask;  /**< 이번 프레임에 새로 눌린 버튼 비트마스크(엣지). onFrameBegin/onFrameEnd 에서 초기화. */
        uint8                  _releasedMask; /**< 이번 프레임에 새로 떼어진 버튼 비트마스크(엣지). */
        uint8                  _bCursorVisible    : 1;
        uint8                  _bPointerInside    : 1; /**< 마우스 포인터가 지금 창 클라이언트 영역 안에 있는지 여부. */
        uint8                  _bPointerEntered   : 1; /**< 이번 프레임에 포인터가 창 안으로 새로 들어왔는지 여부(엣지). */
        uint8                  _bPointerLeft      : 1; /**< 이번 프레임에 포인터가 창 밖으로 새로 나갔는지 여부(엣지). */
        uint8                  _bAnyButtonPressed : 1; /**< 이번 프레임에 어떤 버튼이든 새로 눌렸는지 여부. wasAnyButtonPressed() 가 참조. */
        uint8                  _bHasSubRect       : 1; /**< _clipSubRectXxx 로 지정한 서브 영역 클리핑이 켜져 있는지 여부. */
        [[maybe_unused]] uint8 _reserved          : 2;
    };
} // namespace sw
