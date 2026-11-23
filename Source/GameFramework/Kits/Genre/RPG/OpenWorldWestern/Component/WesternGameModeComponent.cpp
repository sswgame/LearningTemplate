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
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/ReputationComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
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
        , _listWitness{}
        , _bAutoMountRequested{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    WesternGameModeComponent::~WesternGameModeComponent() = default;

    void WesternGameModeComponent::serializeGameState( StateIO& io )
    {
        // 플레이어의 삶만 싣는다 — 지갑 · 평판 · 시계 · 날씨는 판 상태가, 말 상태는 말 컴포넌트가, 자리는 씬 오브젝트가 싣는다.
        io.header( kStateTag, kStateVersion );
        if ( io.isOk() == false )
            return;
        if ( io.isWriting() )
        {
            io.value( _outlaw );
            io.end();
            return;
        }
        // 사본에 읽는다 — 다시 읽힌 카탈로그면 새 판으로 다시 묶고, 거절하면 앞 판에 묶인 무법자가 그대로다.
        if ( io.check( _pWesternCatalog != nullptr && _pWesternCatalog->findRegion( _regionID.key() ) != nullptr ) == false )
            return;
        WesternOutlaw restoredOutlaw = _outlaw;
        restoredOutlaw.rebindCatalog( *_pWesternCatalog );
        io.value( restoredOutlaw );
        io.end();
        if ( io.isOk() )
            _outlaw = std::move( restoredOutlaw );
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
        const ReputationCatalog* const pHonor      = &_pWesternCatalog->getHonorReputation();
        ReputationComponent* const     pReputation = PlayerComponentUtil::openOn<ReputationComponent>( *pPlayerState, pHonor );
        WalletComponent* const         pWallet     = PlayerComponentUtil::requireOn<WalletComponent>( *pPlayerState );
        if ( pReputation == nullptr || pWallet == nullptr )
            return false;                                                                  // 까닭은 찾기 · 열기가 남겼다
        InventoryComponent* const pBag = pPlayerState->getComponent<InventoryComponent>(); // 가방은 선택이다 — 프레리 씬은 붙이지 않는다
        AIPerceptionSettings      sight{};
        sight._sightRange = _witnessSightRange;
        // 전리품 카탈로그는 없다 — 프레리 동물은 전리품 테이블을 적지 않는다(적으면 손질이 오류를 낸다).
        if ( _outlaw.initialize( _pWesternCatalog.get(), nullptr, pWallet->getWallet(), pReputation->getReputation(), pState->getClock(), &pState->getFlags(), pState->findWeather(),
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

    void WesternGameModeComponent::updateGame( float32 deltaTime )
    {
        _outlaw.update( deltaTime );
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

    void WesternGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 세우는 런타임 오브젝트가 없다 — 말과 기수는 씬에 있다.
        (void)manager;
        (void)bRespawnViews;
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

    void WesternGameModeComponent::updateLawSight()
    {
        GameObjectManager*    pManager = getObjectManager();
        const PawnComponent*  pPawn    = findPlayerPawn();
        const SceneComponent* pRoot    = pPawn != nullptr && pPawn->getOwner() != nullptr ? pPawn->getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pRoot == nullptr || _outlaw.getWantedLevel() <= 0 )
            return;
        WesternWitnessComponent::collect( *pManager, _listWitness ); // 수집이 먼저 비운다 — 멤버는 용량만 넘긴다(수배 중 프레임마다 힙 할당을 하지 않는다)
        _outlaw.updateSeenByLaw( pRoot->getWorldPosition(), _listWitness );
    }
} // namespace sw
