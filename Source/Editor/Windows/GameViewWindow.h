/**
 * @file GameViewWindow.h
 * @brief 씬 프레임버퍼 미리보기와 ImGuizmo 조작을 제공하는 Game View 윈도우
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Viewport/EditorViewportClient.h"
#include "Editor/Windows/IEditorWindow.h"

namespace sw
{
	/** @brief 게임 프레임버퍼를 표시하고 선택된 오브젝트 트랜스폼을 편집합니다 */
	class GameViewWindow : public IEditorWindow
	{
	public:
		/** @brief Game View 윈도우를 생성합니다. */
		GameViewWindow();

		// ------------------------------------------------------------------------------
		// 1) IEditorWindow — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getWindowTitle() const override { return "Game View"; }
		/** @brief 게임 캔버스와 선택된 오브젝트의 기즈모를 그립니다. */
		void draw() override;

		EditorViewportClient& getViewportClient() { return _viewportClient; }

	private:
		EditorViewportClient _viewportClient;
	};
} // namespace sw
