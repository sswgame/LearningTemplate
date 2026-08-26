/**
 * @file GameViewPanel.h
 * @brief 씬 프레임버퍼 미리보기와 ImGuizmo 조작을 제공하는 Game View 윈도우
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/Gui/IEditorPanel.h"
#include "Editor/Viewport/EditorViewportClient.h"

namespace sw::editor
{
	/** @brief 게임 프레임버퍼를 표시하고 선택된 오브젝트 트랜스폼을 편집합니다 */
	class GameViewPanel : public IEditorPanel
	{
	public:
		/** @brief Game View 윈도우를 생성합니다. */
		GameViewPanel();

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Game View"; }
		/** @brief 게임 캔버스와 선택된 오브젝트의 기즈모를 그립니다. */
		void drawContent() override;
		/** @brief 창이 숨겨지면 Game View 포커스를 해제합니다. */
		void onPanelCollapsed() override;

		EditorViewportClient& getViewportClient() { return _viewportClient; }

	private:
		/** @brief Play/Sim/Pause/Step/Stop 버튼을 그립니다. */
		void drawTransportControls();

	private:
		EditorViewportClient _viewportClient;
	};
} // namespace sw::editor
