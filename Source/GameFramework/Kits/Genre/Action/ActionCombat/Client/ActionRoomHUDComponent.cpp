#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionRoomHUDComponent.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/CameraShakeChannel.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatEvents.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionRoomHUDViewModel.h"
#include "GameFramework/Runtime/Character/Combat/Damage/DamageEvents.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    ActionRoomHUDComponent::ActionRoomHUDComponent()
        : _notificationSeconds{ 2.5f }
        , _clearShake{ 0.08f }
        , _bossShakeScale{ 2.0f }
        , _defeatShake{ 0.15f }
        , _damageShake{ 0.03f }
        , _damageShakeRadius{ 12.0f }
        , _shakeSeconds{ 0.35f }
        , _viewModel{}
        , _gateSubscription{}
        , _clearedSubscription{}
        , _defeatedSubscription{}
        , _damageSubscription{}
        , _notificationLeft{ 0.0f }
        , _notification{ Notification::None }
        , _bGateLocked{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    ActionRoomHUDComponent::~ActionRoomHUDComponent() = default;

    void ActionRoomHUDComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        // 시작(beginPlay)이 아니라 등록에서 구독한다 — 디렉터가 먼저 시작해 첫 룸의 문을 잠그면 그 이벤트를 놓친다.
        EventDispatcher* const pDispatcher = game::findSubsystem<EventDispatcher>();
        if ( pDispatcher == nullptr )
            return;
        _gateSubscription     = pDispatcher->subscribe<ClearGateStateChangedEvent>( gameEventChannel(), SW_DELEGATE_METHOD( Delegate<void( const ClearGateStateChangedEvent& )>, &ActionRoomHUDComponent::onGateStateChanged, this ) );
        _clearedSubscription  = pDispatcher->subscribe<RoomClearedEvent>( gameEventChannel(), SW_DELEGATE_METHOD( Delegate<void( const RoomClearedEvent& )>, &ActionRoomHUDComponent::onRoomCleared, this ) );
        _defeatedSubscription = pDispatcher->subscribe<PlayerDefeatedInRoomEvent>( gameEventChannel(), SW_DELEGATE_METHOD( Delegate<void( const PlayerDefeatedInRoomEvent& )>, &ActionRoomHUDComponent::onPlayerDefeated, this ) );
        _damageSubscription   = pDispatcher->subscribe<DamageAppliedEvent>( gameEventChannel(), SW_DELEGATE_METHOD( Delegate<void( const DamageAppliedEvent& )>, &ActionRoomHUDComponent::onDamageApplied, this ) );
    }

    void ActionRoomHUDComponent::onUnregister( GameObjectManager& manager )
    {
        _gateSubscription.reset();
        _clearedSubscription.reset();
        _defeatedSubscription.reset();
        _damageSubscription.reset();
        Component::onUnregister( manager );
    }

    void ActionRoomHUDComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        updateHUD(); // 첫 프레임부터 접힌 채로 건다
    }

    void ActionRoomHUDComponent::onEndPlay()
    {
        _notificationLeft                  = 0.0f;
        _notification                      = Notification::None;
        _bGateLocked                       = SW_FALSE;
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        if ( pBinder != nullptr && _viewModel != nullptr && getOwner() != nullptr )
            (void)pBinder->bindHUDViewModel( *getOwner(), nullptr ); // 뷰모델은 이 컴포넌트와 함께 사라진다 — HUD 에서 먼저 뗀다
        _viewModel.reset();
        Component::onEndPlay();
    }

    void ActionRoomHUDComponent::onGateStateChanged( const ClearGateStateChangedEvent& event )
    {
        _bGateLocked = event._bLocked;
    }

    void ActionRoomHUDComponent::onRoomCleared( const RoomClearedEvent& event )
    {
        showNotification( Notification::Cleared, event._bBossDefeated ? _clearShake * _bossShakeScale : _clearShake );
    }

    void ActionRoomHUDComponent::onPlayerDefeated( const PlayerDefeatedInRoomEvent& event )
    {
        (void)event;
        showNotification( Notification::Defeated, _defeatShake );
    }

    void ActionRoomHUDComponent::onDamageApplied( const DamageAppliedEvent& event )
    {
        GameObjectManager* const    pManager = getOwner() != nullptr ? getOwner()->getMutableManager() : nullptr;
        const GameObject* const     pTarget  = pManager != nullptr ? pManager->resolveGameObject( event._target ) : nullptr;
        const SceneComponent* const pRoot    = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr || _damageShake <= 0.0f )
            return;
        CameraShakeRequest shake;
        shake._origin         = pRoot->getWorldPosition();
        shake._sourceObjectID = pTarget->getObjectID();
        shake._amplitude      = _damageShake;
        shake._duration       = _shakeSeconds;
        shake._falloffRadius  = _damageShakeRadius;
        pManager->getCameraRegistry().getShakeChannel().broadcast( shake );
    }

    void ActionRoomHUDComponent::showNotification( Notification notification, float32 shakeAmplitude )
    {
        _notification                     = notification;
        _notificationLeft                 = _notificationSeconds;
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager == nullptr || shakeAmplitude <= 0.0f )
            return;
        const SceneComponent* const pRoot = pOwner->getPrimarySceneComponent();
        CameraShakeRequest          shake; // 거리 감쇠는 없다 — 룸의 일은 어디서 보든 같다
        shake._origin         = pRoot != nullptr ? pRoot->getWorldPosition() : float3{};
        shake._sourceObjectID = pOwner->getObjectID();
        shake._amplitude      = shakeAmplitude;
        shake._duration       = _shakeSeconds;
        pManager->getCameraRegistry().getShakeChannel().broadcast( shake );
    }

    void ActionRoomHUDComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _notificationLeft -= context.getDeltaTime().count();
        if ( _notificationLeft <= 0.0f )
            _notification = Notification::None;
        context.deferPostTickOnComponent( *this, []( ActionRoomHUDComponent& hud )
        { hud.updateHUD(); } );
    }

    void ActionRoomHUDComponent::updateHUD()
    {
        if ( _viewModel == nullptr )
            _viewModel = make_unique<ActionRoomHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        if ( pBinder != nullptr && getOwner() != nullptr )
            (void)pBinder->bindHUDViewModel( *getOwner(), _viewModel.get() );
        _viewModel->setGateLockedShown( _bGateLocked != SW_FALSE );
        _viewModel->setClearedShown( _notification == Notification::Cleared );
        _viewModel->setDefeatedShown( _notification == Notification::Defeated );
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( actionRoomHUDNotification, "ActionCombat.HUDNotification", "Room notification on the HUD: 0 none, 1 cleared, 2 defeated", ActionRoomHUDComponent, &ActionRoomHUDComponent::getNotificationValue );
    SW_AUTOMATION_GETTER_WATCH( actionRoomHUDGate, "ActionCombat.HUDGateLocked", "1 while the HUD shows the locked clear gate", ActionRoomHUDComponent, &ActionRoomHUDComponent::getGateLockedValue );
} // namespace sw
