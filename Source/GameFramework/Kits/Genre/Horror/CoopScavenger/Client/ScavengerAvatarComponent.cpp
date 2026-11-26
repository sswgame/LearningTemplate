#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Client/ScavengerHUDViewModel.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Component/ScavengerGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    class ScavengerGameModeComponent;
    class ScavengerHUDViewModel;
    struct ScavengerCommand;
    class GameObjectManager;

    /**
     * @class ScavengerAvatarComponent
     * @brief 입력 맵 액션을 프레임마다 디렉터 명령으로 넘기고 판을 그리며 HUD 를 채웁니다.
     */
    REFLECT( Category = "CoopScavenger", DisplayName = "Scavenger Avatar",
             Tooltip = "Client side of a coop scavenger expedition: sends commands to the Scavenger Game Mode on the same object, draws the game and fills the HUD" )
    class ScavengerAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        ScavengerAvatarComponent();
        virtual ~ScavengerAvatarComponent() override;

        /** @brief 세운 표식을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 판을 그리며 HUD 를 채웁니다. */
        void updateAvatar( GameObjectManager& manager, ScavengerGameModeComponent& director );
        /** @brief 입력 맵을 읽어 디렉터 명령으로 넘깁니다. */
        void submitInput( const GameModeInput& input, ScavengerGameModeComponent& director );
        /** @brief 궤도의 터미널 — 걷기 축의 좌우로 물건을 고르고 쓰기로 고른 물건의 id 를 @p outCommand 에 적습니다. 사용자 0 만 쓴다. */
        void updateTerminal( const ScavengerGameModeComponent& director, float32 axis, bool bBuy, ScavengerCommand& outCommand );
        /** @brief 판을 표식으로 그립니다. */
        void drawGame( GameObjectManager& manager, const ScavengerGameModeComponent& director );
        /** @brief HUD 값을 채웁니다. */
        void fillHUD( ScavengerHUDViewModel& outViewModel, const ScavengerGameModeComponent& director ) const;

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that steps to the neighbouring area (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Use Action", Tooltip = "InputMap action: pick up, load, sell or unlock (required)" )
        hashed_string _useAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Lever Action", Tooltip = "InputMap action: land from orbit, take off from the ship (required)" )
        hashed_string _leverAction;
        PROPERTY( Required, Category = "Look", DisplayName = "Marker Prefab", Tooltip = "Prefab with one mesh, spawned for everything the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        SW_TRANSIENT PrefabViewBatch _markers;
        SW_TRANSIENT unique_ptr<ScavengerHUDViewModel> _hudViewModel;      ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
        SW_TRANSIENT uint32                            _terminalIndex;     ///< 터미널에서 고른 진열 칸(화면의 상태라 싣지 않는다)
        SW_TRANSIENT uint8                             _bTerminalAxis : 1; ///< 고르는 축이 눌려 있다(눌린 동안 한 칸만 옮긴다)
        [[maybe_unused]] uint8                         _reserved      : 7;
    };

    namespace
    {
        /** @brief 표식의 색 · 크기(m)입니다. */
        struct ScavengerAvatarComponentInternal
        {
            static constexpr float4  kShipColor       = float4( 0.75f, 0.75f, 0.8f, 1.0f );
            static constexpr float4  kOutsideColor    = float4( 0.35f, 0.3f, 0.25f, 1.0f );
            static constexpr float4  kRoomColor       = float4( 0.25f, 0.27f, 0.32f, 1.0f );
            static constexpr float4  kOpenDoorColor   = float4( 0.45f, 0.5f, 0.55f, 1.0f );
            static constexpr float4  kLockedDoorColor = float4( 0.8f, 0.25f, 0.2f, 1.0f );
            static constexpr float4  kScrapColor      = float4( 0.85f, 0.7f, 0.3f, 1.0f );
            static constexpr float4  kBodyColor       = float4( 0.5f, 0.1f, 0.1f, 1.0f ); ///< 동료 시신
            static constexpr float4  kThreatColor     = float4( 0.75f, 0.1f, 0.6f, 1.0f );
            static constexpr float4  kPlayerColor     = float4( 1.0f, 0.55f, 0.15f, 1.0f );
            static constexpr float4  kCrewColor       = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float3  kAreaSize        = float3( 4.0f, 0.1f, 4.0f );
            static constexpr float3  kDoorSize        = float3( 0.5f, 0.3f, 0.5f );
            static constexpr float3  kScrapSize       = float3( 0.4f, 0.4f, 0.4f );
            static constexpr float3  kCrewSize        = float3( 0.6f, 1.7f, 0.6f );
            static constexpr float3  kThreatSize      = float3( 1.0f, 1.2f, 1.0f );
            static constexpr float32 kCrewSpacing     = 0.9f; ///< 한 곳에 선 승무원 사이
            static constexpr float32 kScrapSpacing    = 0.6f; ///< 한 곳에 놓인 고철 사이
            static constexpr int32   kScrapColumns    = 4;
            static constexpr float32 kScrapRow        = -1.5f; ///< 고철 줄이 시작하는 깊이
            static constexpr float32 kTerminalAxis    = 0.5f;  ///< 걷기 축이 이만큼 기울면 터미널의 물건을 한 칸 옮긴다
            static constexpr uint32  kTerminalUser    = 0;     ///< 터미널을 쓰는 로컬 사용자(이 화면의 HUD 가 보이는 승무원)
        };
    } // namespace

    ScavengerAvatarComponent::ScavengerAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&ScavengerAvatarComponent::updateAvatar, &ScavengerAvatarComponent::submitInput>( GameModeInputUser::EachLocal ) )
        , _moveAction{ CommonInputActions::kMove }
        , _useAction{}
        , _leverAction{}
        , _markerPrefab{}
        , _markers{}
        , _hudViewModel{}
        , _terminalIndex{ 0 }
        , _bTerminalAxis{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    ScavengerAvatarComponent::~ScavengerAvatarComponent() = default;

    void ScavengerAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _markers.destroyAll( *pManager );
        GameModeViewComponent::onEndPlay();
    }

    void ScavengerAvatarComponent::updateAvatar( GameObjectManager& manager, ScavengerGameModeComponent& director )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        dispatchInput( manager, director );
        _markers.begin();
        drawGame( manager, director );
        _markers.end( manager );
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<ScavengerHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        fillHUD( *_hudViewModel, director );
    }

    void ScavengerAvatarComponent::updateTerminal( const ScavengerGameModeComponent& director, float32 axis, bool bBuy, ScavengerCommand& outCommand )
    {
        const ShopDef* const pTerminal = director.findTerminal();
        if ( pTerminal == nullptr || pTerminal->_listStock.empty() )
            return;
        const uint32 stockCount = static_cast<uint32>( pTerminal->_listStock.size() );
        const bool   bAxis      = MathUtil::abs( axis ) >= ScavengerAvatarComponentInternal::kTerminalAxis;
        if ( bAxis && _bTerminalAxis == SW_FALSE )
            _terminalIndex = ( _terminalIndex + ( axis > 0.0f ? 1u : stockCount - 1u ) ) % stockCount;
        _bTerminalAxis = bAxis ? SW_TRUE : SW_FALSE;
        if ( bBuy )
            outCommand._buyItemID = pTerminal->_listStock[_terminalIndex % stockCount]._itemID;
    }

    void ScavengerAvatarComponent::submitInput( const GameModeInput& input, ScavengerGameModeComponent& director )
    {
        const uint32     user   = input.getLocalUser(); // 로컬 사용자 n 이 승무원 n 을 잡는다
        const int32      player = static_cast<int32>( user );
        ScavengerCommand command;
        command._moveTo          = director.findNeighborToward( player, input.getVector2D( _moveAction ) );
        command._bUseTriggered   = input.wasActionTriggered( _useAction ) ? SW_TRUE : SW_FALSE;
        command._bLeverTriggered = input.wasActionTriggered( _leverAction ) ? SW_TRUE : SW_FALSE;
        if ( user == ScavengerAvatarComponentInternal::kTerminalUser && director.getExpedition().getPhase() == ScavengerPhase::InOrbit )
            updateTerminal( director, input.getVector2D( _moveAction )._x, command._bUseTriggered == SW_TRUE, command );
        (void)director.submitCrewCommand( player, command ); // 승무원보다 많은 사용자의 입력은 쓸 곳이 없다
    }

    void ScavengerAvatarComponent::drawGame( GameObjectManager& manager, const ScavengerGameModeComponent& director )
    {
        using Internal                        = ScavengerAvatarComponentInternal;
        const ScavengerExpedition& expedition = director.getExpedition();
        const ScavengerFacility&   facility   = expedition.getFacility();
        const hashed_string        ship( ScavengerExpedition::kShipAreaID );
        const hashed_string        outside( ScavengerExpedition::kOutsideAreaID );
        if ( expedition.getPhase() == ScavengerPhase::Landed )
        {
            for ( const AreaDef& area : facility.getGraph().getAreas() )
            {
                const float4& color = area._id == ship ? Internal::kShipColor : ( area._id == outside ? Internal::kOutsideColor : Internal::kRoomColor );
                _markers.showStanding( manager, _markerPrefab, director.computeAreaPosition( area._id ), Internal::kAreaSize, color );
            }
            for ( const AreaLink& link : facility.getGraph().getLinks() )
            {
                const float3 middle = ( director.computeAreaPosition( link._from )._value + director.computeAreaPosition( link._to )._value ) * 0.5f;
                _markers.showStanding( manager, _markerPrefab, AbsolutePosition{ middle }, Internal::kDoorSize,
                                       facility.canTraverse( link._from, link._to ) ? Internal::kOpenDoorColor : Internal::kLockedDoorColor );
            }
            int32 scrapIndex = 0;
            for ( const ScavengerScrap& scrap : facility.getGroundScrap() )
            {
                const float32 column = static_cast<float32>( scrapIndex % Internal::kScrapColumns );
                const float3  offset{ ( column - static_cast<float32>( Internal::kScrapColumns - 1 ) * 0.5f ) * Internal::kScrapSpacing, Internal::kAreaSize._y, Internal::kScrapRow };
                _markers.showStanding( manager, _markerPrefab, director.computeAreaPosition( scrap._areaID ) + offset, Internal::kScrapSize, scrap.isBody() ? Internal::kBodyColor : Internal::kScrapColor );
                ++scrapIndex;
            }
            for ( const ScavengerThreat& threat : director.getThreats() )
            {
                _markers.showStanding( manager, _markerPrefab, director.computeAreaPosition( threat._areaID ), Internal::kThreatSize, Internal::kThreatColor );
            }
        }
        else
        {
            _markers.showStanding( manager, _markerPrefab, director.computeAreaPosition( ship ), Internal::kAreaSize, Internal::kShipColor );
        }
        const int32 crewCount = expedition.getCrewCount();
        for ( int32 player = 0; player < crewCount; ++player )
        {
            const ScavengerCrewMember& member = *expedition.findCrewMember( player );
            if ( member.isDead() )
                continue;
            const float32 column = static_cast<float32>( player ) - static_cast<float32>( crewCount - 1 ) * 0.5f;
            const float3  offset{ column * Internal::kCrewSpacing, Internal::kAreaSize._y, 0.0f };
            _markers.showStanding( manager, _markerPrefab, director.computeAreaPosition( member._areaID ) + offset, Internal::kCrewSize, player == 0 ? Internal::kPlayerColor : Internal::kCrewColor );
        }
    }

    void ScavengerAvatarComponent::fillHUD( ScavengerHUDViewModel& outViewModel, const ScavengerGameModeComponent& director ) const
    {
        constexpr int32                  kPlayer  = 0; // 이 화면의 HUD 는 승무원 0 을 보인다
        const ScavengerCrewMember* const pPlayer  = director.getExpedition().findCrewMember( kPlayer );
        const ScavengerCatalog* const    pCatalog = director.findCatalog();
        const bool                       bHealth  = pPlayer != nullptr && pCatalog != nullptr && pPlayer->isDead() == false;
        outViewModel.setQuotaLeft( MathUtil::max( 0, director.getQuota() - director.getFulfilled() ) );
        outViewModel.setDaysLeft( director.getDaysLeft() );
        outViewModel.setShipValue( director.getShipValue() );
        outViewModel.setCredits( director.getCredits() );
        outViewModel.setHealthRatio( bHealth ? pPlayer->_healthMachine.getHealth() / pCatalog->getCrewHealth() : 0.0f );
        outViewModel.setDayRatio( director.computeDayRatio() );
        outViewModel.setHour( director.getHour() );
        outViewModel.setScrapPercent( director.computeWeatherPercent( "scrapValue" ) );
        outViewModel.setThreatPercent( director.computeWeatherPercent( "threat" ) );
        const ShopDef* const pTerminal = director.findTerminal();
        const bool           bStock    = pTerminal != nullptr && pTerminal->_listStock.empty() == false;
        const hashed_string  itemID    = bStock ? pTerminal->_listStock[_terminalIndex % pTerminal->_listStock.size()]._itemID : hashed_string{};
        const ItemDef* const pItem     = director.findItem( itemID );
        outViewModel.setTerminalItem( pItem != nullptr ? string_view( pItem->_name ) : string_view{}, director.computeTerminalPrice( itemID ) );
        outViewModel.setOrbitShown( director.isAcceptingInput() && director.getExpedition().getPhase() == ScavengerPhase::InOrbit );
        outViewModel.setWinShown( director.getExpeditionResult() == ScavengerResult::Win );
        outViewModel.setLoseShown( director.getExpeditionResult() == ScavengerResult::Lose );
    }
} // namespace sw

#include "ScavengerAvatarComponent.cpp.gen.inl"
