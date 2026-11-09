#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodySurfacePick.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw::editor
{
    void BodyPickSurface::build( vector_reference<const RHIVertex> listVertex, vector_reference<const MeshSkinVertex> listSkin, const vector<float4x4>& listPalette,
                                 const float4x4& world, BodyPickSurface& outSurface )
    {
        const bool bSkinned    = listSkin.size() == listVertex.size() && listPalette.empty() == false;
        outSurface._listVertex = listVertex;
        outSurface._listPosition.resize( listVertex.size() );
        for ( size_t index = 0; index < listVertex.size(); ++index )
        {
            RHIVertex vertex = listVertex[index];
            if ( bSkinned )
                MeshSkinBinding::skinVertex( listPalette, listSkin[index], vertex );
            outSurface._listPosition[index] = float3::transform( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] }, world );
        }
    }

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

    bool BodySurfaceHit::pick( vector_reference<const BodyPickSurface> listSurface, const float3& origin, const float3& direction, BodySurfaceHit& outHit )
    {
        bool bHit = false;
        for ( uint32 surfaceIndex = 0; surfaceIndex < static_cast<uint32>( listSurface.size() ); ++surfaceIndex )
        {
            const BodyPickSurface&                  surface      = listSurface[surfaceIndex];
            const vector<float3>&                   listPosition = surface._listPosition;
            const vector_reference<const RHIVertex> listVertex   = surface._listVertex;
            const vector_reference<const uint32>    listIndex    = surface._listIndex;
            for ( uint32 triangle = 0; triangle * 3u + 2u < static_cast<uint32>( listIndex.size() ); ++triangle )
            {
                const uint32 a = listIndex[triangle * 3u];
                const uint32 b = listIndex[triangle * 3u + 1u];
                const uint32 c = listIndex[triangle * 3u + 2u];
                if ( a >= listPosition.size() || b >= listPosition.size() || c >= listPosition.size() )
                    continue;
                float32 distance{ 0.0f };
                float32 u{ 0.0f };
                float32 v{ 0.0f };
                bool    bFrontFace{ false };
                if ( CharacterGeometryUtil::intersectRayTriangle( origin, direction, listPosition[a], listPosition[b], listPosition[c], distance, u, v, bFrontFace ) == false )
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
                outHit._surface   = surfaceIndex;
                outHit._triangle  = triangle;
                outHit._section   = findSection( surface._listSection, triangle );
                bHit              = true;
            }
        }
        return bHit;
    }
} // namespace sw::editor
