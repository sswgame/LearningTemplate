#pragma once
/**
 * @file X11Window.h
 * @brief Auto-generated documentation header
 */

#include "IWindow.h"

namespace sw
{

	class X11Window : public IWindow
	{
	public:
		X11Window();
		~X11Window() override;

		/**
		 * @brief create 처리를 수행합니다.
		 */
		bool create( const utf16* title, uint32 width, uint32 height ) override;
		/**
		 * @brief destroy 처리를 수행합니다.
		 */
		void destroy() override;
		/**
		 * @brief processMessages 처리를 수행합니다.
		 */
		bool processMessages() override;

#if defined( SW_PLATFORM_LINUX )
		void*  getNativeHandle() const override { return reinterpret_cast<void*>( _x11Window ); }
		void*  getNativeDisplay() const override { return _x11Display; }
		void*  getX11Display() const { return _x11Display; }
		uint64 getX11Window() const { return _x11Window; }

	private:
		void*  _x11Display	= nullptr;
		uint64 _x11Window	= 0;
		uint64 _x11WmDelete = 0;
#else
		void* getNativeHandle() const override { return nullptr; }
#endif
	};
}
