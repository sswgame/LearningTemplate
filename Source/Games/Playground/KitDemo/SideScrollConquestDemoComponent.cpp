#include "pch.h"

#include "Games/Playground/KitDemo/SideScrollConquestDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestWorld.h"

namespace sw
{
    namespace
    {
        struct SideScrollConquestDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDSC" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kUnitID     = "spear";
            static constexpr float32     kOutpostX   = 20.0f;  ///< 데이터의 전초기지 자리
            static constexpr float32     kKeepX      = 100.0f; ///< 데이터의 적 요새 자리
            static constexpr float32     kFrameDelta = 0.5f;   ///< 카탈로그의 고정 걸음과 같은 프레임 시간
            static constexpr int32       kMaxSteps   = 40;     ///< 이 걸음 안에 이기지 못하면 결과에 남는다(무한 반복 방지)
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoConquestState, "KitDemo.SideScrollConquest.State", "Side scroll conquest demo state (0 not started, 1 match run finished)",
                                SideScrollConquestDemoComponent, &SideScrollConquestDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoConquestVictory, "KitDemo.SideScrollConquest.Victory", "1 when the enemy holds no site", SideScrollConquestDemoComponent,
                                &SideScrollConquestDemoComponent::getVictory );
    SW_AUTOMATION_GETTER_WATCH( kitDemoConquestTerritory, "KitDemo.SideScrollConquest.Territory", "Sites the player owns", SideScrollConquestDemoComponent,
                                &SideScrollConquestDemoComponent::getTerritory );
    SW_AUTOMATION_GETTER_WATCH( kitDemoConquestSquad, "KitDemo.SideScrollConquest.Squad", "Soldiers in the commander's squad", SideScrollConquestDemoComponent,
                                &SideScrollConquestDemoComponent::getSquadSize );

    SideScrollConquestDemoComponent::SideScrollConquestDemoComponent()
        : _pCatalog{}
        , _victory{ 0 }
        , _territory{ 0 }
        , _squadSize{ 0 }
    {
    }

    SideScrollConquestDemoComponent::~SideScrollConquestDemoComponent() = default;

    bool SideScrollConquestDemoComponent::runMatch()
    {
        ConquestWorld world;
        world.initialize( _pCatalog.get() );
        const hashed_string   unitID{ SideScrollConquestDemoComponentInternal::kUnitID };
        const optional<int32> outpostUnit = world.spawnUnit( unitID, ConquestTeam::Player, SideScrollConquestDemoComponentInternal::kOutpostX, ConquestOrder::Hold );
        const optional<int32> keepUnit    = world.spawnUnit( unitID, ConquestTeam::Player, SideScrollConquestDemoComponentInternal::kKeepX, ConquestOrder::Hold );
        if ( outpostUnit.has_value() == false || keepUnit.has_value() == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.ConquestUnknownUnit", "Side scroll conquest demo: unit '%#' is not in the catalog - the match does not open", unitID.c_str() );
            return false;
        }
        for ( int32 step = 0; step < SideScrollConquestDemoComponentInternal::kMaxSteps && world.isVictory() == false; ++step )
        {
            world.update( SideScrollConquestDemoComponentInternal::kFrameDelta );
        }
        _victory   = world.isVictory() ? 1 : 0;
        _territory = world.computeTerritory();
        _squadSize = world.getSquadSize();
        markRunFinished();
        return true;
    }

    bool SideScrollConquestDemoComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false )
            return false;
        return runMatch();
    }

    void SideScrollConquestDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.SideScrollConquestDone", "[KitDemo] side scroll conquest: victory %#, %# sites held, squad of %#", _victory, _territory, _squadSize );
    }

    void SideScrollConquestDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, SideScrollConquestDemoComponentInternal::kStateTag, SideScrollConquestDemoComponentInternal::kStateVersion );
    }
} // namespace sw
