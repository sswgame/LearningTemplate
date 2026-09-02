#include "pch.h"

#include "Engine/Input/Windows/GamepadXInput.h"

#include "Core/Math/MathUtil.h"

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

			using PFN_XInputGetBatteryInformation = DWORD( WINAPI* )( DWORD, BYTE, XINPUT_BATTERY_INFORMATION* );

			static PFN_XInputGetBatteryInformation resolveXInputGetBatteryInformation()
			{
				static PFN_XInputGetBatteryInformation s_pfn{ nullptr };
				static bool							   s_bTried{ false };
				if ( s_bTried )
					return s_pfn;
				s_bTried = true;

				HMODULE hModule = getXInputModule();
				if ( hModule != nullptr )
					s_pfn = reinterpret_cast<PFN_XInputGetBatteryInformation>( GetProcAddress( hModule, "XInputGetBatteryInformation" ) );
				return s_pfn;
			}
#endif
		};
	} // namespace
} // namespace sw

namespace sw
{
	GamepadXInput::GamepadXInput( uint32 userIndex )
		: GamepadDevice{ userIndex }
		, _reconnectTimer{ 1.0f }
		, _bConnected{ SW_FALSE }
		, _reservedPad{ 0 }
	{
	}

	void GamepadXInput::poll( float32 deltaTime )
	{
		pollUser( _deviceIndex, deltaTime );
	}

#if defined( _WIN32 )
	void GamepadXInput::pollUser( uint32 userIndex, float32 deltaTime )
	{
		_prevButtonMask = _buttonMask;

		const bool bWasConnected = ( _bConnected == SW_TRUE );

		if ( bWasConnected == false )
		{
			_reconnectTimer += deltaTime;
			if ( _reconnectTimer < 1.0f )
				return;
			_reconnectTimer = 0.0f;
		}

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
			if ( bWasConnected && _onConnectionChanged.isBound() )
				_onConnectionChanged( userIndex, false );
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
			if ( bWasConnected && _onConnectionChanged.isBound() )
				_onConnectionChanged( userIndex, false );
			return;
		}

		_bConnected		= SW_TRUE;
		_reconnectTimer = 0.0f;
		if ( bWasConnected == false && _onConnectionChanged.isBound() )
			_onConnectionChanged( userIndex, true );

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

		const float32 leftX	 = MathUtil::clamp( static_cast<float32>( state.Gamepad.sThumbLX ) / 32767.0f, -1.0f, 1.0f );
		const float32 leftY	 = MathUtil::clamp( static_cast<float32>( state.Gamepad.sThumbLY ) / 32767.0f, -1.0f, 1.0f );
		const float32 rightX = MathUtil::clamp( static_cast<float32>( state.Gamepad.sThumbRX ) / 32767.0f, -1.0f, 1.0f );
		const float32 rightY = MathUtil::clamp( static_cast<float32>( state.Gamepad.sThumbRY ) / 32767.0f, -1.0f, 1.0f );
		_leftStickX			 = leftX;
		_leftStickY			 = leftY;
		_rightStickX		 = rightX;
		_rightStickY		 = rightY;

		_leftTrigger  = static_cast<float32>( state.Gamepad.bLeftTrigger ) / 255.0f;
		_rightTrigger = static_cast<float32>( state.Gamepad.bRightTrigger ) / 255.0f;
	}

	bool GamepadXInput::setVibration( float32 leftMotor, float32 rightMotor )
	{
		_leftMotorSpeed	 = leftMotor;
		_rightMotorSpeed = rightMotor;
		setVibration( leftMotor, rightMotor, _deviceIndex );
		return true;
	}

	void GamepadXInput::setVibration( float32 leftMotor, float32 rightMotor, uint32 userIndex )
	{
		GamepadXInputInternal::PFN_XInputSetState pfnSetState = GamepadXInputInternal::resolveXInputSetState();
		if ( pfnSetState == nullptr || _bConnected == SW_FALSE )
			return;

		XINPUT_VIBRATION vibration{};
		const float32	 clampedLeft  = MathUtil::clamp( leftMotor, 0.0f, 1.0f );
		const float32	 clampedRight = MathUtil::clamp( rightMotor, 0.0f, 1.0f );
		vibration.wLeftMotorSpeed	  = static_cast<WORD>( clampedLeft * 65535.0f );
		vibration.wRightMotorSpeed	  = static_cast<WORD>( clampedRight * 65535.0f );
		pfnSetState( userIndex, &vibration );
	}

	void GamepadXInput::stopVibration()
	{
		GamepadDevice::stopVibration();
		setVibration( 0.0f, 0.0f, _deviceIndex );
	}

	GamepadBatteryInfo GamepadXInput::getBatteryInfo() const
	{
		GamepadBatteryInfo									   info{};
		GamepadXInputInternal::PFN_XInputGetBatteryInformation pfnGetBat = GamepadXInputInternal::resolveXInputGetBatteryInformation();
		if ( pfnGetBat == nullptr || _bConnected == SW_FALSE )
			return info;

		XINPUT_BATTERY_INFORMATION bat{};
		if ( pfnGetBat( _deviceIndex, BATTERY_DEVTYPE_GAMEPAD, &bat ) == ERROR_SUCCESS )
		{
			switch ( bat.BatteryType )
			{
				case BATTERY_TYPE_WIRED:
					info._type = GamepadBatteryType::Wired;
					break;
				case BATTERY_TYPE_ALKALINE:
					info._type = GamepadBatteryType::Alkaline;
					break;
				case BATTERY_TYPE_NIMH:
					info._type = GamepadBatteryType::Nimh;
					break;
				case BATTERY_TYPE_DISCONNECTED:
					info._type = GamepadBatteryType::Disconnected;
					break;
				default:
					info._type = GamepadBatteryType::Unknown;
					break;
			}

			switch ( bat.BatteryLevel )
			{
				case BATTERY_LEVEL_EMPTY:
					info._level = GamepadBatteryLevel::Empty;
					break;
				case BATTERY_LEVEL_LOW:
					info._level = GamepadBatteryLevel::Low;
					break;
				case BATTERY_LEVEL_MEDIUM:
					info._level = GamepadBatteryLevel::Medium;
					break;
				case BATTERY_LEVEL_FULL:
					info._level = GamepadBatteryLevel::Full;
					break;
				default:
					info._level = GamepadBatteryLevel::Empty;
					break;
			}
		}
		return info;
	}
#else
	void GamepadXInput::pollUser( uint32, float32 )
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

	GamepadBatteryInfo GamepadXInput::getBatteryInfo() const
	{
		return GamepadBatteryInfo{};
	}
#endif
} // namespace sw
