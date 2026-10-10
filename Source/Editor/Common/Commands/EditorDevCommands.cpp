/**
 * @file EditorDevCommands.cpp
 * @brief 에디터가 내주는 개발 명령(에디터 커맨드 실행 · 플레이 세션 · 선택 · 레이아웃)입니다. 에디터 모듈과 함께 올라오고 내려갑니다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Viewport/EditorCamera.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Utility/DebugOverlayState.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDevCommandsInternal
        {
            /** @brief 낱말을 양의 정수로 읽습니다. 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool parseCount( const string& text, uint32& outValue )
            {
                if ( text.empty() )
                    return false;
                uint64 value = 0;
                for ( const utf8 ch : text )
                {
                    if ( ch < '0' || '9' < ch || value > 1000000 )
                        return false;
                    value = value * 10 + static_cast<uint64>( ch - '0' );
                }
                outValue = static_cast<uint32>( value );
                return true;
            }

            static bool runEditorCommand( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = EditorContext::get();
                if ( listArgument.size() != 1 || pContext == nullptr )
                    return false;
                if ( pContext->getCommandRegistry().execute( listArgument[0] ) == false )
                {
                    outReply = "no enabled editor command '" + listArgument[0] + "'";
                    return false;
                }
                outReply = "ran " + listArgument[0];
                return true;
            }

            static bool runPlay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::play();
                outReply = "play";
                return true;
            }

            static bool runSimulate( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::simulate();
                outReply = "simulate";
                return true;
            }

            static bool runPause( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::pause();
                outReply = "pause";
                return true;
            }

            static bool runStop( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::stop();
                outReply = "stop";
                return true;
            }

            static bool runStep( const vector<string>& listArgument, string& outReply )
            {
                uint32 frameCount = 1;
                if ( listArgument.size() > 1 || ( listArgument.size() == 1 && parseCount( listArgument[0], frameCount ) == false ) || frameCount == 0 )
                    return false;
                EditorPlaySession::stepFrames( frameCount );
                outReply = "step " + to_string( frameCount );
                return true;
            }

            /** @brief 명령 인자 경로를 절대 경로로 바꿉니다(상대면 작업 폴더 기준 — 시나리오 산출물과 같은 `Saved/`). */
            [[nodiscard]] static bool resolveScenePath( const string& path, string& outAbsolutePath )
            {
                if ( path.empty() )
                    return false;
                if ( FileUtil::isAbsolutePath( path ) )
                {
                    outAbsolutePath = path;
                    return true;
                }
                return FileUtil::makeAbsolutePath( path, outAbsolutePath );
            }

            static bool runSaveSceneAs( const vector<string>& listArgument, string& outReply )
            {
                string absolutePath;
                if ( listArgument.size() != 1 || resolveScenePath( listArgument[0], absolutePath ) == false )
                    return false;
                (void)FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( absolutePath ) );
                if ( EditorAssetCommands::saveActiveScene( absolutePath ) == false )
                {
                    outReply = "could not save the active scene to '" + absolutePath + "'";
                    return false;
                }
                outReply = "saved " + absolutePath;
                return true;
            }

            static bool runOpenScene( const vector<string>& listArgument, string& outReply )
            {
                string absolutePath;
                if ( listArgument.size() != 1 || resolveScenePath( listArgument[0], absolutePath ) == false )
                    return false;
                // 미저장 씬이면 대화상자를 띄우는 길(tryOpenScene)이 아니라 바로 연다 — 무인 실행에서 대화상자는 아무도 닫지 않는다.
                if ( EditorAssetCommands::loadScene( absolutePath ) == false )
                {
                    outReply = "could not open '" + absolutePath + "'";
                    return false;
                }
                outReply = "opening " + absolutePath;
                return true;
            }

            static bool runTheme( const vector<string>& listArgument, string& outReply )
            {
                EditorThemePreset preset = EditorThemePreset::ModernDark;
                if ( listArgument.size() != 1 )
                    return false;
                if ( EditorThemeUtil::findPresetByConfigID( listArgument[0], preset ) == false )
                {
                    outReply = "unknown theme preset '" + listArgument[0] + "' (ModernDark, DeepCharcoal, MidnightBlue, ClassicDark)";
                    return false;
                }
                // 적용만 한다 — 설정 파일에 쓰지 않는다(시험 · 시나리오가 사용자 설정을 바꾸지 않게). 대화상자에서 고르면 저장된다.
                EditorThemeUtil::applyPreset( preset );
                outReply = "theme " + listArgument[0];
                return true;
            }

            static bool runSelectType( const vector<string>& listArgument, string& outReply )
            {
                GameObjectManager* pManager  = editor::getActiveObjectManager();
                TypeRegistry*      pRegistry = editor::getService<TypeRegistry>();
                if ( listArgument.size() != 1 || pManager == nullptr || pRegistry == nullptr )
                    return false;
                const TypeInfo* pType = pRegistry->findType( hashed_string( listArgument[0] ) );
                if ( pType == nullptr )
                {
                    outReply = "unknown type '" + listArgument[0] + "'";
                    return false;
                }
                vector<GameObject*> listObject;
                EditorSceneCommands::collectObjectsWithComponent( *pManager, pType, listObject );
                outReply = "selected " + to_string( EditorSceneCommands::selectObjects( listObject ) );
                return true;
            }

            static bool runSelectTag( const vector<string>& listArgument, string& outReply )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( listArgument.size() != 1 || pManager == nullptr )
                    return false;
                vector<GameObject*> listObject;
                EditorSceneCommands::collectObjectsWithTag( *pManager, TagID::request( listArgument[0] ), listObject );
                outReply = "selected " + to_string( EditorSceneCommands::selectObjects( listObject ) );
                return true;
            }

            /**
             * @brief 씬 뷰 카메라 앞 6 m 에 디버그 도형 넷(상자 · 화살표 · 구 · 글자)과 오버레이 값 하나를 둡니다 — 씬 뷰 시각화 · 게임 뷰 HUD 가 도는지 눈으로 보는 용도.
             */
            static bool runDebugDrawDemo( const vector<string>& listArgument, string& outReply )
            {
                uint32 seconds = 10;
                if ( listArgument.size() > 1 || ( listArgument.size() == 1 && parseCount( listArgument[0], seconds ) == false ) )
                    return false;
                const CameraComponent* pCamera = EditorCamera::ensure( editor::getActiveScene() );
                DebugDrawQueue*        pQueue  = editor::getService<DebugDrawQueue>();
                if ( pCamera == nullptr || pQueue == nullptr )
                    return false;
                const float4x4 world    = pCamera->getWorldMatrix();
                const float3   forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world ).normalize();
                const float3   right    = float3::transformVector( float3{ 1.0f, 0.0f, 0.0f }, world ).normalize();
                const float3   center   = pCamera->getWorldPosition() + forward * 6.0f;
                const float32  duration = static_cast<float32>( seconds );
                pQueue->drawBox( center - right * 1.5f, float3{ 0.5f, 0.5f, 0.5f }, float4{ 1.0f, 0.4f, 0.1f, 1.0f }, duration, "Demo" );
                pQueue->drawSphere( center + right * 1.5f, 0.6f, float4{ 0.2f, 0.8f, 1.0f, 1.0f }, duration, "Demo" );
                pQueue->drawArrow( center - right * 0.6f, center + right * 0.6f, float4{ 0.3f, 1.0f, 0.3f, 1.0f }, duration, "Demo" );
                pQueue->drawText( center + float3{ 0.0f, 1.0f, 0.0f }, "DebugDrawQueue demo", float4{ 1.0f, 1.0f, 0.3f, 1.0f }, duration, "Demo" );
                if ( DebugOverlayState* pOverlay = editor::getService<DebugOverlayState>() )
                    pOverlay->setFloat( hashed_string( "debugdraw.demo.seconds" ), duration );
                outReply = "debug draw demo for " + to_string( seconds ) + " s";
                return true;
            }

            static bool runLayoutSave( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*    pContext = EditorContext::get();
                EditorDockLayout* pDock    = pContext != nullptr ? pContext->findDockLayout() : nullptr;
                if ( listArgument.size() != 1 || pDock == nullptr || pDock->saveNamedLayout( listArgument[0] ) == false )
                    return false;
                outReply = "saved layout " + listArgument[0];
                return true;
            }

            static bool runLayoutLoad( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*    pContext = EditorContext::get();
                EditorDockLayout* pDock    = pContext != nullptr ? pContext->findDockLayout() : nullptr;
                if ( listArgument.size() != 1 || pDock == nullptr || pDock->requestLoadNamedLayout( listArgument[0] ) == false )
                    return false;
                outReply = "loading layout " + listArgument[0];
                return true;
            }

            /** @brief 패널을 열고 그 창을 앞으로 가져옵니다(같은 도크 영역의 탭이면 그 탭이 선택된다). 다음 프레임 시작에 적용됩니다. */
            static bool runPanelFocus( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*      pContext = EditorContext::get();
                const IEditorPanel* pPanel   = ( listArgument.size() == 1 && pContext != nullptr ) ? pContext->getPanelManager().findPanel( listArgument[0] ) : nullptr;
                if ( pPanel == nullptr )
                    return false;
                pContext->getWorkspace().requestOpenPanel( pPanel->getPanelTitle() );
                outReply = string{ "focusing " } + pPanel->getPanelTitle();
                return true;
            }

            static bool runPanelClose( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = EditorContext::get();
                if ( listArgument.size() != 1 || pContext == nullptr || pContext->getPanelManager().setPanelOpen( listArgument[0], false ) == false )
                    return false;
                outReply = "closed " + listArgument[0];
                return true;
            }

            static bool runLayoutReset( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*    pContext = EditorContext::get();
                EditorDockLayout* pDock    = pContext != nullptr ? pContext->findDockLayout() : nullptr;
                if ( listArgument.empty() == false || pDock == nullptr )
                    return false;
                pDock->requestResetDefault();
                outReply = "resetting to the default layout";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( EditorCommand, "editor", "editor <commandID>", "Run an editor command by id (the command palette ids, e.g. scene.saveScene)",
                    &EditorDevCommandsInternal::runEditorCommand );
    SW_DEV_COMMAND( Play, "play", "play", "Start (or switch to) play-in-editor", &EditorDevCommandsInternal::runPlay );
    SW_DEV_COMMAND( Simulate, "simulate", "simulate", "Start (or switch to) simulate - world only, no player or game input",
                    &EditorDevCommandsInternal::runSimulate );
    SW_DEV_COMMAND( Pause, "pause", "pause", "Pause the play session", &EditorDevCommandsInternal::runPause );
    SW_DEV_COMMAND( Stop, "stop", "stop", "Stop the play session and restore the edited scene", &EditorDevCommandsInternal::runStop );
    SW_DEV_COMMAND( SaveSceneAs, "scene.saveAs", "scene.saveAs <path>", "Save the active scene to a file (relative paths start at the working folder)",
                    &EditorDevCommandsInternal::runSaveSceneAs );
    SW_DEV_COMMAND( OpenScene, "scene.open", "scene.open <path>", "Open a scene file without the unsaved-changes prompt (relative paths start at the working folder)",
                    &EditorDevCommandsInternal::runOpenScene );
    SW_DEV_COMMAND( Theme, "editor.theme", "editor.theme <preset>", "Apply an editor theme preset for this session (ModernDark, DeepCharcoal, MidnightBlue, ClassicDark)",
                    &EditorDevCommandsInternal::runTheme );
    SW_DEV_COMMAND( Step, "step", "step [frames]", "Advance the play session by N frames, then pause", &EditorDevCommandsInternal::runStep );
    SW_DEV_COMMAND( SelectType, "select.type", "select.type <ComponentType>", "Select every object with that component type (or a derived one)",
                    &EditorDevCommandsInternal::runSelectType );
    SW_DEV_COMMAND( SelectTag, "select.tag", "select.tag <Tag>", "Select every object with that tag (or a child tag)", &EditorDevCommandsInternal::runSelectTag );
    SW_DEV_COMMAND( DebugDrawDemo, "debugdraw.demo", "debugdraw.demo [seconds]", "Draw a box, sphere, arrow and text in front of the scene view camera",
                    &EditorDevCommandsInternal::runDebugDrawDemo );
    SW_DEV_COMMAND( LayoutSave, "layout.save", "layout.save <name>", "Save the dock layout and panel visibility under a name",
                    &EditorDevCommandsInternal::runLayoutSave );
    SW_DEV_COMMAND( LayoutLoad, "layout.load", "layout.load <name>", "Load a named layout on the next frame", &EditorDevCommandsInternal::runLayoutLoad );
    SW_DEV_COMMAND( PanelFocus, "panel.focus", "panel.focus <panelID>", "Open a panel and bring its window (its tab) to the front on the next frame",
                    &EditorDevCommandsInternal::runPanelFocus );
    SW_DEV_COMMAND( PanelClose, "panel.close", "panel.close <panelID>", "Close a panel", &EditorDevCommandsInternal::runPanelClose );
    SW_DEV_COMMAND( LayoutReset, "layout.reset", "layout.reset", "Reset the dock layout to the default editor layout on the next frame (scenarios that click by position)",
                    &EditorDevCommandsInternal::runLayoutReset );
} // namespace sw::editor
