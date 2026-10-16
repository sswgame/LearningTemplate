#include "pch.h"

#include "Engine/Object/Component/Physics/ClothComponent.h"

#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    SW_LOG_CALLER( "ClothComponent" );

    ClothComponent::ClothComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Body }
        , _width{ 1.0f }
        , _height{ 1.0f }
        , _columnCount{ 11 }
        , _rowCount{ 11 }
        , _pinMode{ ClothPinMode::TopEdge }
        , _mass{ 1.0f }
        , _stretchCompliance{ 0.0f }
        , _shearCompliance{ 0.0f }
        , _bendCompliance{ 1.0e-3f }
        , _linearDamping{ 0.1f }
        , _friction{ 0.2f }
        , _gravityFactor{ 1.0f }
        , _vertexRadius{ 0.01f }
        , _iterationCount{ 5 }
        , _layer{}
        , _listVertex{}
        , _softBody{}
    {
    }

    void ClothComponent::setPinMode( ClothPinMode pinMode )
    {
        _pinMode = pinMode;
        requestRebuild();
    }

    PhysicsSoftBodyDesc ClothComponent::makeSoftBodyDesc( const float3& position, const quaternion& rotation, uint8 layer ) const
    {
        PhysicsSoftBodyDesc desc;
        desc._position          = position;
        desc._rotation          = rotation;
        desc._layer             = layer;
        desc._stretchCompliance = _stretchCompliance;
        desc._shearCompliance   = _shearCompliance;
        desc._bendCompliance    = _bendCompliance;
        desc._linearDamping     = _linearDamping;
        desc._friction          = _friction;
        desc._gravityFactor     = _gravityFactor;
        desc._vertexRadius      = _vertexRadius;
        desc._iterationCount    = _iterationCount;
        if ( _columnCount < 2 || _rowCount < 2 )
        {
            // 빈 서술자는 `createSoftBody` 가 오류로 거절한다(천을 만들지 않는다).
            SW_LOG_ERROR( "Cloth needs at least 2 x 2 vertices (got %# x %#)", _columnCount, _rowCount );
            return desc;
        }

        // 격자 — 행 우선, 윗줄(y = 0)부터 아래(−Y)로. 윗변 가운데가 오브젝트 자리다.
        const float32 stepX = _width / static_cast<float32>( _columnCount - 1 );
        const float32 stepY = _height / static_cast<float32>( _rowCount - 1 );
        desc._listVertex.reserve( static_cast<size_t>( _columnCount ) * _rowCount );
        for ( uint32 row = 0; row < _rowCount; ++row )
        {
            for ( uint32 column = 0; column < _columnCount; ++column )
            {
                desc._listVertex.push_back( float3{ static_cast<float32>( column ) * stepX - _width * 0.5f, -static_cast<float32>( row ) * stepY, 0.0f } );
            }
        }
        desc._listTriangleIndex.reserve( static_cast<size_t>( _columnCount - 1 ) * ( _rowCount - 1 ) * 6 );
        for ( uint32 row = 0; row + 1 < _rowCount; ++row )
        {
            for ( uint32 column = 0; column + 1 < _columnCount; ++column )
            {
                const uint32 topLeft     = row * _columnCount + column;
                const uint32 bottomLeft  = topLeft + _columnCount;
                const uint32 topRight    = topLeft + 1;
                const uint32 bottomRight = bottomLeft + 1;
                desc._listTriangleIndex.insert( desc._listTriangleIndex.end(), { topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight } );
            }
        }
        switch ( _pinMode )
        {
            case ClothPinMode::None:
            {
                break;
            }
            case ClothPinMode::TopEdge:
            {
                for ( uint32 column = 0; column < _columnCount; ++column )
                {
                    desc._listPinnedVertex.push_back( column );
                }
                break;
            }
            case ClothPinMode::TopCorners:
            {
                desc._listPinnedVertex.insert( desc._listPinnedVertex.end(), { 0u, _columnCount - 1 } );
                break;
            }
        }
        const size_t freeVertexCount = desc._listVertex.size() - desc._listPinnedVertex.size();
        desc._vertexMass             = _mass / static_cast<float32>( freeVertexCount );
        return desc;
    }

    void ClothComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene3D* pScene = physics.getScene3D();
        if ( pScene == nullptr )
            return;
        // 순간이동은 천을 그 자리에 다시 건다(정점이 먼 곳에서 끌려오지 않게).
        const bool bTeleported = consumeTeleport();
        if ( consumeRebuild() || bTeleported )
            releasePhysics( physics );
        float3     position;
        quaternion rotation;
        float3     scale;
        PhysicsComponentUtil::readWorldPose( *this, position, rotation, scale );
        if ( _softBody.isValid() == false )
        {
            _softBody = pScene->createSoftBody( makeSoftBodyDesc( position, rotation, physics.resolveLayer( _layer ) ) );
            return;
        }
        pScene->setSoftBodyPinnedTransform( _softBody, position, rotation );
    }

    void ClothComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)alpha;
        const IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr && _softBody.isValid() )
            (void)pScene->getSoftBodyVertices( _softBody, _listVertex );
    }

    void ClothComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _softBody.isValid() )
        {
            IPhysicsScene3D* pScene = physics.findScene3D();
            if ( pScene != nullptr )
                pScene->destroySoftBody( _softBody );
        }
        _softBody = PhysicsSoftBodyHandle{};
        _listVertex.clear();
    }

    void ClothComponent::validateLayer( ValidationContext& context ) const
    {
        ComponentNameValidation::validatePhysicsLayer( context, "_layer", _layer );
    }
} // namespace sw
