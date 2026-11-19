#include "pch.h"

#include "Games/Empty/KitDemo/GhostHuntDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostEncounter.h"

namespace sw
{
    namespace
    {
        struct GhostHuntDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDGH" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr uint32  kSeed        = 3;    ///< 같은 싸움이 나오게 시드를 고정한다
            static constexpr float32 kGhostAhead  = 2.0f; ///< 유령을 내는 앞쪽 거리(손전등 · 청소기 거리 안)
            static constexpr float32 kAppearTime  = 1.0f; ///< 숨음 → 나타남, 나타남 → 공격에 각각 흘리는 시간(카탈로그 hideTime · appearTime)
            static constexpr float32 kSuctionTime = 1.0f; ///< 도망 반대로 당겨 서지 게이지를 채우는 흡입 시간
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoGhostHuntState, "KitDemo.GhostHunt.State", "Ghost hunt demo state (0 not started, 1 fight run finished)", GhostHuntDemoComponent,
                                &GhostHuntDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoGhostHuntStunned, "KitDemo.GhostHunt.Stunned", "Ghosts stunned by the flashlight beam", GhostHuntDemoComponent,
                                &GhostHuntDemoComponent::getStunnedCount );
    SW_AUTOMATION_GETTER_WATCH( kitDemoGhostHuntCoins, "KitDemo.GhostHunt.Coins", "Coins dropped by caught ghosts", GhostHuntDemoComponent, &GhostHuntDemoComponent::getCaughtCoins );
    SW_AUTOMATION_GETTER_WATCH( kitDemoGhostHuntRemaining, "KitDemo.GhostHunt.Remaining", "Ghosts not caught after the fight", GhostHuntDemoComponent,
                                &GhostHuntDemoComponent::getRemainingGhosts );

    GhostHuntDemoComponent::GhostHuntDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _stunnedCount{ 0 }
        , _caughtCoins{ 0 }
        , _remainingGhosts{ 0 }
    {
    }

    GhostHuntDemoComponent::~GhostHuntDemoComponent() = default;

    bool GhostHuntDemoComponent::runFight()
    {
        GhostEncounter encounter;
        if ( encounter.initialize( _pCatalog.get(), GhostHuntDemoComponentInternal::kSeed ) == false )
            return false;
        const float3 player{};
        const float3 forward{ 0.0f, 0.0f, 1.0f };
        const uint32 ghostID = encounter.spawnGhost( "goob", float3{ 0.0f, 0.0f, GhostHuntDemoComponentInternal::kGhostAhead } );
        if ( ghostID == 0 )
        {
            SW_LOG_ERROR_ID( "KitDemo.GhostHuntNoGhost", "Ghost hunt demo: the catalog has no ghost 'goob' - the fight does not open" );
            return false;
        }
        // 심장이 드러나는 공격 동작까지 기다려야 손전등이 기절시킨다.
        encounter.update( GhostHuntDemoComponentInternal::kAppearTime );
        encounter.update( GhostHuntDemoComponentInternal::kAppearTime );
        _stunnedCount = encounter.shineBeam( player, forward );
        if ( encounter.startSuction( ghostID, player ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.GhostHuntSuctionRefused", "Ghost hunt demo: suction did not start on the stunned ghost" );
            return false;
        }
        // 유령은 +Z 로 달아나므로 -Z 로 당겨 보너스 피해와 서지 게이지를 받고, 찬 게이지로 서지를 터뜨려 잡는다.
        (void)encounter.updateSuction( float3{ 0.0f, 0.0f, -1.0f }, GhostHuntDemoComponentInternal::kSuctionTime );
        (void)encounter.triggerSurge();
        vector<GhostEvent> listEvent;
        encounter.pollEvents( listEvent );
        for ( const GhostEvent& event : listEvent )
        {
            if ( event._type == GhostEventKind::Caught )
                _caughtCoins += event._coins;
        }
        _remainingGhosts = encounter.countRemaining();
        markRunFinished();
        return true;
    }

    bool GhostHuntDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runFight();
    }

    void GhostHuntDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.GhostHuntDone", "[KitDemo] ghost hunt: %# stunned, %# coins, %# ghosts left", _stunnedCount, _caughtCoins, _remainingGhosts );
    }

    void GhostHuntDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, GhostHuntDemoComponentInternal::kStateTag, GhostHuntDemoComponentInternal::kStateVersion );
    }
} // namespace sw
