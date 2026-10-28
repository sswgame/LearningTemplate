#include "pch.h"

#include "Engine/TileMap/Component/TileMapRendererComponent.h"

#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"

#include "Engine/2D/SpriteInstanceBatch.h"
#include "Engine/2D/SpriteRenderUtil.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Binding/GPUSpriteInstanceData.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    SW_LOG_CALLER( "TileMapRenderer" );

    TileMapRendererComponent::TileMapRendererComponent()
        : _tileMapPath{}
        , _materialPath{}
        , _sortingLayer{ "Default" }
        , _orderInLayer{ 0 }
        , _colliderLayer{ 0 }
        , _bGenerateColliders{ true }
        , _map{}
        , _tileSet{}
        , _chunkGrid{}
        , _listBrushIndex{}
        , _listAnimatedFlag{}
        , _listAnimatedCell{}
        , _listSolidRect{}
        , _listOutlineEdge{}
        , _listBody{}
        , _listChunkBatch{}
        , _lastOwnerWorld{ float4x4::Identity }
        , _pLastOwnerRoot{ nullptr }
        , _lastOwnerVersion{ 0 }
        , _elapsedSeconds{ 0.0f }
        , _bLoaded{ SW_FALSE }
        , _bChunkBuildQueued{ SW_FALSE }
    {
    }

    TileMapRendererComponent::~TileMapRendererComponent()
    {
        releaseChunks();
    }

    void TileMapRendererComponent::onUnregister( GameObjectManager& manager )
    {
        removePhysicsBodies();
        releaseChunks();
        Component::onUnregister( manager );
    }

    void TileMapRendererComponent::onPostLoad()
    {
        Component::onPostLoad();
        // 편집 중 미리보기 — 플레이 전에도 보이게 그리기 배치만 만든다(바디는 플레이 시작의 rebuild 가 만든다).
        if ( _tileMapPath.empty() == false && loadTileMap() )
            (void)rebuildChunks(); // 관리자가 없으면(소유자 없음) 그릴 곳이 없다 — 알릴 것도 없다
    }

    void TileMapRendererComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const bool bLoadFailed = _bLoaded == SW_FALSE && _tileMapPath.empty() == false && loadTileMap() == false;
        if ( bLoadFailed )
            return;
        if ( _bLoaded == SW_TRUE )
            (void)rebuild(); // 관리자가 없으면(소유자 없음) 그릴 곳이 없다 — 알릴 것도 없다
    }

    void TileMapRendererComponent::onEndPlay()
    {
        removePhysicsBodies();
        Component::onEndPlay();
    }

    void TileMapRendererComponent::onTick( float32 deltaTime )
    {
        if ( _listChunkBatch.empty() )
            return;
        _elapsedSeconds += deltaTime;
        // 오브젝트가 움직였으면 모든 칸을 다시 놓는다. 타일맵은 대개 서 있다 — 장면 컴포넌트와 월드 변경 번호가 그대로면 행렬도 읽지 않는다.
        // 번호가 바뀌어도 행렬이 같으면(같은 값으로 다시 합성) 칸을 다시 놓지 않도록 행렬 비교는 남긴다.
        const GameObject*     pOwner         = getOwner();
        const SceneComponent* pRoot          = ( pOwner != nullptr ) ? pOwner->getPrimarySceneComponent() : nullptr;
        const uint32          version        = ( pRoot != nullptr ) ? pRoot->getWorldTransformVersion() : 0u;
        const bool            bSameTransform = pRoot == _pLastOwnerRoot && version == _lastOwnerVersion;
        _pLastOwnerRoot                      = pRoot;
        _lastOwnerVersion                    = version;
        const float4x4 ownerWorld            = bSameTransform ? _lastOwnerWorld : getOwnerWorld();
        if ( bSameTransform == false && Memory::compare( &ownerWorld, &_lastOwnerWorld, sizeof( ownerWorld ) ) != 0 )
        {
            _lastOwnerWorld = ownerWorld;
            for ( int32 chunkIndex = 0; chunkIndex < _chunkGrid.getChunkCount(); ++chunkIndex )
            {
                refreshChunkCells( chunkIndex );
            }
            return;
        }
        for ( const uint32 cellIndex : _listAnimatedCell )
        {
            refreshCellEntry( static_cast<int32>( cellIndex % static_cast<uint32>( _map._width ) ), static_cast<int32>( cellIndex / static_cast<uint32>( _map._width ) ) );
        }
    }

    bool TileMapRendererComponent::loadTileMap()
    {
        TileMapXMLData map;
        if ( map.load( AssetID{ _tileMapPath } ) == false )
            return false;
        if ( map._tileSetPath.empty() )
        {
            SW_LOG_ERROR( "Tile map '%#' has no tile layer (<tileLayer tileSet=...>)", _tileMapPath );
            return false;
        }
        TileSetAsset tileSet;
        if ( tileSet.loadFromAsset( AssetID{ map._tileSetPath } ) == false )
            return false;
        setTileMapData( map, tileSet );
        return _bLoaded == SW_TRUE;
    }

    void TileMapRendererComponent::setTileMapData( const TileMapXMLData& map, const TileSetAsset& tileSet )
    {
        _map               = map;
        _tileSet           = tileSet;
        _bLoaded           = SW_FALSE;
        const size_t count = static_cast<size_t>( _map._width ) * static_cast<size_t>( _map._height );
        if ( count == 0 || count > kMaxCellCount )
        {
            SW_LOG_ERROR( "Tile map '%#' has %# cells - a tile layer holds 1..%#", _map._name, static_cast<uint32>( count ), static_cast<uint32>( kMaxCellCount ) );
            return;
        }
        // 팔레트의 이름은 타일셋에 있어야 한다 — 모르는 이름은 데이터 오류다(그 칸은 비운다).
        if ( _map.mapTileCells( _tileSet, _listBrushIndex ) == false )
            SW_LOG_ERROR( "Tile map '%#' paints a brush that tile set '%#' does not have - those cells are empty", _map._name, _map._tileSetPath );
        _chunkGrid = TileChunkGrid::make( _map._width, _map._height, kChunkSize );
        _listAnimatedFlag.assign( count, 0 );
        _listAnimatedCell.clear();
        _bLoaded = SW_TRUE;
    }

    void TileMapRendererComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_activeName( "_bActive" );
        static const hashed_string s_tileMapName( "_tileMapPath" );
        static const hashed_string s_materialName( "_materialPath" );
        static const hashed_string s_sortingLayerName( "_sortingLayer" );
        static const hashed_string s_orderName( "_orderInLayer" );
        // 켜고 끄기를 빌더가 다시 보게 — 다른 칸은 다음 rebuild 가 맞춘다.
        if ( propertyName == s_activeName )
        {
            for ( unique_ptr<SpriteInstanceBatch>& pBatch : _listChunkBatch )
            {
                if ( pBatch != nullptr )
                    pBatch->markAllEntriesDirty();
            }
            return;
        }
        // 편집 중 미리보기 — 맵이 바뀌면 다시 읽고, 머티리얼 · 정렬이 바뀌면 배치를 다시 만든다.
        if ( propertyName == s_tileMapName )
        {
            _bLoaded = SW_FALSE;
            if ( _tileMapPath.empty() == false && loadTileMap() )
                (void)rebuildChunks(); // 관리자가 없으면 그릴 곳이 없다
            else
                releaseChunks();
            return;
        }
        const bool bLooksChanged = propertyName == s_materialName || propertyName == s_sortingLayerName || propertyName == s_orderName;
        if ( bLooksChanged && _bLoaded == SW_TRUE )
            (void)rebuildChunks(); // 관리자가 없으면 그릴 곳이 없다
    }

    void TileMapRendererComponent::onOwnerActiveInHierarchyChanged()
    {
        Component::onOwnerActiveInHierarchyChanged();
        for ( unique_ptr<SpriteInstanceBatch>& pBatch : _listChunkBatch )
        {
            if ( pBatch != nullptr )
                pBatch->markAllEntriesDirty();
        }
    }

    bool TileMapRendererComponent::rebuild()
    {
        if ( rebuildChunks() == false )
            return false;
        refreshCollision();
        return true;
    }

    bool TileMapRendererComponent::rebuildChunks()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bLoaded == SW_FALSE )
            return false;
        releaseChunks();
        _listChunkBatch.resize( static_cast<size_t>( _chunkGrid.getChunkCount() ) );
        _lastOwnerWorld = getOwnerWorld();
        _listAnimatedCell.clear();
        _listAnimatedFlag.assign( _listBrushIndex.size(), 0 );
        // 칠한 칸이 있는 청크만 배치를 만든다(빈 청크는 항목을 들지 않는다).
        for ( int32 y = 0; y < _map._height; ++y )
        {
            for ( int32 x = 0; x < _map._width; ++x )
            {
                if ( _listBrushIndex[indexOf( x, y )] != 0 && ensureChunkBatch( _chunkGrid.computeChunkIndex( x, y ) ) == false )
                    return false;
            }
        }
        for ( int32 chunkIndex = 0; chunkIndex < _chunkGrid.getChunkCount(); ++chunkIndex )
        {
            refreshChunkCells( chunkIndex );
        }
        return true;
    }

    bool TileMapRendererComponent::ensureChunkBatch( int32 chunkIndex )
    {
        if ( chunkIndex < 0 || static_cast<size_t>( chunkIndex ) >= _listChunkBatch.size() )
            return false;
        unique_ptr<SpriteInstanceBatch>& pBatch = _listChunkBatch[static_cast<size_t>( chunkIndex )];
        if ( pBatch != nullptr )
            return true;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return false;
        const TileRect                  rect  = _chunkGrid.computeChunkRect( chunkIndex );
        const uint32                    count = static_cast<uint32>( rect._width * rect._height );
        unique_ptr<SpriteInstanceBatch> batch = make_unique<SpriteInstanceBatch>();
        batch->setSorting( _sortingLayer, _orderInLayer );
        batch->setOwnerComponent( this );
        // 칸이 비면 엔진 기본 타일맵 머티리얼(픽셀 아트 스프라이트)이다(값의 기준은 엔진 기본 에셋 표).
        const string& materialPath = _materialPath.empty() ? engine::getSubsystem<EngineDefaultAssets>()._tileMapMaterial.get() : _materialPath;
        if ( batch->initialize( *pManager, _tileSet.getAtlasPath(), count, materialPath, _tileSet.getNormalAtlasPath() ) == false )
            return false;
        pBatch = std::move( batch );
        return true;
    }

    void TileMapRendererComponent::releaseChunks()
    {
        // 레지스트리에서 한 번에 뺀다(청크마다 빼면 항목 목록을 청크 수만큼 접는다). 오너가 매니저에서 빠졌으면 배치마다 소멸자 경로가 뺀다.
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
        {
            SpriteInstanceBatch::shutdownAll( *pManager, _listChunkBatch );
        }
        else
        {
            for ( unique_ptr<SpriteInstanceBatch>& pBatch : _listChunkBatch )
            {
                if ( pBatch != nullptr )
                    pBatch->shutdown();
            }
        }
        _listChunkBatch.clear();
    }

    uint32 TileMapRendererComponent::getBuiltChunkCount() const
    {
        uint32 builtCount{ 0 };
        for ( const unique_ptr<SpriteInstanceBatch>& pBatch : _listChunkBatch )
        {
            if ( pBatch != nullptr )
                ++builtCount;
        }
        return builtCount;
    }

    void TileMapRendererComponent::refreshChunkCells( int32 chunkIndex )
    {
        if ( _listChunkBatch[static_cast<size_t>( chunkIndex )] == nullptr )
            return; // 빈 청크 — 놓을 칸이 없다
        const TileRect rect = _chunkGrid.computeChunkRect( chunkIndex );
        for ( int32 y = rect._y; y < rect._y + rect._height; ++y )
        {
            for ( int32 x = rect._x; x < rect._x + rect._width; ++x )
            {
                refreshCellEntry( x, y );
            }
        }
    }

    bool TileMapRendererComponent::setTileBrush( int32 x, int32 y, string_view brushName )
    {
        const bool bInvalidTileCoordinate = _bLoaded == SW_FALSE || x < 0 || y < 0 || x >= _map._width || y >= _map._height;
        if ( bInvalidTileCoordinate )
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
        if ( _listChunkBatch.empty() == false )
        {
            // 규칙 타일은 이웃을 보므로 칠한 칸과 이웃 여덟 칸을 다시 고른다.
            for ( int32 offsetY = -1; offsetY <= 1; ++offsetY )
            {
                for ( int32 offsetX = -1; offsetX <= 1; ++offsetX )
                {
                    const int32 cellX          = x + offsetX;
                    const int32 cellY          = y + offsetY;
                    const bool  bCellInsideMap = 0 <= cellX && cellX < _map._width && 0 <= cellY && cellY < _map._height;
                    if ( bCellInsideMap )
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
        for ( unique_ptr<SpriteInstanceBatch>& pBatch : _listChunkBatch )
        {
            if ( pBatch != nullptr )
                pBatch->setSorting( _sortingLayer, _orderInLayer );
        }
    }

    void TileMapRendererComponent::refreshCellEntry( int32 x, int32 y )
    {
        const int32                      chunkIndex = _chunkGrid.computeChunkIndex( x, y );
        unique_ptr<SpriteInstanceBatch>& pBatch     = _listChunkBatch[static_cast<size_t>( chunkIndex )];
        const uint32                     entry      = _chunkGrid.computeEntryIndex( x, y );
        const TileVisual*                pVisual    = _tileSet.resolveVisual( _listBrushIndex, _map._width, _map._height, x, y );
        const bool                       bVisible   = pVisual != nullptr && pVisual->_listFrameCell.empty() == false;
        if ( bVisible == false )
        {
            if ( pBatch != nullptr )
                pBatch->setEntryVisible( entry, false );
            return;
        }
        if ( pBatch == nullptr )
        {
            // 빈 청크에 칠했다. 배치를 만드는 것은 구조 변경이라 틱 중에는 틱 뒤로 미룬다(그 청크를 통째로 다시 놓는다).
            if ( _bChunkBuildQueued == SW_TRUE )
                return;
            GameObject*        pOwner   = getOwner();
            GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
            if ( pManager == nullptr )
                return;
            _bChunkBuildQueued = SW_TRUE;
            pManager->executeOrDeferPostTickOnComponent( *this, []( TileMapRendererComponent& tiles )
            {
                tiles._bChunkBuildQueued = SW_FALSE;
                for ( int32 index = 0; index < tiles._chunkGrid.getChunkCount(); ++index )
                {
                    const bool bMissing = tiles._listChunkBatch[static_cast<size_t>( index )] == nullptr;
                    if ( bMissing == false )
                        continue;
                    const TileRect rect = tiles._chunkGrid.computeChunkRect( index );
                    bool           bPainted{ false };
                    for ( int32 cellY = rect._y; cellY < rect._y + rect._height && bPainted == false; ++cellY )
                    {
                        for ( int32 cellX = rect._x; cellX < rect._x + rect._width; ++cellX )
                        {
                            if ( tiles._listBrushIndex[tiles.indexOf( cellX, cellY )] != 0 )
                            {
                                bPainted = true;
                                break;
                            }
                        }
                    }
                    if ( bPainted && tiles.ensureChunkBatch( index ) )
                        tiles.refreshChunkCells( index );
                }
            } );
            return;
        }
        const size_t cellIndex = indexOf( x, y );
        if ( pVisual->isAnimated() && _listAnimatedFlag[cellIndex] == 0 )
        {
            _listAnimatedFlag[cellIndex] = 1;
            _listAnimatedCell.push_back( static_cast<uint32>( cellIndex ) );
        }
        const float32  size = _tileSet.getTileSize();
        const float3   local{ ( static_cast<float32>( x ) + 0.5f ) * size, -( static_cast<float32>( y ) + 0.5f ) * size, 0.0f };
        const float4x4 world = SpriteInstanceBatch::makeQuadWorld( local, size, size ) * _lastOwnerWorld;
        const float4   uv    = _tileSet.computeCellUvRect( pVisual->computeCellAt( _elapsedSeconds ) );
        pBatch->setEntry( entry, world, uv, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
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
        // 타일 충돌은 씬의 2D 물리 씬의 정적 바디다(유니티 TilemapCollider2D) — 강체를 막고, 2D 콜라이더(센서)가 겹침으로 본다.
        IPhysicsScene2D* pScene = pManager->getSubsystem<sw::ScenePhysics>().getScene2D();
        if ( pScene == nullptr )
            return;
        const float32  size  = _tileSet.getTileSize();
        const float4x4 world = getOwnerWorld();
        for ( const TileRect& rect : _listSolidRect )
        {
            const float3 cornerA = float3::transform( float3{ static_cast<float32>( rect._x ) * size, -static_cast<float32>( rect._y ) * size, 0.0f }, world );
            const float3 cornerB = float3::transform(
                float3{ static_cast<float32>( rect._x + rect._width ) * size, -static_cast<float32>( rect._y + rect._height ) * size, 0.0f }, world );
            const float2       minB{ MathUtil::min( cornerA._x, cornerB._x ), MathUtil::min( cornerA._y, cornerB._y ) };
            const float2       maxB{ MathUtil::max( cornerA._x, cornerB._x ), MathUtil::max( cornerA._y, cornerB._y ) };
            PhysicsShapeDesc2D shape;
            shape._type        = PhysicsShapeType2D::Box;
            shape._halfExtents = ( maxB - minB ) * 0.5f;
            PhysicsBodyDesc2D desc;
            desc._listShape.push_back( shape );
            desc._position = ( minB + maxB ) * 0.5f;
            desc._type     = PhysicsBodyType::Static;
            desc._layer    = static_cast<uint8>( _colliderLayer );
            desc._userData = pOwner->getObjectID();
            _listBody.push_back( pScene->createBody( desc ) );
        }
    }

    void TileMapRendererComponent::removePhysicsBodies()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        IPhysicsScene2D*   pScene   = pManager != nullptr ? pManager->getSubsystem<sw::ScenePhysics>().findScene2D() : nullptr;
        if ( pScene != nullptr )
        {
            for ( const PhysicsBodyHandle& body : _listBody )
            {
                if ( pScene->isBodyValid( body ) )
                    pScene->destroyBody( body );
            }
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

namespace sw
{
    void TileMapRendererComponent::validateSortingLayer( ValidationContext& context ) const
    {
        ComponentNameValidation::validateSortingLayer( context, "_sortingLayer", _sortingLayer );
    }
} // namespace sw
