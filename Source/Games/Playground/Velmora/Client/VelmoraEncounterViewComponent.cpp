#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatRoomLook.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"

#include "Games/Playground/Velmora/VelmoraEncounterGameModeComponent.h"

namespace sw
{
    class GameObjectManager;
    class VelmoraEncounterGameModeComponent;

    /**
     * @class VelmoraEncounterViewComponent
     * @brief 공격 · 대시 누름을 프레임마다 전투 입력 명령으로 넘기고 열린 룸을 그립니다.
     */
    REFLECT( Category = "Velmora", DisplayName = "Velmora Encounter View",
             Tooltip = "Client side of the Velmora region fights: sends attack and dash to the Velmora Encounter Game Mode on the same object and draws its room" )
    class VelmoraEncounterViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        VelmoraEncounterViewComponent();
        virtual ~VelmoraEncounterViewComponent() override;

        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 룸을 그립니다. */
        void updateView( GameObjectManager& manager, VelmoraEncounterGameModeComponent& director );
        void submitInput( const GameModeInput& input, VelmoraEncounterGameModeComponent& director );
        /** @brief 싸움이 열려 있으면 룸의 적 · 탄과 무적인 플레이어를 감싸는 공을 보입니다. 룸의 자리는 절대 자리다. */
        void showRoom( const VelmoraEncounterGameModeComponent& encounter, GameObjectManager& manager );

    private:
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that strikes (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Dash Action", Tooltip = "InputMap action that dashes (required)" )
        hashed_string _dashAction;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to the actors, projectiles and the invulnerable player" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Room Look" )
        ActionCombatRoomLook _roomLook;
        PROPERTY( Category = "View", DisplayName = "Invulnerable Radius", Tooltip = "Sphere around the invulnerable player; a little larger than the player sphere of the kit", Min = 0.01, Units = m )
        float32 _invulnerableRadius;
        PROPERTY( Category = "View", DisplayName = "Invulnerable Height", Tooltip = "Height of that sphere's center above the floor", Units = m )
        float32 _invulnerableHeight;
        PROPERTY( Category = "View", DisplayName = "Invulnerable Color" )
        float4 _invulnerableColor;

        SW_TRANSIENT PrefabViewBatch _sphereViews;
    };

    VelmoraEncounterViewComponent::VelmoraEncounterViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&VelmoraEncounterViewComponent::updateView, &VelmoraEncounterViewComponent::submitInput>() )
        , _attackAction{}
        , _dashAction{}
        , _spherePrefab{}
        , _roomLook{}
        , _invulnerableRadius{ 0.34f }
        , _invulnerableHeight{ 0.5f }
        , _invulnerableColor{ 0.6f, 0.9f, 1.0f, 1.0f }
        , _sphereViews{}
    {
    }

    VelmoraEncounterViewComponent::~VelmoraEncounterViewComponent() = default;

    void VelmoraEncounterViewComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _sphereViews.destroyAll( *pManager );
        GameModeViewComponent::onEndPlay();
    }

    void VelmoraEncounterViewComponent::updateView( GameObjectManager& manager, VelmoraEncounterGameModeComponent& director )
    {
        dispatchInput( manager, director );
        showRoom( director, manager );
    }

    void VelmoraEncounterViewComponent::submitInput( const GameModeInput& input, VelmoraEncounterGameModeComponent& director )
    {
        (void)director.submitCombatInput( input.wasActionTriggered( _attackAction ), input.wasActionTriggered( _dashAction ) ); // 열린 판인지는 위에서 보았다
    }

    void VelmoraEncounterViewComponent::showRoom( const VelmoraEncounterGameModeComponent& encounter, GameObjectManager& manager )
    {
        _sphereViews.begin();
        if ( encounter.isFighting() )
        {
            _roomLook.forEachSphere( encounter.getRoom(), [this, &manager]( const AbsolutePosition& position, float32 radius, const float4& color )
            { (void)_sphereViews.show( manager, _spherePrefab, position, float3{ radius * 2.0f }, 0.0f, color ); } );
            const AdventureGameModeComponent* const pDirector = encounter.getRoom().isPlayerInvulnerable() ? PlayerComponentUtil::findOnOwner<AdventureGameModeComponent>( *this ) : nullptr;
            if ( pDirector != nullptr )
                (void)_sphereViews.show( manager, _spherePrefab, ActionCombatRoomLook::makePosition( pDirector->getPlayerPosition(), _invulnerableHeight ), float3{ _invulnerableRadius * 2.0f }, 0.0f,
                                         _invulnerableColor );
        }
        _sphereViews.end( manager );
    }
} // namespace sw

#include "VelmoraEncounterViewComponent.cpp.gen.inl"
