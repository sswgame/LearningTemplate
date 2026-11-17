#include "pch.h"

#include "Games/Empty/KitDemo/CoopScavengerDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Rule/ScavengerExpedition.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

namespace sw
{
    namespace
    {
        struct CoopScavengerDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDCS" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr uint32      kSeed          = 11;        ///< 같은 시설 · 고철이 나오게 시드를 고정한다
            static constexpr int32       kCrewCount     = 2;         ///< 승무원 수
            static constexpr int32       kCarrier       = 0;         ///< 고철을 나르는 사람
            static constexpr const utf8* kCompanyMoonID = "company"; ///< 카탈로그의 회사 위성 id
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoCoopScavengerState, "KitDemo.CoopScavenger.State", "Coop scavenger demo state (0 not started, 1 expedition run finished)", CoopScavengerDemoComponent,
                                &CoopScavengerDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoCoopScavengerSold, "KitDemo.CoopScavenger.Sold", "Credits received for the ship scrap at the company", CoopScavengerDemoComponent,
                                &CoopScavengerDemoComponent::getSoldValue );
    SW_AUTOMATION_GETTER_WATCH( kitDemoCoopScavengerCredits, "KitDemo.CoopScavenger.Credits", "Credits after the quota deadline", CoopScavengerDemoComponent, &CoopScavengerDemoComponent::getCredits );
    SW_AUTOMATION_GETTER_WATCH( kitDemoCoopScavengerQuota, "KitDemo.CoopScavenger.NextQuota", "Quota of the next cycle", CoopScavengerDemoComponent, &CoopScavengerDemoComponent::getNextQuota );

    CoopScavengerDemoComponent::CoopScavengerDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _demoState{ 0 }
        , _soldValue{ 0 }
        , _credits{ 0 }
        , _nextQuota{ 0 }
    {
    }

    CoopScavengerDemoComponent::~CoopScavengerDemoComponent() = default;

    bool CoopScavengerDemoComponent::walkTo( ScavengerExpedition& inoutExpedition, int32 player, const hashed_string& target )
    {
        const ScavengerCrewMember* pMember = inoutExpedition.findCrewMember( player );
        if ( pMember == nullptr )
            return false;
        const ScavengerFacility& facility = inoutExpedition.getFacility();
        vector<hashed_string>    listArea;
        if ( facility.getGraph().findPath( pMember->_areaID, target, facility.getFlags(), listArea ) == false )
            return false;
        // 길의 첫 방은 지금 있는 곳이다.
        for ( size_t index = 1; index < listArea.size(); ++index )
        {
            if ( inoutExpedition.movePlayer( player, listArea[index] ) != ScavengerActionResult::Ok )
                return false;
        }
        return true;
    }

    bool CoopScavengerDemoComponent::runExpedition()
    {
        // 지갑 · 플래그 · 우주선 창고는 원정이 빌려 쓴다 — 한 바퀴 동안만 산다. 시계 · 날씨 · 위협 표 · 상점은 쓰지 않는다.
        Wallet                  wallet;
        GameFlags               flags;
        Inventory               storage;
        ScavengerExpeditionData data;
        data._pCatalog = _pCatalog.get();
        SessionStateRefs refs;
        refs._pWallet = &wallet;
        refs._pFlags  = &flags;
        ScavengerExpedition expedition;
        if ( expedition.initialize( data, refs, storage, CoopScavengerDemoComponentInternal::kSeed, CoopScavengerDemoComponentInternal::kCrewCount ) == false )
            return false;
        if ( expedition.land() != ScavengerActionResult::Ok )
            return false;

        // 바닥 고철을 하나씩 집어 우주선에 싣는다.
        const hashed_string ship( ScavengerExpedition::kShipAreaID );
        const int32         player = CoopScavengerDemoComponentInternal::kCarrier;
        while ( expedition.getFacility().getGroundScrap().empty() == false )
        {
            const ScavengerScrap scrap = expedition.getFacility().getGroundScrap().front();
            if ( walkTo( expedition, player, scrap._areaID ) == false || expedition.pickUp( player, scrap._uid ) != ScavengerPickupResult::Ok )
                return false;
            if ( walkTo( expedition, player, ship ) == false || expedition.depositToShip( player ) != 1 )
                return false;
        }

        // 떠나 회사에 내려 팔고, 마감 날의 이륙에서 할당량을 판정한다.
        if ( expedition.takeOff() != ScavengerActionResult::Ok )
            return false;
        if ( expedition.routeTo( hashed_string( CoopScavengerDemoComponentInternal::kCompanyMoonID ) ) != ScavengerActionResult::Ok || expedition.land() != ScavengerActionResult::Ok )
            return false;
        ScavengerActionResult sellResult = ScavengerActionResult::Ok;
        _soldValue                       = expedition.sellAllShipScrap( sellResult );
        if ( sellResult != ScavengerActionResult::Ok || expedition.takeOff() != ScavengerActionResult::Ok )
            return false;
        _credits   = static_cast<int32>( expedition.getCredits() );
        _nextQuota = expedition.getQuota().getQuota();
        _demoState = 1;
        return true;
    }

    bool CoopScavengerDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        if ( runExpedition() )
            return true;
        SW_LOG_ERROR_ID( "KitDemo.CoopScavengerRunFailed", "Coop scavenger demo: the expedition did not run to the quota deadline - check the catalog moons and facility" );
        return false;
    }

    void CoopScavengerDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.CoopScavengerDone", "[KitDemo] coop scavenger: sold %#, credits %#, next quota %#", _soldValue, _credits, _nextQuota );
    }

    void CoopScavengerDemoComponent::updateGame( float32 deltaTime )
    {
        // 한 바퀴는 판을 열 때 끝났다 — 틱마다 돌릴 규칙이 없다.
        (void)deltaTime;
    }

    void CoopScavengerDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습이 없어 세울 오브젝트가 없다.
        (void)manager;
        (void)bRespawnViews;
    }

    void CoopScavengerDemoComponent::serializeState( StateIO& io )
    {
        io.header( CoopScavengerDemoComponentInternal::kStateTag, CoopScavengerDemoComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        int32 demoState = _demoState;
        int32 soldValue = _soldValue;
        int32 credits   = _credits;
        int32 nextQuota = _nextQuota;
        io.value( demoState );
        io.value( soldValue );
        io.value( credits );
        io.value( nextQuota );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _demoState = demoState;
        _soldValue = soldValue;
        _credits   = credits;
        _nextQuota = nextQuota;
    }
} // namespace sw
