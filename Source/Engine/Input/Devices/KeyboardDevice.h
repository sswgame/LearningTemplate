/**
 * @file KeyboardDevice.h
 * @brief 표준 키보드 입력 장치입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodeUtil.h"

namespace sw
{
    /**
     * @class KeyboardDevice
     * @brief 키보드 키 상태를 맡는 IInputDevice 구현입니다.
     * @details 글자 입력(WM_CHAR · X11 KeyPress 의 문자)은 키가 아니라 앱 수준 이벤트라 `InputManager::setTextInputCallback` 으로 받습니다
     *          (언리얼 `FSlateApplication::OnKeyChar` 자리).
     */
    class SW_API KeyboardDevice : public IInputDevice
    {
    public:
        KeyboardDevice();
        virtual ~KeyboardDevice() override = default;

        KeyboardDevice( const KeyboardDevice& )            = delete;
        KeyboardDevice& operator=( const KeyboardDevice& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) IInputDevice 수명주기
        // ------------------------------------------------------------------------------
        InputDeviceKind getDeviceKind() const override { return InputDeviceKind::Keyboard; }
        string_view     getDeviceName() const override { return "Keyboard"; }
        bool            isConnected() const override { return true; }

        void poll( float32 deltaTime ) override;
        void onFrameBegin( float32 deltaTime ) override;
        void onFrameEnd() override;
        void resetState() override;

        bool isControlDown( uint16 controlIndex ) const override;
        bool wasControlPressed( uint16 controlIndex ) const override;
        bool wasControlReleased( uint16 controlIndex ) const override;

        // ------------------------------------------------------------------------------
        // 2) 키보드 전용 쿼리 · OS 이벤트 처리기
        // ------------------------------------------------------------------------------
        bool isKeyDown( Key key ) const;
        bool wasKeyPressed( Key key ) const;
        bool wasKeyReleased( Key key ) const;
        bool wasAnyKeyPressed() const { return _bAnyKeyPressed == SW_TRUE; }

        void setKeyDown( Key key, bool bDown );

    private:
        static constexpr size_t kKeyCount  = static_cast<size_t>( Key::Count );
        static constexpr size_t kWordCount = ( kKeyCount + 63 ) / 64;

        uint64                 _arrKeyMask[kWordCount];      /**< 이번 프레임의 키 눌림 비트마스크(Key 인덱스 / 64 = 워드, % 64 = 비트). */
        uint64                 _arrPressedMask[kWordCount];  /**< 이번 프레임에 새로 눌린 키 비트마스크(엣지). onFrameBegin/onFrameEnd 에서 초기화. */
        uint64                 _arrReleasedMask[kWordCount]; /**< 이번 프레임에 새로 떼어진 키 비트마스크(엣지). */
        uint8                  _bAnyKeyPressed : 1;          /**< 이번 프레임에 어떤 키든 새로 눌렸는지 여부. wasAnyKeyPressed() 가 참조. */
        [[maybe_unused]] uint8 _reserved       : 7;
    };
} // namespace sw
