#pragma once
/**
 * @file InputManager.h
 * @brief Cross-platform keyboard / mouse state for App / Game
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	struct NativeWindowEvent;

	/** @brief Minimal key set for gameplay / title flow */
	enum class Key : uint8
	{
		Unknown = 0,
		W,
		A,
		S,
		D,
		C,
		Space,
		Escape,
		Enter,
		Left,
		Right,
		Up,
		Down,
		F5,
		F9,
		Count
	};

	enum class MouseButton : uint8
	{
		Left = 0,
		Right,
		Middle,
		Count
	};

	/**
	 * @class InputManager
	 * @brief Per-frame key/mouse state. Poll on Win32; X11 via NativeWindowEvent.
	 */
	class SW_API InputManager
	{
	public:
		InputManager();
		~InputManager() = default;

		InputManager( const InputManager& )			   = delete;
		InputManager& operator=( const InputManager& ) = delete;

		bool initialize();
		void shutdown();

		/** @brief Snapshot previous frame, then refresh platform state (Win32 poll). */
		void beginFrame();
		/** @brief Clears edge-only bookkeeping for the next frame. */
		void endFrame();

		/** @brief Feed Win32 / X11 messages (called from App::onWindowMessage). */
		void processNativeEvent( const NativeWindowEvent& event );

		bool isKeyDown( Key key ) const;
		bool wasKeyPressed( Key key ) const;

		void  getMousePosition( int32& outX, int32& outY ) const;
		bool  isMouseButtonDown( MouseButton button ) const;

	private:
		void setKeyDown( Key key, bool bDown );
		void setMouseButtonDown( MouseButton button, bool bDown );
		void pollPlatform();

		static Key		  mapWin32VirtualKey( uintptr_t vk );
		static Key		  mapX11KeySym( uint64 keySym );
		static MouseButton mapWin32MouseButton( uint32 message, uintptr_t wParam );

		bool  _keys[static_cast<size_t>( Key::Count )]{};
		bool  _prevKeys[static_cast<size_t>( Key::Count )]{};
		bool  _mouseButtons[static_cast<size_t>( MouseButton::Count )]{};
		bool  _prevMouseButtons[static_cast<size_t>( MouseButton::Count )]{};
		int32				   _mouseX		  = 0;
		int32				   _mouseY		  = 0;
		uint8				   _bInitialized  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 7;
	};
} // namespace sw
