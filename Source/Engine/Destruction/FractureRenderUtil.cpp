#include "pch.h"

#include "Engine/Destruction/FractureRenderUtil.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Graphics/Mesh/Mesh.h"

namespace sw
{
    shared_ptr<Skeleton> FractureRenderUtil::createPieceSkeleton( const FractureAsset& asset )
    {
        shared_ptr<Skeleton> skeleton = make_shared<Skeleton>();
        for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
        {
            StringBuilder<constant::kMaxBuffer32> name;
            name.appendFormat( "piece%#", piece );
            const float3& centroid = asset._graph._listNode[piece]._centroid;
            BoneTransform reference;
            reference._translation = centroid;
            (void)skeleton->addBone( hashed_string( name.c_str() ), -1, reference, float4x4::createTranslation( -centroid ) );
        }
        return skeleton;
    }

    shared_ptr<Mesh> FractureRenderUtil::createSkinnedMesh( const FractureAsset& asset, FractureSurfaceSlot slot )
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
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setVertices( std::move( listVertex ) );
        mesh->setSkin( std::move( listSkin ), asset.getPieceCount() );
        return mesh;
    }

    shared_ptr<Mesh> FractureRenderUtil::createBakedMesh( const FractureAsset& asset, FractureSurfaceSlot slot, vector_reference<const BoneTransform> listPose )
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
                    const float3 position = float3::transform( float3{ local._x * pose._scale._x, local._y * pose._scale._y, local._z * pose._scale._z }, pose._rotation ) +
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
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setVertices( std::move( listVertex ) );
        return mesh;
    }

    BoneTransform FractureRenderUtil::makeBoneTransform( const float3& centroid, const quaternion& rotation, const float3& offset, float32 scale )
    {
        BoneTransform transform;
        transform._rotation    = rotation;
        transform._scale       = float3{ scale, scale, scale };
        transform._translation = float3::transform( centroid, rotation ) + offset;
        return transform;
    }
} // namespace sw
