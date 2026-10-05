#include "pch.h"

#include "Engine/Character/Hit/Dismemberment.h"

#include "Engine/Character/Fit/GeometryCut.h"

namespace sw
{
    namespace
    {
        struct DismembermentInternal
        {
            /** @brief 자른 자리와 같은 자리의 정점을 표시한다(UV 이음매로 갈라진 같은 자리 정점도). */
            static void flagSeamVertices( const AppearanceGeometry& piece, const vector<float3>& listSeamPosition, vector<uint8>& outListFlag )
            {
                outListFlag.assign( piece.getVertexCount(), SW_FALSE );
                for ( uint32 vertex = 0; vertex < piece.getVertexCount(); ++vertex )
                {
                    for ( const float3& seamPosition : listSeamPosition )
                    {
                        if ( float3::getDistanceSquared( piece._listPosition[vertex], seamPosition ) <= 1.0e-12f )
                        {
                            outListFlag[vertex] = SW_TRUE;
                            break;
                        }
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool DismembermentUtil::severRegions( const AppearanceGeometry& body, vector_reference<const uint16> listVertexRegion, vector_reference<const uint16> listSeveredRegion,
                                          DismembermentResult& outResult )
    {
        outResult                  = DismembermentResult{};
        const uint32 triangleCount = body.getTriangleCount();
        outResult._listTriangleSevered.assign( triangleCount, SW_FALSE );
        uint32 severedCount = 0;
        for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
        {
            uint32 cornersInRegion = 0;
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const uint32 vertex = body._listIndex[triangle * 3 + corner];
                if ( vertex >= listVertexRegion.size() )
                    continue;
                for ( const uint16 region : listSeveredRegion )
                {
                    if ( listVertexRegion[vertex] == region )
                    {
                        ++cornersInRegion;
                        break;
                    }
                }
            }
            if ( cornersInRegion >= 2 )
            {
                outResult._listTriangleSevered[triangle] = SW_TRUE;
                ++severedCount;
            }
        }
        GeometryCutUtil::splitByTriangleMask( body, outResult._listTriangleSevered, outResult._remaining, outResult._severed );
        if ( severedCount == 0 )
            return false;
        // 자른 자리(남은 쪽 · 떨어진 쪽 삼각형이 함께 쓰는 정점)의 구멍만 막는다 — 원래 있던 구멍은 그대로.
        vector<uint8> listUsedByKept( body.getVertexCount(), SW_FALSE );
        vector<uint8> listUsedBySevered( body.getVertexCount(), SW_FALSE );
        for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
        {
            vector<uint8>& listUsed = outResult._listTriangleSevered[triangle] == SW_TRUE ? listUsedBySevered : listUsedByKept;
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                listUsed[body._listIndex[triangle * 3 + corner]] = SW_TRUE;
            }
        }
        vector<float3> listSeamPosition;
        for ( uint32 vertex = 0; vertex < body.getVertexCount(); ++vertex )
        {
            if ( listUsedByKept[vertex] == SW_TRUE && listUsedBySevered[vertex] == SW_TRUE )
                listSeamPosition.push_back( body._listPosition[vertex] );
        }
        vector<uint8> listSeamFlag;
        DismembermentInternal::flagSeamVertices( outResult._remaining, listSeamPosition, listSeamFlag );
        (void)GeometryCutUtil::createCaps( outResult._remaining, outResult._remainingCap, &listSeamFlag );
        DismembermentInternal::flagSeamVertices( outResult._severed, listSeamPosition, listSeamFlag );
        (void)GeometryCutUtil::createCaps( outResult._severed, outResult._severedCap, &listSeamFlag );
        return true;
    }
} // namespace sw
