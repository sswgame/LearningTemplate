#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
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
                    outValue.setText( formatToString( "no world %#", worldIndex ) );
                    return false;
                }
                const GameObject* pObject = pWorld->getObjectManager().findGameObjectByName( hashed_string( string( query._key ) ) );
                if ( pObject == nullptr || pObject->getPrimarySceneComponent() == nullptr )
                {
                    outValue.setText( formatToString( "world %# has no object '%#'", worldIndex, string( query._key ) ) );
                    return false;
                }
                const float3 position = pObject->getPrimarySceneComponent()->getWorldPosition();
                outValue.setNumber( static_cast<float64>( bX ? position._x : position._z ) );
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
    SW_AUTOMATION_WATCH( editorNetSimViewportCount, "Editor.NetSimViewportCount", "Worlds of the one-process play session whose preview viewport the tab has drawn at least once",
                         &NetSimAutomationInternal::readViewportCount );
} // namespace sw::editor
