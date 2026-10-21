#include "pch.h"

#include "Engine/Object/Component/Physics/ClothComponent.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    namespace
    {
        /** @brief 천 메시 정점 하나입니다. 칸 이름으로 채운다(정점 형식에 칸이 늘어도 그대로 맞는다). 색은 1 — 머티리얼 색이 그대로 보인다. */
        RHIVertex makeClothRenderVertex( const float3& position, const float3& normal, float32 u, float32 v )
        {
            RHIVertex vertex{};
            vertex._arrPosition[0] = position._x;
            vertex._arrPosition[1] = position._y;
            vertex._arrPosition[2] = position._z;
            vertex._arrNormal[0]   = normal._x;
            vertex._arrNormal[1]   = normal._y;
            vertex._arrNormal[2]   = normal._z;
            vertex._arrUv[0]       = u;
            vertex._arrUv[1]       = v;
            vertex._arrColor[0]    = 1.0f;
            vertex._arrColor[1]    = 1.0f;
            vertex._arrColor[2]    = 1.0f;
            vertex._arrColor[3]    = 1.0f;
            return vertex;
        }
    } // namespace
} // namespace sw

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
        , _pinBone{}
        , _listVertex{}
        , _softBody{}
        , _bPinBoneErrorShown{ SW_FALSE }
    {
    }

    void ClothComponent::setPinMode( ClothPinMode pinMode )
    {
        _pinMode = pinMode;
        requestRebuild();
    }

    void ClothComponent::setPinBone( const hashed_string& boneName )
    {
        _pinBone            = boneName;
        _bPinBoneErrorShown = SW_FALSE;
        requestRebuild();
    }

    bool ClothComponent::readPinBonePose( float3& outPosition, quaternion& outRotation )
    {
        // 같은 오브젝트, 없으면 부모 오브젝트의 스켈레탈 메시 — 망토 · 헤어 카드는 보통 몸 오브젝트의 자식이다.
        GameObject*                  pOwner  = getOwner();
        GameObject*                  pParent = pOwner != nullptr ? pOwner->getParent() : nullptr;
        const SkeletalMeshComponent* pUnit   = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pUnit == nullptr && pParent != nullptr )
            pUnit = pParent->getComponent<SkeletalMeshComponent>();
        float4x4   boneModel{};
        const bool bFound = pUnit != nullptr && pUnit->findBoneModelTransform( _pinBone, boneModel );
        if ( bFound == false )
        {
            // 스켈레톤이 풀리기 전 프레임과 정말 없는 본을 가르지 못하므로 한 번만 알리고 계속 기다린다(천은 만들지 않는다 — 오브젝트 자리로 대신하지 않는다).
            if ( _bPinBoneErrorShown == SW_FALSE )
            {
                _bPinBoneErrorShown = SW_TRUE;
                SW_LOG_ERROR( "Cloth '%#': pin bone '%#' is not on a skeletal mesh of this object or its parent yet", pOwner != nullptr ? pOwner->getName().c_str() : "?",
                              _pinBone.c_str() );
            }
            return false;
        }
        const float4x4 boneWorld = boneModel * pUnit->getWorldMatrix();
        outPosition              = boneWorld.getTranslation();
        outRotation              = boneWorld.getRotation();
        return true;
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
        // 본에 건 천(헤어 카드 · 망토)은 그 본의 월드 자세를 따른다. 본을 아직 못 읽으면(스켈레톤 · 포즈 전) 이번 프레임은 천을 만들지 않는다.
        if ( _pinBone.empty() == false && readPinBonePose( position, rotation ) == false )
            return;
        if ( _softBody.isValid() == false )
        {
            _softBody = pScene->createSoftBody( makeSoftBodyDesc( position, rotation, physics.resolveLayer( _layer ) ) );
            return;
        }
        pScene->setSoftBodyPinnedTransform( _softBody, position, rotation );
    }

    bool ClothComponent::makeRenderVertices( const vector<float3>& listWorldVertex, const float4x4& worldToLocal, vector<RHIVertex>& outListVertex ) const
    {
        outListVertex.clear();
        const size_t vertexCount   = static_cast<size_t>( _columnCount ) * _rowCount;
        const bool   bGridMismatch = _columnCount < 2 || _rowCount < 2 || listWorldVertex.size() != vertexCount;
        if ( bGridMismatch )
            return false;

        // 메시 공간 위치와, 격자 삼각형(`makeSoftBodyDesc` 와 같은 둘)의 면 노멀을 정점마다 더한 매끈한 노멀.
        vector<float3> listPosition( vertexCount );
        for ( size_t index = 0; index < vertexCount; ++index )
        {
            listPosition[index] = float3::transform( listWorldVertex[index], worldToLocal );
        }
        vector<float3> listNormal( vertexCount, float3{ 0.0f, 0.0f, 0.0f } );
        for ( uint32 row = 0; row + 1 < _rowCount; ++row )
        {
            for ( uint32 column = 0; column + 1 < _columnCount; ++column )
            {
                const uint32 topLeft     = row * _columnCount + column;
                const uint32 bottomLeft  = topLeft + _columnCount;
                const uint32 topRight    = topLeft + 1;
                const uint32 bottomRight = bottomLeft + 1;
                const float3 firstFace   = ( listPosition[bottomLeft] - listPosition[topLeft] ).cross( listPosition[topRight] - listPosition[topLeft] );
                const float3 secondFace  = ( listPosition[bottomLeft] - listPosition[topRight] ).cross( listPosition[bottomRight] - listPosition[topRight] );
                listNormal[topLeft] += firstFace;
                listNormal[bottomLeft] += firstFace + secondFace;
                listNormal[topRight] += firstFace + secondFace;
                listNormal[bottomRight] += secondFace;
            }
        }
        for ( float3& normal : listNormal )
        {
            normal = normal.normalize(); // 넓이가 0 인 곳(접혀 겹친 정점)은 0 노멀이다
        }

        const float32 uStep = 1.0f / static_cast<float32>( _columnCount - 1 );
        const float32 vStep = 1.0f / static_cast<float32>( _rowCount - 1 );
        outListVertex.reserve( static_cast<size_t>( _columnCount - 1 ) * ( _rowCount - 1 ) * 12 );
        for ( uint32 row = 0; row + 1 < _rowCount; ++row )
        {
            for ( uint32 column = 0; column + 1 < _columnCount; ++column )
            {
                const uint32 topLeft     = row * _columnCount + column;
                const uint32 bottomLeft  = topLeft + _columnCount;
                const uint32 topRight    = topLeft + 1;
                const uint32 bottomRight = bottomLeft + 1;
                const auto   pushVertex  = [&]( uint32 index, float32 normalSign )
                {
                    const float32 u = static_cast<float32>( index % _columnCount ) * uStep;
                    const float32 v = static_cast<float32>( index / _columnCount ) * vStep;
                    outListVertex.push_back( makeClothRenderVertex( listPosition[index], listNormal[index] * normalSign, u, v ) );
                };
                // 앞면 — 격자 삼각형 그대로(정면에서 바깥을 향한 감김).
                pushVertex( topLeft, 1.0f );
                pushVertex( bottomLeft, 1.0f );
                pushVertex( topRight, 1.0f );
                pushVertex( topRight, 1.0f );
                pushVertex( bottomLeft, 1.0f );
                pushVertex( bottomRight, 1.0f );
                // 뒷면 — 감김과 노멀을 뒤집는다.
                pushVertex( topLeft, -1.0f );
                pushVertex( topRight, -1.0f );
                pushVertex( bottomLeft, -1.0f );
                pushVertex( topRight, -1.0f );
                pushVertex( bottomRight, -1.0f );
                pushVertex( bottomLeft, -1.0f );
            }
        }
        return true;
    }

    void ClothComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)alpha;
        const IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr && _softBody.isValid() )
        {
            (void)pScene->getSoftBodyVertices( _softBody, _listVertex );
            updateRenderMesh();
        }
    }

    void ClothComponent::updateRenderMesh()
    {
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr || _listVertex.empty() )
            return;
        vector<RHIVertex> listVertex;
        if ( makeRenderVertices( _listVertex, pMesh->getWorldMatrix().invert(), listVertex ) == false )
        {
            SW_LOG_ERROR( "Cloth vertices (%#) do not match the %# x %# grid - the cloth mesh is not updated", static_cast<uint64>( _listVertex.size() ), _columnCount, _rowCount );
            return;
        }
        // 렌더 스냅샷이 옛 메시를 쥐고 있으므로 정점을 제자리에서 바꾸지 않고 새 메시를 건다.
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setDynamic( true );
        mesh->setVertices( std::move( listVertex ) );
        pMesh->setMesh( std::move( mesh ) );
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
