#include "pch.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/EditorNetSimSession.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

// 한 프로세스 재생의 자동화 감시 값(`Editor.NetSim*`) — 에디터 시나리오(`engine/automation/editor/netsim.scenario.xml`)가 세션(에디터 서브시스템)을 읽는다.
// 세션이 돌지 않으면 값을 내지 않는다(0 으로 지어내지 않는다) — 단, 돌고 있는지 묻는 `Editor.NetSimRunning` 은 0 · 1 이다.

namespace sw::editor
{
    namespace
    {
        struct NetSimAutomationInternal
        {
            /** @brief 돌고 있는 한 프로세스 재생 세션입니다(에디터 서브시스템이 없거나 돌지 않으면 nullptr). */
            static const EditorNetSimSession* findRunningSession()
            {
                const EditorNetSimSession* pSession = editor::findSubsystem<EditorNetSimSession>();
                return pSession != nullptr && pSession->isRunning() ? pSession : nullptr;
            }

            [[nodiscard]] static bool readRunning( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = findRunningSession() != nullptr ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readTick( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetSimSession* pSession = findRunningSession();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getHarness()->getTick() );
                return true;
            }

            /** @brief 서버 하나와 살아 있는 클라이언트의 합입니다. */
            [[nodiscard]] static bool readWorldCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetSimSession* pSession = findRunningSession();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( 1 + pSession->getHarness()->getClientCount() );
                return true;
            }

            [[nodiscard]] static bool readInputFocus( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetSimSession* pSession = findRunningSession();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getInputFocus() );
                return true;
            }

            [[nodiscard]] static bool readViewportCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetSimSession* pSession = findRunningSession();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getViewportCount() );
                return true;
            }

            /**
             * @brief `target` = 월드 번호(0 서버, N 클라이언트), `key` = 오브젝트 이름으로 그 월드 씬의 오브젝트 위치 한 축을 냅니다.
             * @details 세션이 돌지 않거나 · 월드나 오브젝트가 없거나 · 인자가 맞지 않으면 값을 내지 않고 이유를 글로 적는다(0 으로 지어내지 않는다).
             *          게임을 가리지 않는다 — 월드마다 같은 이름의 오브젝트가 같은 곳에 있는지 보는 데 쓴다(본보기 게임의 아바타 `Avatar1`).
             */
            [[nodiscard]] static bool readObjectAxis( const AutomationWatchQuery& query, AutomationWatchValue& outValue, bool bX )
            {
                int32 worldIndex = 0;
                if ( StringUtil::parseInt( query._target, worldIndex ) == false || query._key.empty() )
                {
                    outValue.setText( "target must be the world number and key the object name" );
                    return false;
                }
                const EditorNetSimSession* pSession = findRunningSession();
                NetSimHarness*             pHarness = pSession != nullptr ? pSession->getHarness() : nullptr;
                if ( pHarness == nullptr )
                {
                    outValue.setText( "no one-process play session is running" );
                    return false;
                }
                NetSimWorld* pWorld = worldIndex == 0 ? &pHarness->getServer() : pHarness->findClient( worldIndex );
                if ( pWorld == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "no world %#", worldIndex );
                    outValue.setText( textBuilder.view() );
                    return false;
                }
                const GameObject* pObject = pWorld->getObjectManager().findGameObjectByName( hashed_string( string( query._key ) ) );
                if ( pObject == nullptr || pObject->getPrimarySceneComponent() == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "world %# has no object '%#'", worldIndex, string( query._key ) );
                    outValue.setText( textBuilder.view() );
                    return false;
                }
                const float3 position = pObject->getPrimarySceneComponent()->getWorldPosition();
                outValue.setNumber( static_cast<float64>( bX ? position._x : position._z ) );
                return true;
            }

            /** @brief `target` = 월드 번호, `key` = 게임이 정한 값 이름으로 그 월드 세션의 값을 냅니다(`INetSimSession::readValue`). */
            [[nodiscard]] static bool readSessionValue( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                int32 worldIndex = 0;
                if ( StringUtil::parseInt( query._target, worldIndex ) == false || query._key.empty() )
                {
                    outValue.setText( "target must be the world number and key the value name" );
                    return false;
                }
                const EditorNetSimSession* pSession = findRunningSession();
                NetSimHarness*             pHarness = pSession != nullptr ? pSession->getHarness() : nullptr;
                NetSimWorld*               pWorld   = pHarness == nullptr ? nullptr : ( worldIndex == 0 ? &pHarness->getServer() : pHarness->findClient( worldIndex ) );
                const INetSimSession*      pGame    = pWorld != nullptr ? pWorld->getSession() : nullptr;
                float64                    value    = 0.0;
                if ( pGame == nullptr || pGame->readValue( query._key, value ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "world %# has no value '%#'", worldIndex, string( query._key ) );
                    outValue.setText( textBuilder.view() );
                    return false;
                }
                outValue.setNumber( value );
                return true;
            }

            /** @brief `netsim.start <clients> <game name>` — 게임 목록에서 이름으로 골라 시작합니다. 창의 콤보를 누르지 않고 시나리오가 게임을 고릅니다. 첫 클라이언트가 입력을 받습니다(창의 시작 단추와 같다). */
            static bool runStart( const vector<string>& listArgument, string& outReply )
            {
                int32 clientCount = 0;
                if ( listArgument.size() < 2 || StringUtil::parseInt( listArgument.front(), clientCount ) == false )
                    return false;
                EditorNetSimSettings settings;
                settings._clientCount = clientCount;
                settings._gameName    = listArgument[1];
                for ( size_t index = 2; index < listArgument.size(); ++index )
                {
                    settings._gameName += " ";
                    settings._gameName += listArgument[index];
                }
                string               error;
                EditorNetSimSession& session = editor::getSubsystem<EditorNetSimSession>();
                if ( session.start( settings, error ) == false )
                {
                    outReply = "netsim.start: " + error;
                    return true;
                }
                (void)session.setInputFocus( 1 );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "netsim.start: %# with %# client(s)", settings._gameName, clientCount );
                outReply = string( replyBuilder.view() );
                return true;
            }

            [[nodiscard]] static bool readObjectX( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readObjectAxis( query, outValue, true ); }
            [[nodiscard]] static bool readObjectZ( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readObjectAxis( query, outValue, false ); }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorNetSimRunning, "Editor.NetSimRunning", "1 while the In process tab runs a one-process play session", &NetSimAutomationInternal::readRunning );
    SW_AUTOMATION_WATCH( editorNetSimTick, "Editor.NetSimTick", "Fixed steps the one-process harness has run (no value while no session runs)", &NetSimAutomationInternal::readTick );
    SW_AUTOMATION_WATCH( editorNetSimWorldCount, "Editor.NetSimWorldCount", "Worlds of the one-process play session: the server plus the live clients", &NetSimAutomationInternal::readWorldCount );
    SW_AUTOMATION_WATCH( editorNetSimInputFocus, "Editor.NetSimInputFocus", "World that takes the human input in the one-process play session (0 server, N client, -1 none)",
                         &NetSimAutomationInternal::readInputFocus );
    SW_AUTOMATION_QUERY_WATCH( editorNetSimObjectX, "Editor.NetSimObjectX", "X of an object of one world of the one-process play session (target = world number, key = object name)",
                               &NetSimAutomationInternal::readObjectX, true, true );
    SW_AUTOMATION_QUERY_WATCH( editorNetSimObjectZ, "Editor.NetSimObjectZ", "Z of an object of one world of the one-process play session (target = world number, key = object name)",
                               &NetSimAutomationInternal::readObjectZ, true, true );
    SW_AUTOMATION_QUERY_WATCH( editorNetSimValue, "Editor.NetSimValue", "Value a game exposes for one world of the one-process play session (target = world number, key = value name)",
                               &NetSimAutomationInternal::readSessionValue, true, true );
    SW_DEV_COMMAND( NetSimStart, "netsim.start", "netsim.start <clients> <game name>", "Start the one-process play session of a registered game (the In process tab must stay visible for it to run)", &NetSimAutomationInternal::runStart );
    SW_AUTOMATION_WATCH( editorNetSimViewportCount, "Editor.NetSimViewportCount", "Worlds of the one-process play session whose preview viewport the tab has drawn at least once",
                         &NetSimAutomationInternal::readViewportCount );
} // namespace sw::editor
