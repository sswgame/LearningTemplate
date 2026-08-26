/**
 * @file EditorMenuBar.h
 * @brief 에디터 메인 메뉴바 · 단축키 · File/Panel 요청 처리
 */
#pragma once

namespace sw::editor
{
	class EditorDockLayout;

	/** @brief File / Edit / Assets / Panel 메뉴와 RHI 상태줄을 그립니다. */
	void drawMainMenuBar( EditorDockLayout& dockLayout );
	/** @brief Ctrl+Z/Y/O, Ctrl+P/Space 등 메뉴와 같은 단축키를 처리합니다. */
	void processMenuHotkeys();
	/** @brief Workspace에 쌓인 패널 열기 요청을 소비합니다. */
	void processOpenPanelRequests();
	/** @brief File 메뉴가 고른 씬 경로를 메인 스레드에서 로드합니다. */
	void processPendingSceneLoad();
} // namespace sw::editor
