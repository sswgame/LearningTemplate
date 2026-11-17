#include "pch.h"

#include "Games/Empty/KitDemo/BattleRoyaleDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleMatch.h"

namespace sw
{
    namespace
    {
        struct BattleRoyaleDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDBR" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr uint32  kSeed          = 7;      ///< 같은 판이 나오게 시드를 고정한다
            static constexpr int32   kZoneSteps     = 10;     ///< 피해 전에 돌리는 고정 걸음 수(자기장 · 출혈 진행)
            static constexpr float32 kKnockDamage   = 150.0f; ///< 체력(카탈로그 100)을 넘겨 한 번에 기절시키는 피해
            static constexpr int32   kTeamASize     = 2;
            static constexpr int32   kNoWinningTeam = -1;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoBattleRoyaleState, "KitDemo.BattleRoyale.State", "Battle royale demo state (0 not started, 1 match run finished)", BattleRoyaleDemoComponent,
                                &BattleRoyaleDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoBattleRoyaleWinner, "KitDemo.BattleRoyale.WinningTeam", "Winning team index (-1 none)", BattleRoyaleDemoComponent,
                                &BattleRoyaleDemoComponent::getWinningTeam );
    SW_AUTOMATION_GETTER_WATCH( kitDemoBattleRoyaleKills, "KitDemo.BattleRoyale.WinnerKills", "Kills credited to the last player standing", BattleRoyaleDemoComponent,
                                &BattleRoyaleDemoComponent::getWinnerKills );
    SW_AUTOMATION_GETTER_WATCH( kitDemoBattleRoyaleRemaining, "KitDemo.BattleRoyale.Remaining", "Players not dead after the match", BattleRoyaleDemoComponent,
                                &BattleRoyaleDemoComponent::getRemainingPlayers );

    BattleRoyaleDemoComponent::BattleRoyaleDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _demoState{ 0 }
        , _winningTeam{ BattleRoyaleDemoComponentInternal::kNoWinningTeam }
        , _winnerKills{ 0 }
        , _remainingPlayers{ 0 }
    {
    }

    BattleRoyaleDemoComponent::~BattleRoyaleDemoComponent() = default;

    bool BattleRoyaleDemoComponent::runMatch()
    {
        // 보급 상자를 쓰지 않는 카탈로그라 아이템 · 전리품 카탈로그는 넘기지 않는다.
        BattleRoyaleMatch match;
        if ( match.initialize( _pCatalog.get(), nullptr, nullptr, BattleRoyaleDemoComponentInternal::kSeed, nullptr ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.BattleRoyaleOpenFailed", "Battle royale demo: the zone refused the catalog map size - the match does not open" );
            return false;
        }
        const int32 teamA = match.addTeam( hashed_string( "A" ) );
        const int32 teamB = match.addTeam( hashed_string( "B" ) );
        for ( int32 index = 0; index < BattleRoyaleDemoComponentInternal::kTeamASize; ++index )
        {
            if ( match.addPlayer( teamA ).has_value() == false )
                return false;
        }
        const expected<int32> enemy = match.addPlayer( teamB );
        if ( enemy.has_value() == false )
            return false;
        match.start();
        for ( int32 step = 0; step < BattleRoyaleDemoComponentInternal::kZoneSteps; ++step )
        {
            match.update( BattleRoyaleMatch::kFixedStep );
        }
        // 팀 B 가 팀 A 둘을 기절시킨다 — 서 있는 팀원이 없으면 팀 전멸로 판이 끝난다.
        for ( int32 index = 0; index < BattleRoyaleDemoComponentInternal::kTeamASize; ++index )
        {
            (void)match.applyDamage( index, enemy.value(), BattleRoyaleDemoComponentInternal::kKnockDamage, BattleRoyaleHitZone::Limb );
        }
        const BattleRoyalePlayer* pWinner = match.findPlayer( enemy.value() );
        _winningTeam                      = match.getMatchState().getWinningTeam();
        _winnerKills                      = pWinner != nullptr ? pWinner->_kills : 0;
        _remainingPlayers                 = match.countRemainingPlayers();
        _demoState                        = 1;
        return true;
    }

    bool BattleRoyaleDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runMatch();
    }

    void BattleRoyaleDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.BattleRoyaleDone", "[KitDemo] battle royale: team %# won with %# kills, %# players left", _winningTeam, _winnerKills, _remainingPlayers );
    }

    void BattleRoyaleDemoComponent::updateGame( float32 deltaTime )
    {
        // 한 바퀴는 판을 열 때 끝났다 — 틱마다 돌릴 규칙이 없다.
        (void)deltaTime;
    }

    void BattleRoyaleDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습이 없어 세울 오브젝트가 없다.
        (void)manager;
        (void)bRespawnViews;
    }

    void BattleRoyaleDemoComponent::serializeState( StateIO& io )
    {
        io.header( BattleRoyaleDemoComponentInternal::kStateTag, BattleRoyaleDemoComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        int32 demoState        = _demoState;
        int32 winningTeam      = _winningTeam;
        int32 winnerKills      = _winnerKills;
        int32 remainingPlayers = _remainingPlayers;
        io.value( demoState );
        io.value( winningTeam );
        io.value( winnerKills );
        io.value( remainingPlayers );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _demoState        = demoState;
        _winningTeam      = winningTeam;
        _winnerKills      = winnerKills;
        _remainingPlayers = remainingPlayers;
    }
} // namespace sw
