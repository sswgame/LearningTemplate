#include "pch.h"

#include "Editor/Common/GUI/Model/EditorMenuBarViewModel.h"

#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorLayoutStore.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw::editor
{
    namespace
    {
        struct EditorMenuBarViewModelInternal
        {
            static constexpr float32 kSucceededToastSeconds = 4.0f; ///< 빌드 성공 알림이 떠 있는 시간
            static constexpr float32 kFailedToastSeconds    = 6.0f; ///< 빌드 실패 알림은 더 오래 둔다(이유를 읽을 시간)
        };
    } // namespace

    EditorMenuBarUIState& EditorMenuBarViewModel::editUIState()
    {
        return getEditorState<EditorMenuBarUIState>();
    }

    void EditorMenuBarViewModel::requestOpenDocumentPanel( const EditorAssetType& kind )
    {
        editor::getSubsystem<EditorWorkspace>().requestOpenDocumentPanel( kind );
    }

    bool EditorMenuBarViewModel::consumeOpenPanel( EditorPanelOpenRequest& outRequest )
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().consumeOpenPanel( outRequest );
    }

    void EditorMenuBarViewModel::requestFloatPanelByID( string_view panelID )
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().requestFloatPanelByID( panelID );
    }

    void EditorMenuBarViewModel::processSceneRequests()
    {
        EditorSceneFileCommands::syncAfterSceneGenerationChange();
    }

    EditorPendingSceneAction EditorMenuBarViewModel::getPendingSceneAction()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getPendingSceneAction();
    }

    void EditorMenuBarViewModel::requestUnsavedSceneChoice( EditorUnsavedChoice choice )
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().requestUnsavedSceneChoice( choice );
    }

    void EditorMenuBarViewModel::collectLayoutNames( vector<string>& outListName )
    {
        outListName.clear();
        EditorLayoutStore::collectNames( EditorLayoutStore::getDefaultFolder(), outListName );
    }

    bool EditorMenuBarViewModel::removeLayout( string_view name )
    {
        return EditorLayoutStore::remove( EditorLayoutStore::getDefaultFolder(), name );
    }

    IModuleCompiler* EditorMenuBarViewModel::findModuleCompiler()
    {
        return findSubsystem<IModuleCompiler>(); // 선택: Shipping 에는 모듈 컴파일러가 없다(GameModuleManager 가 SW_SHIPPING 에서 만들지 않는다)
    }

    void EditorMenuBarViewModel::notifyLiveCodingResult( IModuleCompiler& compiler )
    {
        BuildState& lastObservedState = editUIState()._lastObservedBuildState;

        const bool       bCompiling = compiler.isCompiling();
        const BuildState state      = compiler.getBuildState();

        // 컴파일이 끝나는 순간을 잡는다(Compiling -> Success / Failed)
        if ( lastObservedState == BuildState::Compiling && bCompiling == false )
        {
            const string               targetName    = compiler.getTargetName();
            const string               displayName   = targetName.empty() ? string{ SW_LOCKEY( "EdCommon", "LiveCodingAllModules" ) } : targetName;
            const float32              duration      = compiler.getLastDurationSec();
            EditorNotificationManager& notifications = editor::getSubsystem<EditorContext>().getSubsystem<EditorNotificationManager>();

            if ( state == BuildState::Success )
            {
                fixed_string<constant::buffer::kMax32> secondsBuf;
                formatstring( secondsBuf.data(), secondsBuf.capacity(), "%#", Fmt( static_cast<float64>( duration ), Format().precision( 2 ) ) );
                const string content = SW_LOCKEYFORMAT( "EdCommon", "LiveCodingSucceeded", TextArgumentList().addText( "target", displayName ).addText( "seconds", secondsBuf.c_str() ) );
                notifications.push( SW_LOCKEY( "EdCommon", "LiveCodingSucceededTitle" ), content, NotificationKind::Success, EditorMenuBarViewModelInternal::kSucceededToastSeconds );
            }
            else if ( state == BuildState::Failed )
            {
                const string content = SW_LOCKEYFORMAT( "EdCommon", "LiveCodingFailed", TextArgumentList().addText( "target", displayName ).addInteger( "code", compiler.getLastExitCode() ) );
                notifications.push( SW_LOCKEY( "EdCommon", "LiveCodingFailedTitle" ), content, NotificationKind::Error, EditorMenuBarViewModelInternal::kFailedToastSeconds );
            }
        }

        lastObservedState = bCompiling ? BuildState::Compiling : state;
    }

    const utf8* EditorMenuBarViewModel::getRHIBackendName()
    {
        IRHIDevice* pRHIDevice = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>().getRHIDevice();
        return pRHIDevice != nullptr ? pRHIDevice->getBackendName() : "n/a";
    }
} // namespace sw::editor
