#include "pch.h"

#include "Editor/Common/Gui/EditorMenuBar.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/EditorPlaySession.h"
#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
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
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"

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

                const string activePack = GameConfig::getActive()._packRoot;
                const string mapsDir    = ResourceUtil::getDomainFolderPath( activePack, path::kMapsFolder );
                if ( FileUtil::directoryExists( mapsDir ) )
                    params._initialDirectory = mapsDir;
                else if ( ResourceUtil::getDomainFolderPath( activePack ).empty() == false )
                    params._initialDirectory = ResourceUtil::getDomainFolderPath( activePack );
                else if ( ResourceUtil::getDomainFolderPath( path::kGamePack ).empty() == false )
                    params._initialDirectory = ResourceUtil::getDomainFolderPath( path::kGamePack );

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
            if ( ImGui::MenuItem( ICON_FA_FILE "  New Scene" ) )
                EditorAssetCommands::tryCreateNewScene();
            EditorWidgets::drawTooltip( "새로운 빈 씬을 생성합니다" );

            if ( ImGui::MenuItem( ICON_FA_FOLDER_OPEN "  Open Scene...", "Ctrl+O" ) )
                EditorMenuBarInternal::openSceneFileDialog();
            EditorWidgets::drawTooltip( "디스크에서 기존 씬 파일(.scene.xml)을 엽니다 (Ctrl+O)" );

            if ( ImGui::MenuItem( ICON_FA_FLOPPY_DISK "  Save", "Ctrl+S" ) )
                EditorMenuBarInternal::saveFocusedOrScene();
            EditorWidgets::drawTooltip( "현재 포커스된 에셋 또는 활성 씬을 저장합니다 (Ctrl+S)" );

            if ( ImGui::MenuItem( ICON_FA_FLOPPY_DISK "  Save Scene" ) )
                EditorMenuBarInternal::saveSceneOrPrompt();
            EditorWidgets::drawTooltip( "현재 활성화된 씬을 디스크에 저장합니다" );

            ImGui::Separator();
            if ( ImGui::MenuItem( ICON_FA_MAGNIFYING_GLASS "  Quick Open...", "Ctrl+P" ) )
                QuickLauncherPopup::open();
            EditorWidgets::drawTooltip( "에셋, 씬, 스크립트를 빠르게 검색하여 엽니다 (Ctrl+P)" );

            if ( ImGui::MenuItem( ICON_FA_TERMINAL "  Command Palette...", "Ctrl+Shift+P / Ctrl+Space" ) )
                CommandPalettePopup::open();
            EditorWidgets::drawTooltip( "에디터 명령 및 액션을 검색하여 실행합니다 (Ctrl+Shift+P / Ctrl+Space)" );

            ImGui::Separator();
            if ( ImGui::MenuItem( ICON_FA_RIGHT_FROM_BRACKET "  Exit", "Alt+F4" ) )
                EditorAssetCommands::requestExit();
            EditorWidgets::drawTooltip( "에디터를 종료합니다 (Alt+F4)" );

            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "Edit" ) )
        {
            if ( ImGui::MenuItem( ICON_FA_ROTATE_LEFT "  Undo", "Ctrl+Z", false, EditorPlaySession::isStopped() ) )
                getService<CommandStack>()->undo();
            EditorWidgets::drawTooltip( "마지막 편집 작업을 되돌립니다 (Ctrl+Z)" );

            if ( ImGui::MenuItem( ICON_FA_ROTATE_RIGHT "  Redo", "Ctrl+Y", false, EditorPlaySession::isStopped() ) )
                getService<CommandStack>()->redo();
            EditorWidgets::drawTooltip( "되돌린 편집 작업을 다시 실행합니다 (Ctrl+Y)" );

            ImGui::Separator();
            if ( ImGui::MenuItem( ICON_FA_PALETTE "  Theme & Look and Feel..." ) )
                EditorMenuBarInternal::_s_bShowThemeSettings = true;
            EditorWidgets::drawTooltip( "에디터 테마 프리셋, 액센트 색상 및 모서리 라운딩을 설정합니다" );

            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "Build" ) )
        {
            IModuleCompiler* pCompiler  = getService<IModuleCompiler>();
            const bool       bCompiling = ( pCompiler != nullptr && pCompiler->isCompiling() );

            if ( ImGui::MenuItem( ICON_FA_HAMMER "  Compile Game (SWGame)", "Ctrl+Alt+F11", false, bCompiling == false ) )
            {
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "SWGame" );
            }
            EditorWidgets::drawTooltip( "게임 모듈(SWGame)을 라이브 코딩으로 즉시 재컴파일합니다 (Ctrl+Alt+F11)" );

            if ( ImGui::MenuItem( ICON_FA_WRENCH "  Compile Editor (EditorModule)", nullptr, false, bCompiling == false ) )
            {
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "EditorModule" );
            }
            EditorWidgets::drawTooltip( "에디터 모듈(EditorModule)을 라이브 코딩으로 재컴파일합니다" );

            if ( ImGui::MenuItem( ICON_FA_BOXES_STACKED "  Compile All Modules", "Ctrl+Shift+B", false, bCompiling == false ) )
            {
                if ( pCompiler != nullptr )
                    pCompiler->compileAll();
            }
            EditorWidgets::drawTooltip( "엔진 및 모든 게임/에디터 모듈을 전체 빌드합니다 (Ctrl+Shift+B)" );

            ImGui::Separator();

            if ( ImGui::MenuItem( ICON_FA_BAN "  Cancel Build", nullptr, false, bCompiling ) )
            {
                if ( pCompiler != nullptr )
                    pCompiler->cancel();
            }
            EditorWidgets::drawTooltip( "현재 진행 중인 컴파일 작업을 취소합니다" );

            ImGui::EndMenu();
        }

        if ( ImGui::BeginMenu( "Assets" ) )
        {
            uint32                 kindCount{ 0 };
            const EditorAssetKind* pKind = EditorAssetTypeRegistry::getToolPanelKinds( kindCount );
            for ( uint32 index = 0; index < kindCount; ++index )
            {
                const utf8* pTitle = EditorAssetTypeRegistry::getPanelTitle( pKind[index] );
                if ( StringUtil::isNullOrEmpty( pTitle ) )
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
            if ( ImGui::MenuItem( ICON_FA_TABLE_COLUMNS "  Reset Default Layout" ) )
                dockLayout.requestResetDefault();
            EditorWidgets::drawTooltip( "도킹 창 배치를 기본 에디터 레이아웃으로 초기화합니다" );

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
                ImGui::TextColored( ImVec4( 0.95f, 0.75f, 0.25f, 1.0f ), ICON_FA_SPINNER " Compiling..." );
                EditorWidgets::drawTooltip( "현재 백그라운드에서 모듈을 빌드하고 있습니다" );
            }
            else if ( state == BuildState::Success )
            {
                ImGui::TextColored( ImVec4( 0.35f, 0.85f, 0.35f, 1.0f ), ICON_FA_CIRCLE_CHECK " Built (%.1fs)", static_cast<float64>( pCompiler->getLastDurationSec() ) );
                EditorWidgets::drawTooltip( "마지막 빌드가 성공적으로 완료되었습니다" );
            }
            else if ( state == BuildState::Failed )
            {
                ImGui::TextColored( ImVec4( 0.95f, 0.35f, 0.35f, 1.0f ), ICON_FA_CIRCLE_XMARK " Build Failed" );
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
            const bool bVk = RHIAvailability::query( RHIBackend::Vulkan )._bEditorSupported;
            const bool bGl = RHIAvailability::query( RHIBackend::OpenGL )._bEditorSupported;
            ImGui::Text( "Vulkan 에디터 지원: %s", bVk ? "사용 가능" : "미지원" );
            ImGui::Text( "OpenGL 에디터 지원: %s", bGl ? "사용 가능" : "미지원" );
            ImGui::EndTooltip();
        }

        ImGui::EndMainMenuBar();
    }

    void EditorMenuBar::drawThemeDialog()
    {
        if ( EditorMenuBarInternal::_s_bShowThemeSettings )
            EditorThemeUtil::drawThemeSettingsDialog( &EditorMenuBarInternal::_s_bShowThemeSettings );
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
