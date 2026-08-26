/**
 * @file IEditorPanel.h
 * @brief 도킹 가능한 에디터 패널 (셸 크롬은 draw(), 내용은 drawContent())
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Gui/EditorChrome.h"

namespace sw
{
	class IRHIDevice;
} // namespace sw

namespace sw::editor
{
	/**
	 * @class IEditorPanel
	 * @brief 도킹 가능한 ImGui 패널. 파생은 drawContent()만 구현합니다.
	 */
	class IEditorPanel
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 생명주기 — 파생이 GPU 리소스를 쓰면 shutdown 오버라이드
		// ------------------------------------------------------------------------------
		/** @brief 파생 패널이 리소스를 해제할 수 있게 합니다. */
		virtual ~IEditorPanel() = default;

		// ------------------------------------------------------------------------------
		// 2) IEditorPanel — 제목 / 크롬 / 내용
		//    preRender/shutdown은 GPU 리소스가 있는 패널만 오버라이드
		//    isToolPanel이면 닫힌 채 시작 (온디맨드 도구)
		// ------------------------------------------------------------------------------
		/** @brief 패널 제목을 반환합니다. */
		virtual const utf8* getPanelTitle() const = 0;
		/** @brief Panel 크롬을 열고 drawContent()를 호출합니다. */
		void draw();
		/** @brief 렌더링 전에 필요한 RHI 리소스를 업데이트합니다. */
		virtual void preRender( IRHIDevice* /*rhiDevice*/ ) {}
		/** @brief 패널 종료 시 리소스를 정리합니다. */
		virtual void shutdown( IRHIDevice* /*rhiDevice*/ ) {}
		/** @brief 온디맨드 도구는 닫힌 채 시작하고, 핵심 패널은 열린 채 시작합니다. */
		virtual bool isToolPanel() const { return false; }

		// ------------------------------------------------------------------------------
		// 3) 열림 상태 — ImGui Begin의 p_open
		// ------------------------------------------------------------------------------
		/** @brief 패널이 열려 있는지 여부를 반환합니다. */
		bool isOpen() const { return _bOpen; }
		/** @brief 패널 열림 상태를 설정합니다. */
		void setOpen( bool open ) { _bOpen = open; }
		/** @brief ImGui에서 사용할 열림 상태 포인터를 반환합니다. */
		bool* getOpenPtr() { return &_bOpen; }

	protected:
		/** @brief 기본 열림 상태로 에디터 패널을 생성합니다. */
		explicit IEditorPanel( bool bOpenByDefault = true )
			: _bOpen{ bOpenByDefault }
		{
		}

		/** @brief 패널 본문을 그립니다. Begin/End는 draw()가 처리합니다. */
		virtual void drawContent() = 0;
		/** @brief 패널이 접히거나 탭이 숨겨졌을 때 호출됩니다. */
		virtual void onPanelCollapsed() {}
		/** @brief EditorPanelFlags 조합. 기본은 None. */
		virtual EditorPanelFlags getPanelFlags() const { return EditorPanelFlags::None; }
		/** @brief FirstUseEver 크기. (0,0)이면 적용하지 않습니다. */
		virtual float2 getInitialPanelSize() const { return float2{ 0.0f, 0.0f }; }

	private:
		bool _bOpen;
	};
} // namespace sw::editor
