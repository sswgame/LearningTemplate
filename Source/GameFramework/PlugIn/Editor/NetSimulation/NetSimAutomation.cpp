#include "pch.h"

#include "Core/Common/StdHeaders.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"
#include "GameFramework/PlugIn/Editor/NetSimulation/EditorNetSimSession.h"

// 한 프로세스 재생의 자동화 탐침(`Editor.NetSim*`) — 에디터 시나리오(`engine/automation/editor/netsim.scenario.xml`)가 탭이 가진 세션을 읽는다.
// 세션이 돌지 않으면 값을 내지 않는다(0 으로 지어내지 않는다) — 단, 돌고 있는지 묻는 `Editor.NetSimRunning` 은 0 · 1 이다.

namespace sw::editor
{
    namespace
    {
        struct NetSimAutomationInternal
        {
            [[nodiscard]] static bool readRunning( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = EditorNetSimSession::findRunning() != nullptr ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readTick( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetSimSession* pSession = EditorNetSimSession::findRunning();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getHarness()->getTick() );
                return true;
            }

            /** @brief 서버 하나와 살아 있는 클라이언트의 합입니다. */
            [[nodiscard]] static bool readWorldCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetSimSession* pSession = EditorNetSimSession::findRunning();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( 1 + pSession->getHarness()->getClientCount() );
                return true;
            }

            [[nodiscard]] static bool readInputFocus( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetSimSession* pSession = EditorNetSimSession::findRunning();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getInputFocus() );
                return true;
            }

            [[nodiscard]] static bool readViewportCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetSimSession* pSession = EditorNetSimSession::findRunning();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getViewportCount() );
                return true;
            }

            /** @brief 글 @p text 를 정수로 읽습니다. 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool parseInt( string_view text, int32& outValue )
            {
                const auto [pEnd, error] = std::from_chars( text.data(), text.data() + text.size(), outValue );
                return text.empty() == false && error == std::errc{} && pEnd == text.data() + text.size();
            }

            /**
             * @brief `target` = 월드 번호(0 서버, N 클라이언트), `key` = 오브젝트 이름으로 그 월드 씬의 오브젝트 위치 한 축을 냅니다.
             * @details 세션이 돌지 않거나 · 월드나 오브젝트가 없거나 · 인자가 맞지 않으면 값을 내지 않고 이유를 글로 적는다(0 으로 지어내지 않는다).
             *          게임을 가리지 않는다 — 월드마다 같은 이름의 오브젝트가 같은 곳에 있는지 보는 데 쓴다(본보기 게임의 아바타 `Avatar1`).
             */
            [[nodiscard]] static bool readObjectAxis( const AutomationProbeQuery& query, AutomationProbeValue& outValue, bool bX )
            {
                int32 worldIndex = 0;
                if ( parseInt( query._target, worldIndex ) == false || query._key.empty() )
                {
                    outValue.setText( "target must be the world number and key the object name" );
                    return false;
                }
                const EditorNetSimSession* pSession = EditorNetSimSession::findRunning();
                NetSimHarness*             pHarness = pSession != nullptr ? pSession->getHarness() : nullptr;
                if ( pHarness == nullptr )
                {
                    outValue.setText( "no one-process play session is running" );
                    return false;
                }
                NetSimWorld* pWorld = worldIndex == 0 ? &pHarness->getServer() : pHarness->findClient( worldIndex );
                if ( pWorld == nullptr )
                {
                    outValue.setText( "no world " + to_string( worldIndex ) );
                    return false;
                }
                const GameObject* pObject = pWorld->getObjectManager().findGameObjectByName( hashed_string( string( query._key ) ) );
                if ( pObject == nullptr || pObject->getPrimarySceneComponent() == nullptr )
                {
                    outValue.setText( "world " + to_string( worldIndex ) + " has no object '" + string( query._key ) + "'" );
                    return false;
                }
                const float3 position = pObject->getPrimarySceneComponent()->getWorldPosition();
                outValue.setNumber( static_cast<float64>( bX ? position._x : position._z ) );
                return true;
            }

            [[nodiscard]] static bool readObjectX( const AutomationProbeQuery& query, AutomationProbeValue& outValue ) { return readObjectAxis( query, outValue, true ); }
            [[nodiscard]] static bool readObjectZ( const AutomationProbeQuery& query, AutomationProbeValue& outValue ) { return readObjectAxis( query, outValue, false ); }
        };
    } // namespace

    SW_AUTOMATION_PROBE( editorNetSimRunning, "Editor.NetSimRunning", "1 while the In process tab runs a one-process play session", &NetSimAutomationInternal::readRunning );
    SW_AUTOMATION_PROBE( editorNetSimTick, "Editor.NetSimTick", "Fixed steps the one-process harness has run (no value while no session runs)", &NetSimAutomationInternal::readTick );
    SW_AUTOMATION_PROBE( editorNetSimWorldCount, "Editor.NetSimWorldCount", "Worlds of the one-process play session: the server plus the live clients", &NetSimAutomationInternal::readWorldCount );
    SW_AUTOMATION_PROBE( editorNetSimInputFocus, "Editor.NetSimInputFocus", "World that takes the human input in the one-process play session (0 server, N client, -1 none)",
                         &NetSimAutomationInternal::readInputFocus );
    SW_AUTOMATION_QUERY_PROBE( editorNetSimObjectX, "Editor.NetSimObjectX", "X of an object of one world of the one-process play session (target = world number, key = object name)",
                               &NetSimAutomationInternal::readObjectX, true, true );
    SW_AUTOMATION_QUERY_PROBE( editorNetSimObjectZ, "Editor.NetSimObjectZ", "Z of an object of one world of the one-process play session (target = world number, key = object name)",
                               &NetSimAutomationInternal::readObjectZ, true, true );
    SW_AUTOMATION_PROBE( editorNetSimViewportCount, "Editor.NetSimViewportCount", "Worlds of the one-process play session whose preview viewport the tab has drawn at least once",
                         &NetSimAutomationInternal::readViewportCount );
} // namespace sw::editor
