/**
 * @file AIDebugSections.cpp
 * @brief 런타임 NPC 디버거(HUD 섹션 `npc`)입니다 — 일정(왜 지금 이 일인가) · 판단 상태 · 행동 트리의 머문 잎 · 블랙보드를 한 흐름으로 읽는다.
 * @details 일정 창(`ScheduleDebugPanel`)과 행동 트리 창(`BehaviorTreePanel`)은 따로 있고 그대로 둔다 — 이 섹션은 한 개체를 고르면 둘의 근거를 잇는다.
 *          일정 NPC 는 `debug.target` 의 이름과 같은 id 의 NPC(없으면 첫 일정의 첫 NPC), AI 는 같은 대상의 `AIControllerComponent`(없으면 첫 것)다.
 *          스케줄 없는 AI 도 같은 섹션이 아래 절을 보인다(일정 절만 비어 있다). 읽기만 하고, 꺼져 있으면 불리지 않는다.
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTree.h"
#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"

namespace sw
{
    namespace
    {
        struct AIDebugSectionsInternal
        {
            /**
             * @brief 대상 오브젝트 이름에서 일정 NPC id 를 고릅니다 — 이름 그대로, 또는 마지막 `.` 뒤(게임이 NPC 오브젝트를 `Villager.<id>` 처럼 이름 짓는다).
             * @details shortcut: 일정 NPC 와 오브젝트를 잇는 칸이 없어 이름으로 맞춘다, NPC 오브젝트가 자기 NPC id 를 컴포넌트로 들게 되면 그 칸을 읽는다.
             */
            static string_view toNPCID( string_view objectName )
            {
                const size_t dot = objectName.rfind( '.' );
                return dot == string_view::npos ? objectName : objectName.substr( dot + 1 );
            }

            /** @brief 일정 절 — 대상 오브젝트의 NPC, 없으면 첫 일정의 첫 NPC 의 `explainNPC` 를 줄마다 적는다. 적은 줄 수를 돌려준다. */
            static uint32 writeSchedule( DebugHUDSectionWriter& writer, uint32 maxLines )
            {
                vector<const ScheduleSystem*> listSystem;
                ScheduleSystem::collectLiveSystems( listSystem );
                const ScheduleSystem* pFound     = nullptr;
                int32                 npcIndex   = -1;
                const string          targetName = GameplayDebugTarget::getTargetName();
                const hashed_string   wanted( toNPCID( targetName ) );
                for ( const ScheduleSystem* pSystem : listSystem )
                {
                    if ( pSystem == nullptr || pSystem->getNPCCount() == 0 )
                        continue;
                    const optional<int32> namedIndex = targetName.empty() ? optional<int32>{} : pSystem->findNPCIndex( wanted );
                    if ( namedIndex.has_value() )
                    {
                        pFound   = pSystem;
                        npcIndex = *namedIndex;
                        break;
                    }
                    if ( pFound == nullptr )
                    {
                        pFound   = pSystem;
                        npcIndex = 0;
                    }
                }
                if ( pFound == nullptr )
                {
                    writer.addLine( "schedule", "none (no live schedule)" );
                    return 1;
                }
                writer.addLine( "schedule npc", string( pFound->getNPCID( npcIndex ).c_str() ) );
                string explanation;
                pFound->explainNPC( npcIndex, explanation );
                uint32 lineCount = 1;
                StringUtil::forEachLine( explanation, [&]( string_view line )
                {
                    if ( line.empty() || lineCount >= maxLines )
                        return;
                    writer.addLine( "why", line );
                    ++lineCount;
                } );
                return lineCount;
            }

            /** @brief AI 절 — 판단 상태 · 이동 · 행동 트리의 머문 잎 · 블랙보드 칸(이름순). */
            static void writeController( DebugHUDSectionWriter& writer, uint32 maxLines )
            {
                const GameObjectManager* const     pManager    = GameplayDebugTarget::findActiveObjectManager();
                const AIControllerComponent* const pController = pManager != nullptr ? GameplayDebugTarget::findTargetComponent<AIControllerComponent>( *pManager ) : nullptr;
                if ( pController == nullptr )
                {
                    writer.addLine( "ai", "none (no AI controller in the active scene)" );
                    return;
                }
                writer.addLine( "ai target", pController->getOwner() != nullptr ? string( pController->getOwner()->getName().c_str() ) : string( "?" ) );
                writer.addLine( "state", pController->getStateName().size() > 0 ? string( pController->getStateName().c_str() ) : string( "-" ) );
                writer.addLine( "move", string( getEnumName( pController->getMoveStatus() ) ) );
                if ( const BehaviorTreeRunner* pRunner = pController->getBehaviorTreeRunner() )
                {
                    const utf8* pLeaf = pRunner->getActiveLeafName();
                    writer.addLine( "bt leaf", pLeaf != nullptr && pLeaf[0] != '\0' ? string( pLeaf ) : string( "-" ) );
                }
                else
                    writer.addLine( "bt leaf", "no behavior tree runner" );
                vector<hashed_string> listKey;
                pController->getBlackboard().collectKeys( listKey );
                uint32 lineCount = 0;
                for ( const hashed_string& key : listKey )
                {
                    if ( ++lineCount > maxLines )
                    {
                        writer.addLine( "...", to_string( static_cast<uint64>( listKey.size() ) ) + " keys" );
                        break;
                    }
                    const BlackboardValue* pValue = pController->getBlackboard().findValue( key );
                    writer.addLine( string( "bb " ) + key.c_str(), pValue != nullptr ? pValue->formatText() : string( "-" ) );
                }
            }

            static void writeNPC( DebugHUDSectionWriter& writer )
            {
                const uint32 scheduleLines = writeSchedule( writer, GameplayDebugTarget::kMaxLines / 2 );
                writeController( writer, GameplayDebugTarget::kMaxLines - scheduleLines );
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( npc, "npc", "NPC (schedule, AI, blackboard)", 230, DebugHUDSectionFlag::kNone, &AIDebugSectionsInternal::writeNPC );
} // namespace sw
