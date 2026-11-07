#include "pch.h"

#include "Games/WildWest/WildWestGameModeComponent.h"

#include "Core/Container/formatString.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameAutoplay.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/HorseFollowAIController.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternDeedComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"
#include "GameFramework/Runtime/UI/HUD/HUDControllerComponent.h"

#include "Games/WildWest/WildWestHUDViewModel.h"

namespace sw
{
    namespace
    {
        struct WildWestGameModeComponentInternal
        {
            /** @brief HUD 에 보이는 걸음새 이름입니다(`MountGait` 순서). */
            static const utf8* findGaitName( MountGait gait )
            {
                switch ( gait )
                {
                    case MountGait::Idle:
                        return "Standing";
                    case MountGait::Walk:
                        return "Walk";
                    case MountGait::Trot:
                        return "Trot";
                    case MountGait::Canter:
                        return "Canter";
                    case MountGait::Gallop:
                        return "Gallop";
                }
                return "?";
            }
        };
    } // namespace

    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_wildWestAutoPlay, 0, "WildWest: 기수를 가장 가까운 말에 태운다 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_wildWestAutoPlay, "WildWest", "The rider mounts the nearest horse" );

} // namespace sw

namespace sw
{
    WildWestGameModeComponent::WildWestGameModeComponent()
        : _westernDataPath{}
        , _player{}
        , _cameraRig{}
        , _whistleButton{}
        , _useButton{}
        , _weatherDataPath{}
        , _regionID{}
        , _regionBaseTemperature{ 20.0f }
        , _secondsPerDay{ 1440.0f }
        , _witnessSightRange{ 12.0f }
        , _lowCoreWarning{ 30.0f }
        , _startingMoney{ 0 }
        , _outlaw{}
        , _pWesternCatalog{}
        , _pWeatherCatalog{}
        , _bAutoMountRequested{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    WildWestGameModeComponent::~WildWestGameModeComponent() = default;

    void WildWestGameModeComponent::serializeState( StateIO& io )
    {
        // 플레이어의 삶만 싣는다 — 지갑 · 평판 · 시계 · 날씨는 판 상태가, 말 상태는 말 컴포넌트가, 자리는 씬 오브젝트가 싣는다.
        io.header( kStateTag, kStateVersion );
        if ( io.isOk() == false )
            return;
        io.valueAtomic( _outlaw );
        io.end();
    }

    bool WildWestGameModeComponent::startGame()
    {
        if ( acquireRequired( _westernDataPath, _pWesternCatalog ) == false )
            return false;
        const PawnComponent* pPlayerPawn = findPlayerPawn();
        if ( pPlayerPawn == nullptr )
        {
            SW_LOG_ERROR_ID( "WildWestGameMode.PlayerDoesNotPoint", "[WildWest] Player does not point at an object with a pawn - the prairie cannot open" );
            return false;
        }
        if ( pPlayerPawn->findButton( _whistleButton ) < 0 )
        {
            SW_LOG_ERROR_ID( "WildWestGameMode.RiderPawnNoWhistle", "[WildWest] the rider pawn has no whistle button '%#' - the prairie cannot open", _whistleButton.c_str() );
            return false;
        }
        if ( pPlayerPawn->findButton( _useButton ) < 0 )
        {
            SW_LOG_ERROR_ID( "WildWestGameMode.RiderPawnNoUse", "[WildWest] the rider pawn has no use button '%#' - the prairie cannot open", _useButton.c_str() );
            return false;
        }
        if ( acquireRequired( _weatherDataPath, _pWeatherCatalog ) == false )
            return false;
        SessionStateSettings settings{};
        settings._clock._secondsPerDay = _secondsPerDay;
        settings._pReputationCatalog   = &_pWesternCatalog->getHonorReputation();
        settings._pWeatherCatalog      = _pWeatherCatalog.get();
        SessionStateComponent* pState  = SessionStateComponent::openOnOwner( *this, settings );
        if ( pState == nullptr )
            return false; // 까닭은 판 상태가 남겼다
        AIPerceptionSettings sight{};
        sight._sightRange = _witnessSightRange;
        if ( _outlaw.initialize( _pWesternCatalog.get(), pState->makeRefs(), _regionID, _regionBaseTemperature, sight ) == false )
            return false; // 까닭은 무법자가 남겼다
        if ( pState->isFreshGame() && _startingMoney > 0 )
            pState->getWallet().add( _pWesternCatalog->getCurrency(), _startingMoney );
        return true;
    }

    void WildWestGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "WildWestGameMode.PrairieOpenWalkWASD", "[WildWest] the prairie is open - walk with WASD, E to mount or dismount, hold Shift to gallop, F to whistle, G to act" );
    }

    void WildWestGameModeComponent::updateGame( float32 deltaTime )
    {
        _outlaw.update( deltaTime );
        const PawnComponent* pPlayerPawn = findPlayerPawn();
        GameObjectManager*   pManager    = getObjectManager();
        if ( pPlayerPawn == nullptr || pManager == nullptr )
            return;
        updateCameraFocus( *pPlayerPawn );
        // 다른 오브젝트(말 AI · 좌석)를 바꾸는 일과 UI 바인딩은 틱 뒤에 한다.
        const bool bWhistle = pPlayerPawn->wasButtonTriggered( pPlayerPawn->findButton( _whistleButton ) );
        if ( bWhistle )
            pManager->executeOrDeferPostTickOnComponent( *this, []( WildWestGameModeComponent& director )
            { director.whistleHorses(); } );
        const bool bUse = pPlayerPawn->wasButtonTriggered( pPlayerPawn->findButton( _useButton ) );
        if ( bUse )
            pManager->executeOrDeferPostTickOnComponent( *this, []( WildWestGameModeComponent& director )
            { director.useDeed(); } );
        const bool bAutoMount = isAutoPlayOn() && _bAutoMountRequested == SW_FALSE;
        if ( bAutoMount )
        {
            _bAutoMountRequested = SW_TRUE;
            pManager->executeOrDeferPostTickOnComponent( *this, []( WildWestGameModeComponent& director )
            { director.mountNearestHorse(); } );
        }
        pManager->executeOrDeferPostTickOnComponent( *this, []( WildWestGameModeComponent& director )
        {
            director.updateLawSight();
            director.updateHUD();
        } );
    }

    void WildWestGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 세우는 런타임 오브젝트가 없다 — 말과 기수는 씬에 있다.
        (void)manager;
        (void)bRespawnViews;
    }

    const PawnComponent* WildWestGameModeComponent::findPlayerPawn() const
    {
        return findOn<PawnComponent>( _player );
    }

    void WildWestGameModeComponent::updateCameraFocus( const PawnComponent& playerPawn )
    {
        // 카메라 디렉터는 PostPhysics 틱 뒤에 이 초점을 읽고, 카메라 오브젝트에는 다른 쓰기가 없다. 탔으면 기수 루트가 말 좌석에 붙어 있으므로 같은 자리를 따라간다.
        CameraDirectorComponent* pRig        = findOn<CameraDirectorComponent>( _cameraRig );
        const SceneComponent*    pPlayerRoot = playerPawn.getOwner() != nullptr ? playerPawn.getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pRig == nullptr || pPlayerRoot == nullptr )
            return;
        const float3 position = pPlayerRoot->getWorldPosition();
        pRig->setFocusPoint( float3{ position._x, 0.0f, position._z } );
    }

    void WildWestGameModeComponent::whistleHorses()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        int32 calledCount = 0;
        pManager->forEachComponentOfType<HorseFollowAIController>( [this, &calledCount]( HorseFollowAIController* pAI )
        {
            if ( pAI->getOwnerObject() != _player )
                return;
            pAI->whistle();
            ++calledCount;
        } );
        SW_LOG_INFO_ID( "WildWestGameMode.WhistleHorseComing", "[WildWest] whistle - %# horse(s) coming", calledCount );
    }

    void WildWestGameModeComponent::mountNearestHorse()
    {
        PawnComponent* pPawn = findOn<PawnComponent>( _player );
        if ( pPawn == nullptr || VehicleSeatComponent::findSeatOf( *pPawn ) != nullptr )
            return;
        VehicleSeatComponent* pSeat = VehicleSeatComponent::findNearestFreeSeat( *pPawn );
        if ( pSeat == nullptr )
        {
            SW_LOG_WARNING_ID( "WildWestGameMode.AutoPlayNoHorse", "[WildWest] auto play: no horse within reach of the rider" );
            return;
        }
        const MountResult result = VehicleSeatComponent::mount( *pPawn, *pSeat );
        if ( result != MountResult::Mounted )
            SW_LOG_WARNING_ID( "WildWestGameMode.AutoPlayRiderCould", "[WildWest] auto play: the rider could not mount '%#' (result %#)", pSeat->getOwner()->getName().c_str(), static_cast<int32>( result ) );
    }

    void WildWestGameModeComponent::useDeed()
    {
        GameObjectManager*    pManager = getObjectManager();
        const PawnComponent*  pPawn    = findPlayerPawn();
        const SceneComponent* pRoot    = pPawn != nullptr && pPawn->getOwner() != nullptr ? pPawn->getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pRoot == nullptr )
            return;
        const WesternDeedComponent* pUsed = WesternDeedComponent::useNearest( *pManager, pRoot->getWorldPosition(), _outlaw );
        if ( pUsed != nullptr && pUsed->getOwner() != nullptr )
            SW_LOG_INFO_ID( "WildWestGameMode.DeedDone", "[WildWest] did the deed at '%#'", pUsed->getOwner()->getName().c_str() );
    }

    void WildWestGameModeComponent::updateLawSight()
    {
        GameObjectManager*    pManager = getObjectManager();
        const PawnComponent*  pPawn    = findPlayerPawn();
        const SceneComponent* pRoot    = pPawn != nullptr && pPawn->getOwner() != nullptr ? pPawn->getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pRoot == nullptr || _outlaw.getWantedLevel() <= 0 )
            return;
        vector<WesternWitness> listWitness;
        WesternWitnessComponent::collect( *pManager, listWitness );
        _outlaw.updateSeenByLaw( pRoot->getWorldPosition(), listWitness );
    }

    string WildWestGameModeComponent::formatOutlawStatus() const
    {
        const WesternOutlaw* pOutlaw = findOutlaw();
        if ( pOutlaw == nullptr )
            return string{};
        const FactionDef*      pHonor         = _pWesternCatalog->getHonorReputation().find( hashed_string( WesternCatalog::kHonorFactionID ) );
        const int32            startHonor     = pHonor != nullptr ? pHonor->_startValue : 0;
        const WesternSurvival& survival       = pOutlaw->getSurvival();
        const bool             bLowCore       = survival.getCore( WesternCore::Health ) < _lowCoreWarning || survival.getCore( WesternCore::Stamina ) < _lowCoreWarning;
        const bool             bNothingToShow = pOutlaw->getHonorValue() == startHonor && pOutlaw->getWantedLevel() == 0 && pOutlaw->getBounty() == 0 && pOutlaw->getPeltCount() == 0 && pOutlaw->getCarcassCount() == 0 && bLowCore == false;
        if ( bNothingToShow )
            return string{};
        string status = formatToString( "Honor %# (%#)", pOutlaw->getHonorValue(), pOutlaw->getHonorTierName().c_str() );
        if ( pOutlaw->getWantedLevel() > 0 )
            status += formatToString( " - Wanted %#", pOutlaw->getWantedLevel() );
        if ( pOutlaw->getBounty() > 0 )
            status += formatToString( " - Bounty $%#", pOutlaw->getBounty() );
        if ( pOutlaw->getCarcassCount() > 0 )
            status += formatToString( " - Carcasses %#", pOutlaw->getCarcassCount() );
        if ( pOutlaw->getPeltCount() > 0 )
            status += formatToString( " - Pelts %#", pOutlaw->getPeltCount() );
        status += formatToString( " - $%#", pOutlaw->getMoney() );
        if ( bLowCore )
            status += " - Hungry";
        return status;
    }

    void WildWestGameModeComponent::updateHUD()
    {
        GameObject*             pOwner      = getOwner();
        HUDControllerComponent* pHUD        = pOwner != nullptr ? pOwner->getComponent<HUDControllerComponent>() : nullptr;
        const PawnComponent*    pPlayerPawn = findPlayerPawn();
        if ( pHUD == nullptr || pPlayerPawn == nullptr )
            return;
        WildWestHUDViewModel* pViewModel = pHUD->getOrCreateViewModel<WildWestHUDViewModel>();
        if ( pViewModel == nullptr )
            return; // 다른 뷰모델이 걸려 있다 — HUD 컨트롤러가 오류를 남겼다
        pViewModel->setOutlawStatus( formatOutlawStatus() );
        const VehicleSeatComponent* pSeat        = VehicleSeatComponent::findSeatOf( *pPlayerPawn );
        const GameObject*           pHorseObject = pSeat != nullptr ? pSeat->getOwner() : nullptr;
        if ( pHorseObject == nullptr )
        {
            pViewModel->setRideStatus( "On foot - E: mount  F: whistle" );
            pViewModel->setStamina( 0.0f, 0.0f );
            return;
        }
        const MountMovementComponent* pMovement       = pHorseObject->getComponent<MountMovementComponent>();
        const WesternHorseComponent*  pHorseComponent = pHorseObject->getComponent<WesternHorseComponent>();
        const WesternHorse*           pHorse          = pHorseComponent != nullptr ? pHorseComponent->findHorse() : nullptr;
        const MountGait               gait            = pMovement != nullptr ? pMovement->getGait() : MountGait::Idle;
        const float32                 speed           = pMovement != nullptr ? pMovement->getForwardSpeed() : 0.0f;
        utf8                          arrStatus[constant::buffer::kMax128]{};
        formatstring( arrStatus, constant::buffer::kMax128, "Riding %s - %s %.1f m/s - E: dismount  Shift: gallop", pHorseObject->getName().c_str(),
                      WildWestGameModeComponentInternal::findGaitName( gait ), speed );
        pViewModel->setRideStatus( arrStatus );
        pViewModel->setStamina( pHorse != nullptr ? pHorse->getStamina().getValue() : 0.0f, pHorse != nullptr ? pHorse->getStamina().getRatio() : 0.0f );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct WildWestWatchesInternal
        {
            /** @brief 감시 값 `WildWest.OutlawStatus` 입니다. 대상 디렉터가 HUD 에 쓰는 무법자 글입니다. 아무 일도 없으면 빈 글입니다. */
            [[nodiscard]] static bool readOutlawStatus( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*                pTarget   = query.findTargetObject();
                const WildWestGameModeComponent* pDirector = pTarget != nullptr ? pTarget->getComponent<WildWestGameModeComponent>() : nullptr;
                if ( pDirector == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "Wild West Director" ) );
                    return false;
                }
                outValue.setText( string_view{ pDirector->formatOutlawStatus() } );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( wildWestOutlawStatus, "WildWest.OutlawStatus", "Outlaw line the director on target writes to the HUD (empty when nothing happened)", &WildWestWatchesInternal::readOutlawStatus, true, false );
} // namespace sw
