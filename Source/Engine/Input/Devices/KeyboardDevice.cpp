#include "pch.h"

#include "Engine/Input/Devices/KeyboardDevice.h"

#include "Core/Memory/Memory.h"

namespace sw
{
	KeyboardDevice::KeyboardDevice()
		: _arrKeyMask{}
		, _arrPressedMask{}
		, _arrReleasedMask{}
		, _onTextInput{}
		, _bAnyKeyPressed{ SW_FALSE }
		, _reserved{ 0 }
	{
		resetState();
	}

	void KeyboardDevice::poll( [[maybe_unused]] float32 deltaTime )
	{
	}

	void KeyboardDevice::onFrameBegin( [[maybe_unused]] float32 deltaTime )
	{
		for ( size_t wordIndex = 0; wordIndex < kWordCount; ++wordIndex )
		{
			_arrPressedMask[wordIndex]	= 0;
			_arrReleasedMask[wordIndex] = 0;
		}
		_bAnyKeyPressed = SW_FALSE;
	}

	void KeyboardDevice::onFrameEnd()
	{
		for ( size_t wordIndex = 0; wordIndex < kWordCount; ++wordIndex )
		{
			_arrPressedMask[wordIndex]	= 0;
			_arrReleasedMask[wordIndex] = 0;
		}
		_bAnyKeyPressed = SW_FALSE;
	}

	void KeyboardDevice::resetState()
	{
		for ( size_t wordIndex = 0; wordIndex < kWordCount; ++wordIndex )
		{
			_arrKeyMask[wordIndex]		= 0;
			_arrPressedMask[wordIndex]	= 0;
			_arrReleasedMask[wordIndex] = 0;
		}
		_bAnyKeyPressed = SW_FALSE;
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
		if ( index >= kKeyCount )
			return false;
		return ( _arrKeyMask[index / 64] & ( 1ULL << ( index % 64 ) ) ) != 0;
	}

	bool KeyboardDevice::wasKeyPressed( Key key ) const
	{
		const size_t index = static_cast<size_t>( key );
		if ( index >= kKeyCount )
			return false;
		return ( _arrPressedMask[index / 64] & ( 1ULL << ( index % 64 ) ) ) != 0;
	}

	bool KeyboardDevice::wasKeyReleased( Key key ) const
	{
		const size_t index = static_cast<size_t>( key );
		if ( index >= kKeyCount )
			return false;
		return ( _arrReleasedMask[index / 64] & ( 1ULL << ( index % 64 ) ) ) != 0;
	}

	void KeyboardDevice::setKeyDown( Key key, bool bDown )
	{
		const size_t index = static_cast<size_t>( key );
		if ( index >= kKeyCount )
			return;

		const size_t word	  = index / 64;
		const uint64 bit	  = 1ULL << ( index % 64 );
		const bool	 bWasDown = ( _arrKeyMask[word] & bit ) != 0;

		if ( bDown )
		{
			_arrKeyMask[word] |= bit;
			if ( bWasDown == false )
			{
				_arrPressedMask[word] |= bit;
				_bAnyKeyPressed = SW_TRUE;
			}
		}
		else
		{
			_arrKeyMask[word] &= ~bit;
			if ( bWasDown )
			{
				_arrReleasedMask[word] |= bit;
			}
		}
	}

	void KeyboardDevice::notifyTextInput( string_view text )
	{
		if ( _onTextInput.isBound() )
			_onTextInput( text );
	}
} // namespace sw
