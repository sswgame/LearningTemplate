#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorSceneSettingsCommands.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSettings.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    bool EditorSceneSettingsCommands::apply( Scene& scene, const SceneSettings& settings, const string_view undoLabel )
    {
        const string beforeXml = writeSettingsXml( scene.getSceneSettings() );
        const string afterXml  = writeSettingsXml( settings );
        if ( beforeXml == afterXml )
            return false;
        if ( EditorPlaySession::refuseSceneEdit( "Scene settings" ) )
            return false;
        if ( settings.validate( scene.getName() ) == false )
            return false;
        scene.setSceneSettings( settings );
        EditorTransaction::push( SW_UNDO_STEP( EditorSceneSettingsCommands::restoreActiveSceneSettings, beforeXml ),
                                 SW_UNDO_STEP( EditorSceneSettingsCommands::restoreActiveSceneSettings, afterXml ), undoLabel );
        EditorTransaction::markActiveSceneDirty();
        return true;
    }

    string EditorSceneSettingsCommands::writeSettingsXml( const SceneSettings& settings )
    {
        return XMLSerializer::serialize( &settings, *SceneSettings::StaticType() );
    }

    bool EditorSceneSettingsCommands::restoreSettingsXml( Scene& scene, const string_view settingsXml )
    {
        SceneSettings settings{};
        if ( XMLSerializer::deserialize( &settings, *SceneSettings::StaticType(), settingsXml ) == false || settings.validate( scene.getName() ) == false )
        {
            SW_LOG_ERROR_ID( "EditorSceneSettingsCommands.CouldNotRestore", "Scene settings of '%#' could not be read back from the undo record - the settings are left as they are", scene.getName().c_str() );
            return false;
        }
        scene.setSceneSettings( settings );
        return true;
    }

    void EditorSceneSettingsCommands::restoreActiveSceneSettings( string settingsXml )
    {
        Scene* const pScene = editor::getActiveScene();
        if ( pScene == nullptr )
        {
            // 씬을 바꾸면 스택을 비운다(`EditorSceneGenerationSync`) — 씬 없이 이 단계가 도는 것은 기록과 씬이 어긋났다는 뜻이다.
            SW_LOG_ERROR_ID( "EditorSceneSettingsCommands.NoActiveScene", "No active scene - the scene settings undo step is skipped" );
            return;
        }
        if ( restoreSettingsXml( *pScene, settingsXml ) )
            EditorTransaction::markActiveSceneDirty();
    }
} // namespace sw::editor
