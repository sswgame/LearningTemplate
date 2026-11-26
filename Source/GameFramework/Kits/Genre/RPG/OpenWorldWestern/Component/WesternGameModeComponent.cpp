#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/FollowOwnerAIControllerComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternDeedComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternCatalog.h"
#include "GameFramework/Runtime/Character/Combat/TargetFilter.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/ReputationComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    namespace
    {
        struct WesternGameModeComponentInternal
        {
            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventWitnessed     = "WesternWitnessed";
            static constexpr const utf8* kEventReported      = "WesternReported";
            static constexpr const utf8* kEventWantedRaised  = "WesternWantedRaised";
            static constexpr const utf8* kEventWantedLowered = "WesternWantedLowered";
            static constexpr const utf8* kEventBountyPaid    = "WesternBountyPaid";
            static constexpr const utf8* kEventHonorTier     = "WesternHonorTier";

            /** @brief 법의 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다. */
            static const utf8* findSound( WesternLawEvent::Kind kind )
            {
                switch ( kind )
                {
                    case WesternLawEvent::Kind::Witnessed:
                    {
                        return kEventWitnessed;
                    }
                    case WesternLawEvent::Kind::Reported:
                    {
                        return kEventReported;
                    }
                    case WesternLawEvent::Kind::WantedRaised:
                    {
                        return kEventWantedRaised;
                    }
                    case WesternLawEvent::Kind::WantedLowered:
                    case WesternLawEvent::Kind::ReportPrevented:
                    {
                        return kEventWantedLowered;
                    }
                    case WesternLawEvent::Kind::BountyPaid:
                    {
                        return kEventBountyPaid;
                    }
                    case WesternLawEvent::Kind::Unwitnessed:    // 아무도 모르는 일이라 들리지 않는다
                    case WesternLawEvent::Kind::PursuitChanged: // 같은 순간의 수배 단계가 낸다
                    {
                        break;
                    }
                }
                return nullptr;
            }
        };
    } // namespace

    WesternGameModeComponent::WesternGameModeComponent()
        : _westernDataPath{}
        , _player{}
        , _whistleButton{}
        , _useButton{}
        , _weatherDataPath{}
        , _regionID{}
        , _regionBaseTemperature{ 20.0f }
        , _secondsPerDay{ 1440.0f }
        , _witnessSightRange{ 12.0f }
        , _startingMoney{ 0 }
        , _outlaw{}
        , _pWesternCatalog{}
        , _listLawEvent{}
        , _listHonorEvent{}
        , _listWitness{}
        , _bAutoMountRequested{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    WesternGameModeComponent::~WesternGameModeComponent() = default;

    void WesternGameModeComponent::serializeGameState( StateIO& io )
    {
        // 플레이어의 삶만 싣는다 — 지갑 · 평판 · 시계 · 날씨는 판 상태가, 말 상태는 말 컴포넌트가, 자리는 씬 오브젝트가 싣는다.
        io.header( kStateFormat );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            if ( io.check( _pWesternCatalog != nullptr && _pWesternCatalog->findRegion( _regionID.key() ) != nullptr ) == false )
                return;
            _outlaw.rebindCatalog( *_pWesternCatalog ); // 다시 읽힌 카탈로그면 새 판으로 읽는다
        }
        io.value( _outlaw );
        io.end();
    }

    bool WesternGameModeComponent::startGame()
    {
        if ( acquireRequired( _westernDataPath, _pWesternCatalog ) == false )
            return false;
        const PawnComponent* pPlayerPawn = findPlayerPawn();
        if ( pPlayerPawn == nullptr )
        {
            SW_LOG_ERROR_ID( "WesternGameMode.PlayerDoesNotPoint", "[Western] Player does not point at an object with a pawn - the game cannot open" );
            return false;
        }
        if ( pPlayerPawn->findButton( _whistleButton ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "WesternGameMode.RiderPawnNoWhistle", "[Western] the rider pawn has no whistle button '%#' - the game cannot open", _whistleButton.c_str() );
            return false;
        }
        if ( pPlayerPawn->findButton( _useButton ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "WesternGameMode.RiderPawnNoUse", "[Western] the rider pawn has no use button '%#' - the game cannot open", _useButton.c_str() );
            return false;
        }
        shared_ptr<const WeatherCatalog> pWeatherCatalog; // 판 상태가 연 때의 판을 쥔다 — 디렉터가 따라가지 않는다
        if ( acquireRequiredSnapshot( _weatherDataPath, pWeatherCatalog ) == false )
            return false;
        GameStateSettings settings{};
        settings._clock._secondsPerDay = _secondsPerDay;
        settings._pWeatherCatalog      = std::move( pWeatherCatalog );
        GameStateComponent* pState     = GameStateComponent::openOnOwner( *this, settings );
        if ( pState == nullptr )
            return false; // 까닭은 판 상태가 남겼다
        const GameObject* const pPlayerState = requirePlayerState();
        if ( pPlayerState == nullptr )
            return false; // 까닭은 베이스가 남겼다
        ReputationComponent* const pReputation = PlayerComponentUtil::openOn<ReputationComponent>( *pPlayerState, _pWesternCatalog->getHonorReputation() );
        WalletComponent* const     pWallet     = PlayerComponentUtil::requireOn<WalletComponent>( *pPlayerState );
        if ( pReputation == nullptr || pWallet == nullptr )
            return false;                                                                  // 까닭은 찾기 · 열기가 남겼다
        InventoryComponent* const pBag = pPlayerState->getComponent<InventoryComponent>(); // 가방은 선택이다 — 프레리 씬은 붙이지 않는다
        AIPerceptionSettings      sight{};
        sight._sightRange = _witnessSightRange;
        // 전리품 카탈로그는 없다 — 프레리 동물은 전리품 테이블을 적지 않는다(적으면 손질이 오류를 낸다).
        if ( _outlaw.initialize( _pWesternCatalog.get(), nullptr, pWallet->getWallet(), pReputation->getReputation(), pState->getClock(), pState->getFlags(), pState->getWeather(),
                                 pBag != nullptr ? &pBag->getInventory() : nullptr, _regionID.key(), _regionBaseTemperature, sight ) == false )
            return false; // 까닭은 무법자가 남겼다
        if ( pWallet->isFreshGame() && _startingMoney > 0 )
            pWallet->getWallet().add( _pWesternCatalog->getCurrency(), _startingMoney );
        return true;
    }

    void WesternGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "WesternGameMode.Opened", "[Western] the western is open - walk with WASD, E to mount or dismount, hold Shift to gallop, F to whistle, G to act" );
    }

    void WesternGameModeComponent::updateGame( GameSeconds frameTime )
    {
        _outlaw.update( frameTime );
        reportEvents();
        const PawnComponent* pPlayerPawn = findPlayerPawn();
        GameObjectManager*   pManager    = getObjectManager();
        if ( pPlayerPawn == nullptr || pManager == nullptr )
            return;
        // 다른 오브젝트(말 AI · 좌석)를 바꾸는 일은 틱 뒤에 한다.
        const bool bWhistle = pPlayerPawn->wasButtonTriggered( pPlayerPawn->findButton( _whistleButton ) );
        if ( bWhistle )
            pManager->executeOrDeferPostTickOnComponent( *this, []( WesternGameModeComponent& gameMode )
            { gameMode.whistleHorses(); } );
        const bool bUse = pPlayerPawn->wasButtonTriggered( pPlayerPawn->findButton( _useButton ) );
        if ( bUse )
            pManager->executeOrDeferPostTickOnComponent( *this, []( WesternGameModeComponent& gameMode )
            { gameMode.useDeed(); } );
        const bool bAutoMount = isAutoPlayOn() && _bAutoMountRequested == SW_FALSE;
        if ( bAutoMount )
        {
            _bAutoMountRequested = SW_TRUE;
            pManager->executeOrDeferPostTickOnComponent( *this, []( WesternGameModeComponent& gameMode )
            { gameMode.mountNearestHorse(); } );
        }
        pManager->executeOrDeferPostTickOnComponent( *this, []( WesternGameModeComponent& gameMode )
        { gameMode.updateLawSight(); } );
    }

    const PawnComponent* WesternGameModeComponent::findPlayerPawn() const
    {
        return findOn<PawnComponent>( _player );
    }

    void WesternGameModeComponent::whistleHorses()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        int32 calledCount = 0;
        for ( FollowOwnerAIControllerComponent* pAI : pManager->getComponentRegistry().getAll<FollowOwnerAIControllerComponent>() )
        {
            if ( pAI->getOwnerObject() != _player )
                continue;
            pAI->recall();
            ++calledCount;
        }
        SW_LOG_INFO_ID( "WesternGameMode.WhistleHorseComing", "[Western] whistle - %# horse(s) coming", calledCount );
    }

    void WesternGameModeComponent::mountNearestHorse()
    {
        PawnComponent* pPawn = findOn<PawnComponent>( _player );
        if ( pPawn == nullptr || VehicleSeatComponent::findSeatOf( *pPawn ) != nullptr )
            return;
        VehicleSeatComponent* pSeat = VehicleSeatComponent::findNearestFreeSeat( *pPawn );
        if ( pSeat == nullptr )
        {
            SW_LOG_WARNING_ID( "WesternGameMode.AutoPlayNoHorse", "[Western] auto play: no horse within reach of the rider" );
            return;
        }
        const MountResult result = VehicleSeatComponent::mount( *pPawn, *pSeat );
        if ( result != MountResult::Mounted )
            SW_LOG_WARNING_ID( "WesternGameMode.AutoPlayRiderCould", "[Western] auto play: the rider could not mount '%#' (result %#)", pSeat->getOwner()->getName().c_str(), static_cast<int32>( result ) );
    }

    void WesternGameModeComponent::useDeed()
    {
        GameObjectManager*    pManager = getObjectManager();
        const PawnComponent*  pPawn    = findPlayerPawn();
        const SceneComponent* pRoot    = pPawn != nullptr && pPawn->getOwner() != nullptr ? pPawn->getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pRoot == nullptr )
            return;
        const WesternDeedComponent* pUsed = WesternDeedComponent::useNearest( *pManager, pRoot->getWorldPosition(), _outlaw );
        if ( pUsed != nullptr && pUsed->getOwner() != nullptr )
            SW_LOG_INFO_ID( "WesternGameMode.DeedDone", "[Western] did the deed at '%#'", pUsed->getOwner()->getName().c_str() );
    }

    void WesternGameModeComponent::reportEvents()
    {
        _listLawEvent.clear();
        _outlaw.pollLawEvents( _listLawEvent );
        for ( const WesternLawEvent& event : _listLawEvent )
        {
            if ( const utf8* const pSound = WesternGameModeComponentInternal::findSound( event._kind ) )
                getSoundQueue().queueEvent( pSound );
        }
        ReputationComponent* const pReputation = findOnPlayerState<ReputationComponent>();
        if ( pReputation == nullptr )
            return; // 판을 열 때 있었다 — 플레이어 상태가 사라지는 중이다
        _listHonorEvent.clear();
        pReputation->getReputation().pollEvents( _listHonorEvent );
        for ( const ReputationEvent& event : _listHonorEvent )
        {
            getSoundQueue().queueEvent( WesternGameModeComponentInternal::kEventHonorTier );
            SW_LOG_INFO_ID( "WesternGameMode.HonorTier", "[Western] honor is '%#' now (was '%#', %#)", event._newTier.c_str(), event._oldTier.c_str(), event._value );
        }
    }

    void WesternGameModeComponent::updateLawSight()
    {
        GameObjectManager*    pManager = getObjectManager();
        const PawnComponent*  pPawn    = findPlayerPawn();
        const SceneComponent* pRoot    = pPawn != nullptr && pPawn->getOwner() != nullptr ? pPawn->getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pRoot == nullptr || _outlaw.getWantedLevel() <= 0 )
            return;
        if ( TargetFilter::isSelectable( *pPawn->getOwner() ) == false ) // 숨었거나 쓰러진 사람은 보이지 않는다(AI 시각 · 포탑과 같은 판정)
            _listWitness.clear();
        else
            WesternWitnessComponent::collect( *pManager, _listWitness ); // 수집이 먼저 비운다 — 멤버는 용량만 넘긴다(수배 중 프레임마다 힙 할당을 하지 않는다)
        _outlaw.updateSeenByLaw( pRoot->getWorldPosition(), _listWitness );
    }
} // namespace sw
