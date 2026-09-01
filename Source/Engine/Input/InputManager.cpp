#include "pch.h"

#include "Engine/Input/InputManager.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"

#if defined( _WIN32 )
	#include "Engine/Input/Windows/GamepadXInput.h"
#endif

namespace sw
{
	SW_LOG_CALLER( "InputManager" );

	InputManager::InputManager()
		: _lockFreeQueue{}
		, _listDevice{}
		, _pKeyboard{ nullptr }
		, _pMouse{ nullptr }
		, _pGamepad{ nullptr }
		, _pActionMap{ nullptr }
		, _listDrainedEvent{}
		, _activeDeviceType{ InputDeviceType::KeyboardMouse }
		, _onActiveDeviceChanged{}
		, _onTextInput{}
		, _bInitialized{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	InputManager::~InputManager()
	{
		shutdown();
	}

	bool InputManager::initialize()
	{
		if ( _bInitialized == SW_TRUE )
			return true;

		_listDevice.clear();
		_lockFreeQueue.clear();
		_listDrainedEvent.clear();

		// 1) 표준 키보드 장치 등록
		auto pKeyboard = make_unique<KeyboardDevice>();
		_pKeyboard	   = pKeyboard.get();
		registerDevice( std::move( pKeyboard ) );

		// 2) 표준 마우스 장치 등록
		auto pMouse = make_unique<MouseDevice>();
		_pMouse		= pMouse.get();
		registerDevice( std::move( pMouse ) );

		// 3) 표준 게임패드 장치 등록 (플랫폼별)
#if defined( _WIN32 )
		auto pGamepad = make_unique<GamepadXInput>( 0 );
		_pGamepad	  = pGamepad.get();
		registerDevice( std::move( pGamepad ) );
#endif

		// 4) 통합 ActionMap 인스턴스 생성 및 연결
		_pActionMap = make_unique<ActionMap>();
		_pActionMap->setInputManager( this );

		_bInitialized = SW_TRUE;
		SW_LOG_INFO( "InputManager initialized with %d devices.", static_cast<int32>( _listDevice.size() ) );
		return true;
	}

	void InputManager::shutdown()
	{
		if ( _bInitialized == SW_FALSE )
			return;

		for ( auto& pDevice : _listDevice )
		{
			if ( pDevice != nullptr )
				pDevice->resetState();
		}

		_listDevice.clear();
		_pKeyboard	= nullptr;
		_pMouse		= nullptr;
		_pGamepad	= nullptr;
		_pActionMap = nullptr;
		_lockFreeQueue.clear();
		_listDrainedEvent.clear();

		_bInitialized = SW_FALSE;
		SW_LOG_INFO( "InputManager shut down." );
	}

	bool InputManager::postRawEvent( const RawInputEvent& rawEvent )
	{
		return _lockFreeQueue.push( rawEvent );
	}

	uint32 InputManager::drainRawEvents( RawInputEvent* pOutBuffer, uint32 maxCount )
	{
		return _lockFreeQueue.drain( pOutBuffer, maxCount );
	}

	void InputManager::registerDevice( unique_ptr<IInputDevice> pDevice )
	{
		if ( pDevice == nullptr )
			return;

		if ( pDevice->getDeviceKind() == InputDeviceKind::Keyboard && _pKeyboard == nullptr )
			_pKeyboard = static_cast<KeyboardDevice*>( pDevice.get() );
		else if ( pDevice->getDeviceKind() == InputDeviceKind::Mouse && _pMouse == nullptr )
			_pMouse = static_cast<MouseDevice*>( pDevice.get() );
		else if ( pDevice->getDeviceKind() == InputDeviceKind::Gamepad && _pGamepad == nullptr )
			_pGamepad = static_cast<GamepadDevice*>( pDevice.get() );

		_listDevice.push_back( std::move( pDevice ) );
	}

	void InputManager::unregisterDevice( IInputDevice* pDevice )
	{
		if ( pDevice == nullptr )
			return;

		if ( _pKeyboard == pDevice )
			_pKeyboard = nullptr;
		if ( _pMouse == pDevice )
			_pMouse = nullptr;
		if ( _pGamepad == pDevice )
			_pGamepad = nullptr;

		for ( auto it = _listDevice.begin(); it != _listDevice.end(); ++it )
		{
			if ( it->get() == pDevice )
			{
				_listDevice.erase( it );
				break;
			}
		}
	}

	IInputDevice* InputManager::getDevice( InputDeviceKind kind, uint32 deviceIndex ) const
	{
		for ( const auto& pDev : _listDevice )
		{
			if ( pDev != nullptr && pDev->getDeviceKind() == kind && pDev->getDeviceIndex() == deviceIndex )
				return pDev.get();
		}
		return nullptr;
	}

	GamepadDevice* InputManager::getGamepad( uint32 deviceIndex ) const
	{
		IInputDevice* pDev = getDevice( InputDeviceKind::Gamepad, deviceIndex );
		return static_cast<GamepadDevice*>( pDev );
	}

	void InputManager::beginFrame( float32 deltaSeconds )
	{
		// 1) 락프리 큐에서 비동기 인입된 원시 이벤트들을 일괄 드레인
		_listDrainedEvent.clear();
		_lockFreeQueue.drain( _listDrainedEvent );

		// 2) 드레인된 원시 이벤트를 각 디바이스로 디스패치
		for ( const RawInputEvent& rawEvt : _listDrainedEvent )
		{
			dispatchRawEvent( rawEvt );
		}

		// 3) 등록된 모든 장치에 대해 프레임 시작 및 폴링 호출
		for ( auto& pDev : _listDevice )
		{
			if ( pDev != nullptr )
			{
				pDev->onFrameBegin( deltaSeconds );
				pDev->poll( deltaSeconds );
			}
		}

		// 4) 활성 장치 자동 감지
		if ( _pGamepad != nullptr && _pGamepad->isConnected() )
		{
			float32 sx{ 0.0f };
			float32 sy{ 0.0f };
			_pGamepad->getLeftStick( sx, sy );
			const bool bStickActive = ( sx * sx + sy * sy ) > 0.04f;
			if ( bStickActive || _pGamepad->getLeftTrigger() > 0.1f || _pGamepad->getRightTrigger() > 0.1f )
			{
				setActiveDeviceType( InputDeviceType::GamepadXbox );
			}
			else
			{
				for ( size_t btnIdx = 0; btnIdx < static_cast<size_t>( GamepadButton::Count ); ++btnIdx )
				{
					if ( _pGamepad->wasButtonPressed( static_cast<GamepadButton>( btnIdx ) ) )
					{
						setActiveDeviceType( InputDeviceType::GamepadXbox );
						break;
					}
				}
			}
		}

		if ( _pKeyboard != nullptr )
		{
			for ( size_t keyIdx = 1; keyIdx < static_cast<size_t>( Key::Count ); ++keyIdx )
			{
				if ( _pKeyboard->wasKeyPressed( static_cast<Key>( keyIdx ) ) )
				{
					setActiveDeviceType( InputDeviceType::KeyboardMouse );
					break;
				}
			}
		}

		if ( _pMouse != nullptr )
		{
			for ( size_t btnIdx = 0; btnIdx < static_cast<size_t>( MouseButton::Count ); ++btnIdx )
			{
				if ( _pMouse->wasButtonPressed( static_cast<MouseButton>( btnIdx ) ) )
				{
					setActiveDeviceType( InputDeviceType::KeyboardMouse );
					break;
				}
			}
		}
	}

	void InputManager::dispatchRawEvent( const RawInputEvent& rawEvt )
	{
		switch ( rawEvt._type )
		{
			case RawInputEventType::KeyDown:
				if ( _pKeyboard != nullptr )
					_pKeyboard->setKeyDown( rawEvt._payload._keyData._key, true );
				setActiveDeviceType( InputDeviceType::KeyboardMouse );
				break;

			case RawInputEventType::KeyUp:
				if ( _pKeyboard != nullptr )
					_pKeyboard->setKeyDown( rawEvt._payload._keyData._key, false );
				break;

			case RawInputEventType::MouseMove:
				if ( _pMouse != nullptr )
				{
					_pMouse->setPosition( rawEvt._payload._mouseData._x, rawEvt._payload._mouseData._y );
					_pMouse->addRawDelta( rawEvt._payload._mouseData._rawDeltaX, rawEvt._payload._mouseData._rawDeltaY );
				}
				setActiveDeviceType( InputDeviceType::KeyboardMouse );
				break;

			case RawInputEventType::MouseButtonDown:
				if ( _pMouse != nullptr )
				{
					_pMouse->setPosition( rawEvt._payload._mouseData._x, rawEvt._payload._mouseData._y );
					_pMouse->setButtonDown( rawEvt._payload._mouseData._button, true );
				}
				setActiveDeviceType( InputDeviceType::KeyboardMouse );
				break;

			case RawInputEventType::MouseButtonUp:
				if ( _pMouse != nullptr )
				{
					_pMouse->setPosition( rawEvt._payload._mouseData._x, rawEvt._payload._mouseData._y );
					_pMouse->setButtonDown( rawEvt._payload._mouseData._button, false );
				}
				break;

			case RawInputEventType::MouseWheel:
				if ( _pMouse != nullptr )
					_pMouse->addWheelDelta( rawEvt._payload._mouseData._wheelDelta );
				break;

			case RawInputEventType::GamepadButtonDown:
			{
				GamepadDevice* pPad = getGamepad( rawEvt._deviceIndex );
				if ( pPad != nullptr )
					pPad->setButtonDown( rawEvt._payload._gamepadData._button, true );
				setActiveDeviceType( InputDeviceType::GamepadXbox );
				break;
			}

			case RawInputEventType::GamepadButtonUp:
			{
				GamepadDevice* pPad = getGamepad( rawEvt._deviceIndex );
				if ( pPad != nullptr )
					pPad->setButtonDown( rawEvt._payload._gamepadData._button, false );
				break;
			}

			case RawInputEventType::GamepadAxis:
			{
				GamepadDevice* pPad = getGamepad( rawEvt._deviceIndex );
				if ( pPad != nullptr )
					pPad->setAxis( rawEvt._payload._gamepadData._axisIndex, rawEvt._payload._gamepadData._axisValue );
				break;
			}

			case RawInputEventType::TextInput:
				onTextInput( rawEvt._payload._textData._arrUtf8 );
				break;

			case RawInputEventType::FocusGained:
				break;

			case RawInputEventType::FocusLost:
				onWindowFocusLost();
				break;

			case RawInputEventType::None:
			default:
				break;
		}
	}

	void InputManager::endFrame()
	{
		for ( auto& pDev : _listDevice )
		{
			if ( pDev != nullptr )
				pDev->onFrameEnd();
		}
	}

	void InputManager::onWindowFocusLost()
	{
		for ( auto& pDev : _listDevice )
		{
			if ( pDev != nullptr )
				pDev->resetState();
		}
	}

	void InputManager::setActiveDeviceType( InputDeviceType type )
	{
		if ( _activeDeviceType != type )
		{
			_activeDeviceType = type;
			if ( _onActiveDeviceChanged.isBound() )
				_onActiveDeviceChanged( type );
		}
	}

	bool InputManager::wasAnyInputPressed() const
	{
		if ( _pKeyboard != nullptr )
		{
			for ( size_t keyIdx = 1; keyIdx < static_cast<size_t>( Key::Count ); ++keyIdx )
			{
				if ( _pKeyboard->wasKeyPressed( static_cast<Key>( keyIdx ) ) )
					return true;
			}
		}

		if ( _pMouse != nullptr )
		{
			for ( size_t btnIdx = 0; btnIdx < static_cast<size_t>( MouseButton::Count ); ++btnIdx )
			{
				if ( _pMouse->wasButtonPressed( static_cast<MouseButton>( btnIdx ) ) )
					return true;
			}
		}

		if ( _pGamepad != nullptr && _pGamepad->isConnected() )
		{
			for ( size_t btnIdx = 0; btnIdx < static_cast<size_t>( GamepadButton::Count ); ++btnIdx )
			{
				if ( _pGamepad->wasButtonPressed( static_cast<GamepadButton>( btnIdx ) ) )
					return true;
			}
		}

		return false;
	}

	void InputManager::onTextInput( string_view text )
	{
		if ( text.empty() == false && _onTextInput.isBound() )
			_onTextInput( text );
	}

	void InputManager::getMousePosition( int32& outX, int32& outY ) const
	{
		outX = _pMouse != nullptr ? _pMouse->getPositionX() : 0;
		outY = _pMouse != nullptr ? _pMouse->getPositionY() : 0;
	}

	void InputManager::getMouseDelta( int32& outDx, int32& outDy ) const
	{
		if ( _pMouse != nullptr )
			_pMouse->getDelta( outDx, outDy );
		else
		{
			outDx = 0;
			outDy = 0;
		}
	}

	void InputManager::getRawMouseDelta( float32& outDx, float32& outDy ) const
	{
		if ( _pMouse != nullptr )
			_pMouse->getRawDelta( outDx, outDy );
		else
		{
			outDx = 0.0f;
			outDy = 0.0f;
		}
	}

	float32 InputManager::getGamepadLeftTrigger( uint32 deviceIndex ) const
	{
		GamepadDevice* pPad = getGamepad( deviceIndex );
		return pPad != nullptr ? pPad->getLeftTrigger() : 0.0f;
	}

	float32 InputManager::getGamepadRightTrigger( uint32 deviceIndex ) const
	{
		GamepadDevice* pPad = getGamepad( deviceIndex );
		return pPad != nullptr ? pPad->getRightTrigger() : 0.0f;
	}

	bool InputManager::setGamepadVibration( float32 leftMotor, float32 rightMotor, uint32 deviceIndex )
	{
		GamepadDevice* pPad = getGamepad( deviceIndex );
		return pPad != nullptr ? pPad->setVibration( leftMotor, rightMotor ) : false;
	}
} // namespace sw
