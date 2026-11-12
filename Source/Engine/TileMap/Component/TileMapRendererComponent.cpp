#include "pch.h"

#include "Engine/TileMap/Component/TileMapRendererComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/TileMap/TileMapXML.h"
#include "Engine/Visual/2D/SortingLayerNameValidation.h"
#include "Engine/Visual/2D/SpriteInstanceBatch.h"
#include "Engine/Visual/2D/SpriteRenderUtil.h"

namespace sw
{
    TileMapRendererComponent::TileMapRendererComponent()
        : _tileMapPath{}
        , _materialPath{}
        , _sortingLayer{ "Default" }
        , _orderInLayer{ 0 }
        , _colliderLayer{ 0 }
        , _colliderMaterial{}
        , _bGenerateColliders{ true }
        , _map{}
        , _pTileSet{}
        , _chunkGrid{}
        , _listBrushIndex{}
        , _listAnimatedFlag{}
        , _listAnimatedCell{}
        , _listSolidRect{}
        , _listOutlineEdge{}
        , _listChunkBody{}
        , _listChunkDirty{}
        , _colliderScale{ 1.0f, 1.0f }
        , _listChunkBatch{}
        , _lastOwnerWorld{ float4x4::Identity }
        , _pLastOwnerRoot{ nullptr }
        , _lastOwnerVersion{ 0 }
        , _tileSetReload{}
        , _elapsedSeconds{ 0.0f }
        , _bLoaded{ SW_FALSE }
        , _bChunkBuildQueued{ SW_FALSE }
        , _bCollisionDirty{ SW_FALSE }
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

    void TileMapRendererComponent::onTick( TickContext& context )
    {
        // 타일셋을 다시 읽었으면(핫 리로드 — 떼어 낸 새 객체) 칠한 맵은 그대로 두고 새 타일셋으로 번호 · 배치를 다시 짓는다.
        if ( _pTileSet != nullptr && _tileSetReload.takeChanged() )
        {
            shared_ptr<const TileSetAsset> pReloaded = SharedAssetCache<TileSetAsset>::acquire( _map._tileSetPath );
            if ( pReloaded != nullptr && pReloaded != _pTileSet )
            {
                const TileMapXMLData map = _map;
                setTileMapData( map, std::move( pReloaded ) );
                if ( _bLoaded == SW_TRUE )
                    (void)rebuild();
            }
        }
        flushCollision(); // 이번 틱까지 칠한 칸의 청크 바디를 한 번에(칸마다 맵 전체를 다시 만들지 않는다)
        // 오브젝트가 움직였는지 본다. 타일맵은 대개 서 있다 — 장면 컴포넌트와 월드 변경 번호가 그대로면 행렬도 읽지 않는다.
        // 번호가 바뀌어도 행렬이 같으면(같은 값으로 다시 합성) 칸을 다시 놓지 않도록 행렬 비교는 남긴다.
        const GameObject*     pOwner         = getOwner();
        const SceneComponent* pRoot          = ( pOwner != nullptr ) ? pOwner->getPrimarySceneComponent() : nullptr;
        const uint32          version        = ( pRoot != nullptr ) ? pRoot->getWorldTransformVersion() : 0u;
        const bool            bSameTransform = pRoot == _pLastOwnerRoot && version == _lastOwnerVersion;
        _pLastOwnerRoot                      = pRoot;
        _lastOwnerVersion                    = version;
        const float4x4 ownerWorld            = bSameTransform ? _lastOwnerWorld : getOwnerWorld();
        const bool     bOwnerMoved           = bSameTransform == false && Memory::compare( &ownerWorld, &_lastOwnerWorld, sizeof( ownerWorld ) ) != 0;
        if ( bOwnerMoved )
        {
            _lastOwnerWorld = ownerWorld;
            // 충돌은 그리기 배치와 따로 오너를 따른다(그림 없는 서버에서도) — 예전에는 만들 때의 월드 자리에 남았다(감사 P0-14).
            followOwnerTransform();
        }
        if ( _listChunkBatch.empty() )
            return;
        _elapsedSeconds += context.getDeltaTime();
        if ( bOwnerMoved )
        {
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
            SW_LOG_ERROR_ID( "TileMapRenderer.TileMapNoTile", "Tile map '%#' has no tile layer (<tileLayer tileSet=...>)", _tileMapPath.c_str() );
            return false;
        }
        // 타일셋은 공유 캐시의 한 벌이다 — 같은 타일셋을 쓰는 맵이 파일을 다시 읽지 않는다.
        _tileSetReload.markSeen();
        shared_ptr<const TileSetAsset> pTileSet = SharedAssetCache<TileSetAsset>::acquire( map._tileSetPath );
        if ( pTileSet == nullptr )
            return false;
        setTileMapData( map, std::move( pTileSet ) );
        return _bLoaded == SW_TRUE;
    }

    const TileSetAsset& TileMapRendererComponent::getTileSet() const
    {
        // 읽기 전의 상태는 빈 타일셋으로 읽는다 — 값을 지어내는 대체가 아니라 "그릴 브러시 없음" 이다.
        static const TileSetAsset s_noTileSet{};
        return _pTileSet != nullptr ? *_pTileSet : s_noTileSet;
    }

    void TileMapRendererComponent::setTileMapData( const TileMapXMLData& map, shared_ptr<const TileSetAsset> pTileSet )
    {
        _map      = map;
        _pTileSet = std::move( pTileSet );
        _bLoaded  = SW_FALSE;
        if ( _pTileSet == nullptr )
            return;
        const size_t count = static_cast<size_t>( _map._width ) * static_cast<size_t>( _map._height );
        if ( count == 0 || count > kMaxCellCount )
        {
            SW_LOG_ERROR_ID( "TileMapRenderer.TileMapCellsTile", "Tile map '%#' has %# cells - a tile layer holds 1..%#", _map._name, static_cast<uint32>( count ), static_cast<uint32>( kMaxCellCount ) );
            return;
        }
        // 팔레트의 이름은 타일셋에 있어야 한다 — 모르는 이름은 데이터 오류다(그 칸은 비운다).
        if ( _map.mapTileCells( getTileSet(), _listBrushIndex ) == false )
            SW_LOG_ERROR_ID( "TileMapRenderer.TileMapPaintsBrush", "Tile map '%#' paints a brush that tile set '%#' does not have - those cells are empty", _map._name, _map._tileSetPath );
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
        if ( _bLoaded == SW_FALSE )
            return false;
        // 충돌은 그리기 배치를 못 만들어도(그림 없는 서버 · 머티리얼 실패) 짓는다 — 그림 실패는 반환값으로 알린다.
        const bool bDrawn = rebuildChunks();
        refreshCollision();
        return bDrawn;
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
                if ( _listBrushIndex[getIndexOf( x, y )] != 0 && ensureChunkBatch( _chunkGrid.computeChunkIndex( x, y ) ) == false )
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
        const string& materialPath = _materialPath.empty() ? engine::getSubsystem<EngineDefaultAssets>()._tileMapMaterial.get() : _materialPath.path();
        if ( batch->initialize( *pManager, getTileSet().getAtlasPath(), count, materialPath, getTileSet().getNormalAtlasPath() ) == false )
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
            const optional<uint32> brush = getTileSet().findBrush( hashed_string( brushName ) );
            if ( brush.has_value() == false )
            {
                SW_LOG_ERROR_ID( "TileMapRenderer.TileSetNoBrush", "Tile set '%#' has no brush '%#'", _map._tileSetPath, brushName );
                return false;
            }
            value = static_cast<uint16>( *brush + 1u );
        }
        (void)_map.setTileBrush( x, y, brushName ); // 칸 범위는 위에서 봤다
        _listBrushIndex[getIndexOf( x, y )] = value;
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
        }
        // 충돌은 그리기 배치와 따로 표시한다. 단단함은 그 칸의 브러시만 보므로 이웃 청크는 그대로다(바디를 아직 짓지 않았으면 표시는 아무것도 하지 않는다).
        markCollisionDirty( _chunkGrid.computeChunkIndex( x, y ) );
        return true;
    }

    optional<int32> TileMapRendererComponent::getDisplayedCell( int32 x, int32 y ) const
    {
        const TileVisual* pVisual = getTileSet().resolveVisual( _listBrushIndex, _map._width, _map._height, x, y );
        return ( pVisual != nullptr ) ? pVisual->computeCellAt( _elapsedSeconds ) : nullopt;
    }

    float3 TileMapRendererComponent::computeCellCenter( int32 x, int32 y ) const
    {
        const float32 size = getTileSet().getTileSize();
        const float3  local{ ( static_cast<float32>( x ) + 0.5f ) * size, -( static_cast<float32>( y ) + 0.5f ) * size, 0.0f };
        return float3::transform( local, getOwnerWorld() );
    }

    void TileMapRendererComponent::computeWorldOutline( vector<float4>& outListSegment, vector<float2>& outListOutward ) const
    {
        outListSegment.clear();
        outListOutward.clear();
        const float32  size  = getTileSet().getTileSize();
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
        TileGridUtil::makeNavCosts( getTileSet(), _listBrushIndex, kEmptyCost, outListCost );
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
        const TileVisual*                pVisual    = getTileSet().resolveVisual( _listBrushIndex, _map._width, _map._height, x, y );
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
                            if ( tiles._listBrushIndex[tiles.getIndexOf( cellX, cellY )] != 0 )
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
        const size_t cellIndex = getIndexOf( x, y );
        if ( pVisual->isAnimated() && _listAnimatedFlag[cellIndex] == 0 )
        {
            _listAnimatedFlag[cellIndex] = 1;
            _listAnimatedCell.push_back( static_cast<uint32>( cellIndex ) );
        }
        const float32  size = getTileSet().getTileSize();
        const float3   local{ ( static_cast<float32>( x ) + 0.5f ) * size, -( static_cast<float32>( y ) + 0.5f ) * size, 0.0f };
        const float4x4 world = SpriteInstanceBatch::makeQuadWorld( local, size, size ) * _lastOwnerWorld;
        const float4   uv    = getTileSet().computeCellUVRect( pVisual->computeCellAt( _elapsedSeconds ).value() ); // 읽은 타일셋의 모습은 셀이 있다(collectIssues)
        pBatch->setEntry( entry, world, uv, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    }

    void TileMapRendererComponent::refreshCollision()
    {
        _listChunkDirty.assign( static_cast<size_t>( _chunkGrid.getChunkCount() ), 1u );
        _bCollisionDirty = SW_TRUE;
        flushCollision();
    }

    void TileMapRendererComponent::markCollisionDirty( int32 chunkIndex )
    {
        if ( chunkIndex < 0 || static_cast<size_t>( chunkIndex ) >= _listChunkDirty.size() )
            return;
        _listChunkDirty[static_cast<size_t>( chunkIndex )] = 1u;
        _bCollisionDirty                                   = SW_TRUE;
    }

    void TileMapRendererComponent::flushCollision()
    {
        if ( _bCollisionDirty == SW_FALSE )
            return;
        _bCollisionDirty = SW_FALSE;
        vector<uint8> listSolid;
        TileGridUtil::makeSolidMask( getTileSet(), _listBrushIndex, listSolid );
        TileGridUtil::mergeSolidRectangles( listSolid, _map._width, _map._height, _listSolidRect );
        TileGridUtil::traceSolidOutline( listSolid, _map._width, _map._height, _listOutlineEdge );

        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( _bGenerateColliders == false || pManager == nullptr )
        {
            removePhysicsBodies();
            _listChunkDirty.assign( _listChunkDirty.size(), 0u );
            return;
        }
        // 타일 충돌은 씬의 2D 물리 씬의 정적 바디다(유니티 TilemapCollider2D) — 강체를 막고, 2D 콜라이더(센서)가 겹침으로 본다.
        // 청크마다 바디 하나(셰이프 여럿)라 칸 하나를 칠하면 그 청크만 다시 만든다 — 다른 청크에 닿은 쌍은 End → Begin 을 거짓으로 받지 않는다
        // (유니티 TilemapCollider2D 의 늦은 반영 · Godot TileMapLayer 의 사분면 바디).
        IPhysicsScene2D* pScene = pManager->getSubsystem<sw::ScenePhysics>().getScene2D();
        if ( pScene == nullptr )
            return;
        _listChunkBody.resize( static_cast<size_t>( _chunkGrid.getChunkCount() ) );
        for ( int32 chunkIndex = 0; chunkIndex < _chunkGrid.getChunkCount(); ++chunkIndex )
        {
            if ( _listChunkDirty[static_cast<size_t>( chunkIndex )] != 0 )
                rebuildChunkBody( chunkIndex, listSolid, *pScene );
            _listChunkDirty[static_cast<size_t>( chunkIndex )] = 0u;
        }
    }

    void TileMapRendererComponent::rebuildChunkBody( int32 chunkIndex, const vector<uint8>& listSolid, IPhysicsScene2D& scene )
    {
        PhysicsBodyHandle& body = _listChunkBody[static_cast<size_t>( chunkIndex )];
        if ( scene.isBodyValid( body ) )
            scene.destroyBody( body );
        body = PhysicsBodyHandle{};

        const TileRect chunk = _chunkGrid.computeChunkRect( chunkIndex );
        vector<uint8>  listChunkSolid( static_cast<size_t>( chunk._width ) * static_cast<size_t>( chunk._height ), 0u );
        for ( int32 y = 0; y < chunk._height; ++y )
        {
            for ( int32 x = 0; x < chunk._width; ++x )
                listChunkSolid[static_cast<size_t>( y * chunk._width + x )] = listSolid[getIndexOf( chunk._x + x, chunk._y + y )];
        }
        vector<TileRect> listRect;
        TileGridUtil::mergeSolidRectangles( listChunkSolid, chunk._width, chunk._height, listRect );
        if ( listRect.empty() )
            return;

        float2  origin{};
        float32 angle{ 0.0f };
        computeColliderFrame( origin, angle, _colliderScale );
        const float32     size = getTileSet().getTileSize();
        PhysicsBodyDesc2D desc;
        desc._position = origin;
        desc._rotation = angle;
        desc._type     = PhysicsBodyType::Static;
        desc._layer    = static_cast<uint8>( _colliderLayer );
        desc._material = _colliderMaterial;
        desc._userData = getOwner()->getObjectID();
        for ( const TileRect& rect : listRect )
        {
            // 셰이프는 맵 원점(오브젝트 자리) 기준이다 — 칸 (x, y) 는 오른쪽 · 아래(-Y)로 간다. 바디는 배율을 갖지 않아 축 배율을 곱한다.
            PhysicsShapeDesc2D shape;
            shape._type          = PhysicsShapeType2D::Box;
            shape._halfExtents   = float2{ static_cast<float32>( rect._width ) * size * 0.5f * MathUtil::abs( _colliderScale._x ),
                                         static_cast<float32>( rect._height ) * size * 0.5f * MathUtil::abs( _colliderScale._y ) };
            shape._localPosition = float2{ ( static_cast<float32>( chunk._x + rect._x ) + static_cast<float32>( rect._width ) * 0.5f ) * size * _colliderScale._x,
                                           -( static_cast<float32>( chunk._y + rect._y ) + static_cast<float32>( rect._height ) * 0.5f ) * size * _colliderScale._y };
            desc._listShape.push_back( shape );
        }
        body = scene.createBody( desc );
    }

    void TileMapRendererComponent::followOwnerTransform()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        IPhysicsScene2D*   pScene   = pManager != nullptr ? pManager->getSubsystem<sw::ScenePhysics>().findScene2D() : nullptr;
        if ( pScene == nullptr )
            return;
        float2  origin{};
        float32 angle{ 0.0f };
        float2  scale{};
        computeColliderFrame( origin, angle, scale );
        const bool bScaleChanged = MathUtil::abs( scale._x - _colliderScale._x ) > MathUtil::kEpsilon || MathUtil::abs( scale._y - _colliderScale._y ) > MathUtil::kEpsilon;
        if ( bScaleChanged )
        {
            refreshCollision(); // 셰이프 크기가 바뀐다 — 다시 만든다(배율을 바꾸는 타일맵은 드물다)
            return;
        }
        for ( const PhysicsBodyHandle& body : _listChunkBody )
        {
            if ( pScene->isBodyValid( body ) )
                pScene->setBodyTransform( body, origin, angle );
        }
    }

    void TileMapRendererComponent::computeColliderFrame( float2& outOrigin, float32& outAngle, float2& outScale ) const
    {
        const float4x4 world  = getOwnerWorld();
        const float3   origin = float3::transform( float3{ 0.0f, 0.0f, 0.0f }, world );
        const float3   axisX  = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, world ) - origin;
        const float3   axisY  = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, world ) - origin;
        outOrigin             = float2{ origin._x, origin._y };
        outAngle              = MathUtil::atan2( axisX._y, axisX._x );
        // 거울(음수 배율)은 회전으로 담지 못한다 — Y 배율의 부호로 실어 셰이프 자리를 뒤집는다(Godot 은 변환 행렬째, 유니티는 셰이프를 다시 굽는다).
        const float32 handedness = axisX._x * axisY._y - axisX._y * axisY._x;
        const float32 lengthY    = MathUtil::sqrt( axisY._x * axisY._x + axisY._y * axisY._y );
        outScale                 = float2{ MathUtil::sqrt( axisX._x * axisX._x + axisX._y * axisX._y ), handedness < 0.0f ? -lengthY : lengthY };
    }

    void TileMapRendererComponent::removePhysicsBodies()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        IPhysicsScene2D*   pScene   = pManager != nullptr ? pManager->getSubsystem<sw::ScenePhysics>().findScene2D() : nullptr;
        if ( pScene != nullptr )
        {
            for ( const PhysicsBodyHandle& body : _listChunkBody )
            {
                if ( pScene->isBodyValid( body ) )
                    pScene->destroyBody( body );
            }
        }
        _listChunkBody.clear();
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
    void TileMapRendererComponent::validateColliderMaterial( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateMaterial( context, "_colliderMaterial", _colliderMaterial );
    }

    void TileMapRendererComponent::validateSortingLayer( ValidationContext& context ) const
    {
        SortingLayerNameValidation::validate( context, "_sortingLayer", _sortingLayer );
    }
} // namespace sw
