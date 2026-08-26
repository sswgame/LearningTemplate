/**
 * @file ProfilerPanel.h
 * @brief 메모리·시스템 프로파일링 윈도우
 */
#pragma once
#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
	/** @brief 메모리 프로파일러 탭을 표시하는 에디터 도구 윈도우 */
	class ProfilerPanel : public IEditorPanel
	{
	public:
		/** @brief 프로파일러 윈도우를 생성합니다. */
		ProfilerPanel();
		/** @brief 추가 해제할 GPU 리소스는 없습니다. */
		virtual ~ProfilerPanel() override = default;

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Profiler"; }
		/** @brief 프로파일러 UI를 그립니다. */
		void drawContent() override;
		/** @brief 온디맨드 도구이므로 기본적으로 닫힌 채 시작합니다. */
		bool isToolPanel() const override { return true; }

	private:
		/** @brief 메모리 프로파일 탭을 그립니다. */
		void drawMemoryTab();
	};

} // namespace sw
