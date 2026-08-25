/**
 * @file GlobalVariablesWindow.h
 * @brief 전역 변수(치트, 디버그 플래그, 환경 설정 등)를 검사 및 편집하는 에디터 윈도우
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Windows/IEditorWindow.h"

namespace sw
{
	struct GlobalVariableInfo;

	/** @brief 등록된 모든 전역 변수를 목록화하고 실시간으로 편집하는 에디터 도구 윈도우 */
	class GlobalVariablesWindow : public IEditorWindow
	{
	public:
		/** @brief 전역 변수 윈도우를 생성합니다. (도구 창으로 기본 비활성 시작) */
		GlobalVariablesWindow();
		/** @brief 추가 해제할 GPU 리소스는 없습니다. */
		virtual ~GlobalVariablesWindow() override = default;

		// ------------------------------------------------------------------------------
		// 1) IEditorWindow — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getWindowTitle() const override { return "Global Variables"; }
		/** @brief 전역 변수 UI를 그립니다. */
		void draw( const EditorUIContext& ctx ) override;
		/** @brief 온디맨드 도구이므로 기본적으로 닫힌 채 시작합니다. */
		bool isToolWindow() const override { return true; }

	private:
		/** @brief 단일 전역 변수의 편집 컨트롤을 그립니다. */
		void drawVariableRow( GlobalVariableInfo& info );

	private:
		utf8				   _arrSearchFilter[128];
		uint8				   _bGroupByModule : 1;
		[[maybe_unused]] uint8 _reserved	   : 7;
	};

} // namespace sw
