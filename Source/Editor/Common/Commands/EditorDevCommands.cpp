/**
 * @file EditorDevCommands.cpp
 * @brief 에디터가 내주는 개발 명령(에디터 커맨드 실행 · 플레이 세션 · 선택 · 레이아웃)입니다. 에디터 모듈과 함께 올라오고 내려갑니다.
 */
#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/EditorCamera.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Utility/Console/DevCommandRegistry.h"
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
             * @brief 그리는 카메라 앞 6 m 에 디버그 도형 넷(상자 · 화살표 · 구 · 글자)과 오버레이 값 하나를 둡니다 — 시각화 · HUD 가 도는지 눈으로 보는 용도.
             */
            static bool runDebugDrawDemo( const vector<string>& listArgument, string& outReply )
            {
                uint32 seconds = 10;
                if ( listArgument.size() > 1 || ( listArgument.size() == 1 && parseCount( listArgument[0], seconds ) == false ) )
                    return false;
                const CameraComponent* pCamera = EditorCamera::getViewportCamera( editor::getActiveScene(), EditorPlaySession::isPlayerActive() );
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
        };
    } // namespace

    SW_DEV_COMMAND( EditorCommand, "editor", "editor <commandId>", "Run an editor command by id (the command palette ids, e.g. scene.saveScene)",
                    &EditorDevCommandsInternal::runEditorCommand );
    SW_DEV_COMMAND( Play, "play", "play", "Start (or switch to) play-in-editor", &EditorDevCommandsInternal::runPlay );
    SW_DEV_COMMAND( Simulate, "simulate", "simulate", "Start (or switch to) simulate - world only, no player or game input",
                    &EditorDevCommandsInternal::runSimulate );
    SW_DEV_COMMAND( Pause, "pause", "pause", "Pause the play session", &EditorDevCommandsInternal::runPause );
    SW_DEV_COMMAND( Stop, "stop", "stop", "Stop the play session and restore the edited scene", &EditorDevCommandsInternal::runStop );
    SW_DEV_COMMAND( Step, "step", "step [frames]", "Advance the play session by N frames, then pause", &EditorDevCommandsInternal::runStep );
    SW_DEV_COMMAND( SelectType, "select.type", "select.type <ComponentType>", "Select every object with that component type (or a derived one)",
                    &EditorDevCommandsInternal::runSelectType );
    SW_DEV_COMMAND( SelectTag, "select.tag", "select.tag <Tag>", "Select every object with that tag (or a child tag)", &EditorDevCommandsInternal::runSelectTag );
    SW_DEV_COMMAND( DebugDrawDemo, "debugdraw.demo", "debugdraw.demo [seconds]", "Draw a box, sphere, arrow and text in front of the viewport camera",
                    &EditorDevCommandsInternal::runDebugDrawDemo );
    SW_DEV_COMMAND( LayoutSave, "layout.save", "layout.save <name>", "Save the dock layout and panel visibility under a name",
                    &EditorDevCommandsInternal::runLayoutSave );
    SW_DEV_COMMAND( LayoutLoad, "layout.load", "layout.load <name>", "Load a named layout on the next frame", &EditorDevCommandsInternal::runLayoutLoad );
} // namespace sw::editor
