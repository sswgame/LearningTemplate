#include "pch.h"

#include "Engine/Destruction/Fracture/FractureRenderUnits.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    FractureRenderUnits::FractureRenderUnits()
        : _arrSkinnedMesh{}
        , _arrSkinnedUnit{}
        , _arrBakedUnit{}
        , _bBaked{ SW_FALSE }
    {
    }

    template <typename T>
    T* FractureRenderUnits::findOwnedComponent( const GameObject& owner, TComponentHandle<T> handle )
    {
        GameObjectManager* pManager = owner.getMutableManager();
        return ( pManager != nullptr && handle.isValid() ) ? pManager->resolveComponent( handle ) : nullptr;
    }

    void FractureRenderUnits::create( GameObject& owner, const FractureAsset& asset, const MeshComponent* pIntact, const hashed_string& interiorMaterialPath )
    {
        shared_ptr<const Skeleton> skeleton   = FractureRenderUnits::createPieceSkeleton( asset );
        const FractureSurfaceSlot  arrSlot[2] = { FractureSurfaceSlot::Outer, FractureSurfaceSlot::Interior };
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            _arrSkinnedMesh[slot] = FractureRenderUnits::createSkinnedMesh( asset, arrSlot[slot] );
            if ( _arrSkinnedMesh[slot] == nullptr )
                continue;
            SkeletalMeshComponent* pUnit = owner.addComponent<SkeletalMeshComponent>();
            if ( pUnit == nullptr )
                continue;
            pUnit->setTransient( true ); // 부서진 상태에서 다시 세우는 파생 — 저장 · 상태 해시에 넣지 않는다(그 메시는 파일이 아니라 실을 수도 없다)
            pUnit->setSkeleton( skeleton );
            pUnit->setMesh( _arrSkinnedMesh[slot] );
            pUnit->setVisibilityBasedTickOption( VisibilityBasedAnimationTickOption::AlwaysTickPoseAndRefreshBones );
            const bool bInterior = slot == 1 && interiorMaterialPath.empty() == false;
            if ( bInterior )
                pUnit->setMaterialPath( interiorMaterialPath.view() );
            else if ( pIntact != nullptr && pIntact->getMaterialPath().empty() == false )
                pUnit->setMaterialPath( pIntact->getMaterialPath().view() );
            else if ( pIntact != nullptr )
                pUnit->setMaterial( pIntact->getMaterial() );
            _arrSkinnedUnit[slot] = TComponentHandle<SkeletalMeshComponent>{ pUnit };
        }
    }

    void FractureRenderUnits::writePoses( const GameObject& owner, const FractureAsset& asset, const vector<BoneTransform>& listPiecePose, float32 maxOffset )
    {
        const float32 assetRadius = MathUtil::max( asset._boundsMax.getLength(), asset._boundsMin.getLength() );
        for ( const TComponentHandle<SkeletalMeshComponent>& handle : _arrSkinnedUnit )
        {
            SkeletalMeshComponent* pUnit = findOwnedComponent( owner, handle );
            if ( pUnit == nullptr )
                continue;
            Pose& pose = pUnit->getLocalPose();
            if ( pose.getBoneCount() != listPiecePose.size() )
                continue;
            for ( uint32 leaf = 0; leaf < static_cast<uint32>( listPiecePose.size() ); ++leaf )
            {
                pose.setBoneTransform( leaf, listPiecePose[leaf] );
            }
            pUnit->applyExternalPose();
            pUnit->setBoundsRadius( assetRadius + maxOffset );
            pUnit->markRenderStateDirty();
        }
    }

    void FractureRenderUnits::setBaked( GameObject& owner, const FractureAsset& asset, const vector<BoneTransform>& listPiecePose, bool bBaked )
    {
        const FractureSurfaceSlot arrSlot[2] = { FractureSurfaceSlot::Outer, FractureSurfaceSlot::Interior };
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            SkeletalMeshComponent* pSkinned = findOwnedComponent( owner, _arrSkinnedUnit[slot] );
            if ( pSkinned == nullptr )
                continue;
            MeshComponent* pBaked = findOwnedComponent( owner, _arrBakedUnit[slot] );
            if ( bBaked && pBaked == nullptr )
            {
                pBaked = owner.addComponent<MeshComponent>();
                if ( pBaked == nullptr )
                    continue;
                pBaked->setTransient( true );
                if ( pSkinned->getMaterialPath().empty() == false )
                    pBaked->setMaterialPath( pSkinned->getMaterialPath().view() );
                else
                    pBaked->setMaterial( pSkinned->getMaterial() );
                _arrBakedUnit[slot] = TComponentHandle<MeshComponent>{ pBaked };
            }
            if ( bBaked )
            {
                shared_ptr<Mesh> baked = FractureRenderUnits::createBakedMesh( asset, arrSlot[slot], listPiecePose );
                pBaked->setVisible( baked != nullptr );
                if ( baked != nullptr )
                    pBaked->setMesh( std::move( baked ) );
                pSkinned->setVisible( false );
            }
            else
            {
                if ( pBaked != nullptr )
                    pBaked->setVisible( false );
                pSkinned->setVisible( true );
            }
        }
        _bBaked = bBaked ? SW_TRUE : SW_FALSE;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct FractureRenderUnitsInternal
        {
            static constexpr int32 kNoParent = -1; ///< 조각 본은 모두 루트다
        };
    } // namespace

    shared_ptr<Skeleton> FractureRenderUnits::createPieceSkeleton( const FractureAsset& asset )
    {
        shared_ptr<Skeleton> skeleton = make_shared<Skeleton>();
        for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
        {
            StringBuilder<constant::buffer::kMax32> name;
            name.appendFormat( "piece%#", piece );
            const float3& centroid = asset._graph._listNode[piece]._centroid;
            BoneTransform reference;
            reference._translation = centroid;
            (void)skeleton->addBone( hashed_string( name.c_str() ), FractureRenderUnitsInternal::kNoParent, reference );
        }
        return skeleton;
    }

    shared_ptr<Mesh> FractureRenderUnits::createSkinnedMesh( const FractureAsset& asset, FractureSurfaceSlot slot )
    {
        vector<RHIVertex>      listVertex;
        vector<MeshSkinVertex> listSkin;
        for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
        {
            const FracturePiece& range = asset._listPiece[piece];
            for ( uint32 vertex = 0; vertex < range._vertexCount; vertex += 3 )
            {
                if ( asset._listTriangleSlot[( range._firstVertex + vertex ) / 3] != static_cast<uint8>( slot ) )
                    continue;
                for ( uint32 corner = 0; corner < 3; ++corner )
                {
                    listVertex.push_back( asset._listVertex[range._firstVertex + vertex + corner] );
                    MeshSkinVertex skin;
                    skin._arrJoint[0] = static_cast<uint16>( piece );
                    listSkin.push_back( skin );
                }
            }
        }
        if ( listVertex.empty() )
            return nullptr;
        shared_ptr<Mesh> mesh = Mesh::create( MeshCPUAccess::Keep );
        mesh->setVertices( std::move( listVertex ) );
        // 조각 뼈의 레퍼런스가 조각 중심이므로 역 바인드는 중심을 원점으로 옮긴다.
        MeshSkinBinding skinBinding{};
        skinBinding._listInverseBind.reserve( asset.getPieceCount() );
        for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
        {
            skinBinding._listInverseBind.push_back( float4x4::makeTranslation( -asset._graph._listNode[piece]._centroid ) );
        }
        mesh->setSkin( std::move( listSkin ), std::move( skinBinding ) );
        return mesh;
    }

    shared_ptr<Mesh> FractureRenderUnits::createBakedMesh( const FractureAsset& asset, FractureSurfaceSlot slot, vector_reference<const BoneTransform> listPose )
    {
        vector<RHIVertex> listVertex;
        for ( uint32 piece = 0; piece < asset.getPieceCount() && piece < listPose.size(); ++piece )
        {
            const BoneTransform& pose = listPose[piece];
            if ( pose._scale._x <= 0.0f )
                continue;
            const float3&        centroid = asset._graph._listNode[piece]._centroid;
            const FracturePiece& range    = asset._listPiece[piece];
            for ( uint32 vertex = 0; vertex < range._vertexCount; vertex += 3 )
            {
                if ( asset._listTriangleSlot[( range._firstVertex + vertex ) / 3] != static_cast<uint8>( slot ) )
                    continue;
                for ( uint32 corner = 0; corner < 3; ++corner )
                {
                    RHIVertex    baked = asset._listVertex[range._firstVertex + vertex + corner];
                    const float3 local{ baked._arrPosition[0] - centroid._x, baked._arrPosition[1] - centroid._y, baked._arrPosition[2] - centroid._z };
                    const float3 position = float3::transform( float3::multiply( local, pose._scale ), pose._rotation ) +
                                            pose._translation;
                    const float3 normal   = float3::transform( float3{ baked._arrNormal[0], baked._arrNormal[1], baked._arrNormal[2] }, pose._rotation );
                    baked._arrPosition[0] = position._x;
                    baked._arrPosition[1] = position._y;
                    baked._arrPosition[2] = position._z;
                    baked._arrNormal[0]   = normal._x;
                    baked._arrNormal[1]   = normal._y;
                    baked._arrNormal[2]   = normal._z;
                    listVertex.push_back( baked );
                }
            }
        }
        if ( listVertex.empty() )
            return nullptr;
        shared_ptr<Mesh> mesh = Mesh::create( MeshCPUAccess::Keep );
        mesh->setVertices( std::move( listVertex ) );
        return mesh;
    }

    BoneTransform FractureRenderUnits::makeBoneTransform( const float3& centroid, const quaternion& rotation, const float3& offset, float32 scale )
    {
        BoneTransform transform;
        transform._rotation    = rotation;
        transform._scale       = float3{ scale, scale, scale };
        transform._translation = float3::transform( centroid, rotation ) + offset;
        return transform;
    }
} // namespace sw
