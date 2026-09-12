#include "pch.h"

#include "Editor/Common/Gui/EditorMenuBar.h"

#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Gui/EditorCommandGui.h"
#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Gui/EditorNotificationManager.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/IModuleCompiler.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorMenuBarInternal
        {
            inline static bool _s_bShowThemeSettings = false;
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

        drawFileMenu();
        drawEditMenu();
        drawBuildMenu();
        drawAssetsMenu();
        drawPanelMenu( dockLayout );
        drawStatusArea();

        ImGui::EndMainMenuBar();
    }

    void EditorMenuBar::drawFileMenu()
    {
        if ( ImGui::BeginMenu( "File" ) )
        {
            EditorCommandGui::drawMenuItem( "scene.new" );
            EditorCommandGui::drawMenuItem( "scene.open" );
            EditorCommandGui::drawMenuItem( "asset.save" );
            EditorCommandGui::drawMenuItem( "scene.saveScene" );

            ImGui::Separator();
            EditorCommandGui::drawMenuItem( "editor.quickOpen" );
            EditorCommandGui::drawMenuItem( "editor.commandPalette" );

            ImGui::Separator();
            EditorCommandGui::drawMenuItem( "editor.exit" );

            ImGui::EndMenu();
        }
    }

    void EditorMenuBar::drawEditMenu()
    {
        if ( ImGui::BeginMenu( "Edit" ) )
        {
            EditorCommandGui::drawMenuItem( "edit.undo" );
            EditorCommandGui::drawMenuItem( "edit.redo" );

            ImGui::Separator();
            EditorCommandGui::drawMenuItem( "editor.themeSettings" );

            ImGui::EndMenu();
        }
    }

    void EditorMenuBar::drawBuildMenu()
    {
        if ( ImGui::BeginMenu( "Build" ) )
        {
            EditorCommandGui::drawMenuItem( "build.compileGame" );
            EditorCommandGui::drawMenuItem( "build.compileEditor" );
            EditorCommandGui::drawMenuItem( "build.compileAll" );

            ImGui::Separator();
            EditorCommandGui::drawMenuItem( "build.cancel" );

            ImGui::EndMenu();
        }
    }

    void EditorMenuBar::drawAssetsMenu()
    {
        if ( ImGui::BeginMenu( "Assets" ) )
        {
            EditorAssetTypeRegistry::forEachToolPanelTitle( []( const utf8* pTitle )
            {
                if ( ImGui::MenuItem( pTitle ) )
                    EditorContext::get()->getWorkspace().requestOpenPanel( pTitle );
            } );
            ImGui::EndMenu();
        }
    }

    void EditorMenuBar::drawPanelMenu( EditorDockLayout& dockLayout )
    {
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
            if ( ImGui::MenuItem( ICON_FA_TABLE_COLUMNS "  Reset Default Layout" ) )
                dockLayout.requestResetDefault();
            EditorWidgets::drawTooltip( "도킹 창 배치를 기본 에디터 레이아웃으로 초기화합니다" );

            ImGui::EndMenu();
        }
    }

    void EditorMenuBar::drawStatusArea()
    {
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
                formatstring( label.data(), label.capacity(), ICON_FA_SPINNER " Compiling (%#s)", Fmt( static_cast<float64>( elapsed ), Format().precision( 1 ) ) );
                if ( ImGui::SmallButton( label.c_str() ) )
                    pCompiler->cancel();
                ImGui::PopStyleColor( 2 );
            }
            else
            {
                if ( ImGui::SmallButton( ICON_FA_HAMMER " Compile" ) )
                    pCompiler->compileModule( "SWGame" );

                EditorWidgets::drawTooltip( "라이브 코딩: SWGame 모듈을 즉시 컴파일하고 핫리로드합니다 (Ctrl+Alt+F11)" );
            }

            ImGui::SameLine();
            if ( state == BuildState::Compiling )
            {
                EditorThemeUtil::textWarning( ICON_FA_SPINNER " Compiling..." );
                EditorWidgets::drawTooltip( "현재 백그라운드에서 모듈을 빌드하고 있습니다" );
            }
            else if ( state == BuildState::Success )
            {
                EditorThemeUtil::pushTextColor( EditorThemeUtil::getSuccessColor() );
                ImGui::Text( ICON_FA_CIRCLE_CHECK " Built (%.1fs)", static_cast<float64>( pCompiler->getLastDurationSec() ) );
                EditorThemeUtil::popTextColor();
                EditorWidgets::drawTooltip( "마지막 빌드가 성공적으로 완료되었습니다" );
            }
            else if ( state == BuildState::Failed )
            {
                EditorThemeUtil::textError( ICON_FA_CIRCLE_XMARK " Build Failed" );
                EditorWidgets::drawTooltip( "빌드에 실패했습니다. 콘솔 창에서 상세 오류를 확인하세요." );
            }
            else
            {
                ImGui::TextDisabled( ICON_FA_CHECK " Ready" );
                EditorWidgets::drawTooltip( "라이브 코딩 빌드 준비 완료" );
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
            ImGui::Text( "현재 그래픽스 RHI 백엔드: %s (%.0f FPS)", pBackend, static_cast<float64>( ImGui::GetIO().Framerate ) );
            ImGui::Separator();
            ImGui::TextUnformatted( "실행 인수로 RHI 전환: -dx11 / -dx12 / -vk / -gl" );
            ImGui::EndTooltip();
        }
    }

    void EditorMenuBar::drawThemeDialog()
    {
        if ( EditorMenuBarInternal::_s_bShowThemeSettings )
            EditorThemeUtil::drawThemeSettingsDialog( &EditorMenuBarInternal::_s_bShowThemeSettings );
    }

    void EditorMenuBar::openThemeDialog()
    {
        EditorMenuBarInternal::_s_bShowThemeSettings = true;
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
