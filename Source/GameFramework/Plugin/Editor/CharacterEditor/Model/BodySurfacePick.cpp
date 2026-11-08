#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodySurfacePick.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw::editor
{
    uint32 BodySurfaceHit::findSection( vector_reference<const MeshSection> listSection, uint32 triangle )
    {
        const uint32 firstIndex = triangle * 3u;
        for ( uint32 index = 0; index < static_cast<uint32>( listSection.size() ); ++index )
        {
            const MeshSection& section = listSection[index];
            if ( firstIndex >= section._firstIndex && firstIndex < section._firstIndex + section._indexCount )
                return index;
        }
        return constant::invalid_index::kUint32;
    }

    bool BodySurfaceHit::pick( vector_reference<const RHIVertex> listVertex, vector_reference<const uint32> listIndex, vector_reference<const MeshSection> listSection,
                               const float4x4& world, const float3& origin, const float3& direction, BodySurfaceHit& outHit )
    {
        const auto toWorld = [&world, &listVertex]( uint32 vertex )
        {
            const RHIVertex& source = listVertex[vertex];
            return float3::transform( float3{ source._arrPosition[0], source._arrPosition[1], source._arrPosition[2] }, world );
        };
        bool bHit = false;
        for ( uint32 triangle = 0; triangle * 3u + 2u < static_cast<uint32>( listIndex.size() ); ++triangle )
        {
            const uint32 a = listIndex[triangle * 3u];
            const uint32 b = listIndex[triangle * 3u + 1u];
            const uint32 c = listIndex[triangle * 3u + 2u];
            if ( a >= listVertex.size() || b >= listVertex.size() || c >= listVertex.size() )
                continue;
            float32 distance{ 0.0f };
            float32 u{ 0.0f };
            float32 v{ 0.0f };
            bool    bFrontFace{ false };
            if ( CharacterGeometryUtil::intersectRayTriangle( origin, direction, toWorld( a ), toWorld( b ), toWorld( c ), distance, u, v, bFrontFace ) == false )
                continue;
            if ( bHit && distance >= outHit._distance )
                continue;
            // u 는 둘째, v 는 셋째 꼭짓점의 무게다(`intersectRayTriangle`).
            const float32 w   = 1.0f - u - v;
            const float2  uvA = float2{ listVertex[a]._arrUV[0], listVertex[a]._arrUV[1] };
            const float2  uvB = float2{ listVertex[b]._arrUV[0], listVertex[b]._arrUV[1] };
            const float2  uvC = float2{ listVertex[c]._arrUV[0], listVertex[c]._arrUV[1] };
            outHit._distance  = distance;
            outHit._position  = origin + direction * distance;
            outHit._uv        = float2{ uvA._x * w + uvB._x * u + uvC._x * v, uvA._y * w + uvB._y * u + uvC._y * v };
            outHit._triangle  = triangle;
            outHit._section   = findSection( listSection, triangle );
            bHit              = true;
        }
        return bHit;
    }
} // namespace sw::editor
