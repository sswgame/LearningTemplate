#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/Client/ConquestViewComponent.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/Client/ConquestHUDViewModel.h"
#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/Component/ConquestGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct ConquestViewComponentInternal
        {
            static constexpr float32 kSiteDepth    = 4.0f;  ///< 거점 땅의 깊이(m)
            static constexpr float32 kSiteHeight   = 0.1f;  ///< 거점 땅의 높이(m)
            static constexpr float32 kWallHeight   = 2.5f;  ///< 성한 성벽의 높이(m) — 남은 비율만큼 낮아진다
            static constexpr float32 kGateHeight   = 1.6f;  ///< 성한 성문의 높이(m)
            static constexpr float32 kWallWidth    = 0.5f;  ///< 성벽의 두께(m)
            static constexpr float32 kGateWidth    = 0.8f;  ///< 성문의 두께(m)
            static constexpr float32 kWallZ        = 1.0f;  ///< 성벽을 전선 뒤로 물리는 거리(m)
            static constexpr float32 kSoldierZStep = 0.35f; ///< 병사를 앞뒤로 흩는 간격(m)
            static constexpr int32   kSoldierRows  = 3;     ///< 병사를 흩는 줄 수
        };
    } // namespace

    ConquestViewComponent::ConquestViewComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _followAction{}
        , _holdAction{}
        , _chargeAction{}
        , _trainAction{}
        , _sitePrefab{}
        , _soldierPrefab{}
        , _commanderPrefab{}
        , _shownResource{}
        , _neutralColor{ 0.6f, 0.6f, 0.6f, 1.0f }
        , _playerColor{ 0.3f, 0.55f, 1.0f, 1.0f }
        , _enemyColor{ 1.0f, 0.35f, 0.3f, 1.0f }
        , _director{}
        , _siteViews{}
        , _soldierViews{}
        , _commanderViews{}
        , _hudViewModel{ nullptr }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    ConquestViewComponent::~ConquestViewComponent() = default;

    void ConquestViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                      pOwner    = getOwner();
        const ConquestGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<ConquestGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "Conquest.ViewWithoutGameMode", "Conquest view on '%#': no ConquestGameModeComponent in front of it on the same object - the view does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<ConquestGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<ConquestGameModeComponent>( pDirector );
    }

    void ConquestViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const bool bOrders = _followAction.empty() == false && _holdAction.empty() == false && _chargeAction.empty() == false;
        _bActionsValid     = bOrders && _moveAction.empty() == false && _trainAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "Conquest.ViewActionMissing", "Conquest view: move '%#', follow '%#', hold '%#', charge '%#' and train '%#' actions must all be named - the view reads no input",
                             _moveAction.c_str(), _followAction.c_str(), _holdAction.c_str(), _chargeAction.c_str(), _trainAction.c_str() );
    }

    void ConquestViewComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _siteViews.destroyAll( *pManager );
            _soldierViews.destroyAll( *pManager );
            _commanderViews.destroyAll( *pManager );
        }
        _hudViewModel.reset(); // 지워지는 뷰모델은 HUD 가 스스로 뗀다
        Component::onEndPlay();
    }

    void ConquestViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( ConquestViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
    }

    void ConquestViewComponent::updateView( GameObjectManager& manager )
    {
        ConquestGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        if ( _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAwaitingCommand() )
            submitInput( *pDirector );
        showWorld( manager, *pDirector );
        updateHUD( *pDirector );
    }

    void ConquestViewComponent::submitInput( ConquestGameModeComponent& director ) const
    {
        const InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        // 받는지는 부른 쪽이 보았다(`isAwaitingCommand`). 훈련을 거절한 까닭은 디렉터가 남긴다.
        (void)director.submitCommanderMove( input.getVector2D( _moveAction )._x );
        if ( input.wasActionTriggered( _followAction ) )
            (void)director.submitOrder( ConquestOrder::Follow );
        if ( input.wasActionTriggered( _holdAction ) )
            (void)director.submitOrder( ConquestOrder::Hold );
        if ( input.wasActionTriggered( _chargeAction ) )
            (void)director.submitOrder( ConquestOrder::Charge );
        if ( input.wasActionTriggered( _trainAction ) )
            (void)director.submitTrain();
    }

    const float4& ConquestViewComponent::findTeamColor( ConquestTeam team ) const
    {
        switch ( team )
        {
            case ConquestTeam::Neutral:
                return _neutralColor;
            case ConquestTeam::Player:
                return _playerColor;
            case ConquestTeam::Enemy:
                return _enemyColor;
        }
        return _neutralColor; // 열거자는 셋뿐이다(covered-switch) — 닿지 않는다
    }

    void ConquestViewComponent::showWorld( GameObjectManager& manager, const ConquestGameModeComponent& director )
    {
        using Internal             = ConquestViewComponentInternal;
        const ConquestWorld& world = director.getWorld();
        _siteViews.begin();
        for ( const ConquestSite& site : world.getSites() )
        {
            const ConquestSiteDef& def    = *site._pDef;
            const float4&          color  = findTeamColor( site._owner );
            const AbsolutePosition center = ConquestGameModeComponent::makePosition( def._x );
            (void)_siteViews.show( manager, _sitePrefab, center + float3{ 0.0f, -Internal::kSiteHeight * 0.5f, 0.0f }, float3{ def._captureRadius * 2.0f, Internal::kSiteHeight, Internal::kSiteDepth }, 0.0f,
                                   color );
            const float32 wallHeight = def._wallHealth > 0.0f ? Internal::kWallHeight * site._wallHealth / def._wallHealth : 0.0f;
            if ( wallHeight > 0.0f )
                (void)_siteViews.show( manager, _sitePrefab, center + float3{ 0.0f, wallHeight * 0.5f, Internal::kWallZ }, float3{ Internal::kWallWidth, wallHeight, Internal::kSiteDepth }, 0.0f, color );
            const float32 gateHeight = def._gateHealth > 0.0f ? Internal::kGateHeight * site._gateHealth / def._gateHealth : 0.0f;
            if ( gateHeight > 0.0f )
                (void)_siteViews.show( manager, _sitePrefab, center + float3{ -def._captureRadius, gateHeight * 0.5f, 0.0f }, float3{ Internal::kGateWidth, gateHeight, Internal::kSiteDepth }, 0.0f, color );
        }
        _siteViews.end( manager );

        const float3 unitScale{ 1.0f, 1.0f, 1.0f };
        _soldierViews.begin();
        for ( const ConquestUnit& unit : world.getUnits() )
        {
            const float32 z = static_cast<float32>( unit._unitID % Internal::kSoldierRows - Internal::kSoldierRows / 2 ) * Internal::kSoldierZStep;
            (void)_soldierViews.show( manager, _soldierPrefab, ConquestGameModeComponent::makePosition( unit._x ) + float3{ 0.0f, 0.0f, z }, unitScale, 0.0f, findTeamColor( unit._team ) );
        }
        _soldierViews.end( manager );

        _commanderViews.begin();
        if ( world.getCommander()._bAlive == SW_TRUE )
            (void)_commanderViews.show( manager, _commanderPrefab, ConquestGameModeComponent::makePosition( director.getCommanderX() ), unitScale, 0.0f, findTeamColor( ConquestTeam::Player ) );
        _commanderViews.end( manager );
    }

    void ConquestViewComponent::updateHUD( const ConquestGameModeComponent& director )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<ConquestHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const ConquestWorld& world     = director.getWorld();
        const float32        maxHealth = director.findCatalog()->getRules()._commanderHealth; // 열린 판은 카탈로그가 있다. 바닥은 카탈로그의 Min
        _hudViewModel->setTerritory( director.getTerritory() );
        _hudViewModel->setSiteCount( static_cast<int32>( world.getSites().size() ) );
        _hudViewModel->setSquad( director.getSquadSize() );
        _hudViewModel->setPopulation( world.computePopulation() );
        _hudViewModel->setPopulationCap( world.computePopulationCap() );
        _hudViewModel->setResource( _shownResource.empty() ? 0 : world.getResource( _shownResource ) );
        _hudViewModel->setHealthRatio( world.getCommander()._health / maxHealth );
        _hudViewModel->setWonShown( world.isVictory() );
        _hudViewModel->setLostShown( world.isDefeat() );
    }
} // namespace sw
