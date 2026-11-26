#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Client/MonsterCollectorHUDViewModel.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Component/MonsterCollectorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    class GameInputView;
    class GameObjectManager;
    class MonsterCollectorGameModeComponent;
    class MonsterCollectorHUDViewModel;

    /**
     * @class MonsterCollectorAvatarComponent
     * @brief 걷는 동안은 `Move` · 쉬기, 전투 중에는 기술 고르기 · 기술 · 볼 · 교체 · 도망을 디렉터 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "MonsterCollector", DisplayName = "Monster Collector Avatar",
             Tooltip = "Client side of a MonsterCollector outing: sends commands to the Monster Collector Game Mode on the same object, draws the glade and the foe and fills the HUD" )
    class MonsterCollectorAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        MonsterCollectorAvatarComponent();
        virtual ~MonsterCollectorAvatarComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HUD · 그리기를 합니다. */
        void updateAvatar( GameObjectManager& manager, MonsterCollectorGameModeComponent& director );
        void submitInput( const GameModeInput& input, MonsterCollectorGameModeComponent& director );
        void submitExploreInput( MonsterCollectorGameModeComponent& director, const GameInputView& input );
        void submitBattleInput( MonsterCollectorGameModeComponent& director, const GameInputView& input );
        /** @brief 고른 기술을 나선 개체의 빈 슬롯이 아닌 것으로 맞추고, `Move` 축을 한 번 기울였으면 다음 기술로 옮깁니다. */
        void updateMoveSlot( const MonsterCollectorGameModeComponent& director, float32 axis );
        void updateHUD( const MonsterCollectorGameModeComponent& director );
        /** @brief 쉼터 · 플레이어 · 상대를 그립니다. */
        void draw( const MonsterCollectorGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the player and cycles the battle move (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that uses the chosen move (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Ball Action", Tooltip = "InputMap action that throws a ball (required)" )
        hashed_string _ballAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Switch Action", Tooltip = "InputMap action that brings in the next usable monster (required)" )
        hashed_string _switchAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Run Action", Tooltip = "InputMap action that runs from the battle (required)" )
        hashed_string _runAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Rest Action", Tooltip = "InputMap action that rests at the camp (required)" )
        hashed_string _restAction;

        PROPERTY( Required, Category = "View", DisplayName = "Body Prefab", Tooltip = "Prefab with one mesh, feet at its origin and one metre tall, spawned for every body the view shows" )
        AssetRef<PrefabAsset> _bodyPrefab;
        PROPERTY( Required, Category = "View", DisplayName = "Marker Prefab", Tooltip = "One-metre cube prefab, spawned for every marker the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        SW_TRANSIENT PrefabViewBatch _bodies;
        SW_TRANSIENT PrefabViewBatch _markers;
        SW_TRANSIENT unique_ptr<MonsterCollectorHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        SW_TRANSIENT int32                                    _moveSlot;     ///< 고른 기술 슬롯
        SW_TRANSIENT int32                                    _lastStep;     ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 기술을 고른다
    };

    namespace
    {
        /** @brief 모습의 색 · 크기(m)와 입력 문턱입니다. */
        struct MonsterCollectorAvatarComponentInternal
        {
            static constexpr float4  kPlayerColor   = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float4  kLeadColor     = float4( 0.35f, 0.85f, 0.4f, 1.0f );
            static constexpr float4  kFoeColor      = float4( 1.0f, 0.5f, 0.2f, 1.0f );
            static constexpr float4  kRestColor     = float4( 0.95f, 0.6f, 0.15f, 1.0f );
            static constexpr float3  kRestSize      = float3( 1.2f, 0.3f, 1.2f );
            static constexpr float3  kLeadOffset    = float3( 1.0f, 0.0f, 0.6f );
            static constexpr float3  kFoeOffset     = float3( 3.0f, 0.0f, 0.0f );
            static constexpr float3  kPlayerScale   = float3( 1.2f, 1.2f, 1.2f );
            static constexpr float3  kMonsterScale  = float3( 1.4f, 0.8f, 1.4f );
            static constexpr float32 kMoveThreshold = 0.5f; ///< 축이 이보다 기울어야 고른 기술을 옮긴다(스틱 떨림을 고르기로 읽지 않게)

            /** @brief 축 값의 방향(-1 · 0 · 1)입니다. */
            static int32 computeStep( float32 axis )
            {
                if ( axis > kMoveThreshold )
                    return 1;
                return axis < -kMoveThreshold ? -1 : 0;
            }

            /** @brief 종의 이름(현지화 키)입니다. 카탈로그에 없으면 비어 있습니다. */
            static string_view findSpeciesName( const MonsterCollectorCatalog& catalog, const MonsterInstance& monster )
            {
                const MonsterSpeciesDef* const pSpecies = catalog.findSpecies( monster._speciesID );
                return pSpecies != nullptr ? string_view{ pSpecies->_name } : string_view{};
            }
        };
    } // namespace

    MonsterCollectorAvatarComponent::MonsterCollectorAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&MonsterCollectorAvatarComponent::updateAvatar, &MonsterCollectorAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _attackAction{}
        , _ballAction{}
        , _switchAction{}
        , _runAction{}
        , _restAction{}
        , _bodyPrefab{}
        , _markerPrefab{}
        , _bodies{}
        , _markers{}
        , _hudViewModel{}
        , _moveSlot{ 0 }
        , _lastStep{ 0 }
    {
    }

    MonsterCollectorAvatarComponent::~MonsterCollectorAvatarComponent() = default;

    void MonsterCollectorAvatarComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _moveSlot = 0;
        _lastStep = 0;
    }

    void MonsterCollectorAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _bodies.destroyAll( *pManager );
            _markers.destroyAll( *pManager );
        }
        GameModeViewComponent::onEndPlay();
    }

    void MonsterCollectorAvatarComponent::updateAvatar( GameObjectManager& manager, MonsterCollectorGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updateHUD( director );
        _bodies.begin();
        _markers.begin();
        draw( director, manager );
        _bodies.end( manager );
        _markers.end( manager );
    }

    void MonsterCollectorAvatarComponent::submitInput( const GameModeInput& input, MonsterCollectorGameModeComponent& director )
    {
        if ( director.getGamePhase() == MonsterCollectorPhase::Explore )
            submitExploreInput( director, input );
        else if ( director.getGamePhase() == MonsterCollectorPhase::Battle )
            submitBattleInput( director, input );
    }

    void MonsterCollectorAvatarComponent::submitExploreInput( MonsterCollectorGameModeComponent& director, const GameInputView& input )
    {
        // 쉼터 밖은 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        const float2 move = input.getVector2D( _moveAction );
        _lastStep         = MonsterCollectorAvatarComponentInternal::computeStep( move._x ); // 걷던 축을 누른 채 전투가 열려도 기술을 고른 것으로 읽지 않는다
        (void)director.submitMove( move );
        if ( input.wasActionTriggered( _restAction ) )
            (void)director.submitRest();
    }

    void MonsterCollectorAvatarComponent::updateMoveSlot( const MonsterCollectorGameModeComponent& director, float32 axis )
    {
        const MonsterInstance* const pLead = director.findLead();
        const int32                  step  = MonsterCollectorAvatarComponentInternal::computeStep( axis );
        const int32                  fresh = step != _lastStep ? step : 0;
        _lastStep                          = step;
        const int32 moveCount              = pLead != nullptr ? pLead->countMoves() : 0; // 슬롯은 앞에서부터 찬다
        if ( moveCount <= 0 )
            return;
        _moveSlot = ( ( _moveSlot + fresh ) % moveCount + moveCount ) % moveCount;
    }

    void MonsterCollectorAvatarComponent::submitBattleInput( MonsterCollectorGameModeComponent& director, const GameInputView& input )
    {
        updateMoveSlot( director, input.getVector2D( _moveAction )._x );
        // 쓸 수 없는 명령(빈 볼 · 교체 대기 중의 기술)은 디렉터가 거절하고 판은 그대로다.
        if ( input.wasActionTriggered( _attackAction ) )
            (void)director.submitAttack( _moveSlot );
        else if ( input.wasActionTriggered( _ballAction ) )
            (void)director.submitBall();
        else if ( input.wasActionTriggered( _runAction ) )
            (void)director.submitRun();
        else if ( input.wasActionTriggered( _switchAction ) )
        {
            const int32 active    = director.getBattle().getActiveIndex( MonsterBattle::kPlayerSide );
            const int32 partySize = static_cast<int32>( director.getBattle().getParty( MonsterBattle::kPlayerSide ).size() );
            for ( int32 offset = 1; offset < partySize; ++offset )
            {
                if ( director.submitSwitch( ( active + offset ) % partySize ) )
                    break;
            }
        }
    }

    void MonsterCollectorAvatarComponent::updateHUD( const MonsterCollectorGameModeComponent& director )
    {
        using Internal                                = MonsterCollectorAvatarComponentInternal;
        GameObject* const                    pOwner   = getOwner();
        const MonsterCollectorCatalog* const pCatalog = director.findCatalog();
        const MonsterInstance* const         pLead    = director.findLead();
        if ( pOwner == nullptr || pCatalog == nullptr || pLead == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<MonsterCollectorHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const bool bBattle = director.getGamePhase() == MonsterCollectorPhase::Battle;
        _hudViewModel->setPhase( director.getGamePhase(), director.isAwaitingSwitch() );
        _hudViewModel->setLead( Internal::findSpeciesName( *pCatalog, *pLead ), pLead->_level, pLead->_hp, pLead->getMaxHP() );
        _hudViewModel->setCapture( director.getRemainingBallCount(), director.getCaptureCount(), director.getCaptureGoal() );
        _hudViewModel->setRestShown( director.getGamePhase() == MonsterCollectorPhase::Explore && director.isNearRest() );
        if ( bBattle == false )
            return;
        const MonsterInstance& foe = director.getBattle().getActive( MonsterBattle::kFoeSide );
        _hudViewModel->setFoe( Internal::findSpeciesName( *pCatalog, foe ), foe._level, foe._hp );
        const MonsterMoveDef* const pMove = pCatalog->findMove( pLead->_arrMove[_moveSlot]._moveID );
        _hudViewModel->setMoveName( pMove != nullptr ? string_view{ pMove->_name } : string_view{} );
    }

    void MonsterCollectorAvatarComponent::draw( const MonsterCollectorGameModeComponent& director, GameObjectManager& manager )
    {
        using Internal = MonsterCollectorAvatarComponentInternal;
        _markers.showStanding( manager, _markerPrefab, director.getRestPosition(), Internal::kRestSize, Internal::kRestColor );
        const AbsolutePosition player = director.getPlayerPosition();
        (void)_bodies.show( manager, _bodyPrefab, player, Internal::kPlayerScale, 0.0f, Internal::kPlayerColor ); // 실패는 배치가 한 번 알린다
        if ( director.getGamePhase() != MonsterCollectorPhase::Battle )
            return;
        (void)_bodies.show( manager, _bodyPrefab, player + Internal::kLeadOffset, Internal::kMonsterScale, 0.0f, Internal::kLeadColor );
        (void)_bodies.show( manager, _bodyPrefab, player + Internal::kFoeOffset, Internal::kMonsterScale, 0.0f, Internal::kFoeColor );
    }
} // namespace sw

#include "MonsterCollectorAvatarComponent.cpp.gen.inl"
