#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Client/WesternRiderViewComponent.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/formatString.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Client/WesternHUDViewModel.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    namespace
    {
        struct WesternRiderViewComponentInternal
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

            /** @brief 감시 값 `Western.OutlawStatus` 입니다. 대상 디렉터 오브젝트의 뷰가 HUD 에 쓰는 무법자 글입니다. 아무 일도 없으면 빈 글입니다. */
            [[nodiscard]] static bool readOutlawStatus( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const WesternRiderViewComponent* const pView     = query.findTargetComponent<WesternRiderViewComponent>( "Western Rider View", outValue );
                const WesternGameModeComponent* const  pDirector = pView != nullptr ? query.findTargetComponent<WesternGameModeComponent>( "Western Game Mode", outValue ) : nullptr;
                if ( pDirector == nullptr )
                    return false;
                outValue.setText( string_view{ pView->formatOutlawStatus( *pDirector ) } );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( westernOutlawStatus, "Western.OutlawStatus", "Outlaw line the rider view on target writes to the HUD (empty when nothing happened)",
                               &WesternRiderViewComponentInternal::readOutlawStatus, true, false );

    WesternRiderViewComponent::WesternRiderViewComponent()
        : _cameraRig{}
        , _lowCoreWarning{ 30.0f }
        , _director{}
        , _hudViewModel{}
    {
    }

    WesternRiderViewComponent::~WesternRiderViewComponent() = default;

    void WesternRiderViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                     pOwner    = getOwner();
        const WesternGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<WesternGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "Western.RiderViewWithoutGameMode", "Western rider view on '%#': no WesternGameModeComponent in front of it on the same object - the view does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<WesternGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<WesternGameModeComponent>( pDirector );
    }

    void WesternRiderViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        context.deferPostTickOnComponent( *this, []( WesternRiderViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
    }

    void WesternRiderViewComponent::updateView( GameObjectManager& manager )
    {
        const WesternGameModeComponent* const pDirector   = manager.resolveComponent( _director );
        const PawnComponent* const            pPlayerPawn = pDirector != nullptr && pDirector->isStarted() ? pDirector->findPlayerPawn() : nullptr;
        if ( pPlayerPawn == nullptr )
            return;
        updateCameraFocus( manager, *pPlayerPawn );
        updateHUD( *pDirector, *pPlayerPawn );
    }

    void WesternRiderViewComponent::updateCameraFocus( GameObjectManager& manager, const PawnComponent& playerPawn )
    {
        // 카메라 디렉터는 뒤 그룹(PostPhysics)의 틱 뒤에 이 초점을 읽는다. 탔으면 기수 루트가 말 좌석에 붙어 있으므로 같은 자리를 따라간다.
        GameObject* const              pRigObject  = _cameraRig.isValid() ? manager.resolveGameObject( _cameraRig ) : nullptr;
        CameraDirectorComponent* const pRig        = pRigObject != nullptr ? pRigObject->getComponent<CameraDirectorComponent>() : nullptr;
        const SceneComponent* const    pPlayerRoot = playerPawn.getOwner() != nullptr ? playerPawn.getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pRig == nullptr || pPlayerRoot == nullptr )
            return;
        const float3 position = pPlayerRoot->getWorldPosition();
        pRig->setFocusPoint( float3{ position._x, 0.0f, position._z } );
    }

    string WesternRiderViewComponent::formatOutlawStatus( const WesternGameModeComponent& director ) const
    {
        const WesternOutlaw*  pOutlaw  = director.findOutlaw();
        const WesternCatalog* pCatalog = director.findWesternCatalog();
        if ( pOutlaw == nullptr || pCatalog == nullptr )
            return string{};
        const FactionDef*      pHonor         = pCatalog->getHonorReputation().find( WesternCatalog::getHonorFactionKey() );
        const int32            startHonor     = pHonor != nullptr ? pHonor->_startValue : 0;
        const WesternSurvival& survival       = pOutlaw->getSurvival();
        const bool             bLowCore       = survival.getCore( WesternCore::Health ) < _lowCoreWarning || survival.getCore( WesternCore::Stamina ) < _lowCoreWarning;
        const bool             bNothingToShow = pOutlaw->getHonorValue() == startHonor && pOutlaw->getWantedLevel() == 0 && pOutlaw->getBounty() == 0 && pOutlaw->getPeltCount() == 0 && pOutlaw->getCarcassCount() == 0 && bLowCore == false;
        if ( bNothingToShow )
            return string{};
        StringBuilder<constant::buffer::kMax256> status;
        status.appendFormat( "Honor %# (%#)", pOutlaw->getHonorValue(), pOutlaw->getHonorTierName().c_str() );
        if ( pOutlaw->getWantedLevel() > 0 )
            status.appendFormat( " - Wanted %#", pOutlaw->getWantedLevel() );
        if ( pOutlaw->getBounty() > 0 )
            status.appendFormat( " - Bounty $%#", pOutlaw->getBounty() );
        if ( pOutlaw->getCarcassCount() > 0 )
            status.appendFormat( " - Carcasses %#", pOutlaw->getCarcassCount() );
        if ( pOutlaw->getPeltCount() > 0 )
            status.appendFormat( " - Pelts %#", pOutlaw->getPeltCount() );
        status.appendFormat( " - $%#", pOutlaw->getMoney() );
        if ( bLowCore )
            status.append( " - Hungry" );
        return string( status.view() );
    }

    void WesternRiderViewComponent::updateHUD( const WesternGameModeComponent& director, const PawnComponent& playerPawn )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<WesternHUDViewModel>();
        WesternHUDViewModel* const pViewModel = _hudViewModel.get();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, pViewModel );
        pViewModel->setOutlawStatus( formatOutlawStatus( director ) );
        const VehicleSeatComponent* pSeat        = VehicleSeatComponent::findSeatOf( playerPawn );
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
        formatstring( arrStatus, constant::buffer::kMax128, "Riding %# - %# %.1f m/s - E: dismount  Shift: gallop", pHorseObject->getName().c_str(),
                      WesternRiderViewComponentInternal::findGaitName( gait ), speed );
        pViewModel->setRideStatus( arrStatus );
        pViewModel->setStamina( pHorse != nullptr ? pHorse->getStamina().getValue() : 0.0f, pHorse != nullptr ? pHorse->getStamina().getRatio() : 0.0f );
    }
} // namespace sw
