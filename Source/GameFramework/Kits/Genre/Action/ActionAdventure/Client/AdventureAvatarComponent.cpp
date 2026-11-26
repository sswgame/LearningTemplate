#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
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
#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureRegionCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    class AdventureGameModeComponent;
    class GameObjectManager;
    struct AdventureRegionDef;

    /**
     * @class AdventureAvatarComponent
     * @brief `Move` 축 · 쓰기 액션을 프레임마다 디렉터 입력 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "ActionAdventure", DisplayName = "Adventure Avatar",
             Tooltip = "Client side of an action adventure: sends input commands to the Adventure Game Mode on the same object and draws its regions, beacons, camps and player" )
    class AdventureAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        AdventureAvatarComponent();
        virtual ~AdventureAvatarComponent() override;

        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 그립니다. */
        void updateAvatar( GameObjectManager& manager, AdventureGameModeComponent& director );
        void submitInput( const GameModeInput& input, AdventureGameModeComponent& director );
        /** @brief 판을 보입니다. 지역 표 · 플레이어 자리는 절대 자리다. */
        void showWorld( const AdventureGameModeComponent& director, GameObjectManager& manager );
        /** @brief 지역 경계 넷을 얇은 상자로 보입니다 — 씬에 땅 메시가 없는 지역(확장 팩)도 어디까지 걸을 수 있는지 보인다. */
        void showBorder( const AdventureRegionDef& region, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the player (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Use Action", Tooltip = "InputMap action that lights a beacon or rests at a camp (required)" )
        hashed_string _useAction;
        PROPERTY( Category = "View", DisplayName = "Cube Prefab", Required, Tooltip = "1 m cube stretched into region borders and beacons" )
        AssetRef<PrefabAsset> _cubePrefab;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to camps and the player" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Border Color" )
        float4 _borderColor;
        PROPERTY( Category = "View", DisplayName = "Border Width", Min = 0.01, Units = m )
        float32 _borderWidth;
        PROPERTY( Category = "View", DisplayName = "Beacon Size", Tooltip = "Full size of a beacon box; it stands on the floor", Units = m )
        float3 _beaconSize;
        PROPERTY( Category = "View", DisplayName = "Lit Beacon Color" )
        float4 _litBeaconColor;
        PROPERTY( Category = "View", DisplayName = "Unlit Beacon Color" )
        float4 _unlitBeaconColor;
        PROPERTY( Category = "View", DisplayName = "Camp Radius", Min = 0.01, Units = m )
        float32 _campRadius;
        PROPERTY( Category = "View", DisplayName = "Camp Height", Tooltip = "Height of a camp's center above the floor", Units = m )
        float32 _campHeight;
        PROPERTY( Category = "View", DisplayName = "Camp Color" )
        float4 _campColor;
        PROPERTY( Category = "View", DisplayName = "Player Radius", Min = 0.01, Units = m )
        float32 _playerRadius;
        PROPERTY( Category = "View", DisplayName = "Player Height", Tooltip = "Height of the player's center above the floor", Units = m )
        float32 _playerHeight;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;

        SW_TRANSIENT PrefabViewBatch _cubeViews;
        SW_TRANSIENT PrefabViewBatch _sphereViews;
    };

    AdventureAvatarComponent::AdventureAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&AdventureAvatarComponent::updateAvatar, &AdventureAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _useAction{}
        , _cubePrefab{}
        , _spherePrefab{}
        , _borderColor{ 0.75f, 0.8f, 0.7f, 1.0f }
        , _borderWidth{ 0.1f }
        , _beaconSize{ 0.6f, 2.0f, 0.6f }
        , _litBeaconColor{ 1.0f, 0.62f, 0.18f, 1.0f }
        , _unlitBeaconColor{ 0.35f, 0.35f, 0.4f, 1.0f }
        , _campRadius{ 0.4f }
        , _campHeight{ 0.2f }
        , _campColor{ 0.95f, 0.4f, 0.1f, 1.0f }
        , _playerRadius{ 0.28f }
        , _playerHeight{ 0.5f }
        , _playerColor{ 0.2f, 0.6f, 1.0f, 1.0f }
        , _cubeViews{}
        , _sphereViews{}
    {
    }

    AdventureAvatarComponent::~AdventureAvatarComponent() = default;

    void AdventureAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _cubeViews.destroyAll( *pManager );
            _sphereViews.destroyAll( *pManager );
        }
        GameModeViewComponent::onEndPlay();
    }

    void AdventureAvatarComponent::updateAvatar( GameObjectManager& manager, AdventureGameModeComponent& director )
    {
        dispatchInput( manager, director );
        showWorld( director, manager );
    }

    void AdventureAvatarComponent::submitInput( const GameModeInput& input, AdventureGameModeComponent& director )
    {
        (void)director.submitPlayerInput( input.getVector2D( _moveAction ), input.wasActionTriggered( _useAction ) ); // 열린 판인지는 위에서 보았다
    }

    void AdventureAvatarComponent::showWorld( const AdventureGameModeComponent& director, GameObjectManager& manager )
    {
        const AdventureRegionCatalog* const pRegions = director.findRegions();
        const GameStateComponent* const     pState   = GameStateComponent::findOnOwner( director );
        if ( pRegions == nullptr || pState == nullptr )
            return;
        _cubeViews.begin();
        _sphereViews.begin();
        for ( const AdventureRegionDef& region : pRegions->getAll() )
        {
            showBorder( region, manager );
            if ( region._beacon.isSet() )
            {
                const bool             bLit = director.getCampaign().isBeaconLit( region, pState->getFlags() );
                const AbsolutePosition center{ float3( region._beacon._position._x, _beaconSize._y * 0.5f, region._beacon._position._y ) };
                (void)_cubeViews.show( manager, _cubePrefab, center, _beaconSize, 0.0f, bLit ? _litBeaconColor : _unlitBeaconColor );
            }
            if ( region._camp.isSet() )
            {
                const AbsolutePosition center{ float3( region._camp._position._x, _campHeight, region._camp._position._y ) };
                (void)_sphereViews.show( manager, _spherePrefab, center, float3{ _campRadius * 2.0f }, 0.0f, _campColor );
            }
        }
        const float2& position = director.getPlayerPosition();
        (void)_sphereViews.show( manager, _spherePrefab, AbsolutePosition{ float3( position._x, _playerHeight, position._y ) }, float3{ _playerRadius * 2.0f }, 0.0f, _playerColor );
        _cubeViews.end( manager );
        _sphereViews.end( manager );
    }

    void AdventureAvatarComponent::showBorder( const AdventureRegionDef& region, GameObjectManager& manager )
    {
        const float32 half    = _borderWidth * 0.5f; // 땅 위에 앉힌다
        const float32 centerX = ( region._min._x + region._max._x ) * 0.5f;
        const float32 centerZ = ( region._min._y + region._max._y ) * 0.5f;
        const float3  alongX( region._max._x - region._min._x, _borderWidth, _borderWidth );
        const float3  alongZ( _borderWidth, _borderWidth, region._max._y - region._min._y );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( centerX, half, region._min._y ) }, alongX, 0.0f, _borderColor );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( centerX, half, region._max._y ) }, alongX, 0.0f, _borderColor );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( region._min._x, half, centerZ ) }, alongZ, 0.0f, _borderColor );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( region._max._x, half, centerZ ) }, alongZ, 0.0f, _borderColor );
    }
} // namespace sw

#include "AdventureAvatarComponent.cpp.gen.inl"
