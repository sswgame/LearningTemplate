#include "pch.h"

#include "Engine/Input/Devices/KeyboardDevice.h"

#include "Core/Common/StdHeaders.h"

namespace sw
{
	KeyboardDevice::KeyboardDevice()
		: _arrKeyDown{}
		, _arrKeyPressed{}
		, _arrKeyReleased{}
		, _onTextInput{}
	{
		resetState();
	}

	void KeyboardDevice::poll( [[maybe_unused]] float32 deltaTime )
	{
	}

	void KeyboardDevice::onFrameBegin( [[maybe_unused]] float32 deltaTime )
	{
		_arrKeyPressed.fill( false );
		_arrKeyReleased.fill( false );
	}

	void KeyboardDevice::onFrameEnd()
	{
		_arrKeyPressed.fill( false );
		_arrKeyReleased.fill( false );
	}

	void KeyboardDevice::resetState()
	{
		_arrKeyDown.fill( false );
		_arrKeyPressed.fill( false );
		_arrKeyReleased.fill( false );
	}

	bool KeyboardDevice::isControlDown( uint16 controlIndex ) const
	{
		if ( controlIndex >= kKeyCount )
			return false;
		return isKeyDown( static_cast<Key>( controlIndex ) );
	}

	bool KeyboardDevice::wasControlPressed( uint16 controlIndex ) const
	{
		if ( controlIndex >= kKeyCount )
			return false;
		return wasKeyPressed( static_cast<Key>( controlIndex ) );
	}

	bool KeyboardDevice::wasControlReleased( uint16 controlIndex ) const
	{
		if ( controlIndex >= kKeyCount )
			return false;
		return wasKeyReleased( static_cast<Key>( controlIndex ) );
	}

	bool KeyboardDevice::isKeyDown( Key key ) const
	{
		const size_t index = static_cast<size_t>( key );
		return index < kKeyCount ? _arrKeyDown[index] : false;
	}

	bool KeyboardDevice::wasKeyPressed( Key key ) const
	{
		const size_t index = static_cast<size_t>( key );
		return index < kKeyCount ? _arrKeyPressed[index] : false;
	}

	bool KeyboardDevice::wasKeyReleased( Key key ) const
	{
		const size_t index = static_cast<size_t>( key );
		return index < kKeyCount ? _arrKeyReleased[index] : false;
	}

	void KeyboardDevice::setKeyDown( Key key, bool bDown )
	{
		const size_t index = static_cast<size_t>( key );
		if ( index >= kKeyCount )
			return;

		const bool bWasDown = _arrKeyDown[index];
		_arrKeyDown[index]	= bDown;

		if ( bDown && ( bWasDown == false ) )
			_arrKeyPressed[index] = true;
		else if ( ( bDown == false ) && bWasDown )
			_arrKeyReleased[index] = true;
	}

	void KeyboardDevice::notifyTextInput( string_view text )
	{
		if ( _onTextInput.isBound() )
			_onTextInput( text );
	}
} // namespace sw
