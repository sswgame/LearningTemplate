/**
 * @file MouseDevice.h
 * @brief 표준 마우스 입력 장치입니다(버튼, 좌표, 1:1 원시 델타, 휠, 커서 모드).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodeUtil.h"

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
        /**
         * @brief 스무딩 · 가속 설정값이 기준으로 삼는 시간(60 Hz 한 프레임)입니다.
         * @details 설정값의 뜻은 "이 시간 동안 남기는 비율" 입니다. 프레임이 길든 짧든 같은 시간이 지나면 같은 만큼 따라옵니다.
         */
        static constexpr float32 kSmoothingReferenceSeconds = 1.0f / 60.0f;

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
        /** @brief 이번 프레임의 이동(`getMovementDelta`)에 가속과 스무딩을 한 번 적용해 `getSmoothDelta` 를 갱신합니다. */
        void onEventsDispatched( float32 deltaTime ) override;
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

        int2   getPosition() const { return _mouse; }
        int32  getPositionX() const { return _mouse._x; }
        int32  getPositionY() const { return _mouse._y; }
        int2   getDelta() const { return _delta; }
        float2 getRawDelta() const { return _rawDelta; }
        /**
         * @brief 이번 프레임의 이동량입니다. 원시 델타가 있으면 그것(화면 경계에 막히지 않는다), 없으면 위치 차이입니다.
         * @details 스무딩과 InputMap 의 마우스 델타 바인딩이 같은 규칙으로 읽습니다.
         */
        float2 getMovementDelta() const;
        /** @brief 가속 · 스무딩을 적용한 이번 프레임의 이동량입니다. 프레임당 한 번 갱신됩니다(`onEventsDispatched`). */
        float2 getSmoothDelta() const { return _smoothDelta; }

        /**
         * @brief 스무딩 설정값 [0, 0.99] 입니다. 0 이면 스무딩하지 않습니다.
         * @details 값은 `kSmoothingReferenceSeconds`(1/60 초) 동안 이전 속도를 남기는 비율입니다. 시간 상수로는
         *          τ = -kSmoothingReferenceSeconds / ln( factor ) 이고(0.5 → 24 ms, 0.9 → 158 ms), 한 프레임의 계수는
         *          1 - exp( -dt / τ ) = 1 - factor^( dt / kSmoothingReferenceSeconds ) 입니다. 60 Hz 에서는 계수가 1 - factor 이고,
         *          프레임 레이트 · 폴링 레이트와 무관합니다.
         */
        float32 getSmoothing() const { return _smoothingFactor; }
        void    setSmoothing( float32 factor ) { _smoothingFactor = factor < 0.0f ? 0.0f : ( factor > 0.99f ? 0.99f : factor ); }
        /** @brief 스무딩 설정값을 시간 상수(초)로 바꾼 값입니다. 스무딩이 꺼져 있으면 0 입니다. */
        float32 getSmoothingTimeConstant() const;
        /**
         * @brief 가속 지수입니다. 1 이면 가속하지 않습니다.
         * @details 속도를 `kSmoothingReferenceSeconds` 당 픽셀로 잰 값 s 가 1 보다 크면 이동에 s^( power - 1 ) 을 곱합니다(시간 기준이라 프레임 ·
         *          폴링 레이트와 무관).
         */
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
        /**
         * @brief 이번 프레임의 델타를 바꾸지 않고 위치만 옮깁니다.
         * @details 가운데 고정 잠금이 커서를 가운데로 되돌릴 때 씁니다. 되돌림은 사용자가 움직인 것이 아니므로 델타가 되면
         *          안 됩니다. 다음 프레임의 델타는 이 위치에서부터 잽니다.
         */
        void setPositionWithoutDelta( int32 x, int32 y );
        void addRawDelta( float32 dx, float32 dy );
        void addWheelDelta( float32 delta );
        void addHorizontalWheelDelta( float32 delta );
        void setPointerInsideState( bool bInside );

    private:
        static constexpr size_t kButtonCount = static_cast<size_t>( MouseButton::Count );

        int2                   _mouse;                     /**< 현재 프레임의 마우스 화면 좌표(창 클라이언트 기준). */
        int2                   _prevMouse;                 /**< 직전 프레임의 마우스 좌표. 델타 계산에 씀. */
        int2                   _delta;                     /**< 이번 프레임의 좌표 이동량(_mouse - _prevMouse). 화면 경계에 막히면 실제 이동보다 작음. */
        float2                 _rawDelta;                  /**< OS 원시(Raw Input) 델타 누적값. 화면 경계에 막히지 않는 실제 이동량(FPS 카메라 룩에 알맞음). */
        float2                 _smoothDelta;               /**< 가속 · 스무딩(EMA)을 적용한 이번 프레임 이동량. getSmoothDelta() 가 반환하는 값. */
        float2                 _smoothVelocity;            /**< 스무딩한 속도(픽셀/초). EMA 는 속도에 걸어 프레임 길이가 달라도 단위가 섞이지 않는다. */
        float32                _smoothingFactor;           /**< 스무딩 설정값 [0.0, 0.99] — 1/60 초 동안 남기는 비율. 0 이면 스무딩 없이 이동을 그대로 씀. */
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
        uint8                  _bCursorVisible  : 1;
        uint8                  _bPointerInside  : 1; /**< 마우스 포인터가 지금 창 클라이언트 영역 안에 있는지 여부. */
        uint8                  _bPointerEntered : 1; /**< 이번 프레임에 포인터가 창 안으로 새로 들어왔는지 여부(엣지). */
        uint8                  _bPointerLeft    : 1; /**< 이번 프레임에 포인터가 창 밖으로 새로 나갔는지 여부(엣지). */
        uint8                  _bHasSubRect     : 1; /**< _clipSubRectXxx 로 지정한 서브 영역 클리핑이 켜져 있는지 여부. */
        [[maybe_unused]] uint8 _reserved        : 3;
    };
} // namespace sw
