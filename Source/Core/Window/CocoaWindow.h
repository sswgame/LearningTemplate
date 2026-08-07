#pragma once
/**
 * @file CocoaWindow.h
 * @brief macOS Cocoa 네이티브 윈도우
 */

#include "IWindow.h"

namespace sw
{

	class CocoaWindow : public IWindow
	{
	public:
		CocoaWindow();
		~CocoaWindow() override;

		/** @brief Cocoa 윈도우를 생성합니다. */
		bool create( const utf16* title, uint32 width, uint32 height ) override;
		/** @brief Cocoa 윈도우를 파괴합니다. */
		void destroy() override;
		/** @brief Cocoa 이벤트를 처리합니다. 종료 요청 시 false. */
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
