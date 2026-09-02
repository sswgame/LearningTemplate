#include "pch.h"

#include "Editor/Common/Gui/EditorMenuBar.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/EditorPlaySession.h"
#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Popups/CommandPalettePopup.h"
#include "Editor/Popups/QuickLauncherPopup.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"

#include "RuntimeAPI/Service/IModuleCompiler.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorMenuBarInternal
        {
            static void onOpenSceneDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() == false )
                    EditorContext::get()->getWorkspace().requestLoadScene( listPath[0] );
            }

            static void saveSceneOrPrompt()
            {
                EditorAssetCommands::saveActiveSceneOrPrompt();
            }

            static void saveFocusedOrScene()
            {
                EditorAssetCommands::saveFocusedOrScene();
            }

            static void openSceneFileDialog()
            {
                FileDialogParams params{};
                params._type                = FileDialogParams::Type::Open;
                params._title               = "Open Scene";
                params._description         = "Scene";
                params._bEnableMultiselect  = false;
                params._listFilterExtension = { ".scene.xml", ".xml" };

                const string mapsDir = ResourceUtil::joinActivePackPath( path::kMapsFolder );
                if ( FileUtil::directoryExists( mapsDir ) )
                    params._initialDirectory = mapsDir;
                else if ( ResourceUtil::getActivePackFolderPath().empty() == false )
                    params._initialDirectory = ResourceUtil::getActivePackFolderPath();
                else if ( ResourceUtil::getGameFolderPath().empty() == false )
                    params._initialDirectory = ResourceUtil::getGameFolderPath();

                FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onOpenSceneDialogResult ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "Editor" );

    void EditorMenuBar::draw( EditorDockLayout& dockLayout )
    {
        if ( ImGui::BeginMainMenuBar() == false )
            return;

        if ( ImGui::BeginMenu( "File" ) )
        {
            if ( ImGui::MenuItem( "New Scene" ) )
                EditorAssetCommands::tryCreateNewScene();
            if ( ImGui::MenuItem( "Open Scene...", "Ctrl+O" ) )
                EditorMenuBarInternal::openSceneFileDialog();
            if ( ImGui::MenuItem( "Save", "Ctrl+S" ) )
                EditorMenuBarInternal::saveFocusedOrScene();
            if ( ImGui::MenuItem( "Save Scene" ) )
                EditorMenuBarInternal::saveSceneOrPrompt();

            ImGui::Separator();
            if ( ImGui::MenuItem( "Quick Open...", "Ctrl+P" ) )
                QuickLauncherPopup::open();
            if ( ImGui::MenuItem( "Command Palette...", "Ctrl+Shift+P / Ctrl+Space" ) )
                CommandPalettePopup::open();

            ImGui::Separator();
            if ( ImGui::MenuItem( "Exit", "Alt+F4" ) )
                EditorAssetCommands::requestExit();
            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "Edit" ) )
        {
            if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, EditorPlaySession::isStopped() ) )
                getService<CommandStack>()->undo();
            if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, EditorPlaySession::isStopped() ) )
                getService<CommandStack>()->redo();
            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "Build" ) )
        {
            IModuleCompiler* pCompiler  = getService<IModuleCompiler>();
            const bool       bCompiling = ( pCompiler != nullptr && pCompiler->isCompiling() );

            if ( ImGui::MenuItem( "Compile Game (SWGame)", "Ctrl+Alt+F11", false, bCompiling == false ) )
            {
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "SWGame" );
            }

            if ( ImGui::MenuItem( "Compile Editor (EditorModule)", nullptr, false, bCompiling == false ) )
            {
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "EditorModule" );
            }

            if ( ImGui::MenuItem( "Compile All Modules", "Ctrl+Shift+B", false, bCompiling == false ) )
            {
                if ( pCompiler != nullptr )
                    pCompiler->compileAll();
            }

            ImGui::Separator();

            if ( ImGui::MenuItem( "Cancel Build", nullptr, false, bCompiling ) )
            {
                if ( pCompiler != nullptr )
                    pCompiler->cancel();
            }

            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "Assets" ) )
        {
            uint32                 kindCount{ 0 };
            const EditorAssetKind* pKind = EditorAssetTypeRegistry::getToolPanelKinds( kindCount );
            for ( uint32 index = 0; index < kindCount; ++index )
            {
                const utf8* pTitle = EditorAssetTypeRegistry::getPanelTitle( pKind[index] );
                if ( pTitle == nullptr || pTitle[0] == '\0' )
                    continue;
                if ( ImGui::MenuItem( pTitle ) )
                    EditorContext::get()->getWorkspace().requestOpenPanel( pTitle );
            }
            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "Panel" ) )
        {
            ImGui::SeparatorText( "Panels" );
            for ( const EditorPanelEntry& entry : EditorContext::get()->getPanelManager().getPanels() )
            {
                if ( entry._pInstance == nullptr || entry._category != EditorPanelCategory::Core )
                    continue;
                const bool bOpen = entry._pInstance->isOpen();
                if ( ImGui::MenuItem( entry._title.c_str(), nullptr, bOpen ) )
                    entry._pInstance->setOpen( bOpen == false );
            }

            ImGui::SeparatorText( "Tools" );
            for ( const EditorPanelEntry& entry : EditorContext::get()->getPanelManager().getPanels() )
            {
                if ( entry._pInstance == nullptr || entry._category == EditorPanelCategory::Core )
                    continue;
                const bool bOpen = entry._pInstance->isOpen();
                if ( ImGui::MenuItem( entry._title.c_str(), nullptr, bOpen ) )
                    entry._pInstance->setOpen( bOpen == false );
            }

            ImGui::Separator();
            if ( ImGui::MenuItem( "Reset Default Layout" ) )
                dockLayout.requestResetDefault();

            ImGui::EndMenu();
        }

        constexpr float32 statusW = 460.0f;
        ImGui::SameLine( ImGui::GetWindowWidth() - statusW );

        // --- Live Coding Compile Button & Status ---
        static BuildState s_lastObservedState = BuildState::Idle;

        IModuleCompiler* pCompiler = getService<IModuleCompiler>();
        if ( pCompiler != nullptr )
        {
            const bool       bCompiling = pCompiler->isCompiling();
            const BuildState state      = pCompiler->getBuildState();

            // 컴파일 완료 상태 전이 감지 (Compiling -> Success / Failed)
            if ( s_lastObservedState == BuildState::Compiling && bCompiling == false )
            {
                const string  targetName  = pCompiler->getTargetName();
                const string  displayName = targetName.empty() ? "All Modules" : targetName;
                const float32 duration    = pCompiler->getLastDurationSec();

                if ( state == BuildState::Success )
                {
                    fixed_string<constant::kMaxBuffer128> contentBuf;
                    formatstring( contentBuf.data(), contentBuf.capacity(), "%# compiled and reloaded in %#s", displayName.c_str(), Fmt( static_cast<float64>( duration ), Format().precision( 2 ) ) );
                    EditorContext::get()->getNotificationManager().push( "Live Coding Succeeded", contentBuf.c_str(), NotificationType::Success, 4.0f );
                }
                else if ( state == BuildState::Failed )
                {
                    fixed_string<constant::kMaxBuffer128> contentBuf;
                    formatstring( contentBuf.data(), contentBuf.capacity(), "%s build failed (Exit: %d). See Output Log.", displayName.c_str(), pCompiler->getLastExitCode() );
                    EditorContext::get()->getNotificationManager().push( "Live Coding Failed", contentBuf.c_str(), NotificationType::Error, 6.0f );
                }
            }

            s_lastObservedState = bCompiling ? BuildState::Compiling : state;

            if ( bCompiling )
            {
                const float32 elapsed = pCompiler->getElapsedTimeSec();
                ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.7f, 0.5f, 0.1f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.8f, 0.3f, 0.2f, 1.0f ) );
                fixed_string<constant::kMaxBuffer64> label;
                formatstring( label.data(), label.capacity(), "Compiling (%#s) [Cancel]", Fmt( static_cast<float64>( elapsed ), Format().precision( 1 ) ) );
                if ( ImGui::SmallButton( label.c_str() ) )
                    pCompiler->cancel();
                ImGui::PopStyleColor( 2 );
            }
            else
            {
                if ( ImGui::SmallButton( "Compile" ) )
                    pCompiler->compileModule( "SWGame" );

                if ( ImGui::IsItemHovered() )
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted( "Live Coding: Compile SWGame (Ctrl+Alt+F11 / F7)" );
                    ImGui::EndTooltip();
                }
            }

            ImGui::SameLine();
            if ( state == BuildState::Compiling )
            {
                ImGui::TextColored( ImVec4( 0.95f, 0.75f, 0.25f, 1.0f ), "Compiling..." );
            }
            else if ( state == BuildState::Success )
            {
                ImGui::TextColored( ImVec4( 0.35f, 0.85f, 0.35f, 1.0f ), "Built (%.1fs)", static_cast<float64>( pCompiler->getLastDurationSec() ) );
            }
            else if ( state == BuildState::Failed )
            {
                ImGui::TextColored( ImVec4( 0.95f, 0.35f, 0.35f, 1.0f ), "Build Failed" );
            }
            else
            {
                ImGui::TextDisabled( "Ready" );
            }

            ImGui::SameLine();
            ImGui::TextDisabled( "|" );
            ImGui::SameLine();
        }

        EditorContext* pContext   = EditorContext::get();
        IRHIDevice*    pRhiDevice = ( pContext != nullptr ) ? pContext->getRhiDevice() : nullptr;
        const utf8*    pBackend   = ( pRhiDevice != nullptr ) ? pRhiDevice->getBackendName() : "n/a";
        ImGui::TextDisabled( "RHI %s | %.0f FPS", pBackend, static_cast<float64>( ImGui::GetIO().Framerate ) );
        if ( ImGui::IsItemHovered() )
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted( "Switch RHI: -dx11 / -dx12 / -vk / -gl" );
            const bool bVk = RHIAvailability::query( RHIBackend::Vulkan )._bEditorSupported;
            const bool bGl = RHIAvailability::query( RHIBackend::OpenGL )._bEditorSupported;
            ImGui::Text( "Vulkan editor: %s", bVk ? "yes" : "no" );
            ImGui::Text( "OpenGL editor: %s", bGl ? "yes" : "no" );
            ImGui::EndTooltip();
        }

        ImGui::EndMainMenuBar();
    }

    void EditorMenuBar::processHotkeys()
    {
        ImGuiIO& io = ImGui::GetIO();
        if ( io.WantTextInput == false )
        {
            IModuleCompiler* pCompiler = getService<IModuleCompiler>();

            if ( io.KeyCtrl || io.KeySuper )
            {
                if ( ImGui::IsKeyPressed( ImGuiKey_Z, false ) && EditorPlaySession::isStopped() )
                    getService<CommandStack>()->undo();
                if ( ImGui::IsKeyPressed( ImGuiKey_Y, false ) && EditorPlaySession::isStopped() )
                    getService<CommandStack>()->redo();
                if ( ImGui::IsKeyPressed( ImGuiKey_O, false ) )
                    EditorMenuBarInternal::openSceneFileDialog();
                if ( ImGui::IsKeyPressed( ImGuiKey_S, false ) )
                    EditorMenuBarInternal::saveFocusedOrScene();
                if ( io.KeyShift && ImGui::IsKeyPressed( ImGuiKey_P, false ) )
                    CommandPalettePopup::toggle();
                else if ( ImGui::IsKeyPressed( ImGuiKey_P, false ) )
                    QuickLauncherPopup::toggle();
                if ( ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
                    CommandPalettePopup::toggle();
                if ( io.KeyAlt && ImGui::IsKeyPressed( ImGuiKey_F11, false ) )
                {
                    if ( pCompiler != nullptr )
                        pCompiler->compileModule( "SWGame" );
                }
                if ( io.KeyShift && ImGui::IsKeyPressed( ImGuiKey_B, false ) )
                {
                    if ( pCompiler != nullptr )
                        pCompiler->compileAll();
                }
            }
            else if ( ImGui::IsKeyPressed( ImGuiKey_F7, false ) )
            {
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "SWGame" );
            }
        }
    }

    void EditorMenuBar::processOpenPanelRequests()
    {
        string openTitle;
        if ( EditorContext::get()->getWorkspace().consumeOpenPanel( openTitle ) == false )
            return;
        if ( EditorContext::get()->getPanelManager().setPanelOpen( openTitle, true ) )
            ImGui::SetWindowFocus( openTitle.c_str() );
    }

    void EditorMenuBar::processPendingSceneLoad()
    {
        string scenePath;
        if ( EditorContext::get()->getWorkspace().consumeLoadScene( scenePath ) == false )
            return;

        EditorAssetCommands::tryOpenScene( scenePath );
    }

    void EditorMenuBar::processSceneSession()
    {
        EditorAssetCommands::syncAfterSceneGenerationChange();
        processPendingSceneLoad();

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        if ( pContext->getWorkspace().getPendingSceneAction() == EditorPendingSceneAction::None )
            return;

        if ( ImGui::IsPopupOpen( "##UnsavedSceneAction" ) == false )
            ImGui::OpenPopup( "##UnsavedSceneAction" );

        const utf8* pMessage = "You have unsaved changes. Continue?";
        if ( pContext->getWorkspace().getPendingSceneAction() == EditorPendingSceneAction::New )
            pMessage = "Scene has unsaved changes. Create a new scene anyway?";
        else if ( pContext->getWorkspace().getPendingSceneAction() == EditorPendingSceneAction::Load )
            pMessage = "Scene has unsaved changes. Open another scene anyway?";
        else if ( pContext->getWorkspace().getPendingSceneAction() == EditorPendingSceneAction::Quit )
            pMessage = "You have unsaved changes. Exit anyway?";

        const EditorUnsavedChoice choice = EditorWidgets::drawUnsavedChangesModal( "##UnsavedSceneAction", pMessage );
        if ( choice != EditorUnsavedChoice::None )
            EditorAssetCommands::applyUnsavedSceneChoice( choice );
    }
} // namespace sw::editor
