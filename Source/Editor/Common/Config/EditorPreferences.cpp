/**
 * @file EditorPreferences.cpp
 * @brief 기본 환경설정 섹션의 등록 줄입니다. 테마 섹션(Appearance)은 `EditorConfig.cpp` 에 있습니다.
 */
#include "pch.h"

#include "Editor/Common/Config/EditorPreferences.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"

namespace sw::editor
{
    SW_EDITOR_SETTINGS( EditorGeneralPreferences, "general", "Editor/General", 100, nullptr );
    SW_EDITOR_SETTINGS( EditorViewportPreferences, "viewport", "Editor/Viewport", 300, nullptr );
} // namespace sw::editor
