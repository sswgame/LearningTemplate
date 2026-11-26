#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
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
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Component/ActionPlatformerGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    class ActionPlatformerGameModeComponent;
    class GameObjectManager;

    /**
     * @class ActionPlatformerAvatarComponent
     * @brief `Move` 축의 가로 값과 점프 액션을 누른 채인지를 프레임마다 디렉터 입력 명령으로 넘기고 스테이지를 그립니다.
     */
    REFLECT( Category = "ActionPlatformer", DisplayName = "Action Platformer Avatar",
             Tooltip = "Client side of an ActionPlatformer stage: sends input commands to the Action Platformer Game Mode on the same object and draws the stage" )
    class ActionPlatformerAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        ActionPlatformerAvatarComponent();
        virtual ~ActionPlatformerAvatarComponent() override;

        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 스테이지를 그립니다. */
        void updateAvatar( GameObjectManager& manager, ActionPlatformerGameModeComponent& director );
        void submitInput( const GameModeInput& input, ActionPlatformerGameModeComponent& director );
        /** @brief 스테이지를 보입니다. 타일 · 플레이어 자리는 절대 자리다. */
        void showStage( const ActionPlatformerGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action whose X runs the body (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Jump Action", Tooltip = "InputMap action held to jump (required, trigger Down - the game mode finds the press edge)" )
        hashed_string _jumpAction;
        PROPERTY( Category = "View", DisplayName = "Cube Prefab", Required, Tooltip = "1 m cube stretched into the tiles, the player and the goal post" )
        AssetRef<PrefabAsset> _cubePrefab;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to a secret" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Tile Color" )
        float4 _tileColor;
        PROPERTY( Category = "View", DisplayName = "Secret Radius", Min = 0.01, Units = m )
        float32 _secretRadius;
        PROPERTY( Category = "View", DisplayName = "Secret Color" )
        float4 _secretColor;
        PROPERTY( Category = "View", DisplayName = "Player Half Extent", Units = m )
        float3 _playerHalfExtent;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Goal Height", Min = 0.01, Units = m )
        float32 _goalHeight;
        PROPERTY( Category = "View", DisplayName = "Goal Width", Min = 0.01, Units = m )
        float32 _goalWidth;
        PROPERTY( Category = "View", DisplayName = "Goal Color" )
        float4 _goalColor;

        SW_TRANSIENT PrefabViewBatch _cubeViews;
        SW_TRANSIENT PrefabViewBatch _secretViews;
    };

    ActionPlatformerAvatarComponent::ActionPlatformerAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&ActionPlatformerAvatarComponent::updateAvatar, &ActionPlatformerAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _jumpAction{}
        , _cubePrefab{}
        , _spherePrefab{}
        , _tileColor{ 0.45f, 0.4f, 0.35f, 1.0f }
        , _secretRadius{ 0.25f }
        , _secretColor{ 1.0f, 0.85f, 0.2f, 1.0f }
        , _playerHalfExtent{ 0.35f, 0.45f, 0.35f }
        , _playerColor{ 0.2f, 0.6f, 1.0f, 1.0f }
        , _goalHeight{ 4.0f }
        , _goalWidth{ 0.1f }
        , _goalColor{ 0.3f, 1.0f, 0.4f, 1.0f }
        , _cubeViews{}
        , _secretViews{}
    {
    }

    ActionPlatformerAvatarComponent::~ActionPlatformerAvatarComponent() = default;

    void ActionPlatformerAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _cubeViews.destroyAll( *pManager );
            _secretViews.destroyAll( *pManager );
        }
        GameModeViewComponent::onEndPlay();
    }

    void ActionPlatformerAvatarComponent::updateAvatar( GameObjectManager& manager, ActionPlatformerGameModeComponent& director )
    {
        dispatchInput( manager, director );
        showStage( director, manager );
    }

    void ActionPlatformerAvatarComponent::submitInput( const GameModeInput& input, ActionPlatformerGameModeComponent& director )
    {
        // 판이 끝났으면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        (void)director.submitPlayerInput( input.getVector2D( _moveAction )._x, input.isActionDown( _jumpAction ) );
    }

    void ActionPlatformerAvatarComponent::showStage( const ActionPlatformerGameModeComponent& director, GameObjectManager& manager )
    {
        // 화면은 x 오른쪽 · y 위 평면이다(카메라가 -Z 에서 본다). 가로로 이어진 타일은 상자 하나로 보인다.
        const PlatformTileMap& map  = director.getTileMap();
        const float32          size = map.getTileSize();
        const float32          half = size * 0.5f;
        _cubeViews.begin();
        for ( int32 y = 0; y < map.getHeight(); ++y )
        {
            for ( int32 x = 0; x < map.getWidth(); ++x )
            {
                if ( map.getTile( x, y ) != PlatformTile::Solid )
                    continue;
                int32 endX = x + 1;
                while ( endX < map.getWidth() && map.getTile( endX, y ) == PlatformTile::Solid )
                    ++endX;
                const float32 width = static_cast<float32>( endX - x ) * size;
                (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( map.getTileLeft( x ) + width * 0.5f, map.getTileBottom( y ) + half, 0.0f ) }, float3( width, size, size ), 0.0f,
                                       _tileColor );
                x = endX;
            }
        }
        const float2  position = director.getPlayerPosition();
        const float32 goalX    = director.getGoalX();
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( position._x, position._y, 0.0f ) }, _playerHalfExtent * 2.0f, 0.0f, _playerColor );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( goalX, _goalHeight * 0.5f, 0.0f ) }, float3( _goalWidth, _goalHeight, _goalWidth ), 0.0f, _goalColor );
        _cubeViews.end( manager );

        const ActionStageRun& run            = director.getRun();
        const ActionStageDef* pStage         = run.getStage();
        const vector<float2>& listSecretSpot = director.getSecretPositions();
        const float3          secretScale{ _secretRadius * 2.0f };
        _secretViews.begin();
        for ( size_t secretIndex = 0; pStage != nullptr && secretIndex < listSecretSpot.size(); ++secretIndex )
        {
            if ( run.isSecretFound( pStage->_listSecret[secretIndex] ) )
                continue;
            (void)_secretViews.show( manager, _spherePrefab, AbsolutePosition{ float3( listSecretSpot[secretIndex]._x, listSecretSpot[secretIndex]._y, 0.0f ) }, secretScale, 0.0f, _secretColor );
        }
        _secretViews.end( manager );
    }
} // namespace sw

#include "ActionPlatformerAvatarComponent.cpp.gen.inl"
