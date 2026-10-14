#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/FitCheckSummary.h"

#include "Engine/Character/Fit/FitSolver.h"

namespace sw::editor
{
    namespace
    {
        struct FitCheckSummaryInternal
        {
            static hashed_string findPartName( const FitResult& result, uint32 partIndex )
            {
                return partIndex < result._listPart.size() ? result._listPart[partIndex]._name : hashed_string{};
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    FitCheckSummary FitCheckSummary::fromResult( const FitResult& result )
    {
        using Internal = FitCheckSummaryInternal;

        FitCheckSummary summary;
        for ( const FitPartResult& part : result._listPart )
        {
            FitPartSummary row;
            row._name             = part._name;
            row._triangleCount    = part._triangleCount;
            row._cutTriangleCount = part._cutTriangleCount;
            row._cutRatio         = part._triangleCount > 0 ? static_cast<float32>( part._cutTriangleCount ) / static_cast<float32>( part._triangleCount ) : 0.0f;
            for ( const float3& delta : part._listVertexDelta )
            {
                const float32 distance = delta.getLength();
                if ( distance <= 0.0f )
                    continue;
                ++row._movedVertexCount;
                if ( distance > row._maxDelta )
                    row._maxDelta = distance;
            }
            summary._listPart.push_back( row );
        }
        for ( const FitPenetration& penetration : result._report._listPenetration )
        {
            FitPenetrationSummary row;
            row._partA       = Internal::findPartName( result, penetration._partA );
            row._partB       = Internal::findPartName( result, penetration._partB );
            row._vertexCount = penetration._vertexCount;
            row._maxDepth    = penetration._maxDepth;
            summary._listPenetration.push_back( row );
            summary._penetrationVertexCount += penetration._vertexCount;
            if ( penetration._maxDepth > summary._maxPenetrationDepth )
                summary._maxPenetrationDepth = penetration._maxDepth;
        }
        summary._listMessage = result._report._listMessage;
        return summary;
    }
} // namespace sw::editor
