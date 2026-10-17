#include "pch.h"

#include "Games/WildWest/WildWestDirectorComponent.h"

#include "Core/Container/formatString.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Base/Actor/Camera/OrthoCameraRigComponent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Vehicle/MountMovementComponent.h"
#include "GameFramework/Base/Gameplay/Vehicle/MountUtil.h"
#include "GameFramework/Base/Gameplay/Vehicle/VehicleSeatComponent.h"
#include "GameFramework/Base/UI/HUD/HUDControllerComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/HorseFollowAIController.h"

#include "Games/WildWest/WildWestHUDViewModel.h"
#include "Games/WildWest/WildWestHorseComponent.h"

namespace sw
{
    SW_LOG_CALLER( "WildWestDirector" );

    namespace
    {
        struct WildWestDirectorComponentInternal
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

            /** @brief 질의 대상 말의 탈것 이동입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const MountMovementComponent* findTargetMovement( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const MountMovementComponent* pMovement = query.findTargetObject() != nullptr ? query.findTargetObject()->getComponent<MountMovementComponent>() : nullptr;
                if ( pMovement == nullptr )
                    outValue.setText( query.findTargetObject() == nullptr ? query.getTargetProblem() : query.describeMissing( "Mount Movement" ) );
                return pMovement;
            }

            /** @brief 탐침 `WildWest.Gait` 입니다. 대상 말의 걸음새 번호(0 서기 .. 4 질주)입니다. */
            [[nodiscard]] static bool readGait( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const MountMovementComponent* pMovement = findTargetMovement( query, outValue );
                if ( pMovement == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pMovement->getGait() ) );
                return true;
            }

            /** @brief 탐침 `WildWest.Speed` 입니다. 대상 말이 몸 방향으로 가는 속도(m/s)입니다. */
            [[nodiscard]] static bool readSpeed( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const MountMovementComponent* pMovement = findTargetMovement( query, outValue );
                if ( pMovement == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pMovement->getForwardSpeed() ) );
                return true;
            }

            /** @brief 탐침 `WildWest.Stamina` 입니다. 대상 말의 스태미나 비율(0..1)이고, 말을 열지 않았으면 값이 없습니다. */
            [[nodiscard]] static bool readStamina( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const WildWestHorseComponent* pHorseComponent = query.findTargetObject() != nullptr ? query.findTargetObject()->getComponent<WildWestHorseComponent>() : nullptr;
                const WesternHorse*           pHorse          = pHorseComponent != nullptr ? pHorseComponent->findHorse() : nullptr;
                if ( pHorse == nullptr )
                {
                    outValue.setText( query.findTargetObject() == nullptr ? query.getTargetProblem() : query.describeMissing( "opened Wild West Horse" ) );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pHorse->getStamina().getRatio() ) );
                return true;
            }
        };
    } // namespace

    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_wildWestAutoPlay, 0, "WildWest: 기수를 가장 가까운 말에 태운다 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_wildWestAutoPlay, "WildWest", "The rider mounts the nearest horse" );

    SW_AUTOMATION_QUERY_PROBE( wildWestGait, "WildWest.Gait", "Gait of the horse on target (0 standing, 1 walk, 2 trot, 3 canter, 4 gallop)",
                               &WildWestDirectorComponentInternal::readGait, true, false );
    SW_AUTOMATION_QUERY_PROBE( wildWestSpeed, "WildWest.Speed", "Forward speed (m/s) of the horse on target", &WildWestDirectorComponentInternal::readSpeed, true, false );
    SW_AUTOMATION_QUERY_PROBE( wildWestStamina, "WildWest.Stamina", "Stamina ratio (0..1) of the horse on target", &WildWestDirectorComponentInternal::readStamina, true,
                               false );
} // namespace sw

namespace sw
{
    WildWestDirectorComponent::WildWestDirectorComponent()
        : _westernDataPath{}
        , _player{}
        , _cameraRig{}
        , _whistleButton{}
        , _westernCatalog{}
        , _bAutoMountRequested{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    WildWestDirectorComponent::~WildWestDirectorComponent() = default;

    void WildWestDirectorComponent::writeState( Archive& outArchive ) const
    {
        // 판 상태가 없다 — 말 상태는 말 컴포넌트가, 자리는 씬 오브젝트가 싣는다.
        StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
    }

    bool WildWestDirectorComponent::readState( Archive& archive )
    {
        return StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) && archive.getRemainingBytes() == 0;
    }

    bool WildWestDirectorComponent::startGame()
    {
        if ( _westernDataPath.empty() || _westernCatalog.loadFromResource( _westernDataPath ) == false )
        {
            SW_LOG_ERROR( "[WildWest] the western catalog '%#' could not be loaded - the prairie cannot open", _westernDataPath.c_str() );
            return false;
        }
        const PawnComponent* pPlayerPawn = findPlayerPawn();
        if ( pPlayerPawn == nullptr )
        {
            SW_LOG_ERROR( "[WildWest] Player does not point at an object with a pawn - the prairie cannot open" );
            return false;
        }
        if ( pPlayerPawn->findButton( _whistleButton ) < 0 )
        {
            SW_LOG_ERROR( "[WildWest] the rider pawn has no whistle button '%#' - the prairie cannot open", _whistleButton.c_str() );
            return false;
        }
        return true;
    }

    void WildWestDirectorComponent::onGameStarted()
    {
        SW_LOG_INFO( "[WildWest] the prairie is open - walk with WASD, E to mount or dismount, hold Shift to gallop, F to whistle" );
    }

    void WildWestDirectorComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
        const PawnComponent* pPlayerPawn = findPlayerPawn();
        GameObjectManager*   pManager    = getObjectManager();
        if ( pPlayerPawn == nullptr || pManager == nullptr )
            return;
        updateCameraFocus( *pPlayerPawn );
        // 다른 오브젝트(말 AI · 좌석)를 바꾸는 일과 UI 바인딩은 틱 뒤에 한다.
        const bool bWhistle = pPlayerPawn->wasButtonTriggered( pPlayerPawn->findButton( _whistleButton ) );
        if ( bWhistle )
            pManager->executeOrDeferPostTickOnComponent( *this, []( WildWestDirectorComponent& director )
            { director.whistleHorses(); } );
        const bool bAutoMount = isAutoPlayOn() && _bAutoMountRequested == SW_FALSE;
        if ( bAutoMount )
        {
            _bAutoMountRequested = SW_TRUE;
            pManager->executeOrDeferPostTickOnComponent( *this, []( WildWestDirectorComponent& director )
            { director.mountNearestHorse(); } );
        }
        pManager->executeOrDeferPostTickOnComponent( *this, []( WildWestDirectorComponent& director )
        { director.updateHUD(); } );
    }

    void WildWestDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 세우는 런타임 오브젝트가 없다 — 말과 기수는 씬에 있다.
        (void)manager;
        (void)bRespawnViews;
    }

    const PawnComponent* WildWestDirectorComponent::findPlayerPawn() const
    {
        const GameObjectManager* pManager = getObjectManager();
        const GameObject*        pPlayer  = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        return pPlayer != nullptr ? pPlayer->getComponent<PawnComponent>() : nullptr;
    }

    void WildWestDirectorComponent::updateCameraFocus( const PawnComponent& playerPawn )
    {
        // 리그는 PostUpdate 에서 이 초점을 읽고, 리그의 오브젝트에는 다른 쓰기가 없다. 탔으면 기수 루트가 말 좌석에 붙어 있으므로 같은 자리를 따라간다.
        const GameObjectManager* pManager    = getObjectManager();
        const GameObject*        pRigObject  = pManager != nullptr ? pManager->resolveGameObject( _cameraRig ) : nullptr;
        OrthoCameraRigComponent* pRig        = pRigObject != nullptr ? pRigObject->getComponent<OrthoCameraRigComponent>() : nullptr;
        const SceneComponent*    pPlayerRoot = playerPawn.getOwner() != nullptr ? playerPawn.getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pRig == nullptr || pPlayerRoot == nullptr )
            return;
        const float3 position = pPlayerRoot->getWorldPosition();
        pRig->setFocus( float3{ position._x, 0.0f, position._z } );
    }

    void WildWestDirectorComponent::whistleHorses()
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
        SW_LOG_INFO( "[WildWest] whistle - %# horse(s) coming", calledCount );
    }

    void WildWestDirectorComponent::mountNearestHorse()
    {
        GameObjectManager* pManager = getObjectManager();
        GameObject*        pPlayer  = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        PawnComponent*     pPawn    = pPlayer != nullptr ? pPlayer->getComponent<PawnComponent>() : nullptr;
        if ( pPawn == nullptr || MountUtil::findSeatOf( *pPawn ) != nullptr )
            return;
        VehicleSeatComponent* pSeat = MountUtil::findNearestFreeSeat( *pPawn );
        if ( pSeat == nullptr )
        {
            SW_LOG_WARNING( "[WildWest] auto play: no horse within reach of the rider" );
            return;
        }
        const MountResult result = MountUtil::mount( *pPawn, *pSeat );
        if ( result != MountResult::Mounted )
            SW_LOG_WARNING( "[WildWest] auto play: the rider could not mount '%#' (result %#)", pSeat->getOwner()->getName().c_str(), static_cast<int32>( result ) );
    }

    void WildWestDirectorComponent::updateHUD()
    {
        GameObject*             pOwner      = getOwner();
        HUDControllerComponent* pHUD        = pOwner != nullptr ? pOwner->getComponent<HUDControllerComponent>() : nullptr;
        const PawnComponent*    pPlayerPawn = findPlayerPawn();
        if ( pHUD == nullptr || pPlayerPawn == nullptr )
            return;
        WildWestHUDViewModel* pViewModel = pHUD->getOrCreateViewModel<WildWestHUDViewModel>();
        if ( pViewModel == nullptr )
            return; // 다른 뷰모델이 걸려 있다 — HUD 컨트롤러가 오류를 남겼다
        const VehicleSeatComponent* pSeat        = MountUtil::findSeatOf( *pPlayerPawn );
        const GameObject*           pHorseObject = pSeat != nullptr ? pSeat->getOwner() : nullptr;
        if ( pHorseObject == nullptr )
        {
            pViewModel->setRideStatus( "On foot - E: mount  F: whistle" );
            pViewModel->setStamina( 0.0f, 0.0f );
            return;
        }
        const MountMovementComponent* pMovement       = pHorseObject->getComponent<MountMovementComponent>();
        const WildWestHorseComponent* pHorseComponent = pHorseObject->getComponent<WildWestHorseComponent>();
        const WesternHorse*           pHorse          = pHorseComponent != nullptr ? pHorseComponent->findHorse() : nullptr;
        const MountGait               gait            = pMovement != nullptr ? pMovement->getGait() : MountGait::Idle;
        const float32                 speed           = pMovement != nullptr ? pMovement->getForwardSpeed() : 0.0f;
        utf8                          arrStatus[constant::kMaxBuffer128]{};
        formatstring( arrStatus, constant::kMaxBuffer128, "Riding %s - %s %.1f m/s - E: dismount  Shift: gallop", pHorseObject->getName().c_str(),
                      WildWestDirectorComponentInternal::findGaitName( gait ), speed );
        pViewModel->setRideStatus( arrStatus );
        pViewModel->setStamina( pHorse != nullptr ? pHorse->getStamina().getValue() : 0.0f, pHorse != nullptr ? pHorse->getStamina().getRatio() : 0.0f );
    }
} // namespace sw
