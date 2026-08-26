/**
 * @file EditorTransportBar.h
 * @brief 플로팅 Play/Sim/Pause/Step/Stop 바 (도크 윈도우가 아님)
 */
#pragma once

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) EditorTransportBar — Play/Sim/Pause/Step/Stop
	//    GameState를 바꾸고, 도크 레이아웃 밖 플로팅 바
	// ------------------------------------------------------------------------------
	/** @brief 에디터 트랜스포트 바를 그립니다. */
	void drawEditorTransportBar();
} // namespace sw
