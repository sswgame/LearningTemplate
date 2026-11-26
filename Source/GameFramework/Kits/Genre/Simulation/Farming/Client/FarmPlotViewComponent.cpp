#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmPlotGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/SimulationViewSync.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class FarmPlotGameModeComponent;
    class GameObjectManager;

    /**
     * @class FarmPlotViewComponent
     * @brief 밭 칸마다 판 하나를 세우고 칸 상태(풀 · 갈림 · 물 줌 · 다 익음)의 색을 입힙니다. 밭이 바뀐 때(`getFieldRevision`)에 모습이 바뀐 칸만 다시 세웁니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Plot View",
             Tooltip = "Client side of a farm plot: spawns the field tiles of the Farm Plot Game Mode on the same object and sends the fast forward input to it" )
    class FarmPlotViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        FarmPlotViewComponent();
        virtual ~FarmPlotViewComponent() override;

        /** @brief 세운 모습을 걷습니다. */
        void onEndPlay() override;

    private:
        /** @brief 세운 모습을 모두 지웁니다(플레이 끝 · 다시 세울 때). 모습은 파생이라 상태를 실을 때 걷지 않는다(`SpawnLifetime::Derived`). */
        void despawnViews();
        /** @brief 틱 뒤 게임 스레드: 빨리 감기 입력을 넘기고, 밭이 바뀌었으면 모습을 맞춥니다. */
        void updateView( GameObjectManager& manager, FarmPlotGameModeComponent& director );
        void submitInput( const GameModeInput& input, FarmPlotGameModeComponent& director );
        /** @brief 지금 밭의 칸과 세운 모습을 견줘 바뀐 칸만 다시 세웁니다. */
        void syncTiles( GameObjectManager& manager, const FarmPlotGameModeComponent& director );
        /** @brief 칸 하나를 @p center(절대 자리)에 세웁니다. 프리팹을 읽지 못하면 빈 핸들입니다. */
        GameObjectHandle spawnTile( GameObjectManager& manager, const float3& center, uint8 look );

    private:
        PROPERTY( Category = "Prefabs", DisplayName = "Tile Prefab", Tooltip = "One tile view (a mesh)" )
        AssetRef<PrefabAsset> _tilePrefab;
        PROPERTY( Required, Category = "Input", DisplayName = "Fast Forward Action", Tooltip = "InputMap action held to fast-forward the clock (required)" )
        hashed_string _fastForwardAction;
        PROPERTY( Category = "Look", DisplayName = "Ready Color", Tooltip = "Tile tint when the crop is fully grown" )
        float4 _readyColor;
        PROPERTY( Category = "Look", DisplayName = "Watered Color" )
        float4 _wateredColor;
        PROPERTY( Category = "Look", DisplayName = "Tilled Color" )
        float4 _tilledColor;
        PROPERTY( Category = "Look", DisplayName = "Grass Color", Tooltip = "Tile tint before it is tilled" )
        float4 _grassColor;
        PROPERTY( Category = "Look", DisplayName = "Tile Spacing", Tooltip = "Distance between field tiles", Min = 0.01, Units = m )
        float32 _tileSpacing;

        SW_TRANSIENT MaterialTintCache _tintCache;
        SW_TRANSIENT SimulationViewSync<uint8> _tileViews;     ///< 칸 번호(y × 너비 + x) → 모습 번호(.cpp 의 TileLook, 0 은 칸 없음)
        SW_TRANSIENT uint32                    _shownRevision; ///< 맞춘 밭의 판(0 이면 아직 — 열린 디렉터의 판은 1 부터다)
    };

    namespace
    {

        /** @brief 밭 칸의 모습입니다(뷰 동기화의 규칙 id — 바뀐 칸만 다시 세운다). 0 은 "칸 없음" 이다. */
        enum TileLook : uint8
        {
            kTileLookNone = 0,
            kTileLookGrass,
            kTileLookTilled,
            kTileLookWatered,
            kTileLookReady,
        };

        struct FarmPlotViewComponentInternal
        {
            static constexpr float32 kHalfTile = 0.5f;

            static uint8 findTileLook( const FarmTile& tile )
            {
                if ( tile._bReady != SW_FALSE )
                    return kTileLookReady;
                if ( tile._bWatered != SW_FALSE )
                    return kTileLookWatered;
                return tile._bTilled != SW_FALSE ? kTileLookTilled : kTileLookGrass;
            }

            static void destroyView( GameObjectManager& manager, GameObjectHandle handle )
            {
                GameObject* const pView = manager.resolveGameObject( handle );
                if ( pView != nullptr )
                    manager.destroyGameObject( pView );
            }
        };
    } // namespace

    FarmPlotViewComponent::FarmPlotViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&FarmPlotViewComponent::updateView, &FarmPlotViewComponent::submitInput>() )
        , _tilePrefab{}
        , _fastForwardAction{}
        , _readyColor{ 1.60f, 1.35f, 0.55f, 1.0f } // 메시 정점 색이 이미 흙색이라 상태 색은 그 위에 곱하는 틴트다
        , _wateredColor{ 0.60f, 0.58f, 0.62f, 1.0f }
        , _tilledColor{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _grassColor{ 0.80f, 1.30f, 0.60f, 1.0f }
        , _tileSpacing{ 1.0f }
        , _tintCache{}
        , _tileViews{}
        , _shownRevision{ 0 }
    {
    }

    FarmPlotViewComponent::~FarmPlotViewComponent() = default;

    void FarmPlotViewComponent::onEndPlay()
    {
        despawnViews();
        GameModeViewComponent::onEndPlay();
    }

    void FarmPlotViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            for ( const SimulationViewSync<uint8>::Slot& slot : _tileViews.getSlots() )
            {
                FarmPlotViewComponentInternal::destroyView( *pManager, slot._object );
            }
            _tileViews.clear();
        }
        _shownRevision = 0;
    }

    void FarmPlotViewComponent::updateView( GameObjectManager& manager, FarmPlotGameModeComponent& director )
    {
        dispatchInput( manager, director );
        if ( _shownRevision == director.getFieldRevision() )
            return;
        syncTiles( manager, director );
        _shownRevision = director.getFieldRevision();
    }

    void FarmPlotViewComponent::submitInput( const GameModeInput& input, FarmPlotGameModeComponent& director )
    {
        (void)director.submitFastForward( input.isActionDown( _fastForwardAction ) ); // 열린 판인지는 위에서 보았다
    }

    void FarmPlotViewComponent::syncTiles( GameObjectManager& manager, const FarmPlotGameModeComponent& director )
    {
        const int32 width  = director.getFieldWidth();
        const int32 height = director.getFieldHeight();
        for ( int32 y = 0; y < height; ++y )
        {
            for ( int32 x = 0; x < width; ++x )
            {
                const FarmTile* const pTile = director.getField().findTile( x, y );
                _tileViews.show( static_cast<size_t>( y * width + x ), pTile != nullptr ? FarmPlotViewComponentInternal::findTileLook( *pTile ) : uint8{ kTileLookNone } );
            }
        }
        _tileViews.flush( [&manager]( GameObjectHandle& inoutHandle )
        {
            FarmPlotViewComponentInternal::destroyView( manager, inoutHandle );
            inoutHandle = GameObjectHandle{};
        }, [this, &manager, width]( size_t tileIndex, uint8 look )
        {
            const float32 x = static_cast<float32>( static_cast<int32>( tileIndex ) % width ) + FarmPlotViewComponentInternal::kHalfTile;
            const float32 y = static_cast<float32>( static_cast<int32>( tileIndex ) / width ) + FarmPlotViewComponentInternal::kHalfTile;
            return spawnTile( manager, float3{ x * _tileSpacing, 0.0f, y * _tileSpacing }, look );
        } );
    }

    GameObjectHandle FarmPlotViewComponent::spawnTile( GameObjectManager& manager, const float3& center, uint8 look )
    {
        AssetManager* const pAssets = game::findSubsystem<AssetManager>();
        GameObject* const   pView   = pAssets != nullptr && _tilePrefab.empty() == false ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, _tilePrefab, SpawnLifetime::Derived, "FarmPlotTile" ) : nullptr;
        if ( pView == nullptr )
            return GameObjectHandle{}; // 읽지 못한 까닭은 프리팹 캐시가 남겼다
        MeshComponent* const pMesh = pView->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            pMesh->setLocalPosition( manager.getWorldOrigin().toLocal( center ) ); // 밭 자리는 절대 자리다
            const float4& color = look == kTileLookReady ? _readyColor : look == kTileLookWatered ? _wateredColor
                                                                   : look == kTileLookTilled      ? _tilledColor
                                                                                                  : _grassColor;
            if ( _tintCache.apply( *pMesh, color ) == false )
                SW_LOG_ERROR_ONCE_ID( "FarmPlotView.TileWithoutMaterial", "[FarmPlot]: prefab '%#' has a mesh without a material - it keeps the prefab colour", _tilePrefab.c_str() );
        }
        return pView->getHandle();
    }
} // namespace sw

#include "FarmPlotViewComponent.cpp.gen.inl"
