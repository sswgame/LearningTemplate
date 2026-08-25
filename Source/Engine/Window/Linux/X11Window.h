/**
 * @file X11Window.h
 * @brief Linux X11 네이티브 윈도우
 */
#pragma once
#include "Engine/Window/IWindow.h"

namespace sw
{

	/// @brief X11 네이티브 창
	class X11Window : public IWindow
	{
	public:
		/** @brief Display/Window 없이 시작합니다. */
		X11Window();
		/** @brief X11 창을 파괴합니다. */
		virtual ~X11Window() override;

		/** @brief X11 창을 만들고 화면에 띄웁니다. */
		bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
		/** @brief X11 윈도우를 파괴합니다. */
		void destroy() override;
		/** @brief 기존 창의 크기와 위치를 유지한 채 X11 윈도우를 재생성합니다 (컨텍스트 핫스왑용). */
		bool recreate() override;
		/** @brief X11 이벤트를 처리합니다. 종료 요청 시 false를 반환합니다. */
		bool processMessages() override;

#if defined( SW_PLATFORM_LINUX )
		/** @brief X11 윈도우 핸들을 반환합니다. */
		void* getNativeHandle() const override { return reinterpret_cast<void*>( _x11Window ); }
		/** @brief X11 디스플레이 연결 핸들을 반환합니다. */
		void* getNativeDisplay() const override { return _pX11Display; }
		/** @brief X11 전용 윈도우 ID를 반환합니다. */
		uint64 getX11Window() const { return _x11Window; }
#else
		void* getNativeHandle() const override { return nullptr; }
#endif

	private:
		[[maybe_unused]] void*	_pX11Display;
		[[maybe_unused]] uint64 _x11Window;
		[[maybe_unused]] uint64 _x11WmDelete;
		[[maybe_unused]] bool	_bRecreating;
		[[maybe_unused]] int32	_restoreX;
		[[maybe_unused]] int32	_restoreY;
	};
} // namespace sw
