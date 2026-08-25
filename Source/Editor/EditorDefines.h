/**
 * @file EditorDefines.h
 * @brief 에디터 Host 기본 경로. 시드/레이아웃은 EditorConfig + editordata.xml.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
	/** @brief 기본 editordata Host 경로 (프로젝트 루트 상대). */
	inline static constexpr auto kEditorData = "Config/Editor/editordata.xml";
} // namespace sw::editor
