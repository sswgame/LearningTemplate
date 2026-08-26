/**
 * @file GlobalVariablesPanel.h
 * @brief 전역 변수(치트, 디버그 플래그, 환경 설정 등)를 검사 및 편집하는 에디터 윈도우
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
	struct GlobalVariableInfo;
}

namespace sw::editor
{
	/** @brief 등록된 모든 전역 변수를 목록화하고 실시간으로 편집하는 에디터 도구 윈도우 */
	class GlobalVariablesPanel : public IEditorPanel
	{
	public:
		/** @brief 전역 변수 윈도우를 생성합니다. (도구 창으로 기본 비활성 시작) */
		GlobalVariablesPanel();
		/** @brief 추가 해제할 GPU 리소스는 없습니다. */
		virtual ~GlobalVariablesPanel() override = default;

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Global Variables"; }
		/** @brief 전역 변수 UI를 그립니다. */
		void drawContent() override;
		/** @brief 기본 창 크기를 반환합니다. */
		float2 getInitialPanelSize() const override { return float2{ 620.0f, 440.0f }; }
		/** @brief 온디맨드 도구이므로 기본적으로 닫힌 채 시작합니다. */
		bool isToolPanel() const override { return true; }

	private:
		/** @brief 단일 전역 변수의 편집 컨트롤을 그립니다. */
		void drawVariableRow( GlobalVariableInfo& info );

	private:
		utf8				   _arrSearchFilter[128];
		uint8				   _bGroupByModule : 1;
		[[maybe_unused]] uint8 _reserved	   : 7;
	};
} // namespace sw::editor
