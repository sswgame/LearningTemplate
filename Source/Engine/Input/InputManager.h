/**
 * @file InputManager.h
 * @brief App / Game용 다형적 입력 장치 레지스트리, 락프리 비동기 이벤트 큐 및 이벤트 디스패치 중앙 허브
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputSnapshot.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
	struct NativeWindowEvent;
	class ActionMap;

	/** @brief 현재 활성화된 입력 장치 타입 (UI 글리프 자동 변환용) */
	enum class InputDeviceType : uint8
	{
		KeyboardMouse = 0,
		GamepadXbox,
		GamepadPlayStation,
		GamepadSwitch
	};

	/**
	 * @class InputManager
	 * @brief 다형적 IInputDevice들을 등록·관리하고, 락프리 원시 이벤트 큐를 통해 OS 메시지를 프레임 동기화하는 중앙 허브
	 */
	class SW_API InputManager
	{
	public:
		using ActiveDeviceChangedDelegate = Delegate<void( InputDeviceType )>;
		using GamepadConnectionDelegate	  = Delegate<void( uint32, bool )>;
		using TextInputDelegate			  = Delegate<void( string_view )>;

		InputManager();
		~InputManager();

		InputManager( const InputManager& )			   = delete;
		InputManager& operator=( const InputManager& ) = delete;

		// ------------------------------------------------------------------------------
		// 1) 수명주기 및 프레임 제어
		// ------------------------------------------------------------------------------
		bool initialize();
		void shutdown();

		/** @brief 프레임 시작 시 락프리 큐 드레인, 디바이스 상태 갱신 및 프레임 엣지 동기화 */
		void beginFrame( float32 deltaSeconds = 0.016f );
		/** @brief 프레임 종료 시 엣지 플래그 및 원시 델타 리셋 */
		void endFrame();
		/** @brief 윈도우 포커스 인 시 마우스 락 모드 재적용 */
		void onWindowFocusGained();
		/** @brief 윈도우 포커스 아웃 시 모든 장치 입력 상태 초기화 및 마우스 클리핑 해제 */
		void onWindowFocusLost();
		/** @brief 등록된 모든 장치의 입력 상태(키/버튼/축)를 초기화합니다 (리플레이 재동기화 등에 사용). */
		void resetAllDeviceState();

		// ------------------------------------------------------------------------------
		// 2) 락프리 원시 이벤트 큐 (Lock-Free Event Queue)
		// ------------------------------------------------------------------------------
		/** @brief OS 윈도우 스레드 / 백그라운드 폴러에서 락 없이 원시 이벤트를 인입합니다. */
		bool postRawEvent( const RawInputEvent& rawEvent );
		/** @brief 대기 중인 원시 이벤트를 드레인합니다. */
		uint32 drainRawEvents( RawInputEvent* pOutBuffer, uint32 maxCount );
		uint32 getPendingRawEventCount() const { return _queueRawEvent.getCount(); }

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
		void			setGamepadConnectionCallback( GamepadConnectionDelegate callback ) { _onGamepadConnectionChanged = std::move( callback ); }
		void			setTextInputCallback( TextInputDelegate callback ) { _onTextInput = std::move( callback ); }
		void			setTextCompositionCallback( TextInputDelegate callback ) { _onTextComposition = std::move( callback ); }

		bool wasAnyInputPressed() const;
		void onTextInput( string_view text );
		void onTextComposition( string_view text );

		void setInputMuted( bool bMuted ) { _bInputMuted = bMuted ? SW_TRUE : SW_FALSE; }
		bool isInputMuted() const { return _bInputMuted == SW_TRUE; }

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
		void	getMousePositionNormalized( float32& outNormX, float32& outNormY ) const;
		void	getMouseDelta( int32& outDx, int32& outDy ) const;
		void	getRawMouseDelta( float32& outDx, float32& outDy ) const;
		float32 getMouseWheel() const { return _pMouse != nullptr ? _pMouse->getMouseWheel() : 0.0f; }
		float32 getMouseWheelDelta() const { return getMouseWheel(); }
		float32 getMouseWheelHorizontal() const { return _pMouse != nullptr ? _pMouse->getMouseWheelHorizontal() : 0.0f; }

		bool isPointerInside() const { return _pMouse != nullptr ? _pMouse->isPointerInside() : false; }
		bool wasPointerEntered() const { return _pMouse != nullptr ? _pMouse->wasPointerEntered() : false; }
		bool wasPointerLeft() const { return _pMouse != nullptr ? _pMouse->wasPointerLeft() : false; }

		MouseLockMode  getMouseLockMode() const { return _pMouse != nullptr ? _pMouse->getLockMode() : MouseLockMode::None; }
		void		   setMouseLockMode( MouseLockMode mode );
		CursorLockMode getCursorLockMode() const { return getMouseLockMode(); }
		void		   setCursorLockMode( CursorLockMode mode ) { setMouseLockMode( mode ); }
		bool		   isCursorVisible() const { return _pMouse != nullptr ? _pMouse->isCursorVisible() : true; }
		void		   setCursorVisible( bool bVisible );

		void setMouseClipSubRect( int32 left, int32 top, int32 right, int32 bottom );
		bool getMouseClipSubRect( int32& outLeft, int32& outTop, int32& outRight, int32& outBottom ) const
		{
			return _pMouse != nullptr && _pMouse->getClipSubRect( outLeft, outTop, outRight, outBottom );
		}
		void clearMouseClipSubRect();
		void applyMouseLockMode();
		void releaseMouseLockMode();

		void setMouseSmoothing( float32 factor )
		{
			if ( _pMouse != nullptr )
				_pMouse->setSmoothing( factor );
		}
		float32 getMouseSmoothing() const { return _pMouse != nullptr ? _pMouse->getSmoothing() : 0.0f; }
		void	setMouseAcceleration( float32 power )
		{
			if ( _pMouse != nullptr )
				_pMouse->setAcceleration( power );
		}
		float32 getMouseAcceleration() const { return _pMouse != nullptr ? _pMouse->getAcceleration() : 1.0f; }
		void	getSmoothMouseDelta( float32& outDx, float32& outDy ) const
		{
			if ( _pMouse != nullptr )
				_pMouse->getSmoothDelta( outDx, outDy );
			else
			{
				outDx = 0.0f;
				outDy = 0.0f;
			}
		}

		// ------------------------------------------------------------------------------
		// 6) 게임패드 편의성 위임 포워딩 API
		// ------------------------------------------------------------------------------
		float32			   getGamepadLeftTrigger( uint32 deviceIndex = 0 ) const;
		float32			   getGamepadRightTrigger( uint32 deviceIndex = 0 ) const;
		GamepadBatteryInfo getGamepadBatteryInfo( uint32 deviceIndex = 0 ) const
		{
			GamepadDevice* pPad = getGamepad( deviceIndex );
			return pPad != nullptr ? pPad->getBatteryInfo() : GamepadBatteryInfo{};
		}
		bool setGamepadVibration( float32 leftMotor, float32 rightMotor, uint32 deviceIndex = 0 );
		bool playGamepadVibration( float32 leftMotor, float32 rightMotor, float32 durationSeconds, uint32 deviceIndex = 0 );

		// ------------------------------------------------------------------------------
		// 8) 롤백 / 리플레이 입력 스냅샷 버퍼 (Input Snapshot & History) & 가상 입력 주입
		// ------------------------------------------------------------------------------
		void					  recordSnapshot( uint32 tickNumber );
		const InputSnapshot*	  getSnapshot( uint32 tickNumber ) const { return _inputHistory.getSnapshot( tickNumber ); }
		const InputSnapshot*	  getLatestSnapshot() const { return _inputHistory.getLatestSnapshot(); }
		const InputHistoryBuffer& getInputHistory() const { return _inputHistory; }

		void injectRawEvent( const RawInputEvent& evt ) { postRawEvent( evt ); }
		void injectSnapshot( const InputSnapshot& snapshot );

		// ------------------------------------------------------------------------------
		// 7) 플랫폼 네이티브 이벤트 처리 및 접근성 제어
		// ------------------------------------------------------------------------------
		void onNativeWindowEvent( const NativeWindowEvent& event );
		void processNativeEvent( const NativeWindowEvent& event );
		void pollPlatform();
		void disableWindowsAccessibilityShortcuts();
		void restoreWindowsAccessibilityShortcuts();

	private:
		void dispatchRawEvent( const RawInputEvent& rawEvt );

	private:
		ConcurrentQueue<RawInputEvent, 2048> _queueRawEvent;	/**< OS/폴러 스레드가 postRawEvent()로 넣는 락프리 원시 이벤트 큐. beginFrame()이 매 프레임 드레인. */
		vector<unique_ptr<IInputDevice>>	 _listDevice;		/**< 등록된 모든 장치(키보드/마우스/게임패드 등)의 소유 목록. */
		KeyboardDevice*						 _pKeyboard;		/**< 편의 API용 캐시 포인터. 실제 소유는 _listDevice가 함. */
		MouseDevice*						 _pMouse;			/**< 편의 API용 캐시 포인터. */
		GamepadDevice*						 _pGamepad;			/**< 0번 게임패드 편의 API용 캐시 포인터 (1~3번은 getGamepad(index)로 조회). */
		unique_ptr<ActionMap>				 _pActionMap;		/**< 이 InputManager에 연결된 기본 ActionMap 인스턴스. */
		vector<RawInputEvent>				 _listDrainedEvent; /**< beginFrame()에서 큐를 드레인해 담아두는 임시 버퍼 (매 프레임 재사용). */
		InputHistoryBuffer					 _inputHistory;		/**< 롤백/리플레이용 프레임별 입력 스냅샷 링버퍼. */
		InputDeviceType						 _activeDeviceType; /**< 마지막으로 조작이 감지된 장치 종류 (UI 글리프 자동 전환용). */
		ActiveDeviceChangedDelegate			 _onActiveDeviceChanged;
		GamepadConnectionDelegate			 _onGamepadConnectionChanged;
		TextInputDelegate					 _onTextInput;
		TextInputDelegate					 _onTextComposition;
		uint8								 _bInitialized : 1;
		uint8								 _bInputMuted  : 1;
		[[maybe_unused]] uint8				 _reserved	   : 6;
	};
} // namespace sw
