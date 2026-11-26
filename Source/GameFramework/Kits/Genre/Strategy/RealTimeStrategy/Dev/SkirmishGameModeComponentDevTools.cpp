/**
 * @file SkirmishGameModeComponentDevTools.cpp
 * @brief `SkirmishGameModeComponent` 의 개발 명령 · 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/Archive.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/SkirmishGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Skirmish.UnitCount` 입니다. 두 진영의 유닛 · 건물 수의 합입니다(AI 가 일꾼 · 군대를 늘리는지 본다). */
        [[nodiscard]] bool readUnitCount( const SkirmishGameModeComponent& gameMode, float64& outValue )
        {
            uint32 count = 0;
            gameMode.getWorld().forEachUnit( [&count]( const RTSUnit& unit )
            {
                (void)unit;
                ++count;
            } );
            outValue = static_cast<float64>( count );
            return true;
        }

        /** @brief 감시 값 `Skirmish.AIPlayerCount` 입니다. 컴퓨터가 맡은 플레이어 수입니다 — 자동 플레이로 열렸으면 지도의 플레이어 수다. */
        [[nodiscard]] bool readAIPlayerCount( const SkirmishGameModeComponent& gameMode, float64& outValue )
        {
            const SkirmishMatch& match = gameMode.getMatch();
            int32                count = 0;
            for ( int32 player = 0; player < match.getPlayerCount(); ++player )
            {
                count += match.findAI( player ) != nullptr ? 1 : 0;
            }
            outValue = static_cast<float64>( count );
            return true;
        }

        /** @brief 감시 값 `Skirmish.AITask` 입니다. 플레이어 `key`(0 · 1)를 맡은 컴퓨터 상대의 행동 트리가 도는 잎(`GatherIdle` · `TrainWorkers` · `Defend` …)입니다. */
        [[nodiscard]] bool readAITask( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const SkirmishGameModeComponent* pGameMode = query._pManager != nullptr ? SkirmishGameModeComponent::findInScene( *query._pManager ) : nullptr;
            int32                            player    = -1;
            if ( pGameMode == nullptr || StringUtil::parseInt( query._key, player ) == false )
            {
                fixed_string<constant::buffer::kMax256> text;
                formatstring( text.data(), text.capacity(), "key must be a player index, got '%#'", query._key );
                outValue.setText( pGameMode == nullptr ? string_view( "no skirmish game mode" ) : text.view() );
                return false;
            }
            const RTSCommanderAI* pAI = pGameMode->getMatch().findAI( player );
            if ( pAI == nullptr )
            {
                fixed_string<constant::buffer::kMax256> text;
                formatstring( text.data(), text.capacity(), "player %# is not played by the AI", query._key );
                outValue.setText( text.view() );
                return false;
            }
            const utf8* pTask = pAI->getActiveTaskName();
            outValue.setText( pTask != nullptr && pTask[0] != '\0' ? string_view{ pTask } : string_view{ "none" } );
            return true;
        }

        struct SkirmishGameModeComponentDevInternal
        {
            [[nodiscard]] static SkirmishGameModeComponent* findDirector( string& outReply )
            {
                const SceneManager* const  pScenes   = game::findSubsystem<SceneManager>();
                const Scene* const         pScene    = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const   pManager  = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                SkirmishGameModeComponent* pDirector = pManager != nullptr ? SkirmishGameModeComponent::findInScene( *pManager ) : nullptr;
                if ( pDirector == nullptr )
                    outReply = "skirmish: no skirmish director in the active scene";
                return pDirector;
            }

            static bool runOrder( const vector<string>& listArgument, string& outReply )
            {
                float32 x = 0.0f;
                float32 z = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[0], x ) == false || StringUtil::parseFloat( listArgument[1], z ) == false )
                    return false;
                SkirmishGameModeComponent* const pDirector = findDirector( outReply );
                if ( pDirector == nullptr )
                    return true;
                const int32 player = pDirector->getLocalPlayer();
                RTSCommand  command;
                command._kind        = RTSCommandKind::GroupMove;
                command._target      = float3{ x, 0.0f, z };
                command._bAttackMove = SW_TRUE;
                pDirector->getWorld().forEachUnit( [player, &command]( const RTSUnit& unit )
                {
                    const bool bArmy = unit._owner == player && unit.isMobile() && unit._pDef->_bWorker == SW_FALSE && unit._pDef->canAttack();
                    if ( bArmy && static_cast<int32>( command._listUnit.size() ) < RTSCommand::kMaxUnitCount )
                        command._listUnit.push_back( unit._id );
                } );
                const uint32                             unitCount = static_cast<uint32>( command._listUnit.size() );
                const int32                              applied   = pDirector->submitCommand( std::move( command ) );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "skirmish: player %# attack-moves %# units - %#", player, unitCount, applied < 0 ? "sent with the next lockstep input" : "applied" );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( skirmishUnitCount, "Skirmish.UnitCount", "Units and buildings of both players", SkirmishGameModeComponent, &readUnitCount );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishAIPlayerCount, "Skirmish.AIPlayerCount", "Players the AI commander plays - every player of the map when the match opened in autoplay", SkirmishGameModeComponent,
                                   &readAIPlayerCount );
    SW_DEV_COMMAND( SkirmishOrder, "skirmish.order", "skirmish.order <x> <z>", "Attack-move every army unit of this screen's player to the map point (through the lockstep input in a network match)",
                    &SkirmishGameModeComponentDevInternal::runOrder );
    SW_AUTOMATION_QUERY_WATCH( skirmishAITask, "Skirmish.AITask", "Active behavior tree task of the AI commander playing player key (0 or 1)", &readAITask, false, true );
} // namespace sw
