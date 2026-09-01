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
				const float32 safeSpan	 = ( 1.0f - deadzone ) > 1e-4f ? ( 1.0f - deadzone ) : 1.0f;
				const float32 normalized = ( absValue - deadzone ) / safeSpan;
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
			using PFN_XInputSetState = DWORD( WINAPI* )( DWORD, XINPUT_VIBRATION* );

			static HMODULE getXInputModule()
			{
				static HMODULE s_hModule{ nullptr };
				static bool	   s_bTried{ false };
				if ( s_bTried )
					return s_hModule;
				s_bTried = true;

				s_hModule = LoadLibraryW( L"XINPUT1_4.dll" );
				if ( s_hModule == nullptr )
					s_hModule = LoadLibraryW( L"xinput1_3.dll" );
				if ( s_hModule == nullptr )
					s_hModule = LoadLibraryW( L"xinput9_1_0.dll" );
				return s_hModule;
			}

			static PFN_XInputGetState resolveXInputGetState()
			{
				static PFN_XInputGetState s_pfn{ nullptr };
				static bool				  s_bTried{ false };
				if ( s_bTried )
					return s_pfn;
				s_bTried = true;

				HMODULE hModule = getXInputModule();
				if ( hModule != nullptr )
					s_pfn = reinterpret_cast<PFN_XInputGetState>( GetProcAddress( hModule, "XInputGetState" ) );
				return s_pfn;
			}

			static PFN_XInputSetState resolveXInputSetState()
			{
				static PFN_XInputSetState s_pfn{ nullptr };
				static bool				  s_bTried{ false };
				if ( s_bTried )
					return s_pfn;
				s_bTried = true;

				HMODULE hModule = getXInputModule();
				if ( hModule != nullptr )
					s_pfn = reinterpret_cast<PFN_XInputSetState>( GetProcAddress( hModule, "XInputSetState" ) );
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

	GamepadXInput::GamepadXInput( uint32 userIndex )
		: GamepadDevice{ userIndex }
		, _bConnected{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	void GamepadXInput::poll( [[maybe_unused]] float32 deltaTime )
	{
		pollUser( _deviceIndex );
	}

#if defined( _WIN32 )
	void GamepadXInput::pollUser( uint32 userIndex )
	{
		_prevButtonMask = _buttonMask;

		GamepadXInputInternal::PFN_XInputGetState pfnGetState = GamepadXInputInternal::resolveXInputGetState();
		if ( pfnGetState == nullptr )
		{
			_bConnected	  = SW_FALSE;
			_buttonMask	  = 0;
			_leftStickX	  = 0.0f;
			_leftStickY	  = 0.0f;
			_rightStickX  = 0.0f;
			_rightStickY  = 0.0f;
			_leftTrigger  = 0.0f;
			_rightTrigger = 0.0f;
			return;
		}

		XINPUT_STATE state{};
		const DWORD	 result = pfnGetState( userIndex, &state );
		if ( result != ERROR_SUCCESS )
		{
			_bConnected	  = SW_FALSE;
			_buttonMask	  = 0;
			_leftStickX	  = 0.0f;
			_leftStickY	  = 0.0f;
			_rightStickX  = 0.0f;
			_rightStickY  = 0.0f;
			_leftTrigger  = 0.0f;
			_rightTrigger = 0.0f;
			return;
		}

		_bConnected		   = SW_TRUE;
		const WORD buttons = state.Gamepad.wButtons;
		uint32	   mask	   = 0;
		if ( ( buttons & XINPUT_GAMEPAD_A ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::A ) );
		if ( ( buttons & XINPUT_GAMEPAD_B ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::B ) );
		if ( ( buttons & XINPUT_GAMEPAD_X ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::X ) );
		if ( ( buttons & XINPUT_GAMEPAD_Y ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::Y ) );
		if ( ( buttons & XINPUT_GAMEPAD_DPAD_UP ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::DPadUp ) );
		if ( ( buttons & XINPUT_GAMEPAD_DPAD_DOWN ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::DPadDown ) );
		if ( ( buttons & XINPUT_GAMEPAD_DPAD_LEFT ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::DPadLeft ) );
		if ( ( buttons & XINPUT_GAMEPAD_DPAD_RIGHT ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::DPadRight ) );
		if ( ( buttons & XINPUT_GAMEPAD_START ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::Start ) );
		if ( ( buttons & XINPUT_GAMEPAD_BACK ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::Back ) );
		if ( ( buttons & XINPUT_GAMEPAD_LEFT_SHOULDER ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::LeftShoulder ) );
		if ( ( buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::RightShoulder ) );
		if ( ( buttons & XINPUT_GAMEPAD_LEFT_THUMB ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::LeftThumb ) );
		if ( ( buttons & XINPUT_GAMEPAD_RIGHT_THUMB ) != 0 )
			mask |= ( 1u << static_cast<uint32>( GamepadButton::RightThumb ) );
		_buttonMask = mask;

		const float32 leftX	 = static_cast<float32>( state.Gamepad.sThumbLX ) / 32767.0f;
		const float32 leftY	 = static_cast<float32>( state.Gamepad.sThumbLY ) / 32767.0f;
		const float32 rightX = static_cast<float32>( state.Gamepad.sThumbRX ) / 32767.0f;
		const float32 rightY = static_cast<float32>( state.Gamepad.sThumbRY ) / 32767.0f;
		_leftStickX			 = GamepadXInputInternal::applyDeadzone( leftX, GamepadXInputInternal::kStickDeadzone );
		_leftStickY			 = GamepadXInputInternal::applyDeadzone( leftY, GamepadXInputInternal::kStickDeadzone );
		_rightStickX		 = GamepadXInputInternal::applyDeadzone( rightX, GamepadXInputInternal::kStickDeadzone );
		_rightStickY		 = GamepadXInputInternal::applyDeadzone( rightY, GamepadXInputInternal::kStickDeadzone );

		_leftTrigger  = static_cast<float32>( state.Gamepad.bLeftTrigger ) / 255.0f;
		_rightTrigger = static_cast<float32>( state.Gamepad.bRightTrigger ) / 255.0f;
	}

	bool GamepadXInput::setVibration( float32 leftMotor, float32 rightMotor )
	{
		setVibration( leftMotor, rightMotor, _deviceIndex );
		return true;
	}

	void GamepadXInput::setVibration( float32 leftMotor, float32 rightMotor, uint32 userIndex )
	{
		GamepadXInputInternal::PFN_XInputSetState pfnSetState = GamepadXInputInternal::resolveXInputSetState();
		if ( pfnSetState == nullptr || _bConnected == SW_FALSE )
			return;

		XINPUT_VIBRATION vibration{};
		const float32	 clampedLeft  = leftMotor < 0.0f ? 0.0f : ( leftMotor > 1.0f ? 1.0f : leftMotor );
		const float32	 clampedRight = rightMotor < 0.0f ? 0.0f : ( rightMotor > 1.0f ? 1.0f : rightMotor );
		vibration.wLeftMotorSpeed	  = static_cast<WORD>( clampedLeft * 65535.0f );
		vibration.wRightMotorSpeed	  = static_cast<WORD>( clampedRight * 65535.0f );
		pfnSetState( userIndex, &vibration );
	}

	void GamepadXInput::stopVibration()
	{
		setVibration( 0.0f, 0.0f, _deviceIndex );
	}
#else
	void GamepadXInput::pollUser( uint32 )
	{
		_prevButtonMask = _buttonMask;
		_bConnected		= SW_FALSE;
		_buttonMask		= 0;
		_leftStickX		= 0.0f;
		_leftStickY		= 0.0f;
		_rightStickX	= 0.0f;
		_rightStickY	= 0.0f;
		_leftTrigger	= 0.0f;
		_rightTrigger	= 0.0f;
	}

	bool GamepadXInput::setVibration( float32, float32 )
	{
		return false;
	}

	void GamepadXInput::setVibration( float32, float32, uint32 )
	{
	}

	void GamepadXInput::stopVibration()
	{
	}
#endif
} // namespace sw
