#pragma once
/**
 * @file CocoaWindow.h
 * @brief Auto-generated documentation header
 */

#include "IWindow.h"

namespace sw
{

	class CocoaWindow : public IWindow
	{
	public:
		CocoaWindow();
		~CocoaWindow() override;

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

#if defined( SW_PLATFORM_MACOS )
		void* getNativeHandle() const override { return _cocoaMetalLayer; }
		void* getCocoaWindow() const { return _cocoaWindow; }

	private:
		void* _cocoaWindow	   = nullptr;
		void* _cocoaApp		   = nullptr;
		void* _cocoaMetalLayer = nullptr;
#else
		void* getNativeHandle() const override { return nullptr; }
#endif
	};
}
