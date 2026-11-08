#include "pch.h"

#include "Editor/Panels/Settings/Model/UserSettingsViewModel.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw::editor
{
    UserSettingsManager& UserSettingsViewModel::getSettings()
    {
        return editor::getSubsystem<UserSettingsManager>();
    }

    const UserSettingsManager* UserSettingsViewModel::findSettings()
    {
        return editor::findSubsystem<UserSettingsManager>();
    }
} // namespace sw::editor
