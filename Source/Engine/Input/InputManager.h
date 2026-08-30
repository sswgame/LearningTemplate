/**
 * @file InputManager.h
 * @brief App / Game용 크로스플랫폼 키보드·마우스 상태
 * @details
 *   - 공통 상태/쿼리: InputManager.cpp
 *   - Win32 poll·WM_*: InputManagerWin32.cpp
 *   - X11 이벤트: InputManagerX11.cpp
 *   - VK/KeySym 맵: InputKeyMapWin32.cpp / InputKeyMapX11.cpp
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/KeyCodes.h"

namespace sw
{
	struct NativeWindowEvent;
	class GamepadXInput;
	class ActionMap;

	/**
	 * @class InputManager
	 * @brief 프레임 단위 키/마우스 상태. 플랫폼 백엔드가 pollPlatform / processNativeEvent를 구현합니다.
	 */
	class SW_API InputManager
	{
	public:
		/** @brief 빈 입력 매니저. */
		InputManager();
		/** @brief 입력 매니저를 해제합니다. */
		~InputManager();

		/** @brief 복사를 금지합니다. */
		InputManager( const InputManager& ) = delete;
		/** @brief 대입을 금지합니다. */
		InputManager& operator=( const InputManager& ) = delete;

		/** @brief 입력 매니저를 초기화합니다. */
		bool initialize();
		/** @brief 입력 매니저를 종료합니다. */
		void shutdown();

		/**
		 * @brief 윈도우 펌프 이후 플랫폼 폴을 적용합니다.
		 * @details Pressed/Released 에지는 이전 endFrame 스냅샷과 비교하므로,
		 *          processMessages 중 적용된 NativeWindowEvent 키가 이번 프레임에 보입니다.
		 */
		void beginFrame();
		/** @brief 다음 프레임 에지용으로 키/마우스를 스냅샷하고, 한 프레임 휠 델타를 지웁니다. */
		void endFrame();

		/** @brief 플랫폼 메시지를 전달합니다 (App이 NativeWindowEvent를 넘기며 키는 디코드하지 않음). */
		void processNativeEvent( const NativeWindowEvent& event );

		/** @brief 켜면 beginFrame이 XInput 사용자 0도 폴링합니다. */
		void setGamepadPollingEnabled( bool enabled );

		/** @brief 키가 눌린 상태인지 반환합니다. */
		bool isKeyDown( Key key ) const;
		/** @brief 이번 프레임에 키가 눌렸는지 반환합니다. */
		bool wasKeyPressed( Key key ) const;
		/** @brief 이번 프레임에 키가 떼어졌는지 반환합니다. */
		bool wasKeyReleased( Key key ) const;
		/** @brief 마우스 위치를 반환합니다. */
		void getMousePosition( int32& outX, int32& outY ) const;
		/** @brief 마우스 이동량을 반환합니다. */
		void getMouseDelta( int32& outDx, int32& outDy ) const;
		/** @brief 이번 프레임 수직 휠 틱 (WHEEL_DELTA 단위 / 120). */
		float32 getMouseWheelDelta() const { return _mouseWheelDelta; }
		/** @brief 마우스 버튼이 눌린 상태인지 반환합니다. */
		bool isMouseButtonDown( MouseButton button ) const;
		/** @brief 이번 프레임에 마우스 버튼이 눌렸는지 반환합니다. */
		bool wasMouseButtonPressed( MouseButton button ) const;
		/** @brief 이번 프레임에 마우스 버튼이 떼어졌는지 반환합니다. */
		bool wasMouseButtonReleased( MouseButton button ) const;
		/** @brief 포인터가 클라이언트 영역 안인지 반환합니다. */
		bool isPointerInside() const { return _bPointerInside == SW_TRUE; }
		/** @brief 이번 프레임에 포인터가 들어왔는지 반환합니다. */
		bool wasPointerEntered() const { return _bPointerEntered == SW_TRUE; }
		/** @brief 이번 프레임에 포인터가 나갔는지 반환합니다. */
		bool wasPointerLeft() const { return _bPointerLeft == SW_TRUE; }
		/** @brief 게임패드 폴링 활성 여부를 반환합니다. */
		bool isGamepadPollingEnabled() const { return _bPollGamepad == SW_TRUE; }
		/** @brief 게임패드 래퍼를 반환합니다. */
		GamepadXInput* getGamepad() const { return _gamepad.get(); }
		/** @brief 중앙 액션 맵을 반환합니다. */
		ActionMap&		 getActionMap();
		const ActionMap& getActionMap() const;

	private:
		/** @brief 키 눌림 상태를 설정합니다. */
		void setKeyDown( Key key, bool bDown );
		/** @brief 마우스 버튼 눌림 상태를 설정합니다. */
		void setMouseButtonDown( MouseButton button, bool bDown );
		/** @brief 플랫폼별 입력을 폴링합니다. */
		void pollPlatform();
		/** @brief 포인터 내부/진입/이탈 플래그를 갱신합니다. */
		void updatePointerInside();

	private:
		unique_ptr<GamepadXInput> _gamepad;
		unique_ptr<ActionMap>	  _actionMap;
		int32					  _mouseX;
		int32					  _mouseY;
		int32					  _prevMouseX;
		int32					  _prevMouseY;
		float32					  _mouseWheelDelta;
		float32					  _mouseWheelAccum;
		bool					  _arrKey[static_cast<size_t>( Key::Count )];
		bool					  _arrPrevKey[static_cast<size_t>( Key::Count )];
		bool					  _arrMouseButton[static_cast<size_t>( MouseButton::Count )];
		bool					  _arrPrevMouseButton[static_cast<size_t>( MouseButton::Count )];
		uint8					  _bInitialized		  : 1;
		uint8					  _bPollGamepad		  : 1;
		uint8					  _bPointerInside	  : 1;
		uint8					  _bPrevPointerInside : 1;
		uint8					  _bPointerEntered	  : 1;
		uint8					  _bPointerLeft		  : 1;
		[[maybe_unused]] uint8	  _reservedFlags	  : 2;
	};

} // namespace sw
