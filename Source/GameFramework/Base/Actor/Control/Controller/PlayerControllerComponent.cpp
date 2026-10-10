#include "pch.h"

#include "GameFramework/Base/Actor/Control/Controller/PlayerControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Base/Actor/Camera/CameraManagerComponent.h"
#include "GameFramework/Base/Actor/Control/ControlEvents.h"
#include "GameFramework/Base/Actor/Control/ControlSystem.h"
#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "PlayerControllerComponent" );
} // namespace sw

namespace sw
{
    PlayerControllerComponent::PlayerControllerComponent()
        : _viewBlend{}
        , _playerIndex{ 0 }
        , _lookSensitivity{ 0.0022f }
        , _bManageViewTarget{ true }
        , _mouseLockAction{ "ToggleMouseLock" }
        , _pushedLayer{}
        , _previousPawn{}
        , _warnedPawn{}
        , _bMouseLockRequested{ SW_FALSE }
        , _bMouseLockApplied{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void PlayerControllerComponent::onRegister( GameObjectManager& manager )
    {
        ControllerComponent::onRegister( manager );
        manager.getComponentRegistry().add<PlayerControllerComponent>( this ); // 자동 빙의가 플레이어 번호로 찾는다
    }

    void PlayerControllerComponent::onUnregister( GameObjectManager& manager )
    {
        // 씬을 비우는 중이면 조종 시스템이 먼저 떨어져 놓기(onUnpossessed)가 오지 않는다 — 건 잠금은 여기서 푼다.
        InputManager* pInput = _bMouseLockApplied == SW_TRUE ? findInputManager( manager ) : nullptr;
        if ( pInput != nullptr )
            applyMouseLock( *pInput, false );
        _bMouseLockRequested = SW_FALSE;
        manager.getComponentRegistry().remove<PlayerControllerComponent>( this );
        ControllerComponent::onUnregister( manager );
    }

    void PlayerControllerComponent::produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent )
    {
        outIntent = ControlIntent{};
        if ( context._pInput == nullptr )
            return;
        InputManager&   input    = *context._pInput;
        const InputMap& inputMap = input.getInputMap();
        const UISystem* pUI      = context._pUISystem;
        // UI 가 커서를 바라면(메뉴가 열렸다) 잠금을 쉰다 — 닫히면 요청해 둔 잠금으로 돌아간다.
        const bool bUIWantsCursor = pUI != nullptr && pUI->wantsCursor();
        bool       bLook          = bUIWantsCursor == false;
        if ( pawn.wantsMouseLock() )
        {
            if ( _mouseLockAction.empty() == false && wasActionTriggeredForGame( inputMap, pUI, _mouseLockAction ) )
            {
                _bMouseLockRequested = _bMouseLockRequested == SW_TRUE ? SW_FALSE : SW_TRUE;
                SW_LOG_INFO( "Player %# mouse lock %# (%#)", _playerIndex, _bMouseLockRequested == SW_TRUE ? "engaged" : "released", _mouseLockAction.c_str() );
            }
            applyMouseLock( input, _bMouseLockRequested == SW_TRUE && bUIWantsCursor == false );
            // 잠금이 실제로 걸린 동안만 시선을 쌓는다. 배타 가상 입력(시나리오)은 OS 포인터를 쥐지 않으니 잠금 요청만 본다.
            bLook = bLook && _bMouseLockRequested == SW_TRUE && ( input.isMouseLockActive() || input.isOsInputSuppressed() );
        }
        // 모달 · 로딩 화면이 떠 있으면 게임 입력이 없다 — 의도는 0, 조종 회전은 그대로.
        if ( pUI != nullptr && pUI->isGameInputBlocked() )
            return;
        if ( pawn.getMoveAction().empty() == false && ( pUI == nullptr || pUI->isActionConsumed( inputMap, pawn.getMoveAction() ) == false ) )
            outIntent._move = inputMap.getVector2D( pawn.getMoveAction() );
        if ( pawn.getUpAction().empty() == false && ( pUI == nullptr || pUI->isActionConsumed( inputMap, pawn.getUpAction() ) == false ) )
            outIntent._moveUp = inputMap.getAxis1D( pawn.getUpAction() );
        if ( bLook && pawn.getLookAction().empty() == false )
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
            outIntent.setButton( static_cast<int32>( buttonIndex ), isActionDownForGame( inputMap, pUI, name ), wasActionTriggeredForGame( inputMap, pUI, name ) );
        }
        const vector<hashed_string>& listAnalog  = pawn.getAnalogNames();
        const size_t                 analogCount = MathUtil::min( listAnalog.size(), static_cast<size_t>( ControlIntent::kAnalogCount ) );
        for ( size_t analogIndex = 0; analogIndex < analogCount; ++analogIndex )
        {
            const hashed_string& name         = listAnalog[analogIndex];
            outIntent._arrAnalog[analogIndex] = pUI != nullptr && pUI->isActionConsumed( inputMap, name ) ? 0.0f : inputMap.getAxis1D( name );
        }
    }

    bool PlayerControllerComponent::isActionDownForGame( const InputMap& inputMap, const UISystem* pUISystem, const hashed_string& action )
    {
        return inputMap.isActionDown( action ) && ( pUISystem == nullptr || pUISystem->isActionConsumed( inputMap, action ) == false );
    }

    bool PlayerControllerComponent::wasActionTriggeredForGame( const InputMap& inputMap, const UISystem* pUISystem, const hashed_string& action )
    {
        return inputMap.wasActionTriggered( action ) && ( pUISystem == nullptr || pUISystem->isActionConsumed( inputMap, action ) == false );
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
            _bMouseLockRequested = pawn.wantsMouseLock() ? SW_TRUE : SW_FALSE;
            applyMouseLock( *pInput, _bMouseLockRequested == SW_TRUE );
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
        _bMouseLockRequested = SW_FALSE;
        if ( pInput != nullptr )
            applyMouseLock( *pInput, false );
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
        if ( pawn.wantsMouseLock() && _mouseLockAction.empty() == false && inputMap.getActionHandle( _mouseLockAction ).isValid() == false )
            SW_LOG_WARNING( "Pawn '%#' locks the mouse but the InputMap has no '%#' action - the player cannot release the cursor", pPawnName, _mouseLockAction.c_str() );
    }

    void PlayerControllerComponent::applyMouseLock( InputManager& input, bool bLocked )
    {
        if ( bLocked == ( _bMouseLockApplied == SW_TRUE ) )
            return;
        input.setMouseLockMode( bLocked ? MouseLockMode::LockedInCenter : MouseLockMode::None );
        input.setCursorVisible( bLocked == false );
        _bMouseLockApplied = bLocked ? SW_TRUE : SW_FALSE;
    }

    InputManager* PlayerControllerComponent::findInputManager( const GameObjectManager& manager )
    {
        const ControlSystem* pSystem = ControlSystem::find( manager );
        return pSystem != nullptr ? pSystem->findInputManager() : game::getService<InputManager>();
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
