#include "pch.h"

#include "Engine/Animation/Scene/MeshVertexAnimationBaker.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"

namespace sw
{
    SW_LOG_CALLER( "MeshVertexAnimationBaker" );

    void MeshVertexAnimationBaker::skinVertices( const Mesh& skinMesh, const vector<float4x4>& listPalette, vector<float3>& outListPosition,
                                                 vector<float3>& outListNormal )
    {
        const vector<RHIVertex>&      listVertex = skinMesh.getVertices();
        const vector<MeshSkinVertex>& listSkin   = skinMesh.getSkinVertices();
        outListPosition.resize( listVertex.size() );
        outListNormal.resize( listVertex.size() );
        const uint32 boneCount = static_cast<uint32>( listPalette.size() );
        for ( size_t vertexIndex = 0; vertexIndex < listVertex.size(); ++vertexIndex )
        {
            const RHIVertex& vertex = listVertex[vertexIndex];
            const float3     position{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
            const float3     normal{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] };
            if ( vertexIndex >= listSkin.size() || boneCount == 0 )
            {
                outListPosition[vertexIndex] = position;
                outListNormal[vertexIndex]   = normal;
                continue;
            }
            // mesh_skin.hlsl 과 같은 식 — 본 넷을 가중치로 섞고(본 번호는 본 수 안으로), 노멀은 이동 없이 옮겨 정규화한다.
            float3 skinnedPosition{};
            float3 skinnedNormal{};
            for ( uint32 influence = 0; influence < 4; ++influence )
            {
                const uint32    bone   = MathUtil::min( static_cast<uint32>( listSkin[vertexIndex]._arrJoint[influence] ), boneCount - 1u );
                const float32   weight = listSkin[vertexIndex]._arrWeight[influence];
                const float4x4& matrix = listPalette[bone];
                skinnedPosition        = skinnedPosition + float3::transform( position, matrix ) * weight;
                skinnedNormal          = skinnedNormal + float3::transformVector( normal, matrix ) * weight;
            }
            const float32 normalLength   = skinnedNormal.getLength();
            outListPosition[vertexIndex] = skinnedPosition;
            outListNormal[vertexIndex]   = normalLength > 1e-6f ? skinnedNormal * ( 1.0f / normalLength ) : normal;
        }
    }

    bool MeshVertexAnimationBaker::bake( const Mesh& skinMesh, const Skeleton& skeleton, const AnimationClip& clip, float32 framesPerSecond, bool bAnchorRootMotion,
                                         MeshVertexAnimation& outAnimation )
    {
        outAnimation           = MeshVertexAnimation{};
        const bool bCannotBake = skinMesh.hasSkin() == false || skinMesh.getSkinBoneCount() != skeleton.getBoneCount() || framesPerSecond <= 0.0f;
        if ( bCannotBake )
        {
            SW_LOG_WARNING( "Vertex animation bake skipped: the mesh has no skin or its bone count (%#) differs from the skeleton (%#)", skinMesh.getSkinBoneCount(),
                            skeleton.getBoneCount() );
            return false;
        }
        const float32 duration = clip.getDuration();
        const bool    bLoop    = clip.isLoopingByDefault();
        // 반복 클립의 끝 프레임은 첫 프레임과 같아 싣지 않는다(셰이더가 감아 보간한다). 반복하지 않으면 끝 시각을 마지막 프레임으로 싣는다.
        const uint32 frameCount  = bLoop ? MathUtil::max( static_cast<uint32>( MathUtil::round( duration * framesPerSecond ) ), 1u )
                                         : static_cast<uint32>( MathUtil::floor( duration * framesPerSecond ) ) + 1u;
        const uint32 vertexCount = skinMesh.getVertexCount();
        // 반복 클립은 표 한 바퀴가 클립 길이와 꼭 같아야 CPU 시각과 어긋나지 않는다 — 프레임율을 프레임 수 / 길이로 맞춘다.
        const float32 effectiveRate = ( bLoop && duration > 0.0f ) ? static_cast<float32>( frameCount ) / duration : framesPerSecond;

        vector<int32> listTrackToBone;
        clip.makeTrackToBoneMap( skeleton, listTrackToBone );
        Pose             pose;
        Pose             scratchTrackPose;
        vector<float4x4> listModel;
        vector<float4x4> listPalette;
        vector<float3>   listPosition;
        vector<float3>   listNormal;
        // 위치 경계는 모든 프레임을 봐야 나오므로 float 로 다 모은 뒤 한 번에 압축한다(`MeshVertexAnimation::quantize`).
        vector<float3> listAllPosition;
        vector<float3> listAllNormal;
        listAllPosition.reserve( static_cast<size_t>( frameCount ) * vertexCount );
        listAllNormal.reserve( static_cast<size_t>( frameCount ) * vertexCount );
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const float32 time = MathUtil::min( static_cast<float32>( frameIndex ) / effectiveRate, duration );
            pose.setToReference( skeleton );
            if ( clip.samplePose( time, listTrackToBone, pose, scratchTrackPose, bAnchorRootMotion ) == false )
                return false;
            pose.computeModelSpace( skeleton.getParentIndices(), listModel );
            Pose::computeSkinPalette( skeleton, listModel, listPalette );
            skinVertices( skinMesh, listPalette, listPosition, listNormal );
            listAllPosition.insert( listAllPosition.end(), listPosition.begin(), listPosition.end() );
            listAllNormal.insert( listAllNormal.end(), listNormal.begin(), listNormal.end() );
        }
        outAnimation._frameCount  = frameCount;
        outAnimation._vertexCount = vertexCount;
        if ( outAnimation.quantize( listAllPosition, listAllNormal ) == false )
        {
            outAnimation = MeshVertexAnimation{};
            return false;
        }
        outAnimation._framesPerSecond   = effectiveRate;
        outAnimation._duration          = duration;
        outAnimation._bLoop             = bLoop ? SW_TRUE : SW_FALSE;
        outAnimation._bAnchorRootMotion = bAnchorRootMotion ? SW_TRUE : SW_FALSE;
        return true;
    }
} // namespace sw
