#include "pch.h"

#include "Engine/Environment/Water/WaterBodyComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "WaterBodyComponent" );

    namespace
    {
        struct WaterBodyComponentInternal
        {
            /** @brief 지형이 없을 때의 깊이(m) — 깊은 물 색이다. */
            static constexpr float32 kOpenWaterDepth = 100.0f;
            /** @brief 한 변의 격자 칸 상한 — 정점 수가 터지지 않게. */
            static constexpr uint32 kMaxCellsPerSide = 512;

            static const hashed_string& getWaveName( uint32 waveIndex )
            {
                static const hashed_string s_arrName[WaterWaveMath::kMaxWaveCount] = { hashed_string( "wave0" ), hashed_string( "wave1" ), hashed_string( "wave2" ),
                                                                                       hashed_string( "wave3" ) };
                return s_arrName[waveIndex];
            }

            /** @brief 지형 높이 아래로 잰 물 깊이입니다. 지형이 없거나 덮지 않으면 열린 물 깊이입니다. */
            static float32 computeDepth( const TerrainHeightfield* pTerrain, float32 worldX, float32 worldZ, float32 surfaceY )
            {
                float32 groundY{ 0.0f };
                if ( pTerrain == nullptr || pTerrain->findHeightAt( worldX, worldZ, groundY ) == false )
                    return kOpenWaterDepth;
                return MathUtil::max( surfaceY - groundY, 0.0f );
            }

            static RHIVertex makeVertex( const float3& localPosition, const float2& flow, float32 depth )
            {
                RHIVertex vertex{};
                vertex._arrPosition[0] = localPosition._x;
                vertex._arrPosition[1] = localPosition._y;
                vertex._arrPosition[2] = localPosition._z;
                vertex._arrNormal[1]   = 1.0f;
                vertex._arrUv[0]       = localPosition._x;
                vertex._arrUv[1]       = localPosition._z;
                // 색: rg = 흐름 방향(강), b = 0, a = 물 깊이(m) — water.hlsl 이 읽는다.
                vertex._arrColor[0] = flow._x;
                vertex._arrColor[1] = flow._y;
                vertex._arrColor[3] = depth;
                return vertex;
            }

            /** @brief 격자 사각형 하나를 위에서 앞면으로 넣습니다(MeshUtil::createPlane 과 같은 감김). */
            static void pushQuad( vector<RHIVertex>& outListVertex, const RHIVertex& v00, const RHIVertex& v10, const RHIVertex& v01, const RHIVertex& v11 )
            {
                outListVertex.push_back( v00 );
                outListVertex.push_back( v01 );
                outListVertex.push_back( v11 );
                outListVertex.push_back( v00 );
                outListVertex.push_back( v11 );
                outListVertex.push_back( v10 );
            }

            /** @brief 경로 위 표본 하나 — 중심(로컬) · 진행 방향(xz)입니다. */
            struct RiverSample
            {
                float3 _center{};
                float2 _tangent{ 1.0f, 0.0f };
            };

            /** @brief 경로를 @p spacing 간격으로 다시 뽑습니다. 점이 둘 미만이면 비어 있습니다. */
            static void resampleRiver( const vector<float3>& listPoint, float32 spacing, vector<RiverSample>& outListSample )
            {
                outListSample.clear();
                if ( listPoint.size() < 2 )
                    return;
                for ( size_t segment = 0; segment + 1 < listPoint.size(); ++segment )
                {
                    const float3  start  = listPoint[segment];
                    const float3  end    = listPoint[segment + 1];
                    const float32 length = float2{ end._x - start._x, end._z - start._z }.getLength();
                    const uint32  steps  = MathUtil::max( 1u, static_cast<uint32>( MathUtil::ceil( length / spacing ) ) );
                    const bool    bLast  = segment + 2 == listPoint.size();
                    for ( uint32 step = 0; step < steps + ( bLast ? 1u : 0u ); ++step )
                    {
                        const float32 ratio = static_cast<float32>( step ) / static_cast<float32>( steps );
                        RiverSample   sample;
                        sample._center = start + ( end - start ) * ratio;
                        outListSample.push_back( sample );
                    }
                }
                // 진행 방향은 앞뒤 표본의 차 — 꺾이는 자리에서 띠가 갈라지지 않는다.
                for ( size_t index = 0; index < outListSample.size(); ++index )
                {
                    const float3& previous = outListSample[index > 0 ? index - 1 : index]._center;
                    const float3& next     = outListSample[index + 1 < outListSample.size() ? index + 1 : index]._center;
                    const float2  delta{ next._x - previous._x, next._z - previous._z };
                    const float32 length = delta.getLength();
                    if ( length > MathUtil::kEpsilon )
                        outListSample[index]._tangent = delta * ( 1.0f / length );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    WaterBodyComponent::WaterBodyComponent()
        : _shape{ WaterBodyShape::Lake }
        , _materialPath{ "engine/materials/water.material" }
        , _size{ 64.0f, 64.0f }
        , _cellSize{ 1.0f }
        , _listRiverPoint{}
        , _riverWidth{ 6.0f }
        , _listWave{}
        , _shallowColor{ 0.10f, 0.45f, 0.45f, 0.35f }
        , _deepColor{ 0.01f, 0.08f, 0.16f, 0.92f }
        , _skyColor{ 0.55f, 0.70f, 0.85f, 1.0f }
        , _deepDepth{ 4.0f }
        , _foamWidth{ 0.35f }
        , _rippleStrength{ 0.35f }
        , _bUnderwaterFog{ true }
        , _fogColor{ 0.05f, 0.20f, 0.25f, 1.0f }
        , _fogDensity{ 0.15f }
        , _material{}
        , _batch{}
        , _pPrimitiveRegistry{ nullptr }
        , _surfaceOrigin{}
        , _waveTime{ 0.0f }
        , _surfaceVertexCount{ 0 }
        , _bOriginDirty{ false }
        , _bBuiltOnTerrain{ false }
    {
        setCanEverTick( true );
        setTickGroup( TickGroup::PostUpdate );
    }

    WaterBodyComponent::~WaterBodyComponent() = default;

    void WaterBodyComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<WaterBodyComponent>( this ); // `findWaterAt` · `findUnderwaterFog` 가 씬을 훑지 않고 본다
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
        rebuildSurface();
    }

    void WaterBodyComponent::onUnregister( GameObjectManager& manager )
    {
        _batch.reset(); // 소멸자가 등록부에서 뺀다
        _material.release();
        _pPrimitiveRegistry = nullptr;
        manager.getComponentRegistry().remove<WaterBodyComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void WaterBodyComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        rebuildSurface();
    }

    void WaterBodyComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bBuiltOnTerrain )
            return;
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            WaterBodyComponent* pWater = static_cast<WaterBodyComponent*>( pManager->resolveComponent( self ) );
            if ( pWater != nullptr )
                pWater->rebuildSurface();
        } );
    }

    void WaterBodyComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        // 켜고 끄기는 수면을 바꾸지 않는다 — 다시 짓지 않고 빌더가 다시 보게만 한다.
        static const hashed_string s_activeName( "_bActive" );
        if ( propertyName == s_activeName )
        {
            if ( _batch != nullptr )
                _batch->markAllEntriesDirty();
            return;
        }
        rebuildSurface();
    }

    void WaterBodyComponent::onOwnerActiveInHierarchyChanged()
    {
        SceneComponent::onOwnerActiveInHierarchyChanged();
        if ( _batch != nullptr )
            _batch->markAllEntriesDirty();
    }

    void WaterBodyComponent::onWorldTransformUpdated()
    {
        SceneComponent::onWorldTransformUpdated();
        if ( _batch != nullptr )
            _bOriginDirty.store( true, std::memory_order_relaxed );
    }

    void WaterBodyComponent::onTick( float32 deltaTime )
    {
        SceneComponent::onTick( deltaTime );
        if ( EnvironmentUtil::isAnimationEnabled() )
            _waveTime += deltaTime;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && _bOriginDirty.exchange( false, std::memory_order_relaxed ) )
        {
            // 다시 만들기는 배치를 등록부에서 빼고 넣는 구조 변경이다 — 틱 뒤로 미룬다.
            const ComponentHandle self = getHandle();
            pManager->executeOrDeferPostTick( [pManager, self]()
            {
                WaterBodyComponent* pWater = static_cast<WaterBodyComponent*>( pManager->resolveComponent( self ) );
                if ( pWater != nullptr )
                    pWater->rebuildSurface();
            } );
        }
        writeMaterialValues();
    }

    void WaterBodyComponent::setWaveTime( float32 time )
    {
        _waveTime = time;
        writeMaterialValues();
    }

    void WaterBodyComponent::setRiverPath( const vector<float3>& listPoint, float32 width )
    {
        _listRiverPoint = listPoint;
        _riverWidth     = width;
    }

    void WaterBodyComponent::getWaveVectors( float4 ( &outArrWave )[WaterWaveMath::kMaxWaveCount] ) const
    {
        for ( uint32 waveIndex = 0; waveIndex < WaterWaveMath::kMaxWaveCount; ++waveIndex )
            outArrWave[waveIndex] = waveIndex < _listWave.size() ? _listWave[waveIndex].toVector() : float4{ 0.0f, 1.0f, 0.0f, 0.0f };
    }

    void WaterBodyComponent::writeMaterialValues()
    {
        using Internal = WaterBodyComponentInternal;
        if ( _material.getInstance() == nullptr )
            return;
        float4 arrWave[WaterWaveMath::kMaxWaveCount];
        getWaveVectors( arrWave );
        for ( uint32 waveIndex = 0; waveIndex < WaterWaveMath::kMaxWaveCount; ++waveIndex )
            _material.setVector( Internal::getWaveName( waveIndex ), arrWave[waveIndex] );
        // x = 파도 시간, y = 깊은 물 깊이, z = 거품 폭, w = 잔물결 세기
        _material.setVector( hashed_string( "waterParams" ), float4{ _waveTime, _deepDepth, _foamWidth, _rippleStrength } );
        _material.setVector( hashed_string( "shallowColor" ), _shallowColor );
        _material.setVector( hashed_string( "deepColor" ), _deepColor );
        _material.setVector( hashed_string( "skyColor" ), _skyColor );
    }

    void WaterBodyComponent::rebuildSurface()
    {
        _batch.reset();
        _surfaceVertexCount = 0;
        // 질의도 이 원점을 쓴다 — 물은 움직이지 않는 것으로 보고, 옮기면 다시 만든다(트랜스폼 변경은 onPropertyChanged 로 온다).
        _surfaceOrigin = getWorldPosition();
        if ( _pPrimitiveRegistry == nullptr )
            return;

        GameObject*               pOwner   = getOwner();
        GameObjectManager*        pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const TerrainComponent*   pTerrain = pManager != nullptr ? TerrainComponent::findTerrainAt( *pManager, _surfaceOrigin._x, _surfaceOrigin._z ) : nullptr;
        const TerrainHeightfield* pField   = pTerrain != nullptr ? &pTerrain->getHeightfield() : nullptr;
        _bBuiltOnTerrain                   = pField != nullptr;

        vector<RHIVertex> listVertex;
        float32           boundsRadius{ 0.0f };
        if ( _shape == WaterBodyShape::River )
            buildRiverVertices( pField, listVertex, boundsRadius );
        else
            buildLakeVertices( pField, listVertex, boundsRadius );
        if ( listVertex.empty() )
            return;

        // 파도가 메시를 위아래 · 옆으로 민다 — 바운드에 진폭 합만큼 더한다.
        for ( const GerstnerWave& wave : _listWave )
            boundsRadius += wave._amplitude * 2.0f;

        if ( _material.getInstance() == nullptr )
            (void)_material.acquire( _materialPath ); // 못 잡으면 씬 기본 머티리얼로 그린다(경고는 안에서)
        _surfaceVertexCount   = static_cast<uint32>( listVertex.size() );
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setVertices( std::move( listVertex ) );
        _batch = sw::make_unique<MeshInstanceBatch>( std::move( mesh ), _material.getMaterial(), _material.getInstance(), 1u );
        _batch->setOwnerComponent( this );
        _batch->setWorld( 0, float4x4::createTranslation( _surfaceOrigin ) );
        _batch->setBoundsRadius( 0, boundsRadius );
        _pPrimitiveRegistry->addInstanceBatch( _batch.get() );
        writeMaterialValues();
    }

    void WaterBodyComponent::buildLakeVertices( const TerrainHeightfield* pTerrain, vector<RHIVertex>& outListVertex, float32& outBoundsRadius ) const
    {
        using Internal         = WaterBodyComponentInternal;
        const float32 cellSize = MathUtil::max( _cellSize, 0.1f );
        const uint32  cellsX   = MathUtil::clamp( static_cast<uint32>( MathUtil::ceil( _size._x / cellSize ) ), 1u, Internal::kMaxCellsPerSide );
        const uint32  cellsZ   = MathUtil::clamp( static_cast<uint32>( MathUtil::ceil( _size._y / cellSize ) ), 1u, Internal::kMaxCellsPerSide );
        const float32 stepX    = _size._x / static_cast<float32>( cellsX );
        const float32 stepZ    = _size._y / static_cast<float32>( cellsZ );
        outListVertex.reserve( static_cast<size_t>( cellsX ) * cellsZ * 6 );
        auto vertexAt = [&]( uint32 gridX, uint32 gridZ ) -> RHIVertex
        {
            const float3  local{ -0.5f * _size._x + static_cast<float32>( gridX ) * stepX, 0.0f, -0.5f * _size._y + static_cast<float32>( gridZ ) * stepZ };
            const float32 depth = Internal::computeDepth( pTerrain, _surfaceOrigin._x + local._x, _surfaceOrigin._z + local._z, _surfaceOrigin._y );
            return Internal::makeVertex( local, float2{}, depth );
        };
        for ( uint32 gridZ = 0; gridZ < cellsZ; ++gridZ )
        {
            for ( uint32 gridX = 0; gridX < cellsX; ++gridX )
                Internal::pushQuad( outListVertex, vertexAt( gridX, gridZ ), vertexAt( gridX + 1, gridZ ), vertexAt( gridX, gridZ + 1 ), vertexAt( gridX + 1, gridZ + 1 ) );
        }
        outBoundsRadius = 0.5f * MathUtil::sqrt( _size._x * _size._x + _size._y * _size._y );
    }

    void WaterBodyComponent::buildRiverVertices( const TerrainHeightfield* pTerrain, vector<RHIVertex>& outListVertex, float32& outBoundsRadius ) const
    {
        using Internal = WaterBodyComponentInternal;
        vector<Internal::RiverSample> listSample;
        Internal::resampleRiver( _listRiverPoint, MathUtil::max( _cellSize, 0.1f ), listSample );
        if ( listSample.size() < 2 )
            return;
        const uint32  acrossCells = MathUtil::clamp( static_cast<uint32>( MathUtil::ceil( _riverWidth / MathUtil::max( _cellSize, 0.1f ) ) ), 1u, Internal::kMaxCellsPerSide );
        const float32 halfWidth   = 0.5f * _riverWidth;
        auto          vertexAt    = [&]( size_t sampleIndex, uint32 across ) -> RHIVertex
        {
            const Internal::RiverSample& sample = listSample[sampleIndex];
            const float2                 side{ -sample._tangent._y, sample._tangent._x };
            const float32                offset = -halfWidth + _riverWidth * static_cast<float32>( across ) / static_cast<float32>( acrossCells );
            const float3                 local{ sample._center._x + side._x * offset, sample._center._y, sample._center._z + side._y * offset };
            const float32                depth = Internal::computeDepth( pTerrain, _surfaceOrigin._x + local._x, _surfaceOrigin._z + local._z, _surfaceOrigin._y + local._y );
            return Internal::makeVertex( local, sample._tangent, depth );
        };
        outListVertex.reserve( ( listSample.size() - 1 ) * acrossCells * 6 );
        for ( size_t sampleIndex = 0; sampleIndex + 1 < listSample.size(); ++sampleIndex )
        {
            for ( uint32 across = 0; across < acrossCells; ++across )
            {
                // 진행 방향이 +x 이고 옆(왼쪽)이 +z 일 때 위에서 앞면 — (x, z) 격자와 같은 감김이다.
                Internal::pushQuad( outListVertex, vertexAt( sampleIndex, across ), vertexAt( sampleIndex + 1, across ), vertexAt( sampleIndex, across + 1 ),
                                    vertexAt( sampleIndex + 1, across + 1 ) );
            }
        }
        outBoundsRadius = 0.0f;
        for ( const Internal::RiverSample& sample : listSample )
            outBoundsRadius = MathUtil::max( outBoundsRadius, sample._center.getLength() + halfWidth );
    }

    bool WaterBodyComponent::findRiverBase( float32 worldX, float32 worldZ, float32& outBaseHeight ) const
    {
        const float32 localX       = worldX - _surfaceOrigin._x;
        const float32 localZ       = worldZ - _surfaceOrigin._z;
        const float32 halfWidth    = 0.5f * _riverWidth;
        float32       bestDistance = MathUtil::kMaxFloat;
        for ( size_t segment = 0; segment + 1 < _listRiverPoint.size(); ++segment )
        {
            const float3  start = _listRiverPoint[segment];
            const float3  end   = _listRiverPoint[segment + 1];
            const float2  axis{ end._x - start._x, end._z - start._z };
            const float32 lengthSquare = axis.getLengthSquared();
            float32       ratio        = 0.0f;
            if ( lengthSquare > MathUtil::kEpsilon )
                ratio = MathUtil::saturate( ( ( localX - start._x ) * axis._x + ( localZ - start._z ) * axis._y ) / lengthSquare );
            const float32 closestX = start._x + axis._x * ratio;
            const float32 closestZ = start._z + axis._y * ratio;
            const float32 distance = float2{ localX - closestX, localZ - closestZ }.getLength();
            if ( distance < bestDistance )
            {
                bestDistance  = distance;
                outBaseHeight = _surfaceOrigin._y + start._y + ( end._y - start._y ) * ratio;
            }
        }
        return bestDistance <= halfWidth;
    }

    bool WaterBodyComponent::findBaseHeight( float32 worldX, float32 worldZ, float32& outBaseHeight ) const
    {
        if ( _shape == WaterBodyShape::River )
            return findRiverBase( worldX, worldZ, outBaseHeight );
        const bool bInside = MathUtil::abs( worldX - _surfaceOrigin._x ) <= 0.5f * _size._x && MathUtil::abs( worldZ - _surfaceOrigin._z ) <= 0.5f * _size._y;
        outBaseHeight      = _surfaceOrigin._y;
        return bInside;
    }

    bool WaterBodyComponent::coversPosition( float32 worldX, float32 worldZ ) const
    {
        float32 baseHeight{ 0.0f };
        return findBaseHeight( worldX, worldZ, baseHeight );
    }

    bool WaterBodyComponent::computeSurfaceHeight( float32 worldX, float32 worldZ, float32& outHeight, float32 time ) const
    {
        float32 baseHeight{ 0.0f };
        if ( findBaseHeight( worldX, worldZ, baseHeight ) == false )
            return false;
        float4 arrWave[WaterWaveMath::kMaxWaveCount];
        getWaveVectors( arrWave );
        outHeight = baseHeight + WaterWaveMath::computeSurfaceHeight( float2{ worldX, worldZ }, time < 0.0f ? _waveTime : time, arrWave );
        return true;
    }

    bool WaterBodyComponent::computeSurfaceNormal( float32 worldX, float32 worldZ, float3& outNormal, float32 time ) const
    {
        float32 baseHeight{ 0.0f };
        if ( findBaseHeight( worldX, worldZ, baseHeight ) == false )
            return false;
        float4 arrWave[WaterWaveMath::kMaxWaveCount];
        getWaveVectors( arrWave );
        const float32 waveTime = time < 0.0f ? _waveTime : time;
        float2        origin{};
        (void)WaterWaveMath::computeSurfaceHeight( float2{ worldX, worldZ }, waveTime, arrWave, 4u, &origin );
        outNormal = WaterWaveMath::computeNormal( origin, waveTime, arrWave );
        return true;
    }

    bool WaterBodyComponent::isUnderwater( const float3& worldPosition, float32& outDepth ) const
    {
        float32 surfaceHeight{ 0.0f };
        if ( computeSurfaceHeight( worldPosition._x, worldPosition._z, surfaceHeight ) == false )
            return false;
        outDepth = surfaceHeight - worldPosition._y;
        return outDepth > 0.0f;
    }

    bool WaterBodyComponent::findUnderwaterFog( const GameObjectManager& manager, const float3& viewPosition, WaterUnderwaterFog& outFog )
    {
        for ( const WaterBodyComponent* pWater : manager.getComponentRegistry().getAll<WaterBodyComponent>() )
        {
            float32 depth{ 0.0f };
            if ( pWater->isPendingDestroy() || pWater->_bUnderwaterFog == false || pWater->isUnderwater( viewPosition, depth ) == false )
                continue;
            outFog._color   = pWater->_fogColor;
            outFog._density = pWater->_fogDensity;
            outFog._depth   = depth;
            return true;
        }
        return false;
    }

    WaterBodyComponent* WaterBodyComponent::findWaterAt( const GameObjectManager& manager, float32 worldX, float32 worldZ )
    {
        for ( WaterBodyComponent* pWater : manager.getComponentRegistry().getAll<WaterBodyComponent>() )
        {
            if ( pWater->isPendingDestroy() == false && pWater->coversPosition( worldX, worldZ ) )
                return pWater;
        }
        return nullptr;
    }
} // namespace sw
