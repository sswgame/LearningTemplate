/**
 * @file MouseDevice.h
 * @brief 독립된 표준 마우스 입력 장치 클래스 (버튼, 좌표, 1:1 Raw 델타, 휠, 커서 모드)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
	/** @brief 마우스 커서 잠금 및 클리핑 모드 */
	enum class CursorLockMode : uint8
	{
		None = 0,
		Locked,
		Confined
	};

	/**
	 * @class MouseDevice
	 * @brief 마우스 버튼, 좌표, 센서 델타 및 커서 컨텍스트를 전담하는 IInputDevice 구현체
	 */
	class SW_API MouseDevice : public IInputDevice
	{
	public:
		MouseDevice();
		virtual ~MouseDevice() override = default;

		MouseDevice( const MouseDevice& )			 = delete;
		MouseDevice& operator=( const MouseDevice& ) = delete;

		// ------------------------------------------------------------------------------
		// 1) IInputDevice 수명주기
		// ------------------------------------------------------------------------------
		InputDeviceKind getDeviceKind() const override { return InputDeviceKind::Mouse; }
		string_view		getDeviceName() const override { return "Mouse"; }
		bool			isConnected() const override { return true; }

		void poll( float32 deltaTime ) override;
		void onFrameBegin( float32 deltaTime ) override;
		void onFrameEnd() override;
		void resetState() override;

		bool isControlDown( uint16 controlIndex ) const override;
		bool wasControlPressed( uint16 controlIndex ) const override;
		bool wasControlReleased( uint16 controlIndex ) const override;

		// ------------------------------------------------------------------------------
		// 2) 마우스 전용 쿼리
		// ------------------------------------------------------------------------------
		bool isButtonDown( MouseButton button ) const;
		bool wasButtonPressed( MouseButton button ) const;
		bool wasButtonReleased( MouseButton button ) const;

		void getPosition( int32& outX, int32& outY ) const
		{
			outX = _mouseX;
			outY = _mouseY;
		}
		int32 getPositionX() const { return _mouseX; }
		int32 getPositionY() const { return _mouseY; }
		void  getDelta( int32& outDx, int32& outDy ) const
		{
			outDx = _deltaX;
			outDy = _deltaY;
		}
		void getRawDelta( float32& outDx, float32& outDy ) const
		{
			outDx = _rawDeltaX;
			outDy = _rawDeltaY;
		}
		float32 getMouseWheel() const { return _mouseWheelDelta; }

		bool isPointerInside() const { return _bPointerInside == SW_TRUE; }
		bool wasPointerEntered() const { return _bPointerEntered == SW_TRUE; }
		bool wasPointerLeft() const { return _bPointerLeft == SW_TRUE; }

		CursorLockMode getCursorLockMode() const { return _cursorLockMode; }
		void		   setCursorLockMode( CursorLockMode mode ) { _cursorLockMode = mode; }
		bool		   isCursorVisible() const { return _bCursorVisible == SW_TRUE; }
		void		   setCursorVisible( bool bVisible ) { _bCursorVisible = bVisible ? SW_TRUE : SW_FALSE; }

		// ------------------------------------------------------------------------------
		// 3) OS 이벤트 핸들러
		// ------------------------------------------------------------------------------
		void setButtonDown( MouseButton button, bool bDown );
		void setPosition( int32 x, int32 y );
		void addRawDelta( float32 dx, float32 dy );
		void addWheelDelta( float32 delta );
		void setPointerInsideState( bool bInside );

	private:
		static constexpr size_t kButtonCount = static_cast<size_t>( MouseButton::Count );

		array<bool, kButtonCount> _arrButtonDown;
		array<bool, kButtonCount> _arrButtonPressed;
		array<bool, kButtonCount> _arrButtonReleased;
		int32					  _mouseX;
		int32					  _mouseY;
		int32					  _prevMouseX;
		int32					  _prevMouseY;
		int32					  _deltaX;
		int32					  _deltaY;
		float32					  _rawDeltaX;
		float32					  _rawDeltaY;
		float32					  _mouseWheelDelta;
		float32					  _mouseWheelAccum;
		CursorLockMode			  _cursorLockMode;
		uint8					  _bCursorVisible  : 1;
		uint8					  _bPointerInside  : 1;
		uint8					  _bPointerEntered : 1;
		uint8					  _bPointerLeft	   : 1;
		[[maybe_unused]] uint8	  _reserved		   : 4;
	};
} // namespace sw
