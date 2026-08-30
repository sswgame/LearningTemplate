#include "pch.h"

#include "Engine/Input/InputManager.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/Windows/GamepadXInput.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
	SW_LOG_CALLER( "InputManager" );

	InputManager::InputManager()
		: _gamepad{ nullptr }
		, _actionMap{ make_unique<ActionMap>() }
		, _mouseX{ 0 }
		, _mouseY{ 0 }
		, _prevMouseX{ 0 }
		, _prevMouseY{ 0 }
		, _mouseWheelDelta{ 0.0f }
		, _mouseWheelAccum{ 0.0f }
		, _arrKey{}
		, _arrPrevKey{}
		, _arrMouseButton{}
		, _arrPrevMouseButton{}
		, _bInitialized{ SW_FALSE }
		, _bPollGamepad{ SW_FALSE }
		, _bPointerInside{ SW_FALSE }
		, _bPrevPointerInside{ SW_FALSE }
		, _bPointerEntered{ SW_FALSE }
		, _bPointerLeft{ SW_FALSE }
		, _reservedFlags{ 0 }
	{
	}

	InputManager::~InputManager() = default;

	ActionMap& InputManager::getActionMap()
	{
		return *_actionMap;
	}

	const ActionMap& InputManager::getActionMap() const
	{
		return *_actionMap;
	}

	/**
	 * @brief 입력 매니저를 초기화하고 모든 키/마우스 버퍼를 0으로 리셋합니다.
	 */
	bool InputManager::initialize()
	{
		Memory::set( _arrKey, 0, sizeof( _arrKey ) );
		Memory::set( _arrPrevKey, 0, sizeof( _arrPrevKey ) );
		Memory::set( _arrMouseButton, 0, sizeof( _arrMouseButton ) );
		Memory::set( _arrPrevMouseButton, 0, sizeof( _arrPrevMouseButton ) );
		_mouseX				= 0;
		_mouseY				= 0;
		_prevMouseX			= 0;
		_prevMouseY			= 0;
		_mouseWheelDelta	= 0.0f;
		_mouseWheelAccum	= 0.0f;
		_bPointerInside		= SW_FALSE;
		_bPrevPointerInside = SW_FALSE;
		_bPointerEntered	= SW_FALSE;
		_bPointerLeft		= SW_FALSE;
		_gamepad			= make_unique<GamepadXInput>();
		_bPollGamepad		= SW_TRUE;
		_bInitialized		= SW_TRUE;
		SW_LOG_INFO( "Initialized." );
		return true;
	}

	/**
	 * @brief 입력 매니저를 종료하고 게임패드 리소스를 해제합니다.
	 */
	void InputManager::shutdown()
	{
		_gamepad.reset();
		_bPollGamepad = SW_FALSE;
		_bInitialized = SW_FALSE;
		SW_LOG_INFO( "Shut down." );
	}

	/**
	 * @brief 매 프레임 시작 시 호출되어 플랫폼 윈도우 이벤트를 폴링하고 휠 및 게임패드 상태를 갱신합니다.
	 */
	void InputManager::beginFrame()
	{
		if ( _bInitialized == SW_FALSE )
			return;

		_mouseWheelDelta = _mouseWheelAccum;
		_mouseWheelAccum = 0.0f;
		_bPointerEntered = SW_FALSE;
		_bPointerLeft	 = SW_FALSE;

		pollPlatform();
		updatePointerInside();
		if ( _bPollGamepad == SW_TRUE && _gamepad != nullptr )
			_gamepad->poll( 0 );
	}

	/**
	 * @brief 매 프레임 종료 시 호출되어 현재 프레임 입력 상태를 이전 프레임 버퍼로 복사(스냅샷)합니다.
	 */
	void InputManager::endFrame()
	{
		if ( _bInitialized == SW_FALSE )
			return;

		Memory::copy( _arrPrevKey, _arrKey, sizeof( _arrKey ) );
		Memory::copy( _arrPrevMouseButton, _arrMouseButton, sizeof( _arrMouseButton ) );
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
		_bPollGamepad = enabled ? SW_TRUE : SW_FALSE;
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
		return _arrKey[static_cast<size_t>( key )];
	}

	/**
	 * @brief 이전 프레임에는 안 눌렸다가 이번 프레임에 처음 눌렸는지 검사합니다. (Down Edge)
	 */
	bool InputManager::wasKeyPressed( Key key ) const
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return false;
		const size_t keyIndex = static_cast<size_t>( key );
		return ( _arrKey[keyIndex] == true ) && ( _arrPrevKey[keyIndex] == false );
	}

	/**
	 * @brief 이전 프레임에는 눌려있었다가 이번 프레임에 떼어졌는지 검사합니다. (Up Edge)
	 */
	bool InputManager::wasKeyReleased( Key key ) const
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return false;
		const size_t keyIndex = static_cast<size_t>( key );
		return ( _arrKey[keyIndex] == false ) && ( _arrPrevKey[keyIndex] == true );
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
		return _arrMouseButton[static_cast<size_t>( button )];
	}

	/**
	 * @brief 마우스 버튼이 이번 프레임에 처음 클릭되었는지 검사합니다.
	 */
	bool InputManager::wasMouseButtonPressed( MouseButton button ) const
	{
		if ( button >= MouseButton::Count )
			return false;
		const size_t buttonIndex = static_cast<size_t>( button );
		return ( _arrMouseButton[buttonIndex] == true ) && ( _arrPrevMouseButton[buttonIndex] == false );
	}

	/**
	 * @brief 마우스 버튼이 이번 프레임에 떼어졌는지 검사합니다.
	 */
	bool InputManager::wasMouseButtonReleased( MouseButton button ) const
	{
		if ( button >= MouseButton::Count )
			return false;
		const size_t buttonIndex = static_cast<size_t>( button );
		return ( _arrMouseButton[buttonIndex] == false ) && ( _arrPrevMouseButton[buttonIndex] == true );
	}

	void InputManager::setKeyDown( Key key, bool bDown )
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return;
		_arrKey[static_cast<size_t>( key )] = bDown;
	}

	void InputManager::setMouseButtonDown( MouseButton button, bool bDown )
	{
		if ( button >= MouseButton::Count )
			return;
		_arrMouseButton[static_cast<size_t>( button )] = bDown;
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
		_bPointerInside	 = bIsMouseInside ? SW_TRUE : SW_FALSE;
		_bPointerEntered = ( bIsMouseInside && _bPrevPointerInside == SW_FALSE ) ? SW_TRUE : SW_FALSE;
		_bPointerLeft	 = ( bIsMouseInside == false && _bPrevPointerInside == SW_TRUE ) ? SW_TRUE : SW_FALSE;
	}
} // namespace sw
