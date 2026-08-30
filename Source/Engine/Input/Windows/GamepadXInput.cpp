#include "pch.h"

#include "Engine/Input/Windows/GamepadXInput.h"

#include "Engine/Input/GamepadButtons.h"

#if defined( _WIN32 )
	#include "Core/Common/PlatformOsHeaders.h"
#endif

namespace sw
{
	namespace
	{
		struct GamepadXInputInternal
		{
			static constexpr float32 kStickDeadzone = 0.2f;

			static float32 applyDeadzone( float32 value, float32 deadzone )
			{
				const float32 absValue = value < 0.0f ? -value : value;
				if ( absValue < deadzone )
					return 0.0f;
				const float32 sign		 = value < 0.0f ? -1.0f : 1.0f;
				const float32 normalized = ( absValue - deadzone ) / ( 1.0f - deadzone );
				return sign * ( normalized > 1.0f ? 1.0f : normalized );
			}

			struct GamepadNameEntry
			{
				const utf8*	  _pName;
				GamepadButton _button;
			};

			static constexpr GamepadNameEntry kArrGamepadNames[] = {
				{			  "A",			   GamepadButton::A},
				{			  "B",			   GamepadButton::B},
				{			  "X",			   GamepadButton::X},
				{			  "Y",			   GamepadButton::Y},
				{		  "DPadUp",		GamepadButton::DPadUp},
				{	  "DPadDown",	  GamepadButton::DPadDown},
				{	  "DPadLeft",	  GamepadButton::DPadLeft},
				{	  "DPadRight",	   GamepadButton::DPadRight},
				{		  "Start",		   GamepadButton::Start},
				{		  "Back",		  GamepadButton::Back},
				{ "LeftShoulder",  GamepadButton::LeftShoulder},
				{"RightShoulder", GamepadButton::RightShoulder},
				{	  "LeftThumb",	   GamepadButton::LeftThumb},
				{	  "RightThumb",	GamepadButton::RightThumb},
			};

#if defined( _WIN32 )
			using PFN_XInputGetState = DWORD( WINAPI* )( DWORD, XINPUT_STATE* );

			/**
			 * @brief 첫 폴링 시 이름으로 XInputGetState를 해석합니다.
			 * @details xinput.lib를 링크하지 마세요. import lib는 서수로 바인딩하고,
			 *          XINPUT1_4.dll의 서수 1은 DllMain입니다. 이를 Engine IAT에 넣으면
			 *          LoadLibrary(Engine.dll) 중 BEX64 / STATUS_STACK_BUFFER_OVERRUN (0xC0000409)가 납니다.
			 */
			static PFN_XInputGetState resolveXInputGetState()
			{
				static PFN_XInputGetState s_pfn{ nullptr };
				static bool				  s_bTried{ false };
				if ( s_bTried )
					return s_pfn;
				s_bTried = true;

				HMODULE hModule = LoadLibraryW( L"XINPUT1_4.dll" );
				if ( hModule == nullptr )
					hModule = LoadLibraryW( L"xinput1_3.dll" );
				if ( hModule == nullptr )
					hModule = LoadLibraryW( L"xinput9_1_0.dll" );
				if ( hModule != nullptr )
					s_pfn = reinterpret_cast<PFN_XInputGetState>( GetProcAddress( hModule, "XInputGetState" ) );
				return s_pfn;
			}
#endif
		};
	} // namespace
} // namespace sw

namespace sw
{
	GamepadButton GamepadButtons::fromName( string_view name )
	{
		if ( name.empty() )
			return GamepadButton::Count;
		for ( const GamepadXInputInternal::GamepadNameEntry& entry : GamepadXInputInternal::kArrGamepadNames )
		{
			if ( StringUtil::equals( name, entry._pName, true ) )
				return entry._button;
		}
		return GamepadButton::Count;
	}

	const utf8* GamepadButtons::toName( GamepadButton button )
	{
		for ( const GamepadXInputInternal::GamepadNameEntry& entry : GamepadXInputInternal::kArrGamepadNames )
		{
			if ( entry._button == button )
				return entry._pName;
		}
		return nullptr;
	}

	GamepadXInput::GamepadXInput()
		: _arrButton{}
		, _arrPrevButton{}
		, _leftStickX{ 0.0f }
		, _leftStickY{ 0.0f }
		, _rightStickX{ 0.0f }
		, _rightStickY{ 0.0f }
		, _bConnected{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	void GamepadXInput::setButtonDown( GamepadButton button, bool bDown )
	{
		if ( button >= GamepadButton::Count )
			return;
		_arrButton[static_cast<size_t>( button )] = bDown;
	}

#if defined( _WIN32 )
	void GamepadXInput::poll( uint32 userIndex )
	{
		Memory::copy( _arrPrevButton, _arrButton, sizeof( _arrButton ) );

		GamepadXInputInternal::PFN_XInputGetState pfnGetState = GamepadXInputInternal::resolveXInputGetState();
		if ( pfnGetState == nullptr )
		{
			_bConnected	 = SW_FALSE;
			_leftStickX	 = 0.0f;
			_leftStickY	 = 0.0f;
			_rightStickX = 0.0f;
			_rightStickY = 0.0f;
			Memory::set( _arrButton, 0, sizeof( _arrButton ) );
			return;
		}

		XINPUT_STATE state{};
		const DWORD	 result = pfnGetState( userIndex, &state );
		if ( result != ERROR_SUCCESS )
		{
			_bConnected	 = SW_FALSE;
			_leftStickX	 = 0.0f;
			_leftStickY	 = 0.0f;
			_rightStickX = 0.0f;
			_rightStickY = 0.0f;
			Memory::set( _arrButton, 0, sizeof( _arrButton ) );
			return;
		}

		_bConnected		   = SW_TRUE;
		const WORD buttons = state.Gamepad.wButtons;
		setButtonDown( GamepadButton::A, ( buttons & XINPUT_GAMEPAD_A ) != 0 );
		setButtonDown( GamepadButton::B, ( buttons & XINPUT_GAMEPAD_B ) != 0 );
		setButtonDown( GamepadButton::X, ( buttons & XINPUT_GAMEPAD_X ) != 0 );
		setButtonDown( GamepadButton::Y, ( buttons & XINPUT_GAMEPAD_Y ) != 0 );
		setButtonDown( GamepadButton::DPadUp, ( buttons & XINPUT_GAMEPAD_DPAD_UP ) != 0 );
		setButtonDown( GamepadButton::DPadDown, ( buttons & XINPUT_GAMEPAD_DPAD_DOWN ) != 0 );
		setButtonDown( GamepadButton::DPadLeft, ( buttons & XINPUT_GAMEPAD_DPAD_LEFT ) != 0 );
		setButtonDown( GamepadButton::DPadRight, ( buttons & XINPUT_GAMEPAD_DPAD_RIGHT ) != 0 );
		setButtonDown( GamepadButton::Start, ( buttons & XINPUT_GAMEPAD_START ) != 0 );
		setButtonDown( GamepadButton::Back, ( buttons & XINPUT_GAMEPAD_BACK ) != 0 );
		setButtonDown( GamepadButton::LeftShoulder, ( buttons & XINPUT_GAMEPAD_LEFT_SHOULDER ) != 0 );
		setButtonDown( GamepadButton::RightShoulder, ( buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER ) != 0 );
		setButtonDown( GamepadButton::LeftThumb, ( buttons & XINPUT_GAMEPAD_LEFT_THUMB ) != 0 );
		setButtonDown( GamepadButton::RightThumb, ( buttons & XINPUT_GAMEPAD_RIGHT_THUMB ) != 0 );

		const float32 leftX	 = static_cast<float32>( state.Gamepad.sThumbLX ) / 32767.0f;
		const float32 leftY	 = static_cast<float32>( state.Gamepad.sThumbLY ) / 32767.0f;
		const float32 rightX = static_cast<float32>( state.Gamepad.sThumbRX ) / 32767.0f;
		const float32 rightY = static_cast<float32>( state.Gamepad.sThumbRY ) / 32767.0f;
		_leftStickX			 = GamepadXInputInternal::applyDeadzone( leftX, GamepadXInputInternal::kStickDeadzone );
		_leftStickY			 = GamepadXInputInternal::applyDeadzone( leftY, GamepadXInputInternal::kStickDeadzone );
		_rightStickX		 = GamepadXInputInternal::applyDeadzone( rightX, GamepadXInputInternal::kStickDeadzone );
		_rightStickY		 = GamepadXInputInternal::applyDeadzone( rightY, GamepadXInputInternal::kStickDeadzone );
	}
#else
	void GamepadXInput::poll( uint32 )
	{
		Memory::copy( _arrPrevButton, _arrButton, sizeof( _arrButton ) );
		_bConnected	 = SW_FALSE;
		_leftStickX	 = 0.0f;
		_leftStickY	 = 0.0f;
		_rightStickX = 0.0f;
		_rightStickY = 0.0f;
		Memory::set( _arrButton, 0, sizeof( _arrButton ) );
	}
#endif

	bool GamepadXInput::isButtonDown( GamepadButton button ) const
	{
		if ( button >= GamepadButton::Count )
			return false;
		return _arrButton[static_cast<size_t>( button )];
	}

	bool GamepadXInput::wasButtonPressed( GamepadButton button ) const
	{
		if ( button >= GamepadButton::Count )
			return false;
		const size_t buttonIndex = static_cast<size_t>( button );
		return ( _arrButton[buttonIndex] == true ) && ( _arrPrevButton[buttonIndex] == false );
	}

	bool GamepadXInput::wasButtonReleased( GamepadButton button ) const
	{
		if ( button >= GamepadButton::Count )
			return false;
		const size_t buttonIndex = static_cast<size_t>( button );
		return ( _arrButton[buttonIndex] == false ) && ( _arrPrevButton[buttonIndex] == true );
	}

	void GamepadXInput::getLeftStick( float32& outX, float32& outY ) const
	{
		outX = _leftStickX;
		outY = _leftStickY;
	}

	void GamepadXInput::getRightStick( float32& outX, float32& outY ) const
	{
		outX = _rightStickX;
		outY = _rightStickY;
	}
} // namespace sw
