#include "pch.h"

#include "Engine/Character/Fit/SurfaceTransfer.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/SurfaceBvh.h"

namespace sw
{
    namespace
    {
        struct SurfaceTransferInternal
        {
            static constexpr uint32 kMaxBlendedJoint = CharacterGeometryConstant::kMaxSkinInfluence * 3;

            static float3 computeInterpolatedNormal( const AppearanceGeometry& source, uint32 indexA, uint32 indexB, uint32 indexC, float32 u, float32 v )
            {
                if ( source._listNormal.size() == source._listPosition.size() )
                {
                    const float3 blended = CharacterGeometryUtil::interpolateBarycentric( source._listNormal[indexA], source._listNormal[indexB], source._listNormal[indexC], u, v );
                    return CharacterGeometryUtil::makeUnitOr( blended, float3::UnitY );
                }
                const float3& a = source._listPosition[indexA];
                return CharacterGeometryUtil::makeUnitOr( ( source._listPosition[indexB] - a ).cross( source._listPosition[indexC] - a ), float3::UnitY );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SurfaceTransferUtil::bindPoints( const AppearanceGeometry& source, const SurfaceBvh& sourceBvh, vector_reference<const float3> listPoint, float32 maxDistance,
                                          vector<SurfaceBinding>& outListBinding )
    {
        outListBinding.assign( listPoint.size(), SurfaceBinding{} );
        for ( size_t pointIndex = 0; pointIndex < listPoint.size(); ++pointIndex )
        {
            GeometryClosestPoint closest;
            if ( sourceBvh.findClosestPoint( listPoint[pointIndex], maxDistance, closest ) == false )
                continue;
            SurfaceBinding& binding = outListBinding[pointIndex];
            binding._triangle       = closest._triangle;
            binding._baryU          = closest._baryU;
            binding._baryV          = closest._baryV;
            const uint32 indexA     = source._listIndex[closest._triangle * 3];
            const uint32 indexB     = source._listIndex[closest._triangle * 3 + 1];
            const uint32 indexC     = source._listIndex[closest._triangle * 3 + 2];
            const float3 normal     = SurfaceTransferInternal::computeInterpolatedNormal( source, indexA, indexB, indexC, closest._baryU, closest._baryV );
            binding._normalOffset   = ( listPoint[pointIndex] - closest._point ).dot( normal );
            binding._bBound         = SW_TRUE;
        }
    }

    float3 SurfaceTransferUtil::evaluatePoint( const AppearanceGeometry& source, const SurfaceBinding& binding )
    {
        const uint32 indexA = source._listIndex[binding._triangle * 3];
        const uint32 indexB = source._listIndex[binding._triangle * 3 + 1];
        const uint32 indexC = source._listIndex[binding._triangle * 3 + 2];
        const float3 surfacePoint =
            CharacterGeometryUtil::interpolateBarycentric( source._listPosition[indexA], source._listPosition[indexB], source._listPosition[indexC], binding._baryU, binding._baryV );
        const float3 normal = SurfaceTransferInternal::computeInterpolatedNormal( source, indexA, indexB, indexC, binding._baryU, binding._baryV );
        return surfacePoint + normal * binding._normalOffset;
    }

    void SurfaceTransferUtil::transferMorphs( const AppearanceGeometry& source, vector_reference<const SurfaceBinding> listBinding, AppearanceGeometry& inoutTarget )
    {
        const uint32 targetVertexCount = inoutTarget.getVertexCount();
        for ( const GeometryMorphTarget& sourceMorph : source._listMorph )
        {
            GeometryMorphTarget transferred;
            transferred._name = sourceMorph._name;
            transferred._listPositionDelta.assign( targetVertexCount, float3::Zero );
            const bool bHasNormalDelta = sourceMorph._listNormalDelta.size() == source._listPosition.size();
            if ( bHasNormalDelta )
                transferred._listNormalDelta.assign( targetVertexCount, float3::Zero );
            for ( uint32 vertex = 0; vertex < targetVertexCount && vertex < listBinding.size(); ++vertex )
            {
                const SurfaceBinding& binding = listBinding[vertex];
                if ( binding._bBound == SW_FALSE )
                    continue;
                const uint32 indexA                    = source._listIndex[binding._triangle * 3];
                const uint32 indexB                    = source._listIndex[binding._triangle * 3 + 1];
                const uint32 indexC                    = source._listIndex[binding._triangle * 3 + 2];
                transferred._listPositionDelta[vertex] = CharacterGeometryUtil::interpolateBarycentric(
                    sourceMorph._listPositionDelta[indexA], sourceMorph._listPositionDelta[indexB], sourceMorph._listPositionDelta[indexC], binding._baryU, binding._baryV );
                if ( bHasNormalDelta )
                {
                    transferred._listNormalDelta[vertex] = CharacterGeometryUtil::interpolateBarycentric(
                        sourceMorph._listNormalDelta[indexA], sourceMorph._listNormalDelta[indexB], sourceMorph._listNormalDelta[indexC], binding._baryU, binding._baryV );
                }
            }
            bool bReplaced = false;
            for ( GeometryMorphTarget& existing : inoutTarget._listMorph )
            {
                if ( existing._name == transferred._name )
                {
                    existing  = std::move( transferred );
                    bReplaced = true;
                    break;
                }
            }
            if ( bReplaced == false )
                inoutTarget._listMorph.push_back( std::move( transferred ) );
        }
    }

    void SurfaceTransferUtil::transferSkinWeights( const AppearanceGeometry& source, vector_reference<const SurfaceBinding> listBinding, AppearanceGeometry& inoutTarget )
    {
        if ( source.isSkinned() == false )
            return;
        const uint32 targetVertexCount = inoutTarget.getVertexCount();
        inoutTarget._listSkin.resize( targetVertexCount );
        for ( uint32 vertex = 0; vertex < targetVertexCount && vertex < listBinding.size(); ++vertex )
        {
            const SurfaceBinding& binding = listBinding[vertex];
            if ( binding._bBound == SW_FALSE )
                continue;
            uint16        arrJoint[SurfaceTransferInternal::kMaxBlendedJoint]{};
            float32       arrWeight[SurfaceTransferInternal::kMaxBlendedJoint]{};
            uint32        jointCount         = 0;
            const float32 arrCornerWeight[3] = { 1.0f - binding._baryU - binding._baryV, binding._baryU, binding._baryV };
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const SkinInfluence& influence = source._listSkin[source._listIndex[binding._triangle * 3 + corner]];
                for ( uint32 slot = 0; slot < CharacterGeometryConstant::kMaxSkinInfluence; ++slot )
                {
                    const float32 weight = influence._arrWeight[slot] * arrCornerWeight[corner];
                    if ( weight <= 0.0f )
                        continue;
                    uint32 found = jointCount;
                    for ( uint32 existing = 0; existing < jointCount; ++existing )
                    {
                        if ( arrJoint[existing] == influence._arrJoint[slot] )
                        {
                            found = existing;
                            break;
                        }
                    }
                    if ( found == jointCount )
                    {
                        arrJoint[jointCount]  = influence._arrJoint[slot];
                        arrWeight[jointCount] = 0.0f;
                        ++jointCount;
                    }
                    arrWeight[found] += weight;
                }
            }
            // 큰 가중치 넷만 남긴다(선택 정렬 — 많아야 열둘).
            SkinInfluence result;
            float32       total = 0.0f;
            for ( uint32 slot = 0; slot < CharacterGeometryConstant::kMaxSkinInfluence; ++slot )
            {
                uint32  bestIndex  = jointCount;
                float32 bestWeight = 0.0f;
                for ( uint32 candidate = 0; candidate < jointCount; ++candidate )
                {
                    if ( arrWeight[candidate] > bestWeight )
                    {
                        bestWeight = arrWeight[candidate];
                        bestIndex  = candidate;
                    }
                }
                if ( bestIndex == jointCount )
                    break;
                result._arrJoint[slot]  = arrJoint[bestIndex];
                result._arrWeight[slot] = bestWeight;
                total += bestWeight;
                arrWeight[bestIndex] = 0.0f;
            }
            if ( total > 0.0f )
            {
                for ( float32& weight : result._arrWeight )
                {
                    weight /= total;
                }
            }
            inoutTarget._listSkin[vertex] = result;
        }
    }
} // namespace sw
