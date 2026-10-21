#include "pch.h"

#include "Editor/Common/Config/EditorConfig.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"

namespace sw::editor
{
    void EditorConfig::saveToPreferences()
    {
        (void)EditorPreferencesStore::saveAll( EditorPreferencesStore::getDefaultFilePath() ); // 실패는 saveAll 이 경고로 알린다
    }
} // namespace sw::editor
