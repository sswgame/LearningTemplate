#include "pch.h"

#include "Engine/Input/Devices/MouseDevice.h"

#include "Core/Common/StdHeaders.h"

namespace sw
{
	MouseDevice::MouseDevice()
		: _arrButtonDown{}
		, _arrButtonPressed{}
		, _arrButtonReleased{}
		, _mouseX{ 0 }
		, _mouseY{ 0 }
		, _prevMouseX{ 0 }
		, _prevMouseY{ 0 }
		, _deltaX{ 0 }
		, _deltaY{ 0 }
		, _rawDeltaX{ 0.0f }
		, _rawDeltaY{ 0.0f }
		, _mouseWheelDelta{ 0.0f }
		, _mouseWheelAccum{ 0.0f }
		, _cursorLockMode{ CursorLockMode::None }
		, _bCursorVisible{ SW_TRUE }
		, _bPointerInside{ SW_FALSE }
		, _bPointerEntered{ SW_FALSE }
		, _bPointerLeft{ SW_FALSE }
		, _reserved{ 0 }
	{
		resetState();
	}

	void MouseDevice::poll( [[maybe_unused]] float32 deltaTime )
	{
	}

	void MouseDevice::onFrameBegin( [[maybe_unused]] float32 deltaTime )
	{
		_arrButtonPressed.fill( false );
		_arrButtonReleased.fill( false );

		_deltaX			 = _mouseX - _prevMouseX;
		_deltaY			 = _mouseY - _prevMouseY;
		_prevMouseX		 = _mouseX;
		_prevMouseY		 = _mouseY;
		_mouseWheelDelta = _mouseWheelAccum;
		_mouseWheelAccum = 0.0f;

		_bPointerEntered = SW_FALSE;
		_bPointerLeft	 = SW_FALSE;
	}

	void MouseDevice::onFrameEnd()
	{
		_prevMouseX = _mouseX;
		_prevMouseY = _mouseY;
		_arrButtonPressed.fill( false );
		_arrButtonReleased.fill( false );
		_rawDeltaX		 = 0.0f;
		_rawDeltaY		 = 0.0f;
		_mouseWheelDelta = 0.0f;
	}

	void MouseDevice::resetState()
	{
		_arrButtonDown.fill( false );
		_arrButtonPressed.fill( false );
		_arrButtonReleased.fill( false );
		_deltaX			 = 0;
		_deltaY			 = 0;
		_rawDeltaX		 = 0.0f;
		_rawDeltaY		 = 0.0f;
		_mouseWheelDelta = 0.0f;
		_mouseWheelAccum = 0.0f;
	}

	bool MouseDevice::isControlDown( uint16 controlIndex ) const
	{
		if ( controlIndex >= kButtonCount )
			return false;
		return isButtonDown( static_cast<MouseButton>( controlIndex ) );
	}

	bool MouseDevice::wasControlPressed( uint16 controlIndex ) const
	{
		if ( controlIndex >= kButtonCount )
			return false;
		return wasButtonPressed( static_cast<MouseButton>( controlIndex ) );
	}

	bool MouseDevice::wasControlReleased( uint16 controlIndex ) const
	{
		if ( controlIndex >= kButtonCount )
			return false;
		return wasButtonReleased( static_cast<MouseButton>( controlIndex ) );
	}

	bool MouseDevice::isButtonDown( MouseButton button ) const
	{
		const size_t index = static_cast<size_t>( button );
		return index < kButtonCount ? _arrButtonDown[index] : false;
	}

	bool MouseDevice::wasButtonPressed( MouseButton button ) const
	{
		const size_t index = static_cast<size_t>( button );
		return index < kButtonCount ? _arrButtonPressed[index] : false;
	}

	bool MouseDevice::wasButtonReleased( MouseButton button ) const
	{
		const size_t index = static_cast<size_t>( button );
		return index < kButtonCount ? _arrButtonReleased[index] : false;
	}

	void MouseDevice::setButtonDown( MouseButton button, bool bDown )
	{
		const size_t index = static_cast<size_t>( button );
		if ( index >= kButtonCount )
			return;

		const bool bWasDown	  = _arrButtonDown[index];
		_arrButtonDown[index] = bDown;

		if ( bDown && ( bWasDown == false ) )
			_arrButtonPressed[index] = true;
		else if ( ( bDown == false ) && bWasDown )
			_arrButtonReleased[index] = true;
	}

	void MouseDevice::setPosition( int32 x, int32 y )
	{
		_deltaX = x - _prevMouseX;
		_deltaY = y - _prevMouseY;
		_mouseX = x;
		_mouseY = y;
	}

	void MouseDevice::addRawDelta( float32 dx, float32 dy )
	{
		_rawDeltaX += dx;
		_rawDeltaY += dy;
	}

	void MouseDevice::addWheelDelta( float32 delta )
	{
		_mouseWheelAccum += delta;
	}

	void MouseDevice::setPointerInsideState( bool bInside )
	{
		const bool bWasInside = _bPointerInside == SW_TRUE;
		_bPointerInside		  = bInside ? SW_TRUE : SW_FALSE;

		if ( bInside && ( bWasInside == false ) )
			_bPointerEntered = SW_TRUE;
		else if ( ( bInside == false ) && bWasInside )
			_bPointerLeft = SW_TRUE;
	}
} // namespace sw
