#pragma once
/**
 * @file GlobalVariablesPanel.h
 * @brief gv_* 전역 변수 조회·편집 패널
 */
#include "Panels/IEditorPanel.h"

namespace sw
{
	/** @brief GlobalVariableManager 등록 변수를 필터링·편집하는 패널 */
	class GlobalVariablesPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Global Variables Control"; }
		/** @brief 전역 변수 목록과 편집 UI를 그립니다. */
		void		draw( const EditorUIContext& ctx ) override;

	private:
		char _filterBuffer[128] = {}; ///< 이름 필터 입력 버퍼
	};
}
