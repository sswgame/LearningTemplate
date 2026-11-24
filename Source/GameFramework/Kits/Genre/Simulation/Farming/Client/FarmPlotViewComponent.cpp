#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Client/FarmPlotViewComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmPlotGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        /** @brief 상태를 싣기 전에 세운 모습을 걷습니다 — 씬 섹션에 실리면 복원한 뒤 다시 세운 것과 겹친다. */
        const StatefulComponentRegistrar<FarmPlotViewComponent> s_viewRegistration{ "FarmPlotViewComponent" };

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
        : _tilePrefab{}
        , _fastForwardAction{}
        , _readyColor{ 1.60f, 1.35f, 0.55f, 1.0f } // 메시 정점 색이 이미 흙색이라 상태 색은 그 위에 곱하는 틴트다
        , _wateredColor{ 0.60f, 0.58f, 0.62f, 1.0f }
        , _tilledColor{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _grassColor{ 0.80f, 1.30f, 0.60f, 1.0f }
        , _tileSpacing{ 1.0f }
        , _director{}
        , _tintCache{}
        , _tileViews{}
        , _shownRevision{ 0 }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FarmPlotViewComponent::~FarmPlotViewComponent() = default;

    void FarmPlotViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                      pOwner    = getOwner();
        const FarmPlotGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<FarmPlotGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "FarmPlot.ViewWithoutGameMode", "Farm plot view on '%#': no FarmPlotGameModeComponent in front of it on the same object - the view does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<FarmPlotGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<FarmPlotGameModeComponent>( pDirector );
    }

    void FarmPlotViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _bActionsValid = _fastForwardAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "FarmPlot.ViewActionMissing", "Farm plot view: the fast forward action must be named - the view reads no input" );
    }

    void FarmPlotViewComponent::onEndPlay()
    {
        despawnViews();
        Component::onEndPlay();
    }

    void FarmPlotViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령 · 스폰은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( FarmPlotViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
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

    void FarmPlotViewComponent::updateView( GameObjectManager& manager )
    {
        FarmPlotGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
        {
            const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
            (void)pDirector->submitFastForward( input.isActionDown( _fastForwardAction ) ); // 열린 판인지는 위에서 보았다
        }
        if ( _shownRevision == pDirector->getFieldRevision() )
            return;
        syncTiles( manager, *pDirector );
        _shownRevision = pDirector->getFieldRevision();
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
        GameObject* const   pView   = pAssets != nullptr && _tilePrefab.empty() == false ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, _tilePrefab, "FarmPlotTile" ) : nullptr;
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
