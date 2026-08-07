#pragma once
/**
 * @file IWindow.h
 * @brief 플랫폼 독립적인 윈도우(OS 디스플레이 창) 생성 및 메시지 처리를 위한 인터페이스
 */
#include "Core/CoreMinimal.h"
#include "Core/Common/Types.h"
#include "Core/Utility/Delegate/Delegate.h"
#include "Core/Window/NativeWindowEvent.h"

namespace sw
{
	SW_DECLARE_DELEGATE( bool, WindowMessageHandlerDelegate, const NativeWindowEvent& event );
	SW_DECLARE_DELEGATE( void, WindowResizeDelegate, uint32 width, uint32 height );

	/**
	 * @class IWindow
	 * @brief 애플리케이션의 주 화면 또는 보조 화면을 추상화하는 기본 인터페이스입니다.
	 * @details 플랫폼별(Windows, Linux, macOS 등) 구체 클래스는 이 인터페이스를 상속하여 구현합니다.
	 */
	class IWindow
	{
	public:
		IWindow()		   = default;
		virtual ~IWindow() = default;

		/**
		 * @brief 윈도우 인스턴스를 생성하고 OS 화면에 표시합니다.
		 * @param title 윈도우 캡션(제목 줄)
		 * @param width 클라이언트 영역 가로 크기
		 * @param height 클라이언트 영역 세로 크기
		 * @return 생성 성공 여부
		 */
		virtual bool create( const utf8* title, uint32 width, uint32 height ) = 0;

		/** @brief 윈도우 리소스를 반환하고 화면에서 제거합니다. */
		virtual void destroy() = 0;

		/**
		 * @brief 동일 크기/제목으로 네이티브 창을 다시 만듭니다 (OpenGL↔DXGI 핫스왑용).
		 * @details WM_DESTROY로 앱 종료가 걸리지 않도록 구현해야 합니다.
		 */
		virtual bool recreate();

		/**
		 * @brief 플랫폼 종속적인 메시지 루프를 1회 순회합니다. 매 프레임 호출되어야 합니다.
		 * @return 계속 실행되어야 하면 true, 종료 요청(WM_CLOSE 등)이 있으면 false
		 */
		virtual bool processMessages() = 0;

		virtual void*  getNativeHandle() const = 0;
		virtual void*  getNativeDisplay() const { return nullptr; }
		virtual uint32 getWidth() const { return _width; }
		virtual uint32 getHeight() const { return _height; }
		virtual bool   shouldClose() const { return _bShouldClose; }

		/** @brief 외부 이벤트 핸들러(예: ImGui)를 연결합니다. */
		void setCustomMessageHandler( WindowMessageHandlerDelegate handler ) { _customHandler = handler; }

		/** @brief 윈도우 크기 변경 시 호출될 콜백을 설정합니다. */
		void setResizeCallback( WindowResizeDelegate cb ) { _onResize = cb; }

		/** @brief 현재 플랫폼 환경에 맞는 IWindow 인스턴스를 동적 할당하여 반환합니다. */
		static std::unique_ptr<IWindow> createPlatformWindow();

		/** @brief App이 소유하는 활성 윈도우 포인터 (RHI 초기화 등) */
		static void		setActiveWindow( IWindow* window );
		static IWindow* getActiveWindow();

	protected:
		uint32		 _width		   = 1280;
		uint32		 _height	   = 720;
		bool		 _bShouldClose = false;
		std::wstring _title;

		WindowMessageHandlerDelegate _customHandler;
		WindowResizeDelegate		 _onResize;
	};
} // namespace sw
