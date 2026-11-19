#include "pch.h"

#include "Games/Empty/KitDemo/MonsterHuntRPGDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntBestiary.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntContract.h"

namespace sw
{
    namespace
    {
        struct MonsterHuntRPGDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDMH" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr int32   kGreedyAsk     = 151;   ///< 한도(보상 100 x 1.5 = 150)를 1 넘는 요구 — 맞제안을 받는다
            static constexpr int32   kLimitAsk      = 150;   ///< 한도와 같은 요구 — 받아들여진다
            static constexpr int32   kKillCount     = 2;     ///< 지식 한 단계에 드는 처치 수(카탈로그 killsPerLevel)
            static constexpr float32 kFeatherOffset = 10.0f; ///< 단서 feather 의 x 위치(카탈로그와 같다)
            static constexpr float32 kEggsOffset    = 20.0f; ///< 단서 eggs 의 x 위치(카탈로그와 같다)
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoMonsterHuntState, "KitDemo.MonsterHuntRPG.State", "Monster hunt demo state (0 not started, 1 contract run finished)", MonsterHuntRPGDemoComponent,
                                &MonsterHuntRPGDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMonsterHuntSolved, "KitDemo.MonsterHuntRPG.Solved", "1 if the contract investigation was solved", MonsterHuntRPGDemoComponent,
                                &MonsterHuntRPGDemoComponent::getSolved );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMonsterHuntReward, "KitDemo.MonsterHuntRPG.Reward", "Reward settled by haggling (0 if not settled)", MonsterHuntRPGDemoComponent,
                                &MonsterHuntRPGDemoComponent::getReward );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMonsterHuntKnowledge, "KitDemo.MonsterHuntRPG.Knowledge", "Bestiary knowledge level of the hunted monster", MonsterHuntRPGDemoComponent,
                                &MonsterHuntRPGDemoComponent::getKnowledge );

    MonsterHuntRPGDemoComponent::MonsterHuntRPGDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _solved{ 0 }
        , _reward{ 0 }
        , _knowledge{ 0 }
    {
    }

    MonsterHuntRPGDemoComponent::~MonsterHuntRPGDemoComponent() = default;

    bool MonsterHuntRPGDemoComponent::runContract()
    {
        const hashed_string contractID = hashed_string( "griffin" );
        const hashed_string monsterID  = hashed_string( "ghoul" );

        // 조사: 첫 단계의 단서 둘을 순서대로, 다음 단계의 단서 하나를 찾으면 풀린다(단서 자리에 서서 찾는다). 퀘스트 일지는 쓰지 않는다.
        MonsterHuntInvestigation investigation;
        if ( investigation.initialize( _pCatalog.get(), contractID, nullptr ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.MonsterHuntRPGOpenFailed", "Monster hunt demo: the catalog has no contract griffin - the contract does not open" );
            return false;
        }
        (void)investigation.investigate( hashed_string( "blood" ), float3{ 0.0f, 0.0f, 0.0f } );
        (void)investigation.investigate( hashed_string( "feather" ), float3{ MonsterHuntRPGDemoComponentInternal::kFeatherOffset, 0.0f, 0.0f } );
        (void)investigation.investigate( hashed_string( "eggs" ), float3{ MonsterHuntRPGDemoComponentInternal::kEggsOffset, 0.0f, 0.0f } );
        _solved = investigation.isSolved() ? 1 : 0;

        // 흥정: 한도를 넘는 요구에 맞제안을 받고, 한도와 같은 요구로 끝낸다.
        MonsterHuntHaggle haggle;
        if ( haggle.initialize( _pCatalog.get(), contractID ) == false )
            return false;
        (void)haggle.propose( MonsterHuntRPGDemoComponentInternal::kGreedyAsk );
        (void)haggle.propose( MonsterHuntRPGDemoComponentInternal::kLimitAsk );
        _reward = haggle.getFinalReward();

        // 처치: 정한 수만큼 쓰러뜨리면 도감 지식이 한 단계 오른다.
        MonsterHuntBestiary bestiary;
        bestiary.initialize( _pCatalog.get(), nullptr );
        for ( int32 kill = 0; kill < MonsterHuntRPGDemoComponentInternal::kKillCount; ++kill )
        {
            (void)bestiary.recordKill( monsterID );
        }
        _knowledge = bestiary.getKnowledge( monsterID );
        markRunFinished();
        return true;
    }

    bool MonsterHuntRPGDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runContract();
    }

    void MonsterHuntRPGDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.MonsterHuntRPGDone", "[KitDemo] monster hunt: solved %#, reward %#, knowledge %#", _solved, _reward, _knowledge );
    }

    void MonsterHuntRPGDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, MonsterHuntRPGDemoComponentInternal::kStateTag, MonsterHuntRPGDemoComponentInternal::kStateVersion );
    }
} // namespace sw
