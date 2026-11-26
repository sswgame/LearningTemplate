#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/Client/PartyHUDViewModel.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/Component/PartyGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    class GameObjectManager;
    class PartyGameModeComponent;
    class PartyHUDViewModel;

    /**
     * @class PartyArenaViewComponent
     * @brief `Move` 축은 공중 이동, 점프 · 공격 · 찍기 액션은 눌린 순간을 디렉터에 넘깁니다.
     */
    REFLECT( Category = "PartyArena", DisplayName = "Party Arena View", Tooltip = "Client side of a party arena: moves the local player from input, shows the arena, players and items and fills the HUD" )
    class PartyArenaViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        PartyArenaViewComponent();
        virtual ~PartyArenaViewComponent() override;

        /** @brief 세운 모습을 지우고 HUD 에서 뷰모델을 뗍니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 내고 모습과 HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, PartyGameModeComponent& director );
        /** @brief 이번 프레임의 조작 입력을 디렉터에 냅니다. */
        void submitInput( const GameModeInput& input, PartyGameModeComponent& director ) const;
        /** @brief 무대 · 사람 · 아이템을 지금 자리에 보입니다. */
        void showArena( GameObjectManager& manager, const PartyGameModeComponent& director );
        /** @brief HUD 뷰모델을 채웁니다. HUD 가 없는 씬이면 값만 듭니다. */
        void updateHUD( const PartyGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that moves the player in the air (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Jump Action", Tooltip = "InputMap action timed with the landing to bounce higher (required)" )
        hashed_string _jumpAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that dashes into a rival (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Pound Action", Tooltip = "InputMap action that pounds the trampoline (required)" )
        hashed_string _poundAction;
        PROPERTY( Category = "View", DisplayName = "Floor Prefab", Required, Tooltip = "Unit disc stretched to the arena radius" )
        AssetRef<PrefabAsset> _floorPrefab;
        PROPERTY( Category = "View", DisplayName = "Player Prefab", Required )
        AssetRef<PrefabAsset> _playerPrefab;
        PROPERTY( Category = "View", DisplayName = "Item Prefab", Required )
        AssetRef<PrefabAsset> _itemPrefab;
        PROPERTY( Category = "View", DisplayName = "Floor Color" )
        float4 _floorColor;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Opponent Color" )
        float4 _opponentColor;
        PROPERTY( Category = "View", DisplayName = "Item Color" )
        float4 _itemColor;

        SW_TRANSIENT PrefabViewBatch _floorViews;
        SW_TRANSIENT PrefabViewBatch _playerViews;
        SW_TRANSIENT PrefabViewBatch _itemViews;
        SW_TRANSIENT unique_ptr<PartyHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
    };

    namespace
    {
        struct PartyArenaViewComponentInternal
        {
            static constexpr float32 kFloorThickness = 0.2f; ///< 무대 원판의 두께(m) — 윗면이 높이 0 에 온다
        };
    } // namespace

    PartyArenaViewComponent::PartyArenaViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&PartyArenaViewComponent::updateView, &PartyArenaViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _jumpAction{}
        , _attackAction{}
        , _poundAction{}
        , _floorPrefab{}
        , _playerPrefab{}
        , _itemPrefab{}
        , _floorColor{ 0.3f, 0.55f, 0.85f, 1.0f }
        , _playerColor{ 0.25f, 1.0f, 0.45f, 1.0f }
        , _opponentColor{ 1.0f, 0.4f, 0.3f, 1.0f }
        , _itemColor{ 1.0f, 0.85f, 0.2f, 1.0f }
        , _floorViews{}
        , _playerViews{}
        , _itemViews{}
        , _hudViewModel{ nullptr }
    {
        setCanEverTick( true );
    }

    PartyArenaViewComponent::~PartyArenaViewComponent() = default;

    void PartyArenaViewComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _floorViews.destroyAll( *pManager );
            _playerViews.destroyAll( *pManager );
            _itemViews.destroyAll( *pManager );
        }
        _hudViewModel.reset(); // 지워지는 뷰모델은 HUD 가 스스로 뗀다
        GameModeViewComponent::onEndPlay();
    }

    void PartyArenaViewComponent::updateView( GameObjectManager& manager, PartyGameModeComponent& director )
    {
        dispatchInput( manager, director );
        showArena( manager, director );
        updateHUD( director );
    }

    void PartyArenaViewComponent::submitInput( const GameModeInput& input, PartyGameModeComponent& director ) const
    {
        const float2    axis = input.getVector2D( _moveAction );
        TrampolineInput player;
        player._moveX          = axis._x;
        player._moveZ          = axis._y;
        player._bJumpPressed   = input.wasActionTriggered( _jumpAction ) ? SW_TRUE : SW_FALSE;
        player._bAttackPressed = input.wasActionTriggered( _attackAction ) ? SW_TRUE : SW_FALSE;
        player._bPoundPressed  = input.wasActionTriggered( _poundAction ) ? SW_TRUE : SW_FALSE;
        (void)director.submitPlayerInput( player ); // 받는지는 부른 쪽이 보았다
    }

    void PartyArenaViewComponent::showArena( GameObjectManager& manager, const PartyGameModeComponent& director )
    {
        using Internal                  = PartyArenaViewComponentInternal;
        const TrampolineArena& arena    = director.getArena();
        const float32          diameter = arena.getSettings()._arenaRadius * 2.0f;
        const float32          body     = arena.getSettings()._bodyRadius * 2.0f;
        _floorViews.begin();
        (void)_floorViews.show( manager, _floorPrefab, AbsolutePosition{
                                                           float3{ 0.0f, -Internal::kFloorThickness * 0.5f, 0.0f }
        },
                                float3{ diameter, Internal::kFloorThickness, diameter }, 0.0f, _floorColor );
        _floorViews.end( manager );

        _playerViews.begin();
        for ( int32 player = 0; player < arena.getPlayerCount(); ++player )
        {
            const TrampolinePlayer& person = *arena.findPlayer( player );
            if ( person._state == TrampolinePlayerState::Respawning )
                continue;
            const float32 yaw = MathUtil::atan2( person._facing._x, person._facing._z );
            (void)_playerViews.show( manager, _playerPrefab, director.getPlayerPosition( player ), float3{ body, body, body }, yaw, player == PartyGameModeComponent::kLocalPlayer ? _playerColor : _opponentColor );
        }
        _playerViews.end( manager );

        // 아이템 자리는 무대 좌표다 — 무대 가운데가 절대 원점이라 그대로 절대 자리다(`PartyGameModeComponent`).
        _itemViews.begin();
        for ( const PartyItemInstance& item : director.getArena().getItemSpawner().getInstances() )
        {
            (void)_itemViews.show( manager, _itemPrefab, AbsolutePosition{ item._position }, float3{ 1.0f, 1.0f, 1.0f }, 0.0f, _itemColor );
        }
        _itemViews.end( manager );
    }

    void PartyArenaViewComponent::updateHUD( const PartyGameModeComponent& director )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<PartyHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const TrampolinePlayer* const pPlayer = director.getArena().findPlayer( PartyGameModeComponent::kLocalPlayer );
        const MatchState&             match   = director.getArena().getMatch();
        const bool                    bOver   = director.getGamePhase() == PartyGamePhase::Finished;
        _hudViewModel->setRound( director.getRound() + 1 );
        _hudViewModel->setTimeLeft( static_cast<int32>( MathUtil::ceil( match.getRemaining() ) ) );
        _hudViewModel->setRoundScore( director.getPlayerRoundScore() );
        _hudViewModel->setTotal( director.getPlayerTotal() );
        _hudViewModel->setWinScore( director.getSeries().getWinScore() );
        _hudViewModel->setCombo( pPlayer != nullptr ? pPlayer->_combo : 0 );
        _hudViewModel->setWonShown( bOver && director.getWinner() == PartyGameModeComponent::kLocalPlayer );
        _hudViewModel->setLostShown( bOver && director.getWinner() != PartyGameModeComponent::kLocalPlayer );
    }
} // namespace sw

#include "PartyArenaViewComponent.cpp.gen.inl"
