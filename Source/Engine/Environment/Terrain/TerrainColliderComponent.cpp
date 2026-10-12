#include "pch.h"

#include "Engine/Environment/Terrain/TerrainColliderComponent.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsDesc.h"

namespace sw
{
    SW_LOG_CALLER( "TerrainColliderComponent" );

    bool TerrainCollisionUtil::makeCollisionMesh( const TerrainHeightfield& heightfield, PhysicsShapeDesc3D& outShape )
    {
        outShape       = PhysicsShapeDesc3D{};
        outShape._type = PhysicsShapeType3D::TriangleMesh;
        if ( heightfield.isValid() == false )
            return false;

        const uint32  resolution = heightfield.getResolution();
        const float3& origin     = heightfield.getOrigin();
        outShape._listPoint.reserve( static_cast<size_t>( resolution ) * resolution );
        for ( uint32 sampleZ = 0; sampleZ < resolution; ++sampleZ )
        {
            for ( uint32 sampleX = 0; sampleX < resolution; ++sampleX )
            {
                outShape._listPoint.push_back( heightfield.getSamplePosition( sampleX, sampleZ ) - origin );
            }
        }

        const uint32 cellCount = resolution - 1;
        outShape._listIndex.reserve( static_cast<size_t>( cellCount ) * cellCount * 6 );
        for ( uint32 cellZ = 0; cellZ < cellCount; ++cellZ )
        {
            for ( uint32 cellX = 0; cellX < cellCount; ++cellX )
            {
                if ( heightfield.isHoleCell( cellX, cellZ ) )
                    continue;
                // a = (x, z), b = (x+1, z), c = (x, z+1), d = (x+1, z+1). 대각선 a–d 로 나눠 (a, d, b) · (a, c, d) — 둘 다 (b−a)×(c−a) 가 +Y 다.
                const uint32 a = cellZ * resolution + cellX;
                const uint32 b = a + 1;
                const uint32 c = a + resolution;
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
} // namespace sw

namespace sw
{
    TerrainColliderComponent::TerrainColliderComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Body }
        , _layer{ "Default" }
        , _material{}
        , _body{}
        , _builtGeneration{ 0 }
        , _triangleCount{ 0 }
    {
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
        const bool bBodyAlive = _body.isValid() && pScene->isBodyValid( _body );
        if ( bBodyAlive && _builtGeneration == pTerrain->getHeightfieldGeneration() )
            return;
        releasePhysics( physics );

        const TerrainHeightfield& heightfield = pTerrain->getHeightfield();
        PhysicsBodyDesc3D         desc;
        desc._listShape.resize( 1 );
        if ( TerrainCollisionUtil::makeCollisionMesh( heightfield, desc._listShape[0] ) == false )
        {
            SW_LOG_WARNING( "Terrain collider on '%#': the terrain has no solid cells", getOwner()->getName().c_str() );
            return;
        }
        desc._position   = heightfield.getOrigin();
        desc._rotation   = quaternion{};
        desc._userData   = getOwner()->getObjectID();
        desc._material   = _material;
        desc._type       = PhysicsBodyType::Static;
        desc._layer      = physics.resolveLayer( _layer );
        _body            = pScene->createBody( desc );
        _builtGeneration = pTerrain->getHeightfieldGeneration();
        _triangleCount   = _body.isValid() ? static_cast<uint32>( desc._listShape[0]._listIndex.size() / 3 ) : 0;
    }

    void TerrainColliderComponent::releasePhysics( ScenePhysics& physics )
    {
        _triangleCount = 0;
        if ( _body.isValid() == false )
            return;
        IPhysicsBodyScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr )
            pScene->destroyBody( _body );
        _body = PhysicsBodyHandle{};
    }
} // namespace sw
