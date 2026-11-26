#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
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
#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/Client/ConquestHUDViewModel.h"
#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/Component/ConquestGameModeComponent.h"
#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestCatalog.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    class ConquestGameModeComponent;
    class ConquestHUDViewModel;
    class GameObjectManager;

    /**
     * @class ConquestViewComponent
     * @brief `Move` 축의 좌우는 지휘관 이동, 따르기 · 대기 · 돌격 액션은 부대 명령, 훈련 액션은 병사 훈련으로 디렉터에 넘깁니다.
     */
    REFLECT( Category = "SideScrollConquest", DisplayName = "Conquest View", Tooltip = "Client side of a side scroll conquest: commands from input, shows sites, soldiers and the commander and fills the HUD" )
    class ConquestViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        ConquestViewComponent();
        virtual ~ConquestViewComponent() override;

        /** @brief 세운 모습을 지우고 HUD 에서 뷰모델을 뗍니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 내고 모습과 HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, ConquestGameModeComponent& director );
        /** @brief 이번 프레임의 지휘 명령을 디렉터에 냅니다. */
        void submitInput( const GameModeInput& input, ConquestGameModeComponent& director ) const;
        /** @brief 거점 · 병사 · 지휘관을 지금 자리에 보입니다. */
        void showWorld( GameObjectManager& manager, const ConquestGameModeComponent& director );
        /** @brief HUD 뷰모델을 채웁니다. HUD 가 없는 씬이면 값만 듭니다. */
        void          updateHUD( const ConquestGameModeComponent& director );
        const float4& findTeamColor( ConquestTeam team ) const;

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action: left/right moves the commander (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Follow Action", Tooltip = "InputMap action that orders the squad to follow (required)" )
        hashed_string _followAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Hold Action", Tooltip = "InputMap action that orders the squad to hold (required)" )
        hashed_string _holdAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Charge Action", Tooltip = "InputMap action that orders the squad to charge (required)" )
        hashed_string _chargeAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Train Action", Tooltip = "InputMap action that trains one soldier (required)" )
        hashed_string _trainAction;
        PROPERTY( Category = "View", DisplayName = "Site Prefab", Required, Tooltip = "Unit piece used for sites, gates and walls" )
        AssetRef<PrefabAsset> _sitePrefab;
        PROPERTY( Category = "View", DisplayName = "Soldier Prefab", Required )
        AssetRef<PrefabAsset> _soldierPrefab;
        PROPERTY( Category = "View", DisplayName = "Commander Prefab", Required )
        AssetRef<PrefabAsset> _commanderPrefab;
        PROPERTY( Category = "View", DisplayName = "Shown Resource", Tooltip = "Resource id the HUD counts" )
        hashed_string _shownResource;
        PROPERTY( Category = "View", DisplayName = "Neutral Color" )
        float4 _neutralColor;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Enemy Color" )
        float4 _enemyColor;

        SW_TRANSIENT PrefabViewBatch _siteViews;
        SW_TRANSIENT PrefabViewBatch _soldierViews;
        SW_TRANSIENT PrefabViewBatch _commanderViews;
        SW_TRANSIENT unique_ptr<ConquestHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
    };

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
        : GameModeViewComponent( GameModeViewBinding::of<&ConquestViewComponent::updateView, &ConquestViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
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
        , _siteViews{}
        , _soldierViews{}
        , _commanderViews{}
        , _hudViewModel{ nullptr }
    {
        setCanEverTick( true );
    }

    ConquestViewComponent::~ConquestViewComponent() = default;

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
        GameModeViewComponent::onEndPlay();
    }

    void ConquestViewComponent::updateView( GameObjectManager& manager, ConquestGameModeComponent& director )
    {
        dispatchInput( manager, director );
        showWorld( manager, director );
        updateHUD( director );
    }

    void ConquestViewComponent::submitInput( const GameModeInput& input, ConquestGameModeComponent& director ) const
    {
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
        using Internal                  = ConquestViewComponentInternal;
        const ConquestSimulation& world = director.getWorld();
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
        const ConquestSimulation& world     = director.getWorld();
        const float32             maxHealth = director.findCatalog()->getRules()._commanderHealth; // 열린 판은 카탈로그가 있다. 바닥은 카탈로그의 Min
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

#include "ConquestViewComponent.cpp.gen.inl"
