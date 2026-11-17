#include "pch.h"

#include "Games/Empty/KitDemo/MechArenaDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"

namespace sw
{
    namespace
    {
        struct MechArenaDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDMA" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kMechID              = "plain"; ///< 카탈로그의 무기 없는 기체(코스트 300)
            static constexpr float32     kStep                = 0.05f;   ///< 고정 걸음(키트 테스트와 같다)
            static constexpr float32     kRespawnDelay        = 0.5f;    ///< 격추 뒤 다시 나오기까지(열 걸음)
            static constexpr float32     kRespawnInvulnerable = 0.0f;    ///< 다시 나오자마자 맞게 무적을 끈다
            static constexpr float32     kSpawnDistance       = 10.0f;   ///< B 를 +Z 로 떨어뜨려 세운다
            static constexpr float32     kKillDamage          = 1.0e6f;  ///< 체력을 넘겨 한 번에 격추하는 피해
            static constexpr int32       kStepLimit           = 40;      ///< 리스폰 · 판 끝을 기다리는 걸음 상한(넘으면 규칙이 틀렸다)
            static constexpr int32       kTeamA               = 0;
            static constexpr int32       kTeamB               = 1;
            static constexpr int32       kNoWinningTeam       = -1;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoMechArenaState, "KitDemo.MechArena.State", "Mech arena demo state (0 not started, 1 match run finished)", MechArenaDemoComponent,
                                &MechArenaDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMechArenaWinner, "KitDemo.MechArena.WinningTeam", "Winning team index (-1 none)", MechArenaDemoComponent,
                                &MechArenaDemoComponent::getWinningTeam );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMechArenaKills, "KitDemo.MechArena.WinnerKills", "Kills credited to team A's pilot", MechArenaDemoComponent,
                                &MechArenaDemoComponent::getWinnerKills );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMechArenaLoserGauge, "KitDemo.MechArena.LoserGauge", "Team B gauge after the match", MechArenaDemoComponent,
                                &MechArenaDemoComponent::getLoserGauge );

    MechArenaDemoComponent::MechArenaDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _demoState{ 0 }
        , _winningTeam{ MechArenaDemoComponentInternal::kNoWinningTeam }
        , _winnerKills{ 0 }
        , _loserGauge{ 0 }
    {
    }

    MechArenaDemoComponent::~MechArenaDemoComponent() = default;

    bool MechArenaDemoComponent::runMatch()
    {
        // 무기 없는 기체만 써서 무기 · 탄 · 기술 카탈로그는 넘기지 않는다(격추는 피해를 직접 준다).
        MechArenaSettings settings;
        settings._fixedStep           = MechArenaDemoComponentInternal::kStep;
        settings._respawnDelay        = MechArenaDemoComponentInternal::kRespawnDelay;
        settings._respawnInvulnerable = MechArenaDemoComponentInternal::kRespawnInvulnerable;
        MechArenaWorld world;
        if ( world.initialize( settings, _pCatalog.get(), nullptr, nullptr, nullptr ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.MechArenaOpenFailed", "Mech arena demo: the world refused the settings - the match does not open" );
            return false;
        }
        (void)world.addTeam( hashed_string( "A" ) );
        (void)world.addTeam( hashed_string( "B" ) );
        MechPilotConfig config;
        config._listMechID             = { hashed_string( MechArenaDemoComponentInternal::kMechID ) };
        config._team                   = MechArenaDemoComponentInternal::kTeamA;
        const expected<int32> attacker = world.addPilot( config );
        config._team                   = MechArenaDemoComponentInternal::kTeamB;
        config._spawnPosition          = float3{ 0.0f, 0.0f, MechArenaDemoComponentInternal::kSpawnDistance };
        const expected<int32> victim   = world.addPilot( config );
        if ( attacker.has_value() == false || victim.has_value() == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.MechArenaPilotFailed", "Mech arena demo: the catalog has no usable mech '%#' - the match does not open", MechArenaDemoComponentInternal::kMechID );
            return false;
        }
        world.start();

        // 격추 → 리스폰 대기 → 다시 격추. B 팀 게이지(코스트 x 2)가 두 번에 바닥나 판이 끝난다.
        world.applyDamage( attacker.value(), victim.value(), MechArenaDemoComponentInternal::kKillDamage, 0.0f, 0.0f );
        for ( int32 step = 0; step < MechArenaDemoComponentInternal::kStepLimit && world.findPilot( victim.value() )->_state != MechPilotState::Active; ++step )
        {
            world.update( MechArenaDemoComponentInternal::kStep );
        }
        world.applyDamage( attacker.value(), victim.value(), MechArenaDemoComponentInternal::kKillDamage, 0.0f, 0.0f );
        for ( int32 step = 0; step < MechArenaDemoComponentInternal::kStepLimit && world.isEnded() == false; ++step )
        {
            world.update( MechArenaDemoComponentInternal::kStep );
        }
        _winningTeam = world.getMatch().getWinningTeam();
        _winnerKills = world.findPilot( attacker.value() )->_kills;
        _loserGauge  = world.getTeamGauge( MechArenaDemoComponentInternal::kTeamB );
        _demoState   = 1;
        return true;
    }

    bool MechArenaDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runMatch();
    }

    void MechArenaDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.MechArenaDone", "[KitDemo] mech arena: team %# won with %# kills, loser gauge %#", _winningTeam, _winnerKills, _loserGauge );
    }

    void MechArenaDemoComponent::updateGame( float32 deltaTime )
    {
        // 한 바퀴는 판을 열 때 끝났다 — 틱마다 돌릴 규칙이 없다.
        (void)deltaTime;
    }

    void MechArenaDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습이 없어 세울 오브젝트가 없다.
        (void)manager;
        (void)bRespawnViews;
    }

    void MechArenaDemoComponent::serializeState( StateIO& io )
    {
        io.header( MechArenaDemoComponentInternal::kStateTag, MechArenaDemoComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        int32 demoState   = _demoState;
        int32 winningTeam = _winningTeam;
        int32 winnerKills = _winnerKills;
        int32 loserGauge  = _loserGauge;
        io.value( demoState );
        io.value( winningTeam );
        io.value( winnerKills );
        io.value( loserGauge );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _demoState   = demoState;
        _winningTeam = winningTeam;
        _winnerKills = winnerKills;
        _loserGauge  = loserGauge;
    }
} // namespace sw
