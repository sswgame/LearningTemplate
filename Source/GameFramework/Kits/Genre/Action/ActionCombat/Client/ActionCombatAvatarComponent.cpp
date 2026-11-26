#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatRoomLook.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/ActionCombatGameModeComponent.h"
#include "GameFramework/Runtime/Character/Combat/Health/RuleHealthViewComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    class ActionCombatGameModeComponent;
    class GameObjectManager;

    /**
     * @class ActionCombatAvatarComponent
     * @brief `Move` 축 · 공격 · 대시 액션을 프레임마다 디렉터 입력 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "ActionCombat", DisplayName = "Action Combat Avatar",
             Tooltip = "Client side of an ActionCombat run: sends input commands to the Action Combat Game Mode on the same object, draws the room and player and feeds the HP bar" )
    class ActionCombatAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        ActionCombatAvatarComponent();
        virtual ~ActionCombatAvatarComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HP 바 · 그리기를 합니다. */
        void updateAvatar( GameObjectManager& manager, ActionCombatGameModeComponent& director );
        void submitInput( const GameModeInput& input, ActionCombatGameModeComponent& director );
        /** @brief 지금 플레이어 HP 를 뷰 오브젝트의 체력 뷰에 넣습니다. 칸이 비었으면 할 일이 없고, 가리킨 오브젝트에 체력 뷰가 없으면 한 번 오류를 남깁니다. */
        void pushPlayerHealthView( GameObjectManager& manager, const ActionCombatGameModeComponent& director );
        /** @brief 룸과 플레이어를 보입니다. 룸의 자리는 절대 자리다. */
        void showRoom( const ActionCombatGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the player (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that strikes (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Dash Action", Tooltip = "InputMap action that dashes (required)" )
        hashed_string _dashAction;
        PROPERTY( Category = "View", DisplayName = "Player View", Tooltip = "Object whose RuleHealthViewComponent shows the player's rule HP (health bar); empty = no bar" )
        GameObjectHandle _playerView;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to the actors, projectiles and the player" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Room Look" )
        ActionCombatRoomLook _roomLook;
        PROPERTY( Category = "View", DisplayName = "Player Radius", Min = 0.01, Units = m )
        float32 _playerRadius;
        PROPERTY( Category = "View", DisplayName = "Player Height", Tooltip = "Height of the player's center above the floor", Units = m )
        float32 _playerHeight;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Invulnerable Color", Tooltip = "Player color while a dash makes it invulnerable" )
        float4 _invulnerableColor;

        SW_TRANSIENT PrefabViewBatch _sphereViews;
        SW_TRANSIENT uint8           _bViewReset         : 1; ///< 이 판에서 HP 바 스냅숏을 연출 없이 다시 두었다
        SW_TRANSIENT uint8           _bPlayerViewMissing : 1; ///< 가리킨 뷰 오브젝트에 체력 뷰가 없다고 이미 알렸다(매 프레임 되풀이하지 않는다)
        [[maybe_unused]] uint8       _reserved           : 6;
    };

    ActionCombatAvatarComponent::ActionCombatAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&ActionCombatAvatarComponent::updateAvatar, &ActionCombatAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _attackAction{}
        , _dashAction{}
        , _playerView{}
        , _spherePrefab{}
        , _roomLook{}
        , _playerRadius{ 0.28f }
        , _playerHeight{ 0.5f }
        , _playerColor{ 0.2f, 0.6f, 1.0f, 1.0f }
        , _invulnerableColor{ 0.6f, 0.9f, 1.0f, 1.0f }
        , _sphereViews{}
        , _bViewReset{ SW_FALSE }
        , _bPlayerViewMissing{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ActionCombatAvatarComponent::~ActionCombatAvatarComponent() = default;

    void ActionCombatAvatarComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _bViewReset         = SW_FALSE;
        _bPlayerViewMissing = SW_FALSE;
    }

    void ActionCombatAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _sphereViews.destroyAll( *pManager );
        GameModeViewComponent::onEndPlay();
    }

    void ActionCombatAvatarComponent::updateAvatar( GameObjectManager& manager, ActionCombatGameModeComponent& director )
    {
        dispatchInput( manager, director );
        pushPlayerHealthView( manager, director ); // 시간이 멈춘 프레임 · 복원 직후에도 넣는다
        showRoom( director, manager );
    }

    void ActionCombatAvatarComponent::submitInput( const GameModeInput& input, ActionCombatGameModeComponent& director )
    {
        // 판이 끝났으면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        (void)director.submitPlayerInput( input.getVector2D( _moveAction ), input.wasActionTriggered( _attackAction ), input.wasActionTriggered( _dashAction ) );
    }

    void ActionCombatAvatarComponent::pushPlayerHealthView( GameObjectManager& manager, const ActionCombatGameModeComponent& director )
    {
        if ( _playerView.isValid() == false )
            return;
        GameObject* const              pViewObject = manager.resolveGameObject( _playerView );
        RuleHealthViewComponent* const pView       = pViewObject != nullptr ? pViewObject->getComponent<RuleHealthViewComponent>() : nullptr;
        if ( pView == nullptr )
        {
            if ( _bPlayerViewMissing == SW_FALSE )
                SW_LOG_ERROR_ID( "ActionCombat.PlayerViewMissing", "Action combat avatar: the Player View object has no RuleHealthViewComponent - the player HP bar does not move" );
            _bPlayerViewMissing = SW_TRUE;
            return;
        }
        if ( _bViewReset == SW_FALSE )
        {
            pView->resetSnapshot(); // 새 판의 HP 는 앞 판에서 이어지지 않는다 — 연출 없이 다시 둔다
            _bViewReset = SW_TRUE;
        }
        pView->applySnapshot( static_cast<float32>( director.getPlayerHP() ), static_cast<float32>( director.getPlayerMaxHP() ) );
    }

    void ActionCombatAvatarComponent::showRoom( const ActionCombatGameModeComponent& director, GameObjectManager& manager )
    {
        const ActionCombatRoom& room  = director.getRoom();
        const ActionCombatPhase phase = director.getCombatPhase();
        _sphereViews.begin();
        (void)_sphereViews.show( manager, _spherePrefab, ActionCombatRoomLook::makePosition( director.getPlayerPosition(), _playerHeight ), float3{ _playerRadius * 2.0f }, 0.0f,
                                 room.isPlayerInvulnerable() ? _invulnerableColor : _playerColor );
        if ( phase == ActionCombatPhase::Hall || phase == ActionCombatPhase::Boss )
        {
            _roomLook.forEachSphere( room, [this, &manager]( const AbsolutePosition& position, float32 radius, const float4& color )
            { (void)_sphereViews.show( manager, _spherePrefab, position, float3{ radius * 2.0f }, 0.0f, color ); } );
        }
        _sphereViews.end( manager );
    }
} // namespace sw

#include "ActionCombatAvatarComponent.cpp.gen.inl"
