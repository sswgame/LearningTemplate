/**
 * @file BoneHierarchyPopup.h
 * @brief 플로팅 본 계층 유틸리티 (ASF 영감, 도크 윈도우가 아님)
 */
#pragma once

namespace sw::editor
{
	// ------------------------------------------------------------------------------
	// 1) BoneHierarchyPopup — 플로팅 유틸 (도크 아님)
	//    열림 상태는 editor::boneHierarchyPopupOpen
	// ------------------------------------------------------------------------------
	/** @brief 본 계층 팝업을 그립니다. */
	void drawBoneHierarchyPopup();
} // namespace sw::editor
