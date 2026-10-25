#include "pch.h"

#include "Editor/Common/GUI/EditorMenuBar.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorCommandGUI.h"
#include "Editor/Common/GUI/EditorDockLayout.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorStatusBar.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorLayoutStore.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/ReflectionValidation.h"
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
            /** @brief 메뉴바가 프레임 사이에 들고 있는 상태입니다. 에디터 범위의 상태 저장소가 소유해 에디터마다(테스트마다) 따로이다. */
            struct MenuBarState
            {
                bool                                 _bShowThemeSettings{ false };
                float32                              _statusAreaWidth{ 0.0f };                    ///< 지난 프레임에 측정한 상태 영역 너비(0 = 아직 모름)
                fixed_string<constant::kMaxBuffer64> _layoutName{};                               ///< 이름 붙인 레이아웃 저장 칸의 글
                BuildState                           _lastObservedBuildState{ BuildState::Idle }; ///< 지난 프레임에 본 빌드 상태(컴파일이 끝나는 순간을 잡는다)
            };

            static MenuBarState& getState() { return getEditorState<MenuBarState>(); }
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

        // File · Edit · Build 는 커맨드 테이블의 메뉴 경로 · 순서 칸에서 나온다(`EditorCommandGUI.cpp`).
        EditorCommandGUI::drawMainMenus();
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
            EditorAssetTypeRegistry::forEachToolPanel( []( EditorAssetType kind, const utf8* pTitle )
            {
                // 람다는 바깥의 `pContext` 를 캡처하지 않는다. 여기서 다시 받아 다시 확인한다.
                EditorContext* pMenuContext = EditorContext::get();
                if ( pMenuContext != nullptr && ImGui::MenuItem( pTitle ) )
                    pMenuContext->getSubsystem<EditorWorkspace>().requestOpenDocumentPanel( kind );
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
            for ( const EditorPanelEntry& entry : pContext->getSubsystem<EditorPanelManager>().getPanels() )
            {
                if ( entry._pInstance == nullptr || entry._category != EditorPanelCategory::Core )
                    continue;
                const bool bOpen = entry._pInstance->isOpen();
                if ( ImGui::MenuItem( entry._title.c_str(), nullptr, bOpen ) )
                    entry._pInstance->setOpen( bOpen == false );
            }

            ImGui::SeparatorText( "Tools" );
            for ( const EditorPanelEntry& entry : pContext->getSubsystem<EditorPanelManager>().getPanels() )
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
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ResetLayout" ) );
            drawNamedLayoutMenu( dockLayout );

            ImGui::EndMenu();
        }
    }

    void EditorMenuBar::drawNamedLayoutMenu( EditorDockLayout& dockLayout )
    {
        if ( ImGui::BeginMenu( "Layouts" ) == false )
            return;

        fixed_string<constant::kMaxBuffer64>& layoutName = EditorMenuBarInternal::getState()._layoutName;
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 12.0f );
        const bool bEnter = ImGui::InputTextWithHint( "##LayoutName", SW_LOCKEY( "EdCommon", "LayoutNameHint" ), layoutName.data(), layoutName.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        ImGui::SameLine();
        if ( ( ImGui::Button( "Save" ) || bEnter ) && dockLayout.saveNamedLayout( layoutName.c_str() ) )
            layoutName.clear();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "SaveLayout" ) );

        vector<string> listName;
        EditorLayoutStore::collectNames( EditorLayoutStore::getDefaultFolder(), listName );
        if ( listName.empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "NoSavedLayouts" ) );
        for ( const string& name : listName )
        {
            ImGui::PushID( name.c_str() );
            if ( ImGui::MenuItem( name.c_str() ) )
                (void)dockLayout.requestLoadNamedLayout( name ); // 파일이 사라졌으면 경고만 남긴다
            ImGui::SameLine( ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFontSize() * 1.5f );
            if ( ImGui::SmallButton( "x" ) && EditorLayoutStore::remove( EditorLayoutStore::getDefaultFolder(), name ) == false )
                SW_LOG_WARNING( "Could not delete layout '%#'", name.c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "DeleteLayout" ) );
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }

    void EditorMenuBar::drawStatusArea()
    {
        IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>(); // 선택: Shipping 에는 모듈 컴파일러가 없다(ModuleHost 가 SW_SHIPPING 에서 만들지 않는다)
        if ( pCompiler != nullptr )
            notifyLiveCodingResult( *pCompiler );

        // 상태 영역은 지난 프레임에 측정한 너비로 오른쪽 끝에 붙인다. 메뉴와 겹칠 위치면 이번 프레임은 그리지 않는다 — 창을 줄여도 메뉴가
        // 가려지지 않고, 넓히면 다시 나온다. 첫 프레임은 너비를 모르므로 오른쪽 끝에서 그려 측정한다.
        const float32 menuEndX     = ImGui::GetCursorPosX();
        const float32 statusStartX = ImGui::GetWindowWidth() - EditorMenuBarInternal::getState()._statusAreaWidth - ImGui::GetStyle().WindowPadding.x;
        if ( statusStartX < menuEndX )
            return;

        ImGui::SameLine( statusStartX );
        ImGui::BeginGroup();
        drawStatusContent( pCompiler );
        ImGui::EndGroup();
        EditorMenuBarInternal::getState()._statusAreaWidth = ImGui::GetItemRectSize().x;
    }

    void EditorMenuBar::notifyLiveCodingResult( IModuleCompiler& compiler )
    {
        BuildState& lastObservedState = EditorMenuBarInternal::getState()._lastObservedBuildState;

        const bool       bCompiling = compiler.isCompiling();
        const BuildState state      = compiler.getBuildState();

        // 컴파일이 끝나는 순간을 잡는다(Compiling -> Success / Failed)
        if ( lastObservedState == BuildState::Compiling && bCompiling == false )
        {
            const string   targetName  = compiler.getTargetName();
            const string   displayName = targetName.empty() ? string{ SW_LOCKEY( "EdCommon", "LiveCodingAllModules" ) } : targetName;
            const float32  duration    = compiler.getLastDurationSec();
            EditorContext* pContext    = EditorContext::get();
            if ( pContext == nullptr )
                return;

            if ( state == BuildState::Success )
            {
                fixed_string<constant::kMaxBuffer32> secondsBuf;
                formatstring( secondsBuf.data(), secondsBuf.capacity(), "%#", Fmt( static_cast<float64>( duration ), Format().precision( 2 ) ) );
                const string content = SW_LOCKEYFORMAT( "EdCommon", "LiveCodingSucceeded", TextArgumentList().addText( "target", displayName ).addText( "seconds", secondsBuf.c_str() ) );
                pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "LiveCodingSucceededTitle" ), content, NotificationType::Success, 4.0f );
            }
            else if ( state == BuildState::Failed )
            {
                const string content = SW_LOCKEYFORMAT( "EdCommon", "LiveCodingFailed", TextArgumentList().addText( "target", displayName ).addInteger( "code", compiler.getLastExitCode() ) );
                pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "LiveCodingFailedTitle" ), content, NotificationType::Error, 6.0f );
            }
        }

        lastObservedState = bCompiling ? BuildState::Compiling : state;
    }

    void EditorMenuBar::drawStatusContent( IModuleCompiler* pCompiler )
    {
        if ( pCompiler != nullptr )
        {
            const bool       bCompiling = pCompiler->isCompiling();
            const BuildState state      = pCompiler->getBuildState();

            if ( bCompiling )
            {
                static constexpr Color kCompilingButton{ 0.7f, 0.5f, 0.1f, 1.0f };
                static constexpr Color kCompilingButtonHovered{ 0.8f, 0.3f, 0.2f, 1.0f };
                const float32          elapsed = pCompiler->getElapsedTimeSec();
                ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( kCompilingButton ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorColorUtil::toImVec4( kCompilingButtonHovered ) );
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

                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "CompileGame" ) );
            }

            ImGui::SameLine();
            if ( state == BuildState::Compiling )
            {
                EditorThemeUtil::textWarning( EditorThemeUtil::makeIconLabel( editoricon::kSpinner, "Compiling..." ) );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "BuildCompiling" ) );
            }
            else if ( state == BuildState::Success )
            {
                EditorThemeUtil::pushTextColor( EditorThemeUtil::getSuccessColor() );
                ImGui::Text( "%s  Built (%.1fs)", editoricon::kSuccess, static_cast<float64>( pCompiler->getLastDurationSec() ) );
                EditorThemeUtil::popTextColor();
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "BuildSucceeded" ) );
            }
            else if ( state == BuildState::Failed )
            {
                EditorThemeUtil::textError( EditorThemeUtil::makeIconLabel( editoricon::kError, "Build Failed" ) );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "BuildFailed" ) );
            }
            else
            {
                ImGui::TextDisabled( "%s  Ready", editoricon::kCheck );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "BuildReady" ) );
            }

            ImGui::SameLine();
            ImGui::TextDisabled( "|" );
            ImGui::SameLine();
        }

        EditorContext* pContext = EditorContext::get();

        // 패널 · 확장이 더한 상태 항목(맵 검사 수 …) — 메뉴 막대는 무엇인지 모르고 순서대로 그린다.
        const uint32 statusItemCount = EditorExtensionRegistry<EditorStatusBarItemRegistration>::getCount();
        for ( uint32 itemIndex = 0; itemIndex < statusItemCount; ++itemIndex )
        {
            EditorExtensionRegistry<EditorStatusBarItemRegistration>::getAt( itemIndex )._pfnDraw();
            ImGui::SameLine();
        }

        IRHIDevice* pRHIDevice = ( pContext != nullptr ) ? pContext->getSubsystem<EditorRenderViews>().getRHIDevice() : nullptr;
        const utf8* pBackend   = ( pRHIDevice != nullptr ) ? pRHIDevice->getBackendName() : "n/a";
        ImGui::TextDisabled( "RHI %s | %.0f FPS", pBackend, static_cast<float64>( ImGui::GetIO().Framerate ) );
        if ( ImGui::IsItemHovered() )
        {
            const int64  fps     = static_cast<int64>( ImGui::GetIO().Framerate + 0.5f );
            const string backend = SW_LOCKEYFORMAT( "EdCommon", "RHIBackend", TextArgumentList().addText( "backend", pBackend ).addInteger( "fps", fps ) );
            ImGui::BeginTooltip();
            ImGui::TextUnformatted( backend.c_str() );
            ImGui::Separator();
            ImGui::TextUnformatted( SW_LOCKEY( "EdCommon", "RHISwitch" ) );
            ImGui::EndTooltip();
        }
    }

    void EditorMenuBar::drawThemeDialog()
    {
        if ( EditorMenuBarInternal::getState()._bShowThemeSettings )
            EditorThemeUtil::drawThemeSettingsDialog( &EditorMenuBarInternal::getState()._bShowThemeSettings );
    }

    void EditorMenuBar::openThemeDialog()
    {
        EditorMenuBarInternal::getState()._bShowThemeSettings = true;
    }

    void EditorMenuBar::closeThemeDialog()
    {
        EditorMenuBarInternal::getState()._bShowThemeSettings = false;
    }

    void EditorMenuBar::processOpenPanelRequests()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        EditorPanelOpenRequest request;
        if ( pContext->getSubsystem<EditorWorkspace>().consumeOpenPanel( request ) == false )
            return;
        const EditorPanelManager& panelManager = pContext->getSubsystem<EditorPanelManager>();
        IEditorPanel*             pPanel       = request._documentKind != EditorAssetType::Unknown ? panelManager.findDocumentPanel( request._documentKind ) : panelManager.findPanelByID( request._panelID );
        if ( pPanel == nullptr )
        {
            SW_LOG_ERROR( "Open panel request found no panel (id '%#', document kind %#)", request._panelID.c_str(), static_cast<uint32>( request._documentKind ) );
            return;
        }
        pPanel->setOpen( true );
        ImGui::SetWindowFocus( pPanel->getPanelTitle() ); // 창 이름은 인스턴스의 제목이다(`IEditorPanel::draw`)
    }

    void EditorMenuBar::processPendingSceneLoad()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        string scenePath;
        if ( pContext->getSubsystem<EditorWorkspace>().consumeLoadScene( scenePath ) == false )
            return;

        (void)EditorSceneFileCommands::tryOpenScene( scenePath ); // 실패는 tryOpenScene 이 알린다
    }

    void EditorMenuBar::processSceneSession()
    {
        EditorSceneFileCommands::syncAfterSceneGenerationChange();
        processPendingSceneLoad();

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        if ( pContext->getSubsystem<EditorWorkspace>().getPendingSceneAction() == EditorPendingSceneAction::None )
            return;

        if ( ImGui::IsPopupOpen( "##UnsavedSceneAction" ) == false )
            ImGui::OpenPopup( "##UnsavedSceneAction" );

        const utf8* pMessage = "You have unsaved changes. Continue?";
        if ( pContext->getSubsystem<EditorWorkspace>().getPendingSceneAction() == EditorPendingSceneAction::New )
            pMessage = "Scene has unsaved changes. Create a new scene anyway?";
        else if ( pContext->getSubsystem<EditorWorkspace>().getPendingSceneAction() == EditorPendingSceneAction::Load )
            pMessage = "Scene has unsaved changes. Open another scene anyway?";
        else if ( pContext->getSubsystem<EditorWorkspace>().getPendingSceneAction() == EditorPendingSceneAction::Quit )
            pMessage = "You have unsaved changes. Exit anyway?";

        const EditorUnsavedChoice choice = EditorWidgets::drawUnsavedChangesModal( "##UnsavedSceneAction", pMessage );
        if ( choice != EditorUnsavedChoice::None )
            EditorSceneFileCommands::applyUnsavedSceneChoice( choice );
    }
} // namespace sw::editor
