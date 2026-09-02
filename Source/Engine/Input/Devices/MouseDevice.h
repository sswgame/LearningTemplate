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
	enum class MouseLockMode : uint8
	{
		None = 0,
		ConfinedToWindow,
		LockedInCenter
	};
	using CursorLockMode = MouseLockMode;

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

		bool	isControlDown( uint16 controlIndex ) const override;
		bool	wasControlPressed( uint16 controlIndex ) const override;
		bool	wasControlReleased( uint16 controlIndex ) const override;
		float32 getControlValue( uint16 controlIndex ) const override;

		// ------------------------------------------------------------------------------
		// 2) 마우스 전용 쿼리
		// ------------------------------------------------------------------------------
		bool isButtonDown( MouseButton button ) const;
		bool wasButtonPressed( MouseButton button ) const;
		bool wasButtonReleased( MouseButton button ) const;
		bool wasAnyButtonPressed() const { return _pressedMask != 0; }

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
		void getSmoothDelta( float32& outDx, float32& outDy ) const
		{
			outDx = _smoothDeltaX;
			outDy = _smoothDeltaY;
		}
		float32 getSmoothing() const { return _smoothingFactor; }
		void	setSmoothing( float32 factor ) { _smoothingFactor = factor < 0.0f ? 0.0f : ( factor > 0.99f ? 0.99f : factor ); }
		float32 getAcceleration() const { return _accelerationPower; }
		void	setAcceleration( float32 power ) { _accelerationPower = power < 1.0f ? 1.0f : power; }

		float32 getMouseWheel() const { return _mouseWheelDelta; }
		float32 getMouseWheelHorizontal() const { return _mouseWheelHorizontalDelta; }

		bool isPointerInside() const { return _bPointerInside == SW_TRUE; }
		bool wasPointerEntered() const { return _bPointerEntered == SW_TRUE; }
		bool wasPointerLeft() const { return _bPointerLeft == SW_TRUE; }

		MouseLockMode  getLockMode() const { return _lockMode; }
		void		   setLockMode( MouseLockMode mode ) { _lockMode = mode; }
		CursorLockMode getCursorLockMode() const { return _lockMode; }
		void		   setCursorLockMode( CursorLockMode mode ) { _lockMode = mode; }
		bool		   isCursorVisible() const { return _bCursorVisible == SW_TRUE; }
		void		   setCursorVisible( bool bVisible ) { _bCursorVisible = bVisible ? SW_TRUE : SW_FALSE; }

		void setClipSubRect( int32 left, int32 top, int32 right, int32 bottom )
		{
			_clipSubRectLeft   = left;
			_clipSubRectTop	   = top;
			_clipSubRectRight  = right;
			_clipSubRectBottom = bottom;
			_bHasSubRect	   = SW_TRUE;
		}
		void clearClipSubRect()
		{
			_clipSubRectLeft   = 0;
			_clipSubRectTop	   = 0;
			_clipSubRectRight  = 0;
			_clipSubRectBottom = 0;
			_bHasSubRect	   = SW_FALSE;
		}
		bool hasClipSubRect() const { return _bHasSubRect == SW_TRUE; }
		bool getClipSubRect( int32& outLeft, int32& outTop, int32& outRight, int32& outBottom ) const
		{
			outLeft	  = _clipSubRectLeft;
			outTop	  = _clipSubRectTop;
			outRight  = _clipSubRectRight;
			outBottom = _clipSubRectBottom;
			return _bHasSubRect == SW_TRUE;
		}

		// ------------------------------------------------------------------------------
		// 3) OS 이벤트 핸들러
		// ------------------------------------------------------------------------------
		void setButtonDown( MouseButton button, bool bDown );
		void setPosition( int32 x, int32 y );
		void addRawDelta( float32 dx, float32 dy );
		void addWheelDelta( float32 delta );
		void addHorizontalWheelDelta( float32 delta );
		void setPointerInsideState( bool bInside );

	private:
		void updateSmoothDelta( float32 dx, float32 dy );

		static constexpr size_t kButtonCount = static_cast<size_t>( MouseButton::Count );

		int32				   _mouseX;
		int32				   _mouseY;
		int32				   _prevMouseX;
		int32				   _prevMouseY;
		int32				   _deltaX;
		int32				   _deltaY;
		float32				   _rawDeltaX;
		float32				   _rawDeltaY;
		float32				   _smoothDeltaX;
		float32				   _smoothDeltaY;
		float32				   _smoothingFactor;
		float32				   _accelerationPower;
		float32				   _accumulatedRawDx;
		float32				   _accumulatedRawDy;
		float32				   _mouseWheelDelta;
		float32				   _mouseWheelAccum;
		float32				   _mouseWheelHorizontalDelta;
		float32				   _mouseWheelHorizontalAccum;
		int32				   _clipSubRectLeft;
		int32				   _clipSubRectTop;
		int32				   _clipSubRectRight;
		int32				   _clipSubRectBottom;
		MouseLockMode		   _lockMode;
		uint8				   _buttonMask;
		uint8				   _pressedMask;
		uint8				   _releasedMask;
		uint8				   _bCursorVisible	  : 1;
		uint8				   _bPointerInside	  : 1;
		uint8				   _bPointerEntered	  : 1;
		uint8				   _bPointerLeft	  : 1;
		uint8				   _bAnyButtonPressed : 1;
		uint8				   _bHasSubRect		  : 1;
		[[maybe_unused]] uint8 _reserved		  : 2;
	};
} // namespace sw
