#include "pch.h"

#include "Editor/Common/Gui/EditorMenuBar.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Gui/EditorCommandGui.h"
#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Gui/EditorIconGlyphs.h"
#include "Editor/Common/Gui/EditorNotificationManager.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Gui/IEditorPanel.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorLayoutStore.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/IModuleCompiler.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorMenuBarInternal
        {
            inline static bool    _s_bShowThemeSettings = false;
            inline static float32 _s_statusAreaWidth    = 0.0f; ///< 지난 프레임에 잰 상태 영역 너비(0 = 아직 모름)
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

        // File · Edit · Build 는 커맨드 표의 메뉴 경로 · 순서 칸에서 나온다(`EditorCommandGui.cpp`).
        EditorCommandGui::drawMainMenus();
        drawAssetsMenu();
        drawPanelMenu( dockLayout );
        drawStatusArea();

        ImGui::EndMainMenuBar();
    }

    void EditorMenuBar::drawAssetsMenu()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        if ( ImGui::BeginMenu( "Assets" ) )
        {
            EditorAssetTypeRegistry::forEachToolPanelTitle( []( const utf8* pTitle )
            {
                // 람다는 바깥의 `pContext` 를 캡처하지 않는다. 여기서 다시 받아 다시 확인한다.
                EditorContext* pMenuContext = EditorContext::get();
                if ( pMenuContext != nullptr && ImGui::MenuItem( pTitle ) )
                    pMenuContext->getWorkspace().requestOpenPanel( pTitle );
            } );
            ImGui::EndMenu();
        }
    }

    void EditorMenuBar::drawPanelMenu( EditorDockLayout& dockLayout )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        if ( ImGui::BeginMenu( "Panel" ) )
        {
            ImGui::SeparatorText( "Panels" );
            for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
            {
                if ( entry._pInstance == nullptr || entry._category != EditorPanelCategory::Core )
                    continue;
                const bool bOpen = entry._pInstance->isOpen();
                if ( ImGui::MenuItem( entry._title.c_str(), nullptr, bOpen ) )
                    entry._pInstance->setOpen( bOpen == false );
            }

            ImGui::SeparatorText( "Tools" );
            for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
            {
                if ( entry._pInstance == nullptr || entry._category == EditorPanelCategory::Core )
                    continue;
                const bool bOpen = entry._pInstance->isOpen();
                if ( ImGui::MenuItem( entry._title.c_str(), nullptr, bOpen ) )
                    entry._pInstance->setOpen( bOpen == false );
            }

            ImGui::Separator();
            if ( ImGui::MenuItem( EditorThemeUtil::makeIconLabel( editoricon::kLayout, "Reset Default Layout" ) ) )
                dockLayout.requestResetDefault();
            EditorWidgets::drawTooltip( "도킹 창 배치를 기본 에디터 레이아웃으로 초기화합니다" );
            drawNamedLayoutMenu( dockLayout );

            ImGui::EndMenu();
        }
    }

    void EditorMenuBar::drawNamedLayoutMenu( EditorDockLayout& dockLayout )
    {
        if ( ImGui::BeginMenu( "Layouts" ) == false )
            return;

        static fixed_string<constant::kMaxBuffer64> s_layoutName;
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 12.0f );
        const bool bEnter = ImGui::InputTextWithHint( "##LayoutName", "layout name", s_layoutName.data(), s_layoutName.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        ImGui::SameLine();
        if ( ( ImGui::Button( "Save" ) || bEnter ) && dockLayout.saveNamedLayout( s_layoutName.c_str() ) )
            s_layoutName.clear();
        EditorWidgets::drawTooltip( "지금 도킹 배치와 패널 가시성을 이 이름으로 저장합니다 (Saved/Editor/Layouts, 영숫자 · 공백 · _ · -)" );

        vector<string> listName;
        EditorLayoutStore::collectNames( EditorLayoutStore::getDefaultFolder(), listName );
        if ( listName.empty() )
            ImGui::TextDisabled( "No saved layouts." );
        for ( const string& name : listName )
        {
            ImGui::PushID( name.c_str() );
            if ( ImGui::MenuItem( name.c_str() ) )
                (void)dockLayout.requestLoadNamedLayout( name ); // 파일이 사라졌으면 경고만 남긴다
            ImGui::SameLine( ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFontSize() * 1.5f );
            if ( ImGui::SmallButton( "x" ) && EditorLayoutStore::remove( EditorLayoutStore::getDefaultFolder(), name ) == false )
                SW_LOG_WARNING( "Could not delete layout '%#'", name.c_str() );
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }

    void EditorMenuBar::drawStatusArea()
    {
        IModuleCompiler* pCompiler = getService<IModuleCompiler>();
        if ( pCompiler != nullptr )
            notifyLiveCodingResult( *pCompiler );

        // 상태 영역은 지난 프레임에 잰 너비로 오른쪽 끝에 붙인다. 메뉴와 겹칠 자리면 이번 프레임은 그리지 않는다 — 창을 줄여도 메뉴가
        // 가려지지 않고, 넓히면 다시 나온다. 첫 프레임은 너비를 모르므로 오른쪽 끝에서 그려 잰다.
        const float32 menuEndX     = ImGui::GetCursorPosX();
        const float32 statusStartX = ImGui::GetWindowWidth() - EditorMenuBarInternal::_s_statusAreaWidth - ImGui::GetStyle().WindowPadding.x;
        if ( statusStartX < menuEndX )
            return;

        ImGui::SameLine( statusStartX );
        ImGui::BeginGroup();
        drawStatusContent( pCompiler );
        ImGui::EndGroup();
        EditorMenuBarInternal::_s_statusAreaWidth = ImGui::GetItemRectSize().x;
    }

    void EditorMenuBar::notifyLiveCodingResult( IModuleCompiler& compiler )
    {
        static BuildState s_lastObservedState = BuildState::Idle;

        const bool       bCompiling = compiler.isCompiling();
        const BuildState state      = compiler.getBuildState();

        // 컴파일이 끝나는 순간을 잡는다(Compiling -> Success / Failed)
        if ( s_lastObservedState == BuildState::Compiling && bCompiling == false )
        {
            const string   targetName  = compiler.getTargetName();
            const string   displayName = targetName.empty() ? "All Modules" : targetName;
            const float32  duration    = compiler.getLastDurationSec();
            EditorContext* pContext    = EditorContext::get();
            if ( pContext == nullptr )
                return;

            if ( state == BuildState::Success )
            {
                fixed_string<constant::kMaxBuffer128> contentBuf;
                formatstring( contentBuf.data(), contentBuf.capacity(), "%# compiled and reloaded in %#s", displayName.c_str(), Fmt( static_cast<float64>( duration ), Format().precision( 2 ) ) );
                pContext->getNotificationManager().push( "Live Coding Succeeded", contentBuf.c_str(), NotificationType::Success, 4.0f );
            }
            else if ( state == BuildState::Failed )
            {
                fixed_string<constant::kMaxBuffer128> contentBuf;
                formatstring( contentBuf.data(), contentBuf.capacity(), "%s build failed (Exit: %d). See Output Log.", displayName.c_str(), compiler.getLastExitCode() );
                pContext->getNotificationManager().push( "Live Coding Failed", contentBuf.c_str(), NotificationType::Error, 6.0f );
            }
        }

        s_lastObservedState = bCompiling ? BuildState::Compiling : state;
    }

    void EditorMenuBar::drawStatusContent( IModuleCompiler* pCompiler )
    {
        if ( pCompiler != nullptr )
        {
            const bool       bCompiling = pCompiler->isCompiling();
            const BuildState state      = pCompiler->getBuildState();

            if ( bCompiling )
            {
                const float32 elapsed = pCompiler->getElapsedTimeSec();
                ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.7f, 0.5f, 0.1f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.8f, 0.3f, 0.2f, 1.0f ) );
                fixed_string<constant::kMaxBuffer64> label;
                formatstring( label.data(), label.capacity(), "%# Compiling (%#s)", editoricon::kSpinner, Fmt( static_cast<float64>( elapsed ), Format().precision( 1 ) ) );
                if ( ImGui::SmallButton( label.c_str() ) )
                    pCompiler->cancel();
                ImGui::PopStyleColor( 2 );
            }
            else
            {
                if ( ImGui::SmallButton( EditorThemeUtil::makeIconLabel( editoricon::kHammer, "Compile" ) ) )
                    pCompiler->compileModule( "SWGame" );

                EditorWidgets::drawTooltip( "라이브 코딩: SWGame 모듈을 즉시 컴파일하고 핫리로드합니다 (Ctrl+Alt+F11)" );
            }

            ImGui::SameLine();
            if ( state == BuildState::Compiling )
            {
                EditorThemeUtil::textWarning( EditorThemeUtil::makeIconLabel( editoricon::kSpinner, "Compiling..." ) );
                EditorWidgets::drawTooltip( "현재 백그라운드에서 모듈을 빌드하고 있습니다" );
            }
            else if ( state == BuildState::Success )
            {
                EditorThemeUtil::pushTextColor( EditorThemeUtil::getSuccessColor() );
                ImGui::Text( "%s  Built (%.1fs)", editoricon::kSuccess, static_cast<float64>( pCompiler->getLastDurationSec() ) );
                EditorThemeUtil::popTextColor();
                EditorWidgets::drawTooltip( "마지막 빌드가 성공적으로 완료되었습니다" );
            }
            else if ( state == BuildState::Failed )
            {
                EditorThemeUtil::textError( EditorThemeUtil::makeIconLabel( editoricon::kError, "Build Failed" ) );
                EditorWidgets::drawTooltip( "빌드에 실패했습니다. 콘솔 창에서 상세 오류를 확인하세요." );
            }
            else
            {
                ImGui::TextDisabled( "%s  Ready", editoricon::kCheck );
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

    void EditorMenuBar::closeThemeDialog()
    {
        EditorMenuBarInternal::_s_bShowThemeSettings = false;
    }

    void EditorMenuBar::processOpenPanelRequests()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        string openTitle;
        if ( pContext->getWorkspace().consumeOpenPanel( openTitle ) == false )
            return;
        if ( pContext->getPanelManager().setPanelOpen( openTitle, true ) )
            ImGui::SetWindowFocus( openTitle.c_str() );
    }

    void EditorMenuBar::processPendingSceneLoad()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        string scenePath;
        if ( pContext->getWorkspace().consumeLoadScene( scenePath ) == false )
            return;

        (void)EditorAssetCommands::tryOpenScene( scenePath ); // 실패는 tryOpenScene 이 알린다
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
