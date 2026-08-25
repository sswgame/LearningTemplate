/**
 * @file HierarchyWindow.h
 * @brief 씬 GameObject / Component 계층 윈도우
 */
#pragma once
#include "Editor/Windows/IEditorWindow.h"

#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/** @brief 활성 씬의 오브젝트 아웃라이너 */
	class HierarchyWindow : public IEditorWindow
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) IEditorWindow — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getWindowTitle() const override { return "Hierarchy"; }
		/** @brief Hierarchy UI를 그립니다. */
		void draw( const EditorUIContext& ctx ) override;

	private:
		/** @brief Save Scene 파일 대화상자 결과. */
		static void onSaveScenePicked( const vector<string>& paths );

	private:
		utf8 _arrFilterBuffer[constant::kMaxBuffer128]{};
	};
} // namespace sw
