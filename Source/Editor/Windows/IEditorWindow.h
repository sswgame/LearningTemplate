/**
 * @file IEditorWindow.h
 * @brief 도킹 가능한 에디터 윈도우 인터페이스 (셸 크롬 제외)
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
	class IRHIDevice;
	struct EditorUIContext;

	/** @brief 도킹 가능한 ImGui 윈도우 (Hierarchy, Inspector, Tools, ...) */
	class IEditorWindow
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 생명주기 — 파생이 GPU 리소스를 쓰면 shutdown 오버라이드
		// ------------------------------------------------------------------------------
		/** @brief 파생 윈도우가 리소스를 해제할 수 있게 합니다. */
		virtual ~IEditorWindow() = default;

		// ------------------------------------------------------------------------------
		// 2) IEditorWindow — 제목/그리기
		//    preRender/shutdown은 GPU 리소스가 있는 패널만 오버라이드
		//    isToolWindow면 닫힌 채 시작 (온디맨드 도구)
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		virtual const utf8* getWindowTitle() const = 0;
		/** @brief 에디터 윈도우의 UI를 그립니다. */
		virtual void draw( const EditorUIContext& ctx ) = 0;
		/** @brief 렌더링 전에 필요한 RHI 리소스를 업데이트합니다. */
		virtual void preRender( IRHIDevice* /*rhiDevice*/ ) {}
		/** @brief 윈도우 종료 시 리소스를 정리합니다. */
		virtual void shutdown( IRHIDevice* /*rhiDevice*/ ) {}
		/** @brief 온디맨드 도구는 닫힌 채 시작하고, 핵심 윈도우는 열린 채 시작합니다. */
		virtual bool isToolWindow() const { return false; }

		// ------------------------------------------------------------------------------
		// 3) 열림 상태 — ImGui Begin의 p_open
		// ------------------------------------------------------------------------------
		/** @brief 윈도우가 열려 있는지 여부를 반환합니다. */
		bool isOpen() const { return _bOpen; }
		/** @brief 윈도우 열림 상태를 설정합니다. */
		void setOpen( bool open ) { _bOpen = open; }
		/** @brief ImGui에서 사용할 열림 상태 포인터를 반환합니다. */
		bool* getOpenPtr() { return &_bOpen; }

	protected:
		/** @brief 기본 열림 상태로 에디터 윈도우를 생성합니다. */
		explicit IEditorWindow( bool bOpenByDefault = true )
			: _bOpen{ bOpenByDefault }
		{
		}

	private:
		bool _bOpen;
	};
} // namespace sw
