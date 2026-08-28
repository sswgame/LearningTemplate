#include "pch.h"

#include "Engine/Input/InputManager.h"

#include "Engine/Input/Windows/GamepadXInput.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
	SW_LOG_CALLER( "InputManager" );

	InputManager::InputManager()
		: _gamepad{ nullptr }
		, _mouseX{ 0 }
		, _mouseY{ 0 }
		, _prevMouseX{ 0 }
		, _prevMouseY{ 0 }
		, _mouseWheelDelta{ 0.0f }
		, _mouseWheelAccum{ 0.0f }
		, _arrKeys{}
		, _arrPrevKeys{}
		, _arrMouseButtons{}
		, _arrPrevMouseButtons{}
		, _bInitialized{ 0 }
		, _bPollGamepad{ 0 }
		, _bPointerInside{ 0 }
		, _bPrevPointerInside{ 0 }
		, _bPointerEntered{ 0 }
		, _bPointerLeft{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	InputManager::~InputManager() = default;

	/**
	 * @brief 입력 매니저를 초기화하고 모든 키/마우스 버퍼를 0으로 리셋합니다.
	 */
	bool InputManager::initialize()
	{
		Memory::set( _arrKeys, 0, sizeof( _arrKeys ) );
		Memory::set( _arrPrevKeys, 0, sizeof( _arrPrevKeys ) );
		Memory::set( _arrMouseButtons, 0, sizeof( _arrMouseButtons ) );
		Memory::set( _arrPrevMouseButtons, 0, sizeof( _arrPrevMouseButtons ) );
		_mouseX				= 0;
		_mouseY				= 0;
		_prevMouseX			= 0;
		_prevMouseY			= 0;
		_mouseWheelDelta	= 0.0f;
		_mouseWheelAccum	= 0.0f;
		_bPointerInside		= 0;
		_bPrevPointerInside = 0;
		_bPointerEntered	= 0;
		_bPointerLeft		= 0;
		_gamepad			= make_unique<GamepadXInput>();
		_bPollGamepad		= 1;
		_bInitialized		= 1;
		SW_LOG_INFO( "Initialized." );
		return true;
	}

	/**
	 * @brief 입력 매니저를 종료하고 게임패드 리소스를 해제합니다.
	 */
	void InputManager::shutdown()
	{
		_gamepad.reset();
		_bPollGamepad = 0;
		_bInitialized = 0;
		SW_LOG_INFO( "Shut down." );
	}

	/**
	 * @brief 매 프레임 시작 시 호출되어 플랫폼 윈도우 이벤트를 폴링하고 휠 및 게임패드 상태를 갱신합니다.
	 */
	void InputManager::beginFrame()
	{
		if ( _bInitialized == 0 )
			return;

		_mouseWheelDelta = _mouseWheelAccum;
		_mouseWheelAccum = 0.0f;
		_bPointerEntered = 0;
		_bPointerLeft	 = 0;

		pollPlatform();
		updatePointerInside();
		if ( _bPollGamepad != 0 && _gamepad != nullptr )
			_gamepad->poll( 0 );
	}

	/**
	 * @brief 매 프레임 종료 시 호출되어 현재 프레임 입력 상태를 이전 프레임 버퍼로 복사(스냅샷)합니다.
	 */
	void InputManager::endFrame()
	{
		if ( _bInitialized == 0 )
			return;

		Memory::copy( _arrPrevKeys, _arrKeys, sizeof( _arrKeys ) );
		Memory::copy( _arrPrevMouseButtons, _arrMouseButtons, sizeof( _arrMouseButtons ) );
		_prevMouseX			= _mouseX;
		_prevMouseY			= _mouseY;
		_bPrevPointerInside = _bPointerInside;
		_mouseWheelDelta	= 0.0f;
	}

	/**
	 * @brief 게임패드 하드웨어 폴링 활성화 여부를 설정합니다.
	 */
	void InputManager::setGamepadPollingEnabled( bool enabled )
	{
		_bPollGamepad = enabled ? 1 : 0;
		if ( enabled && _gamepad == nullptr )
			_gamepad = make_unique<GamepadXInput>();
	}

	/**
	 * @brief 해당 키가 현재 프레임에 눌려있는 상태인지 검사합니다. (Held)
	 */
	bool InputManager::isKeyDown( Key key ) const
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return false;
		return _arrKeys[static_cast<size_t>( key )];
	}

	/**
	 * @brief 이전 프레임에는 안 눌렸다가 이번 프레임에 처음 눌렸는지 검사합니다. (Down Edge)
	 */
	bool InputManager::wasKeyPressed( Key key ) const
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return false;
		const size_t keyIndex = static_cast<size_t>( key );
		return ( _arrKeys[keyIndex] ) && ( _arrPrevKeys[keyIndex] == false );
	}

	/**
	 * @brief 이전 프레임에는 눌려있었다가 이번 프레임에 떼어졌는지 검사합니다. (Up Edge)
	 */
	bool InputManager::wasKeyReleased( Key key ) const
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return false;
		const size_t keyIndex = static_cast<size_t>( key );
		return ( _arrKeys[keyIndex] == false ) && ( _arrPrevKeys[keyIndex] );
	}

	/**
	 * @brief 현재 마우스 커서의 윈도우 클라이언트 기준 좌표(X, Y)를 반환합니다.
	 */
	void InputManager::getMousePosition( int32& outX, int32& outY ) const
	{
		outX = _mouseX;
		outY = _mouseY;
	}

	/**
	 * @brief 이전 프레임 대비 마우스의 이동 델타(Delta X, Delta Y)를 반환합니다.
	 */
	void InputManager::getMouseDelta( int32& outDx, int32& outDy ) const
	{
		outDx = _mouseX - _prevMouseX;
		outDy = _mouseY - _prevMouseY;
	}

	/**
	 * @brief 마우스 버튼이 현재 눌려있는지 검사합니다.
	 */
	bool InputManager::isMouseButtonDown( MouseButton button ) const
	{
		if ( button >= MouseButton::Count )
			return false;
		return _arrMouseButtons[static_cast<size_t>( button )];
	}

	/**
	 * @brief 마우스 버튼이 이번 프레임에 처음 클릭되었는지 검사합니다.
	 */
	bool InputManager::wasMouseButtonPressed( MouseButton button ) const
	{
		if ( button >= MouseButton::Count )
			return false;
		const size_t buttonIndex = static_cast<size_t>( button );
		return ( _arrMouseButtons[buttonIndex] ) && ( _arrPrevMouseButtons[buttonIndex] == false );
	}

	/**
	 * @brief 마우스 버튼이 이번 프레임에 떼어졌는지 검사합니다.
	 */
	bool InputManager::wasMouseButtonReleased( MouseButton button ) const
	{
		if ( button >= MouseButton::Count )
			return false;
		const size_t buttonIndex = static_cast<size_t>( button );
		return ( _arrMouseButtons[buttonIndex] == false ) && ( _arrPrevMouseButtons[buttonIndex] );
	}

	void InputManager::setKeyDown( Key key, bool bDown )
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return;
		_arrKeys[static_cast<size_t>( key )] = bDown;
	}

	void InputManager::setMouseButtonDown( MouseButton button, bool bDown )
	{
		if ( button >= MouseButton::Count )
			return;
		_arrMouseButtons[static_cast<size_t>( button )] = bDown;
	}

#if !defined( SW_PLATFORM_WINDOWS ) && !defined( SW_PLATFORM_LINUX )
	void InputManager::pollPlatform()
	{
	}

	void InputManager::processNativeEvent( const NativeWindowEvent& )
	{
	}
#endif

	void InputManager::updatePointerInside()
	{
		bool		   bIsMouseInside{ false };
		const IWindow* pWindow = IWindow::getActiveWindow();
		if ( pWindow != nullptr )
		{
			const int32 width  = static_cast<int32>( pWindow->getWidth() );
			const int32 height = static_cast<int32>( pWindow->getHeight() );
			bIsMouseInside	   = ( 0 <= _mouseX && _mouseX < width && 0 <= _mouseY && _mouseY < height );
		}
		_bPointerInside	 = ( bIsMouseInside ) ? 1 : 0;
		_bPointerEntered = ( bIsMouseInside && _bPrevPointerInside == 0 ) ? 1 : 0;
		_bPointerLeft	 = ( bIsMouseInside == false && _bPrevPointerInside != 0 ) ? 1 : 0;
	}
} // namespace sw
