#include "pch.h"

#include "Editor/Common/Config/EditorConfig.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"

namespace sw::editor
{
    namespace
    {
        EditorConfig s_activeEditorConfig{};
    } // namespace

    void EditorConfig::setActive( const EditorConfig& config )
    {
        s_activeEditorConfig = config;
    }

    const EditorConfig& EditorConfig::getActive()
    {
        return s_activeEditorConfig;
    }

    EditorConfig* EditorConfig::getActiveInstance()
    {
        return &s_activeEditorConfig;
    }

    void EditorConfig::saveToPreferences()
    {
        (void)EditorPreferencesStore::saveAll( EditorPreferencesStore::getDefaultFilePath() ); // 실패는 saveAll 이 경고로 알린다
    }
} // namespace sw::editor
