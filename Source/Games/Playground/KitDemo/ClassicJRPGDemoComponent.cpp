#include "pch.h"

#include "Games/Playground/KitDemo/ClassicJRPGDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGBattle.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGParty.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    namespace
    {
        struct ClassicJRPGDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDJR" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr uint32 kSeed      = 7; ///< 같은 전투가 나오게 시드를 고정한다
            static constexpr int32  kHeroIndex = 0; ///< 파티의 첫 멤버(용사)
            static constexpr int32  kTarget    = 0; ///< 첫 적
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoClassicJRPGState, "KitDemo.ClassicJRPG.State", "Classic JRPG demo state (0 not started, 1 battle run finished)", ClassicJRPGDemoComponent,
                                &ClassicJRPGDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoClassicJRPGResult, "KitDemo.ClassicJRPG.Result", "Battle result (0 ongoing, 1 victory, 2 defeat, 3 fled)", ClassicJRPGDemoComponent,
                                &ClassicJRPGDemoComponent::getBattleResult );
    SW_AUTOMATION_GETTER_WATCH( kitDemoClassicJRPGExp, "KitDemo.ClassicJRPG.HeroExp", "Experience held by the hero after the rewards", ClassicJRPGDemoComponent,
                                &ClassicJRPGDemoComponent::getHeroExp );
    SW_AUTOMATION_GETTER_WATCH( kitDemoClassicJRPGGold, "KitDemo.ClassicJRPG.Gold", "Default currency in the party wallet after the rewards", ClassicJRPGDemoComponent,
                                &ClassicJRPGDemoComponent::getGold );

    ClassicJRPGDemoComponent::ClassicJRPGDemoComponent()
        : _pCatalog{}
        , _battleResult{ 0 }
        , _heroExp{ 0 }
        , _gold{ 0 }
    {
    }

    ClassicJRPGDemoComponent::~ClassicJRPGDemoComponent() = default;

    bool ClassicJRPGDemoComponent::runBattle()
    {
        // 장비 · 전직을 쓰지 않는 시연이라 아이템 카탈로그 · 가방은 넘기지 않고, 골드 보상을 받을 지갑만 빌려 준다.
        Wallet    wallet;
        JRPGParty party;
        party.initialize( _pCatalog.get(), nullptr, nullptr, &wallet );
        if ( party.addMember( "hero", "Hero", "hero" ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.ClassicJRPGNoHero", "Classic JRPG demo: the catalog has no class 'hero' - the battle does not open" );
            return false;
        }
        // 타이밍 판정이 없으면 타이밍 공격 · 방어는 항상 실패한다 — 추가 타격 없이 공식대로 한 번 친다.
        JRPGBattle battle;
        battle.initialize( _pCatalog.get(), nullptr, JRPGBattleSettings{}, ClassicJRPGDemoComponentInternal::kSeed );
        vector<hashed_string> listEnemy;
        listEnemy.push_back( "frail" );
        if ( battle.start( &party, listEnemy ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.ClassicJRPGStartFailed", "Classic JRPG demo: the battle refused enemy 'frail'" );
            return false;
        }
        if ( battle.setCommand( ClassicJRPGDemoComponentInternal::kHeroIndex, JRPGCommand::makeAttack( ClassicJRPGDemoComponentInternal::kTarget ) ) == false )
            return false;
        battle.resolveRound();
        _battleResult = static_cast<int32>( battle.getResult() );
        _heroExp      = static_cast<int32>( party.getMember( ClassicJRPGDemoComponentInternal::kHeroIndex )._level.getXP() );
        _gold         = static_cast<int32>( wallet.getBalance( Wallet::getDefaultCurrency() ) );
        markRunFinished();
        return true;
    }

    bool ClassicJRPGDemoComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false )
            return false;
        return runBattle();
    }

    void ClassicJRPGDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.ClassicJRPGDone", "[KitDemo] classic JRPG: result %#, hero exp %#, gold %#", _battleResult, _heroExp, _gold );
    }

    void ClassicJRPGDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, ClassicJRPGDemoComponentInternal::kStateTag, ClassicJRPGDemoComponentInternal::kStateVersion );
    }
} // namespace sw
