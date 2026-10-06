#include "pch.h"

#include "GameFramework/Base/Control/PlayerControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Camera/CameraManagerComponent.h"
#include "GameFramework/Base/Control/ControlEvents.h"
#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/Base/Control/ControlSystem.h"
#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/Framework/GameEventUtil.h"

namespace sw
{
    SW_LOG_CALLER( "PlayerControllerComponent" );
} // namespace sw

namespace sw
{
    PlayerControllerComponent::PlayerControllerComponent()
        : _viewBlend{}
        , _playerIndex{ 0 }
        , _lookSensitivity{ 0.0025f }
        , _bManageViewTarget{ true }
        , _pushedLayer{}
        , _previousPawn{}
        , _warnedPawn{}
    {
    }

    void PlayerControllerComponent::onRegister( GameObjectManager& manager )
    {
        ControllerComponent::onRegister( manager );
        manager.getComponentRegistry().add<PlayerControllerComponent>( this ); // 자동 빙의가 플레이어 번호로 찾는다
    }

    void PlayerControllerComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<PlayerControllerComponent>( this );
        ControllerComponent::onUnregister( manager );
    }

    void PlayerControllerComponent::produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent )
    {
        outIntent = ControlIntent{};
        if ( context._pInput == nullptr )
            return;
        const InputMap& inputMap = context._pInput->getInputMap();
        if ( pawn.getMoveAction().empty() == false )
            outIntent._move = inputMap.getVector2D( pawn.getMoveAction() );
        if ( pawn.getUpAction().empty() == false )
            outIntent._moveUp = inputMap.getAxis1D( pawn.getUpAction() );
        if ( pawn.getLookAction().empty() == false )
        {
            const float2  look  = inputMap.getVector2D( pawn.getLookAction() );
            const float32 yaw   = getControlYaw() + look._x * _lookSensitivity;
            const float32 pitch = MathUtil::clamp( getControlPitch() - look._y * _lookSensitivity, -pawn.getMaxPitch(), pawn.getMaxPitch() );
            setControlRotation( yaw, pitch );
        }
        const vector<hashed_string>& listButton  = pawn.getButtonNames();
        const size_t                 buttonCount = MathUtil::min( listButton.size(), static_cast<size_t>( ControlIntent::kButtonCount ) );
        for ( size_t buttonIndex = 0; buttonIndex < buttonCount; ++buttonIndex )
        {
            const hashed_string& name = listButton[buttonIndex];
            outIntent.setButton( static_cast<int32>( buttonIndex ), inputMap.isActionDown( name ), inputMap.wasActionTriggered( name ) );
        }
        const vector<hashed_string>& listAnalog  = pawn.getAnalogNames();
        const size_t                 analogCount = MathUtil::min( listAnalog.size(), static_cast<size_t>( ControlIntent::kAnalogCount ) );
        for ( size_t analogIndex = 0; analogIndex < analogCount; ++analogIndex )
            outIntent._arrAnalog[analogIndex] = inputMap.getAxis1D( listAnalog[analogIndex] );
    }

    void PlayerControllerComponent::onPossessed( PawnComponent& pawn )
    {
        GameObject*        pOwner     = getOwner();
        GameObjectManager* pManager   = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*        pPawnOwner = pawn.getOwner();
        if ( pManager == nullptr || pPawnOwner == nullptr )
            return;
        const ControlSystem* pSystem = ControlSystem::find( *pManager );
        InputManager*        pInput  = pSystem != nullptr ? pSystem->findInputManager() : nullptr;
        if ( pInput != nullptr )
        {
            if ( pawn.getInputLayer().empty() == false )
            {
                pInput->getInputMap().pushLayer( pawn.getInputLayer() );
                _pushedLayer = pawn.getInputLayer();
            }
            if ( _warnedPawn != pawn.getHandle() )
            {
                _warnedPawn = pawn.getHandle();
                warnMissingActions( pawn, *pInput );
            }
        }
        if ( _bManageViewTarget )
        {
            CameraManagerComponent* pCameraManager = CameraManagerComponent::findForPlayer( *pManager, _playerIndex );
            if ( pCameraManager != nullptr )
                pCameraManager->setViewTarget( pPawnOwner->getHandle(), _viewBlend );
        }
        sendPossessionChanged( pPawnOwner->getHandle() );
    }

    void PlayerControllerComponent::onUnpossessed( PawnComponent& pawn )
    {
        GameObject*          pOwner   = getOwner();
        GameObjectManager*   pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ControlSystem* pSystem  = pManager != nullptr ? ControlSystem::find( *pManager ) : nullptr;
        InputManager*        pInput   = pSystem != nullptr ? pSystem->findInputManager() : nullptr;
        if ( pInput != nullptr && _pushedLayer.empty() == false )
            pInput->getInputMap().popLayer( _pushedLayer );
        _pushedLayer  = hashed_string{};
        _previousPawn = pawn.getOwner() != nullptr ? pawn.getOwner()->getHandle() : GameObjectHandle{};
        if ( isSwitchingPawn() == false )
            sendPossessionChanged( GameObjectHandle{} );
    }

    void PlayerControllerComponent::warnMissingActions( const PawnComponent& pawn, const InputManager& input )
    {
        const InputMap& inputMap  = input.getInputMap();
        const utf8*     pPawnName = pawn.getOwner() != nullptr ? pawn.getOwner()->getName().c_str() : "?";
        for ( const hashed_string& name : pawn.getButtonNames() )
        {
            if ( inputMap.getActionHandle( name ).isValid() == false )
                SW_LOG_WARNING( "Pawn '%#' intent button '%#' has no InputMap action - a player cannot press it", pPawnName, name.c_str() );
        }
        for ( const hashed_string& name : pawn.getAnalogNames() )
        {
            if ( inputMap.getActionHandle( name ).isValid() == false )
                SW_LOG_WARNING( "Pawn '%#' intent analog '%#' has no InputMap action - a player cannot drive it", pPawnName, name.c_str() );
        }
    }

    void PlayerControllerComponent::sendPossessionChanged( const GameObjectHandle& pawnObject )
    {
        PossessionChangedEvent event{};
        event._controller   = getOwner() != nullptr ? getOwner()->getHandle() : GameObjectHandle{};
        event._previousPawn = _previousPawn;
        event._pawn         = pawnObject;
        event._playerIndex  = _playerIndex;
        GameEventUtil::send( event );
    }
} // namespace sw
