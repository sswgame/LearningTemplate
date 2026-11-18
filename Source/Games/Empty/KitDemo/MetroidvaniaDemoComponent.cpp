#include "pch.h"

#include "Games/Empty/KitDemo/MetroidvaniaDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaDeathRestState.h"
#include "GameFramework/Runtime/Character/Combat/Health/HealthStateMachine.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

namespace sw
{
    namespace
    {
        struct MetroidvaniaDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDMV" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr uint32      kSeed       = 7;        ///< 같은 판이 나오게 시드를 고정한다(전리품 카탈로그가 없어 쓰이지 않는다)
            static constexpr const utf8* kRestSite   = "bench";  ///< 카탈로그의 쉬는 곳
            static constexpr const utf8* kCorpseArea = "cave";   ///< 죽는 방(쉬는 곳과 다른 방)
            static constexpr const utf8* kEnemy      = "husk";   ///< 보통 적
            static constexpr const utf8* kBoss       = "hornet"; ///< 보스
            static constexpr const utf8* kEnemySpawn = "spawnA";
            static constexpr const utf8* kBossSpawn  = "spawnB";
            static constexpr float32     kCorpseX    = 3.0f; ///< 죽은 자리이자 되찾는 자리(시체 반경 안)
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoMetroidvaniaState, "KitDemo.Metroidvania.State", "Metroidvania demo state (0 not started, 1 loop run finished)", MetroidvaniaDemoComponent,
                                &MetroidvaniaDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMetroidvaniaCorpse, "KitDemo.Metroidvania.CorpseCurrency", "Currency dropped on the corpse at death", MetroidvaniaDemoComponent,
                                &MetroidvaniaDemoComponent::getCorpseCurrency );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMetroidvaniaBalance, "KitDemo.Metroidvania.RecoveredBalance", "Wallet balance after recovering the corpse", MetroidvaniaDemoComponent,
                                &MetroidvaniaDemoComponent::getRecoveredBalance );
    SW_AUTOMATION_GETTER_WATCH( kitDemoMetroidvaniaRespawn, "KitDemo.Metroidvania.RespawnAtRest", "1 when death respawns at the rested site", MetroidvaniaDemoComponent,
                                &MetroidvaniaDemoComponent::getRespawnAtRest );

    MetroidvaniaDemoComponent::MetroidvaniaDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _corpseCurrency{ 0 }
        , _recoveredBalance{ 0 }
        , _respawnAtRest{ 0 }
    {
    }

    MetroidvaniaDemoComponent::~MetroidvaniaDemoComponent() = default;

    bool MetroidvaniaDemoComponent::runLoop()
    {
        const MetroidvaniaCatalog& catalog = *_pCatalog;
        Wallet                     wallet;
        HealthStateMachine         healthMachine;
        if ( healthMachine.initialize( catalog.getRules()._health ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.MetroidvaniaHealthFailed", "Metroidvania demo: the catalog health settings were refused - the loop does not run" );
            return false;
        }
        SessionStateRefs refs;
        refs._pWallet = &wallet;
        MetroidvaniaDeathRestState state;
        state.initialize( &catalog, refs );
        const hashed_string restSite{ MetroidvaniaDemoComponentInternal::kRestSite };
        if ( state.rest( restSite, healthMachine ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.MetroidvaniaRestFailed", "Metroidvania demo: the catalog has no rest site '%#' - the loop does not run", MetroidvaniaDemoComponentInternal::kRestSite );
            return false;
        }
        // 처치 통화는 지갑으로 들어간다. 전리품 카탈로그는 넘기지 않는다(전리품만 없다).
        GameFlags     flags;
        GameRandom    random{ MetroidvaniaDemoComponentInternal::kSeed };
        ItemStackList drops;
        (void)state.registerKill( hashed_string( MetroidvaniaDemoComponentInternal::kEnemySpawn ), hashed_string( MetroidvaniaDemoComponentInternal::kEnemy ), flags, nullptr, random, drops );
        (void)state.registerKill( hashed_string( MetroidvaniaDemoComponentInternal::kBossSpawn ), hashed_string( MetroidvaniaDemoComponentInternal::kBoss ), flags, nullptr, random, drops );

        // 다른 방에서 죽으면 가진 통화가 그 자리의 시체가 되고, 같은 방 · 같은 자리에서 되찾는다.
        const hashed_string corpseArea{ MetroidvaniaDemoComponentInternal::kCorpseArea };
        const float2        corpsePosition{ MetroidvaniaDemoComponentInternal::kCorpseX, 0.0f };
        healthMachine.kill();
        const hashed_string respawnSite = state.die( corpseArea, corpsePosition, healthMachine );
        _corpseCurrency                 = state.getCorpse()._currency;
        _respawnAtRest                  = respawnSite == restSite ? 1 : 0;
        (void)state.tryRecoverCorpse( corpseArea, corpsePosition );
        _recoveredBalance = static_cast<int32>( wallet.getBalance( catalog.getRules()._currency ) );
        markRunFinished();
        return true;
    }

    bool MetroidvaniaDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runLoop();
    }

    void MetroidvaniaDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.MetroidvaniaDone", "[KitDemo] metroidvania: corpse held %#, recovered balance %#, respawn at rest %#", _corpseCurrency, _recoveredBalance, _respawnAtRest );
    }

    void MetroidvaniaDemoComponent::serializeState( StateIO& io )
    {
        serializeResults( io, MetroidvaniaDemoComponentInternal::kStateTag, MetroidvaniaDemoComponentInternal::kStateVersion, { &_corpseCurrency, &_recoveredBalance, &_respawnAtRest } );
    }
} // namespace sw
