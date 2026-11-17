#include "pch.h"

#include "Games/Empty/KitDemo/SideScrollConquestDemoComponent.h"

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
        : _catalogPath{}
        , _pCatalog{}
        , _demoState{ 0 }
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
        const hashed_string unitID{ SideScrollConquestDemoComponentInternal::kUnitID };
        const int32         outpostUnit = world.spawnUnit( unitID, ConquestTeam::Player, SideScrollConquestDemoComponentInternal::kOutpostX, ConquestOrder::Hold );
        const int32         keepUnit    = world.spawnUnit( unitID, ConquestTeam::Player, SideScrollConquestDemoComponentInternal::kKeepX, ConquestOrder::Hold );
        if ( outpostUnit < 0 || keepUnit < 0 )
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
        _demoState = 1;
        return true;
    }

    bool SideScrollConquestDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runMatch();
    }

    void SideScrollConquestDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.SideScrollConquestDone", "[KitDemo] side scroll conquest: victory %#, %# sites held, squad of %#", _victory, _territory, _squadSize );
    }

    void SideScrollConquestDemoComponent::updateGame( float32 deltaTime )
    {
        // 한 바퀴는 판을 열 때 끝났다 — 틱마다 돌릴 규칙이 없다.
        (void)deltaTime;
    }

    void SideScrollConquestDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습이 없어 세울 오브젝트가 없다.
        (void)manager;
        (void)bRespawnViews;
    }

    void SideScrollConquestDemoComponent::serializeState( StateIO& io )
    {
        io.header( SideScrollConquestDemoComponentInternal::kStateTag, SideScrollConquestDemoComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        int32 demoState = _demoState;
        int32 victory   = _victory;
        int32 territory = _territory;
        int32 squadSize = _squadSize;
        io.value( demoState );
        io.value( victory );
        io.value( territory );
        io.value( squadSize );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _demoState = demoState;
        _victory   = victory;
        _territory = territory;
        _squadSize = squadSize;
    }
} // namespace sw
