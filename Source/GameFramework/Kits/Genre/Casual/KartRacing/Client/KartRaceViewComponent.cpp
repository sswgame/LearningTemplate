#include "pch.h"

#include "Core/Container/vector.h"
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
#include "GameFramework/Kits/Genre/Casual/KartRacing/Client/KartHUDViewModel.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/Component/KartGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    class GameObjectManager;
    class KartGameModeComponent;
    class KartHUDViewModel;

    /**
     * @class KartRaceViewComponent
     * @brief `Move` 축의 앞뒤는 페달, 좌우는 조향이고, 드리프트 · 부스트 · 아이템 액션을 더해 한 프레임의 운전 입력으로 디렉터에 넘깁니다.
     */
    REFLECT( Category = "KartRacing", DisplayName = "Kart Race View", Tooltip = "Client side of a kart race: drives the player kart from input, shows the track, karts and items and fills the HUD" )
    class KartRaceViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        KartRaceViewComponent();
        virtual ~KartRaceViewComponent() override;

        /** @brief 세운 모습을 지우고 HUD 에서 뷰모델을 뗍니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 내고 모습과 HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, KartGameModeComponent& director );
        /** @brief 이번 프레임의 운전 입력을 디렉터에 냅니다. */
        void submitInput( const GameModeInput& input, KartGameModeComponent& director ) const;
        /** @brief 트랙 조각 · 결승선을 보입니다. */
        void showTrack( GameObjectManager& manager, const KartGameModeComponent& director );
        /** @brief 차 · 아이템 상자 · 날아가는 아이템을 지금 자리에 보입니다. */
        void showRace( GameObjectManager& manager, const KartGameModeComponent& director );
        /** @brief HUD 뷰모델을 채웁니다. HUD 가 없는 씬이면 값만 듭니다. */
        void updateHUD( const KartGameModeComponent& director );
        /** @brief 세운 모습을 모두 지웁니다. */
        void destroyViews();

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action: up/down pedal, left/right steering (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Drift Action", Tooltip = "InputMap action held to drift (required)" )
        hashed_string _driftAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Boost Action", Tooltip = "InputMap action that fires a nitro (required)" )
        hashed_string _boostAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Item Action", Tooltip = "InputMap action that uses the held item (required)" )
        hashed_string _itemAction;
        PROPERTY( Category = "View", DisplayName = "Track Prefab", Required, Tooltip = "Unit piece stretched along the track and across the finish line" )
        AssetRef<PrefabAsset> _trackPrefab;
        PROPERTY( Category = "View", DisplayName = "Kart Prefab", Required )
        AssetRef<PrefabAsset> _kartPrefab;
        PROPERTY( Category = "View", DisplayName = "Item Prefab", Required, Tooltip = "Item boxes and flying items" )
        AssetRef<PrefabAsset> _itemPrefab;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Opponent Color" )
        float4 _opponentColor;
        PROPERTY( Category = "View", DisplayName = "Track Color" )
        float4 _trackColor;
        PROPERTY( Category = "View", DisplayName = "Finish Color" )
        float4 _finishColor;
        PROPERTY( Category = "View", DisplayName = "Item Color" )
        float4 _itemColor;

        SW_TRANSIENT PrefabViewBatch _trackViews;
        SW_TRANSIENT PrefabViewBatch _kartViews;
        SW_TRANSIENT PrefabViewBatch _itemViews;
        SW_TRANSIENT unique_ptr<KartHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
    };

    namespace
    {
        struct KartRaceViewComponentInternal
        {
            static constexpr float32 kTrackThickness    = 0.1f;  ///< 트랙 조각의 두께(m) — 윗면이 길 높이에 온다
            static constexpr float32 kPieceOverlap      = 1.05f; ///< 조각을 이만큼 길게 해 굽은 길의 틈을 메운다
            static constexpr float32 kFinishLength      = 1.0f;  ///< 결승선 띠의 길이(m)
            static constexpr float32 kFinishLift        = 0.06f; ///< 결승선 띠를 길 위로 띄우는 높이(m)
            static constexpr float32 kItemLift          = 0.8f;  ///< 아이템을 길 위로 띄우는 높이(m)
            static constexpr float32 kSpeedDisplayScale = 3.6f;  ///< m/s → km/h
        };
    } // namespace

    KartRaceViewComponent::KartRaceViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&KartRaceViewComponent::updateView, &KartRaceViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _driftAction{}
        , _boostAction{}
        , _itemAction{}
        , _trackPrefab{}
        , _kartPrefab{}
        , _itemPrefab{}
        , _playerColor{ 0.25f, 0.55f, 1.0f, 1.0f }
        , _opponentColor{ 1.0f, 0.35f, 0.25f, 1.0f }
        , _trackColor{ 0.35f, 0.35f, 0.38f, 1.0f }
        , _finishColor{ 0.95f, 0.95f, 0.95f, 1.0f }
        , _itemColor{ 1.0f, 0.85f, 0.2f, 1.0f }
        , _trackViews{}
        , _kartViews{}
        , _itemViews{}
        , _hudViewModel{ nullptr }
    {
        setCanEverTick( true );
    }

    KartRaceViewComponent::~KartRaceViewComponent() = default;

    void KartRaceViewComponent::onEndPlay()
    {
        destroyViews();
        _hudViewModel.reset(); // 지워지는 뷰모델은 HUD 가 스스로 뗀다
        GameModeViewComponent::onEndPlay();
    }

    void KartRaceViewComponent::destroyViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _trackViews.destroyAll( *pManager );
            _kartViews.destroyAll( *pManager );
            _itemViews.destroyAll( *pManager );
        }
    }

    void KartRaceViewComponent::updateView( GameObjectManager& manager, KartGameModeComponent& director )
    {
        dispatchInput( manager, director );
        showTrack( manager, director );
        showRace( manager, director );
        updateHUD( director );
    }

    void KartRaceViewComponent::submitInput( const GameModeInput& input, KartGameModeComponent& director ) const
    {
        const float2   axis = input.getVector2D( _moveAction );
        KartRacerInput driver;
        driver._vehicle._throttle      = axis._y;
        driver._vehicle._steer         = axis._x;
        driver._vehicle._bDriftHeld    = input.isActionDown( _driftAction ) ? SW_TRUE : SW_FALSE;
        driver._vehicle._bBoostPressed = input.wasActionTriggered( _boostAction ) ? SW_TRUE : SW_FALSE;
        driver._bUseItem               = input.wasActionTriggered( _itemAction ) ? SW_TRUE : SW_FALSE;
        (void)director.submitDriverInput( driver ); // 받는지는 부른 쪽이 보았다
    }

    void KartRaceViewComponent::showTrack( GameObjectManager& manager, const KartGameModeComponent& director )
    {
        using Internal                            = KartRaceViewComponentInternal;
        const KartTrack&                 track    = director.getTrack();
        const vector<KartTrack::Sample>& listNode = track.getSamples();
        _trackViews.begin();
        for ( size_t nodeIndex = 0; nodeIndex < listNode.size(); ++nodeIndex )
        {
            const float3& from   = listNode[nodeIndex]._position;
            const float3& to     = listNode[( nodeIndex + 1 ) % listNode.size()]._position;
            const float3  delta  = to - from;
            const float32 length = delta.getLength();
            if ( length <= MathUtil::kEpsilon )
                continue;
            const float3 center = ( from + to ) * 0.5f;
            if ( _trackViews.show( manager, _trackPrefab, AbsolutePosition{
                                                              float3{ center._x, center._y - Internal::kTrackThickness * 0.5f, center._z }
            },
                                   float3{ track.getWidth(), Internal::kTrackThickness, length * Internal::kPieceOverlap }, MathUtil::atan2( delta._x, delta._z ), _trackColor ) == false )
                return; // 까닭은 묶음이 남겼다
        }
        const KartTrackFrame finish = track.sample( 0.0f );
        (void)_trackViews.show( manager, _trackPrefab, AbsolutePosition{
                                                           float3{ finish._position._x, finish._position._y + Internal::kFinishLift - Internal::kTrackThickness * 0.5f, finish._position._z }
        },
                                float3{ track.getWidth(), Internal::kTrackThickness, Internal::kFinishLength }, MathUtil::atan2( finish._tangent._x, finish._tangent._z ), _finishColor );
        _trackViews.end( manager );
    }

    void KartRaceViewComponent::showRace( GameObjectManager& manager, const KartGameModeComponent& director )
    {
        using Internal       = KartRaceViewComponentInternal;
        const KartRace& race = director.getRace();
        const float3    unitScale{ 1.0f, 1.0f, 1.0f };
        _kartViews.begin();
        for ( int32 racer = 0; racer < race.getRacerCount(); ++racer )
        {
            const bool bPlayer = racer == KartGameModeComponent::kPlayerRacer;
            (void)_kartViews.show( manager, _kartPrefab, director.getRacerPosition( racer ), unitScale, race.findRacer( racer )->_motor.getYaw(), bPlayer ? _playerColor : _opponentColor );
        }
        _kartViews.end( manager );

        _itemViews.begin();
        const vector<float3>& listBox = director.getTrack().getItemBoxPositions();
        for ( size_t boxIndex = 0; boxIndex < listBox.size(); ++boxIndex )
        {
            if ( race.isItemBoxActive( static_cast<int32>( boxIndex ) ) )
                (void)_itemViews.show( manager, _itemPrefab, AbsolutePosition{ listBox[boxIndex] } + float3{ 0.0f, Internal::kItemLift, 0.0f }, unitScale, 0.0f, _itemColor );
        }
        for ( const ProjectileState& projectile : race.getProjectiles() )
        {
            (void)_itemViews.show( manager, _itemPrefab, AbsolutePosition{ projectile._position } + float3{ 0.0f, Internal::kItemLift, 0.0f }, unitScale, 0.0f, _opponentColor );
        }
        _itemViews.end( manager );
    }

    void KartRaceViewComponent::updateHUD( const KartGameModeComponent& director )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<KartHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const KartRace&        race    = director.getRace();
        const KartRacer* const pPlayer = race.findRacer( KartGameModeComponent::kPlayerRacer );
        if ( pPlayer == nullptr )
            return;
        _hudViewModel->setPlace( pPlayer->_place );
        _hudViewModel->setRacerCount( race.getRacerCount() );
        _hudViewModel->setLap( MathUtil::clamp( pPlayer->_lap, 1, director.getTrack().getLapCount() ) );
        _hudViewModel->setLapCount( director.getTrack().getLapCount() );
        _hudViewModel->setSpeed( static_cast<int32>( MathUtil::abs( pPlayer->_motor.getForwardSpeed() ) * KartRaceViewComponentInternal::kSpeedDisplayScale ) );
        _hudViewModel->setCountdown( static_cast<int32>( MathUtil::ceil( race.getCountdown() ) ) );
        _hudViewModel->setCountdownShown( race.getPhase() == MatchPhase::Warmup );
        _hudViewModel->setItemShown( pPlayer->_itemID.empty() == false );
        _hudViewModel->setWrongWayShown( pPlayer->_bWrongWay == SW_TRUE );
        _hudViewModel->setFinishShown( pPlayer->_finishPlace > 0 );
    }
} // namespace sw

#include "KartRaceViewComponent.cpp.gen.inl"
