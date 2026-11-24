#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Client/KartRaceViewComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Client/KartHUDViewModel.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/Component/KartGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"

namespace sw
{
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
        : _moveAction{ CommonInputActions::kMove }
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
        , _director{}
        , _trackViews{}
        , _kartViews{}
        , _itemViews{}
        , _hudViewModel{ nullptr }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    KartRaceViewComponent::~KartRaceViewComponent() = default;

    void KartRaceViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                  pOwner    = getOwner();
        const KartGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<KartGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "KartRacing.ViewWithoutGameMode", "Kart race view on '%#': no KartGameModeComponent in front of it on the same object - the view does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<KartGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<KartGameModeComponent>( pDirector );
    }

    void KartRaceViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _bActionsValid = _moveAction.empty() == false && _driftAction.empty() == false && _boostAction.empty() == false && _itemAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "KartRacing.ViewActionMissing", "Kart race view: move '%#', drift '%#', boost '%#' and item '%#' actions must all be named - the view reads no input", _moveAction.c_str(),
                             _driftAction.c_str(), _boostAction.c_str(), _itemAction.c_str() );
    }

    void KartRaceViewComponent::onEndPlay()
    {
        destroyViews();
        _hudViewModel.reset(); // 지워지는 뷰모델은 HUD 가 스스로 뗀다
        Component::onEndPlay();
    }

    void KartRaceViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( KartRaceViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
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

    void KartRaceViewComponent::updateView( GameObjectManager& manager )
    {
        KartGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        if ( _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAcceptingInput() )
            submitInput( *pDirector );
        showTrack( manager, *pDirector );
        showRace( manager, *pDirector );
        updateHUD( *pDirector );
    }

    void KartRaceViewComponent::submitInput( KartGameModeComponent& director ) const
    {
        const InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        const float2        axis = input.getVector2D( _moveAction );
        KartRacerInput      driver;
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
