#include "pch.h"

#include "Engine/Object/Component/2D/TileMapRendererComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GpuSpriteInstanceData.h"
#include "Engine/Object/Component/2D/SpriteRenderUtil.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/AABB.h"
#include "Engine/Physics/PhysicsWorld.h"

namespace sw
{
    SW_LOG_CALLER( "TileMapRenderer" );

    namespace
    {
        struct TileMapRendererComponentInternal
        {
            /** @brief 한 레이어가 칸마다 항목을 드는 상한입니다(청크로 나누지 않으므로 큰 맵은 여러 레이어 · 오브젝트로 나눕니다). */
            static constexpr size_t kMaxCellCount = 65536;
        };
    } // namespace

    TileMapRendererComponent::TileMapRendererComponent()
        : _tileMapPath{}
        , _materialPath{ "engine/materials/sprite2dpixel.material" }
        , _sortingLayer{ "Default" }
        , _orderInLayer{ 0 }
        , _colliderLayer{ 0 }
        , _bGenerateColliders{ true }
        , _map{}
        , _tileSet{}
        , _listBrushIndex{}
        , _listAnimatedCell{}
        , _listSolidRect{}
        , _listOutlineEdge{}
        , _listBody{}
        , _batch{}
        , _lastOwnerWorld{ float4x4::Identity }
        , _elapsedSeconds{ 0.0f }
        , _bLoaded{ SW_FALSE }
    {
    }

    void TileMapRendererComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        if ( _bLoaded == SW_FALSE && _tileMapPath.empty() == false && loadTileMap() == false )
            return;
        if ( _bLoaded == SW_TRUE )
            (void)rebuild(); // 관리자가 없으면(소유자 없음) 그릴 곳이 없다 — 알릴 것도 없다
    }

    void TileMapRendererComponent::onEndPlay()
    {
        removePhysicsBodies();
        _batch.shutdown();
        Component::onEndPlay();
    }

    void TileMapRendererComponent::onTick( float32 deltaTime )
    {
        if ( _batch.isInitialized() == false )
            return;
        _elapsedSeconds += deltaTime;
        // 오브젝트가 움직였으면 모든 칸을 다시 놓는다(타일맵은 대개 서 있다 — 행렬 비교 한 번).
        const float4x4 ownerWorld = getOwnerWorld();
        if ( Memory::compare( &ownerWorld, &_lastOwnerWorld, sizeof( ownerWorld ) ) != 0 )
        {
            _lastOwnerWorld = ownerWorld;
            for ( int32 y = 0; y < _map._height; ++y )
            {
                for ( int32 x = 0; x < _map._width; ++x )
                    refreshCellEntry( x, y );
            }
            return;
        }
        for ( const uint32 cellIndex : _listAnimatedCell )
            refreshCellEntry( static_cast<int32>( cellIndex % static_cast<uint32>( _map._width ) ), static_cast<int32>( cellIndex / static_cast<uint32>( _map._width ) ) );
    }

    bool TileMapRendererComponent::loadTileMap()
    {
        TileMapXmlData map;
        if ( map.load( _tileMapPath ) == false )
            return false;
        if ( map._tileSetPath.empty() )
        {
            SW_LOG_ERROR( "Tile map '%#' has no tile layer (<tileLayer tileSet=...>)", _tileMapPath );
            return false;
        }
        TileSetAsset tileSet;
        if ( tileSet.loadFromResource( map._tileSetPath ) == false )
            return false;
        setTileMapData( map, tileSet );
        return _bLoaded == SW_TRUE;
    }

    void TileMapRendererComponent::setTileMapData( const TileMapXmlData& map, const TileSetAsset& tileSet )
    {
        _map               = map;
        _tileSet           = tileSet;
        _bLoaded           = SW_FALSE;
        const size_t count = static_cast<size_t>( _map._width ) * static_cast<size_t>( _map._height );
        if ( count == 0 || count > TileMapRendererComponentInternal::kMaxCellCount )
        {
            SW_LOG_ERROR( "Tile map '%#' has %# cells - a tile layer holds 1..%#", _map._name, static_cast<uint32>( count ),
                          static_cast<uint32>( TileMapRendererComponentInternal::kMaxCellCount ) );
            return;
        }
        // 팔레트의 이름은 타일셋에 있어야 한다 — 모르는 이름은 데이터 오류다(그 칸은 비운다).
        if ( _map.mapTileCells( _tileSet, _listBrushIndex ) == false )
            SW_LOG_ERROR( "Tile map '%#' paints a brush that tile set '%#' does not have - those cells are empty", _map._name, _map._tileSetPath );
        _bLoaded = SW_TRUE;
    }

    bool TileMapRendererComponent::rebuild()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bLoaded == SW_FALSE )
            return false;
        const uint32 count = static_cast<uint32>( _listBrushIndex.size() );
        _batch.setSorting( _sortingLayer, _orderInLayer );
        if ( _batch.initialize( *pManager, _tileSet.getAtlasPath(), count, _materialPath ) == false )
            return false;
        _lastOwnerWorld = getOwnerWorld();
        _listAnimatedCell.clear();
        for ( int32 y = 0; y < _map._height; ++y )
        {
            for ( int32 x = 0; x < _map._width; ++x )
                refreshCellEntry( x, y );
        }
        refreshCollision();
        return true;
    }

    bool TileMapRendererComponent::setTileBrush( int32 x, int32 y, string_view brushName )
    {
        if ( _bLoaded == SW_FALSE || x < 0 || y < 0 || x >= _map._width || y >= _map._height )
            return false;
        uint16 value = 0;
        if ( brushName.empty() == false )
        {
            const int32 brush = _tileSet.findBrush( hashed_string( brushName ) );
            if ( brush < 0 )
            {
                SW_LOG_ERROR( "Tile set '%#' has no brush '%#'", _map._tileSetPath, brushName );
                return false;
            }
            value = static_cast<uint16>( brush + 1 );
        }
        (void)_map.setTileBrush( x, y, brushName ); // 칸 범위는 위에서 봤다
        _listBrushIndex[indexOf( x, y )] = value;
        if ( _batch.isInitialized() )
        {
            // 규칙 타일은 이웃을 보므로 칠한 칸과 이웃 여덟 칸을 다시 고른다.
            for ( int32 offsetY = -1; offsetY <= 1; ++offsetY )
            {
                for ( int32 offsetX = -1; offsetX <= 1; ++offsetX )
                {
                    const int32 cellX = x + offsetX;
                    const int32 cellY = y + offsetY;
                    if ( 0 <= cellX && cellX < _map._width && 0 <= cellY && cellY < _map._height )
                        refreshCellEntry( cellX, cellY );
                }
            }
            refreshCollision();
        }
        return true;
    }

    int32 TileMapRendererComponent::getDisplayedCell( int32 x, int32 y ) const
    {
        const TileVisual* pVisual = _tileSet.resolveVisual( _listBrushIndex, _map._width, _map._height, x, y );
        return ( pVisual != nullptr ) ? pVisual->computeCellAt( _elapsedSeconds ) : -1;
    }

    float3 TileMapRendererComponent::computeCellCenter( int32 x, int32 y ) const
    {
        const float32 size = _tileSet.getTileSize();
        const float3  local{ ( static_cast<float32>( x ) + 0.5f ) * size, -( static_cast<float32>( y ) + 0.5f ) * size, 0.0f };
        return float3::transform( local, getOwnerWorld() );
    }

    void TileMapRendererComponent::computeWorldOutline( vector<float4>& outListSegment, vector<float2>& outListOutward ) const
    {
        outListSegment.clear();
        outListOutward.clear();
        const float32  size  = _tileSet.getTileSize();
        const float4x4 world = getOwnerWorld();
        for ( const TileEdge& edge : _listOutlineEdge )
        {
            // 격자는 y 가 아래로, 월드는 위로 자란다.
            const float3 start = float3::transform( float3{ static_cast<float32>( edge._start._x ) * size, -static_cast<float32>( edge._start._y ) * size, 0.0f }, world );
            const float3 end   = float3::transform( float3{ static_cast<float32>( edge._end._x ) * size, -static_cast<float32>( edge._end._y ) * size, 0.0f }, world );
            outListSegment.push_back( float4{ start._x, start._y, end._x, end._y } );
            outListOutward.push_back( float2{ static_cast<float32>( edge._outward._x ), -static_cast<float32>( edge._outward._y ) } );
        }
    }

    void TileMapRendererComponent::computeNavCosts( vector<uint8>& outListCost ) const
    {
        constexpr uint8 kEmptyCost = 10;
        TileGridUtil::makeNavCosts( _tileSet, _listBrushIndex, kEmptyCost, outListCost );
    }

    void TileMapRendererComponent::setSorting( const hashed_string& layerName, int32 orderInLayer )
    {
        _sortingLayer = layerName;
        _orderInLayer = orderInLayer;
        _batch.setSorting( _sortingLayer, _orderInLayer );
    }

    void TileMapRendererComponent::refreshCellEntry( int32 x, int32 y )
    {
        const uint32      entry   = static_cast<uint32>( indexOf( x, y ) );
        const TileVisual* pVisual = _tileSet.resolveVisual( _listBrushIndex, _map._width, _map._height, x, y );
        if ( pVisual == nullptr || pVisual->_listFrameCell.empty() )
        {
            _batch.setEntryVisible( entry, false );
            return;
        }
        if ( pVisual->isAnimated() && std::find( _listAnimatedCell.begin(), _listAnimatedCell.end(), entry ) == _listAnimatedCell.end() )
            _listAnimatedCell.push_back( entry );
        const float32  size = _tileSet.getTileSize();
        const float3   local{ ( static_cast<float32>( x ) + 0.5f ) * size, -( static_cast<float32>( y ) + 0.5f ) * size, 0.0f };
        const float4x4 world = SpriteInstanceBatch::makeQuadWorld( local, size, size ) * _lastOwnerWorld;
        const float4   uv    = _tileSet.computeCellUvRect( pVisual->computeCellAt( _elapsedSeconds ) );
        _batch.setEntry( entry, world, uv, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    }

    void TileMapRendererComponent::refreshCollision()
    {
        vector<uint8> listSolid;
        TileGridUtil::makeSolidMask( _tileSet, _listBrushIndex, listSolid );
        TileGridUtil::mergeSolidRectangles( listSolid, _map._width, _map._height, _listSolidRect );
        TileGridUtil::traceSolidOutline( listSolid, _map._width, _map._height, _listOutlineEdge );

        removePhysicsBodies();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( _bGenerateColliders == false || pManager == nullptr )
            return;
        const float32  size  = _tileSet.getTileSize();
        const float4x4 world = getOwnerWorld();
        for ( const TileRect& rect : _listSolidRect )
        {
            const float3 cornerA = float3::transform( float3{ static_cast<float32>( rect._x ) * size, -static_cast<float32>( rect._y ) * size, 0.0f }, world );
            const float3 cornerB = float3::transform(
                float3{ static_cast<float32>( rect._x + rect._width ) * size, -static_cast<float32>( rect._y + rect._height ) * size, 0.0f }, world );
            AABB box{};
            box._min = float3{ MathUtil::min( cornerA._x, cornerB._x ), MathUtil::min( cornerA._y, cornerB._y ), 0.0f };
            box._max = float3{ MathUtil::max( cornerA._x, cornerB._x ), MathUtil::max( cornerA._y, cornerB._y ), 0.0f };
            _listBody.push_back( pManager->getPhysicsWorld().addBody( box, static_cast<uint8>( _colliderLayer ), pOwner->getObjectId() ) );
        }
    }

    void TileMapRendererComponent::removePhysicsBodies()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
        {
            for ( const SlotHandle& body : _listBody )
                pManager->getPhysicsWorld().removeBody( body );
        }
        _listBody.clear();
    }

    float4x4 TileMapRendererComponent::getOwnerWorld() const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pRoot  = ( pOwner != nullptr ) ? pOwner->getPrimarySceneComponent() : nullptr;
        return ( pRoot != nullptr ) ? pRoot->getWorldMatrix() : float4x4::Identity;
    }
} // namespace sw
