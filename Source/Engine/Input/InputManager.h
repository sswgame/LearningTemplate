/**
 * @file InputManager.h
 * @brief App / Game용 다형적 입력 장치 레지스트리, 락프리 비동기 이벤트 큐 및 이벤트 디스패치 중앙 허브
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodes.h"
#include "Engine/Input/Queue/LockFreeInputQueue.h"

namespace sw
{
	struct NativeWindowEvent;
	class ActionMap;

	/** @brief 현재 활성화된 입력 장치 타입 (UI 글리프 자동 변환용) */
	enum class InputDeviceType : uint8
	{
		KeyboardMouse = 0,
		GamepadXbox,
		GamepadPlayStation
	};

	/**
	 * @class InputManager
	 * @brief 다형적 IInputDevice들을 등록·관리하고, 락프리 원시 이벤트 큐를 통해 OS 메시지를 프레임 동기화하는 중앙 허브
	 */
	class SW_API InputManager
	{
	public:
		using ActiveDeviceChangedDelegate = Delegate<void( InputDeviceType )>;
		using TextInputDelegate			  = Delegate<void( string_view )>;

		InputManager();
		~InputManager();

		// ------------------------------------------------------------------------------
		// 1) 수명주기 및 프레임 제어
		// ------------------------------------------------------------------------------
		bool initialize();
		void shutdown();

		/** @brief 프레임 시작 시 락프리 큐 드레인, 디바이스 상태 갱신 및 프레임 엣지 동기화 */
		void beginFrame( float32 deltaSeconds = 0.016f );
		/** @brief 프레임 종료 시 엣지 플래그 및 원시 델타 리셋 */
		void endFrame();
		/** @brief 윈도우 포커스 아웃 시 모든 장치 입력 상태 초기화 */
		void onWindowFocusLost();

		// ------------------------------------------------------------------------------
		// 2) 락프리 원시 이벤트 큐 (Lock-Free Event Queue)
		// ------------------------------------------------------------------------------
		/** @brief OS 윈도우 스레드 / 백그라운드 폴러에서 락 없이 원시 이벤트를 인입합니다. */
		bool postRawEvent( const RawInputEvent& rawEvent );
		/** @brief 대기 중인 원시 이벤트를 드레인합니다. */
		uint32 drainRawEvents( RawInputEvent* pOutBuffer, uint32 maxCount );
		uint32 getPendingRawEventCount() const { return _lockFreeQueue.getCount(); }

		// ------------------------------------------------------------------------------
		// 3) 다형적 디바이스 레지스트리 (Device Registry)
		// ------------------------------------------------------------------------------
		void			registerDevice( unique_ptr<IInputDevice> pDevice );
		void			unregisterDevice( IInputDevice* pDevice );
		IInputDevice*	getDevice( InputDeviceKind kind, uint32 deviceIndex = 0 ) const;
		KeyboardDevice* getKeyboard() const { return _pKeyboard; }
		MouseDevice*	getMouse() const { return _pMouse; }
		GamepadDevice*	getGamepad( uint32 deviceIndex = 0 ) const;
		void			setGamepadPollingEnabled( [[maybe_unused]] bool bEnabled ) {}

		// ------------------------------------------------------------------------------
		// 4) ActionMap & 장치 상태 조회
		// ------------------------------------------------------------------------------
		ActionMap&		 getActionMap() { return *_pActionMap; }
		const ActionMap& getActionMap() const { return *_pActionMap; }

		InputDeviceType getActiveDeviceType() const { return _activeDeviceType; }
		void			setActiveDeviceType( InputDeviceType type );
		void			setActiveDeviceChangedCallback( ActiveDeviceChangedDelegate callback ) { _onActiveDeviceChanged = std::move( callback ); }

		bool wasAnyInputPressed() const;
		void onTextInput( string_view text );
		void setTextInputCallback( TextInputDelegate callback ) { _onTextInput = std::move( callback ); }

		// ------------------------------------------------------------------------------
		// 5) 키보드/마우스 편의성 위임 포워딩 API (100% 호환성 보장)
		// ------------------------------------------------------------------------------
		bool isKeyDown( Key key ) const { return _pKeyboard != nullptr ? _pKeyboard->isKeyDown( key ) : false; }
		bool wasKeyPressed( Key key ) const { return _pKeyboard != nullptr ? _pKeyboard->wasKeyPressed( key ) : false; }
		bool wasKeyReleased( Key key ) const { return _pKeyboard != nullptr ? _pKeyboard->wasKeyReleased( key ) : false; }

		bool isMouseButtonDown( MouseButton button ) const { return _pMouse != nullptr ? _pMouse->isButtonDown( button ) : false; }
		bool wasMouseButtonPressed( MouseButton button ) const { return _pMouse != nullptr ? _pMouse->wasButtonPressed( button ) : false; }
		bool wasMouseButtonReleased( MouseButton button ) const { return _pMouse != nullptr ? _pMouse->wasButtonReleased( button ) : false; }

		void	getMousePosition( int32& outX, int32& outY ) const;
		int32	getMousePositionX() const { return _pMouse != nullptr ? _pMouse->getPositionX() : 0; }
		int32	getMousePositionY() const { return _pMouse != nullptr ? _pMouse->getPositionY() : 0; }
		void	getMouseDelta( int32& outDx, int32& outDy ) const;
		void	getRawMouseDelta( float32& outDx, float32& outDy ) const;
		float32 getMouseWheel() const { return _pMouse != nullptr ? _pMouse->getMouseWheel() : 0.0f; }
		float32 getMouseWheelDelta() const { return getMouseWheel(); }

		bool isPointerInside() const { return _pMouse != nullptr ? _pMouse->isPointerInside() : false; }
		bool wasPointerEntered() const { return _pMouse != nullptr ? _pMouse->wasPointerEntered() : false; }
		bool wasPointerLeft() const { return _pMouse != nullptr ? _pMouse->wasPointerLeft() : false; }

		CursorLockMode getCursorLockMode() const { return _pMouse != nullptr ? _pMouse->getCursorLockMode() : CursorLockMode::None; }
		void		   setCursorLockMode( CursorLockMode mode )
		{
			if ( _pMouse != nullptr )
				_pMouse->setCursorLockMode( mode );
		}
		bool isCursorVisible() const { return _pMouse != nullptr ? _pMouse->isCursorVisible() : true; }
		void setCursorVisible( bool bVisible )
		{
			if ( _pMouse != nullptr )
				_pMouse->setCursorVisible( bVisible );
		}

		// ------------------------------------------------------------------------------
		// 6) 게임패드 편의성 위임 포워딩 API
		// ------------------------------------------------------------------------------
		float32 getGamepadLeftTrigger( uint32 deviceIndex = 0 ) const;
		float32 getGamepadRightTrigger( uint32 deviceIndex = 0 ) const;
		bool	setGamepadVibration( float32 leftMotor, float32 rightMotor, uint32 deviceIndex = 0 );

		// ------------------------------------------------------------------------------
		// 7) 플랫폼 네이티브 이벤트 처리
		// ------------------------------------------------------------------------------
		void onNativeWindowEvent( const NativeWindowEvent& event );
		void processNativeEvent( const NativeWindowEvent& event );
		void pollPlatform();

	private:
		void dispatchRawEvent( const RawInputEvent& rawEvt );

	private:
		LockFreeInputQueue<RawInputEvent, 2048> _lockFreeQueue;
		vector<unique_ptr<IInputDevice>>		_listDevice;
		KeyboardDevice*							_pKeyboard;
		MouseDevice*							_pMouse;
		GamepadDevice*							_pGamepad;
		unique_ptr<ActionMap>					_pActionMap;
		vector<RawInputEvent>					_listDrainedEvent;
		InputDeviceType							_activeDeviceType;
		ActiveDeviceChangedDelegate				_onActiveDeviceChanged;
		TextInputDelegate						_onTextInput;
		uint8									_bInitialized : 1;
		[[maybe_unused]] uint8					_reserved	  : 7;
	};
} // namespace sw
