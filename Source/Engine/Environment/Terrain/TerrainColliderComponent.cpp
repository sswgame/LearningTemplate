#include "pch.h"

#include "Engine/Environment/Terrain/TerrainColliderComponent.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Environment/Terrain/TerrainMeshBuilder.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    SW_LOG_CALLER( "TerrainColliderComponent" );

    namespace
    {
        /** @brief 셀 사각형 [@p cellMinX, @p cellEndX) × [@p cellMinZ, @p cellEndZ) 를 삼각형 메시로 만듭니다. 점은 그 셀들의 샘플만이고 샘플 (0, 0) 기준입니다. */
        bool makeCellMesh( const TerrainHeightfield& heightfield, uint32 cellMinX, uint32 cellMinZ, uint32 cellEndX, uint32 cellEndZ, PhysicsShapeDesc3D& outShape )
        {
            outShape           = PhysicsShapeDesc3D{};
            outShape._type     = PhysicsShapeType3D::TriangleMesh;
            const bool bEmpty  = cellEndX <= cellMinX || cellEndZ <= cellMinZ;
            const bool bInside = heightfield.isValid() && cellEndX < heightfield.getResolution() && cellEndZ < heightfield.getResolution();
            if ( bEmpty || bInside == false )
                return false;

            const float3& origin  = heightfield.getOrigin();
            const uint32  columns = cellEndX - cellMinX + 1; // 점 한 줄의 수
            outShape._listPoint.reserve( static_cast<size_t>( columns ) * ( cellEndZ - cellMinZ + 1 ) );
            for ( uint32 sampleZ = cellMinZ; sampleZ <= cellEndZ; ++sampleZ )
            {
                for ( uint32 sampleX = cellMinX; sampleX <= cellEndX; ++sampleX )
                {
                    outShape._listPoint.push_back( heightfield.getSamplePosition( sampleX, sampleZ ) - origin );
                }
            }

            outShape._listIndex.reserve( static_cast<size_t>( cellEndX - cellMinX ) * ( cellEndZ - cellMinZ ) * 6 );
            for ( uint32 cellZ = cellMinZ; cellZ < cellEndZ; ++cellZ )
            {
                for ( uint32 cellX = cellMinX; cellX < cellEndX; ++cellX )
                {
                    if ( heightfield.isHoleCell( cellX, cellZ ) )
                        continue;
                    // a = (x, z), b = (x+1, z), c = (x, z+1), d = (x+1, z+1). 대각선 a–d 로 나눠 (a, d, b) · (a, c, d) — 둘 다 (b−a)×(c−a) 가 +Y 다.
                    const uint32 a = ( cellZ - cellMinZ ) * columns + ( cellX - cellMinX );
                    const uint32 b = a + 1;
                    const uint32 c = a + columns;
                    const uint32 d = c + 1;
                    outShape._listIndex.push_back( a );
                    outShape._listIndex.push_back( d );
                    outShape._listIndex.push_back( b );
                    outShape._listIndex.push_back( a );
                    outShape._listIndex.push_back( c );
                    outShape._listIndex.push_back( d );
                }
            }
            if ( outShape._listIndex.empty() )
            {
                outShape._listPoint.clear();
                return false;
            }
            return true;
        }
    } // namespace

    bool TerrainCollisionUtil::makeCollisionMesh( const TerrainHeightfield& heightfield, PhysicsShapeDesc3D& outShape )
    {
        const uint32 last = heightfield.isValid() ? heightfield.getResolution() - 1 : 0u;
        return makeCellMesh( heightfield, 0, 0, last, last, outShape );
    }

    bool TerrainCollisionUtil::makeChunkCollisionMesh( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ,
                                                       PhysicsShapeDesc3D& outShape )
    {
        const bool bInside = chunkX < layout._chunkCountX && chunkZ < layout._chunkCountZ;
        if ( bInside == false )
        {
            outShape = PhysicsShapeDesc3D{};
            return false;
        }
        const uint32 cells = layout._chunkCells;
        return makeCellMesh( heightfield, chunkX * cells, chunkZ * cells, ( chunkX + 1 ) * cells, ( chunkZ + 1 ) * cells, outShape );
    }
} // namespace sw

namespace sw
{
    TerrainColliderComponent::TerrainColliderComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Body }
        , _layer{ "Default" }
        , _material{}
        , _listBody{}
        , _listBuiltGeneration{}
        , _listTriangleCount{}
        , _triangleCount{ 0 }
        , _chunkBuildCount{ 0 }
    {
    }

    uint32 TerrainColliderComponent::getBodyCount() const
    {
        uint32 count{ 0 };
        for ( const PhysicsBodyHandle& body : _listBody )
        {
            count += body.isValid() ? 1u : 0u;
        }
        return count;
    }

    void TerrainColliderComponent::setLayer( const hashed_string& layer )
    {
        _layer = layer;
        requestRebuild();
    }

    void TerrainColliderComponent::setMaterial( const hashed_string& material )
    {
        _material = material;
        requestRebuild();
    }

    void TerrainColliderComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsBodyScene3D* pScene = physics.getScene3D();
        if ( pScene == nullptr )
            return;
        if ( consumeRebuild() )
            releasePhysics( physics );
        (void)consumeTeleport(); // 지형은 오너의 순간이동이 아니라 다시 읽기(세대)로 옮겨 간다

        const TerrainComponent* pTerrain = getOwner()->getComponent<TerrainComponent>();
        if ( pTerrain == nullptr || pTerrain->getHeightfield().isValid() == false )
        {
            releasePhysics( physics );
            return;
        }
        // 청크 배치가 바뀌었으면(다시 읽기로 해상도 · 청크 크기가 달라짐) 모두 버리고 새로 만든다.
        const vector<uint32>& listGeneration = pTerrain->getChunkGenerations();
        if ( _listBody.size() != listGeneration.size() )
        {
            releasePhysics( physics );
            _listBody.resize( listGeneration.size() );
            _listBuiltGeneration.assign( listGeneration.size(), 0u );
            _listTriangleCount.assign( listGeneration.size(), 0u );
        }

        const TerrainHeightfield& heightfield = pTerrain->getHeightfield();
        const TerrainChunkLayout& layout      = pTerrain->getChunkLayout();
        PhysicsBodyDesc3D         desc;
        desc._position = heightfield.getOrigin();
        desc._rotation = quaternion{};
        desc._userData = getOwner()->getObjectID();
        desc._material = _material;
        desc._type     = PhysicsBodyType::Static;
        desc._layer    = physics.resolveLayer( _layer );
        desc._listShape.resize( 1 );
        bool bBuiltAny{ false };
        for ( uint32 chunkIndex = 0; chunkIndex < listGeneration.size(); ++chunkIndex )
        {
            PhysicsBodyHandle& body      = _listBody[chunkIndex];
            const bool         bBodyGood = body.isValid() == false || pScene->isBodyValid( body ); // 모두 구멍인 청크는 바디 없이도 최신이다
            const bool         bUpToDate = _listBuiltGeneration[chunkIndex] != 0 && _listBuiltGeneration[chunkIndex] == listGeneration[chunkIndex];
            if ( bUpToDate && bBodyGood )
                continue;
            bBuiltAny = true;
            if ( body.isValid() )
                pScene->destroyBody( body );
            body = PhysicsBodyHandle{};
            _triangleCount -= _listTriangleCount[chunkIndex];
            _listTriangleCount[chunkIndex]   = 0;
            _listBuiltGeneration[chunkIndex] = listGeneration[chunkIndex];
            ++_chunkBuildCount;
            // 모두 구멍인 청크는 바디가 없다(무효 핸들로 남는다).
            if ( TerrainCollisionUtil::makeChunkCollisionMesh( heightfield, layout, chunkIndex % layout._chunkCountX, chunkIndex / layout._chunkCountX, desc._listShape[0] ) == false )
                continue;
            body = pScene->createBody( desc );
            if ( body.isValid() == false )
                continue;
            _listTriangleCount[chunkIndex] = static_cast<uint32>( desc._listShape[0]._listIndex.size() / 3 );
            _triangleCount += _listTriangleCount[chunkIndex];
        }
        if ( bBuiltAny && _triangleCount == 0 )
            SW_LOG_WARNING( "Terrain collider on '%#': the terrain has no solid cells", getOwner()->getName().c_str() );
    }

    void TerrainColliderComponent::releasePhysics( ScenePhysics& physics )
    {
        _triangleCount              = 0;
        IPhysicsBodyScene3D* pScene = physics.findScene3D();
        for ( PhysicsBodyHandle& body : _listBody )
        {
            if ( body.isValid() && pScene != nullptr )
                pScene->destroyBody( body );
            body = PhysicsBodyHandle{};
        }
        _listBuiltGeneration.assign( _listBuiltGeneration.size(), 0u );
        _listTriangleCount.assign( _listTriangleCount.size(), 0u );
    }
} // namespace sw

namespace sw
{
    void TerrainColliderComponent::validateLayer( ValidationContext& context ) const
    {
        ComponentNameValidation::validatePhysicsLayer( context, "_layer", _layer );
    }

    void TerrainColliderComponent::validateMaterial( ValidationContext& context ) const
    {
        ComponentNameValidation::validatePhysicsMaterial( context, "_material", _material );
    }
} // namespace sw
