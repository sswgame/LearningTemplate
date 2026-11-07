/**
 * @file TouchDevice.h
 * @brief 터치 스크린 장치입니다. 손가락 번호가 컨트롤 번호라 입력 맵은 키처럼 `Touch.0` 슬롯으로 묶습니다.
 * @details 언리얼 `EKeys::Touch1` … 처럼 손가락을 키로 둔다(기억). 새 바인딩 종류를 더하지 않아 충돌 검사 · 재바인딩 · 저장이 그대로 돈다. 모바일 · 웹 창이 `RawInputEventType::Touch*` 를 낸다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Input/IInputDevice.h"

namespace sw
{
    /**
     * @class TouchDevice
     * @brief 손가락 눌림과 위치를 맡는 IInputDevice 구현입니다.
     */
    class SW_API TouchDevice : public IInputDevice
    {
    public:
        /** @brief 따로 추적하는 손가락 수입니다. 넘는 번호는 무시합니다. */
        static constexpr uint16 kMaxPointerCount = 10;

        TouchDevice();
        ~TouchDevice() override = default;

        TouchDevice( const TouchDevice& )            = delete;
        TouchDevice& operator=( const TouchDevice& ) = delete;

        InputDeviceKind getDeviceKind() const override { return InputDeviceKind::Touch; }
        string_view     getDeviceName() const override { return "Touch"; }

        void poll( float32 deltaTime ) override;
        void onFrameBegin( float32 deltaTime ) override;
        void onFrameEnd() override;
        void resetState() override;

        bool isControlDown( uint16 controlIndex ) const override;
        bool wasControlPressed( uint16 controlIndex ) const override;
        bool wasControlReleased( uint16 controlIndex ) const override;

        /** @brief 손가락의 눌림과 위치(클라이언트 픽셀)를 적습니다. */
        void setPointerState( uint16 pointerIndex, int32 x, int32 y, bool bDown );
        /** @brief 손가락의 마지막 위치(클라이언트 픽셀)입니다. */
        int2 getPointerPosition( uint16 pointerIndex ) const;

    private:
        int2   _arrPosition[kMaxPointerCount];
        uint16 _downMask;
        uint16 _pressedMask;
        uint16 _releasedMask;
    };
} // namespace sw
