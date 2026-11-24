/**
 * @file ActionRoomHUDComponent.h
 * @brief 액션 룸 이벤트(`ActionCombatEvents.h`)를 받아 HUD 알림과 카메라 흔들림으로 보입니다 — 룸을 든 디렉터가 무엇이든(키트의 디렉터 · 게임의 전투) 같은 이벤트 하나로 이어진다.
 * @details 같은 오브젝트의 HUD(`HUDControllerComponent` — `actionroom.ui.xml`)에 뷰모델을 건다. 문이 잠겨 있는 동안 잠김 표시를, 방을 비우거나 쓰러지면 알림을 `_notificationSeconds` 동안 보인다.
 *          카메라는 씬의 흔들림 채널(`CameraShakeChannel`)로 흔든다 — 카메라 매니저 · 디렉터가 없는 씬이면 듣는 쪽이 없다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ClearGateStateChangedEvent;
    struct PlayerDefeatedInRoomEvent;
    struct RoomClearedEvent;

    class ActionRoomHUDViewModel;
    class GameObjectManager;

    /** @class ActionRoomHUDComponent @brief 파일 머리말 참고. 이벤트는 게임 스레드에서 받고 HUD 는 틱 뒤 게임 스레드에서 쓴다. */
    REFLECT( TickMode = Main, Category = "ActionCombat", DisplayName = "Action Room HUD", Tooltip = "Shows room events (gate locked, cleared, defeated) on the HUD of this object and shakes the camera" )
    class SW_KIT_CLIENT_API ActionRoomHUDComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 지금 보이는 알림입니다(시나리오 감시 값 `ActionCombat.HUDNotification`). */
        enum class Notification : uint8
        {
            None = 0,
            Cleared,
            Defeated
        };

        ActionRoomHUDComponent();
        ~ActionRoomHUDComponent() override;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        int32 getNotificationValue() const { return static_cast<int32>( _notification ); }
        int32 getGateLockedValue() const { return _bGateLocked != SW_FALSE ? 1 : 0; }
        /** @brief 걸어 둔 뷰모델입니다. 시작하지 않았으면 nullptr 입니다. */
        const ActionRoomHUDViewModel* findViewModel() const { return _viewModel.get(); }

    private:
        void onGateStateChanged( const ClearGateStateChangedEvent& event );
        void onRoomCleared( const RoomClearedEvent& event );
        void onPlayerDefeated( const PlayerDefeatedInRoomEvent& event );
        void showNotification( Notification notification, float32 shakeAmplitude );
        void updateHUD();

    private:
        PROPERTY( Category = "Notification", DisplayName = "Notification Seconds", Min = 0.1, Units = s, Tooltip = "How long the cleared and defeated notifications stay" )
        float32 _notificationSeconds;
        PROPERTY( Category = "Camera", DisplayName = "Clear Shake", Min = 0.0, Units = m, Tooltip = "Camera shake amplitude when the room is cleared; 0 = none" )
        float32 _clearShake;
        PROPERTY( Category = "Camera", DisplayName = "Boss Shake Scale", Min = 0.0, Tooltip = "Clear Shake is multiplied by this when a boss fell" )
        float32 _bossShakeScale;
        PROPERTY( Category = "Camera", DisplayName = "Defeat Shake", Min = 0.0, Units = m, Tooltip = "Camera shake amplitude when the player is defeated; 0 = none" )
        float32 _defeatShake;
        PROPERTY( Category = "Camera", DisplayName = "Shake Seconds", Min = 0.01, Units = s )
        float32 _shakeSeconds;

        unique_ptr<ActionRoomHUDViewModel> _viewModel;
        Subscription                       _gateSubscription;
        Subscription                       _clearedSubscription;
        Subscription                       _defeatedSubscription;
        float32                            _notificationLeft;
        Notification                       _notification;
        uint8                              _bGateLocked;
    };
} // namespace sw
