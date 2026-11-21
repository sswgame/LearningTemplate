#include "pch.h"

#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"

#include "Core/Math/Half.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/RHIMeshVertexLayout.h"

namespace sw
{
    uint32 RHIMeshVertexStreams::computeBufferBytes( RHIMeshVertexFormat format, uint32 vertexCapacity )
    {
        return vertexCapacity * ( constant::render::kMeshPositionStride + constant::render::getMeshAttributeStride( format ) );
    }

    void RHIMeshVertexStreams::bind( IRHIPassCommandList& cmd, RHIBufferHandle vertexBuffer, uint32 attributeOffset, RHIMeshVertexFormat format )
    {
        cmd.setVertexBuffer( constant::render::kMeshPositionSlot, vertexBuffer, constant::render::kMeshPositionStride, 0 );
        cmd.setVertexBuffer( constant::render::kMeshAttributeSlot, vertexBuffer, constant::render::getMeshAttributeStride( format ), attributeOffset );
    }

    void RHIMeshVertexStreams::write( RHIMeshVertexFormat format, const RHIVertex* pSource, uint32 count, uint32 firstVertex, uint32 vertexCapacity, uint8* pBuffer )
    {
        SW_ASSERT( firstVertex + count <= vertexCapacity );
        uint8* const pOutPosition  = pBuffer + static_cast<size_t>( firstVertex ) * constant::render::kMeshPositionStride;
        uint8* const pOutAttribute = pBuffer + computeAttributeOffset( vertexCapacity ) + static_cast<size_t>( firstVertex ) * constant::render::getMeshAttributeStride( format );
        for ( uint32 vertexIndex = 0; vertexIndex < count; ++vertexIndex )
        {
            const RHIVertex& vertex = pSource[vertexIndex];
            Memory::copy( pOutPosition + static_cast<size_t>( vertexIndex ) * constant::render::kMeshPositionStride, vertex._arrPosition, constant::render::kMeshPositionStride );
            if ( format == RHIMeshVertexFormat::HighPrecision )
            {
                RHIHighPrecisionMeshAttributes attributes{};
                Memory::copy( attributes._arrNormal, vertex._arrNormal, sizeof( attributes._arrNormal ) );
                Memory::copy( attributes._arrUV, vertex._arrUV, sizeof( attributes._arrUV ) );
                Memory::copy( attributes._arrColor, vertex._arrColor, sizeof( attributes._arrColor ) );
                Memory::copy( attributes._arrTangent, vertex._arrTangent, sizeof( attributes._arrTangent ) );
                Memory::copy( pOutAttribute + static_cast<size_t>( vertexIndex ) * sizeof( RHIHighPrecisionMeshAttributes ), &attributes, sizeof( attributes ) );
                continue;
            }
            RHIPackedMeshAttributes attributes{};
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                attributes._arrNormal[axis]  = snorm8::fromFloat( vertex._arrNormal[axis] ).getRaw();
                attributes._arrTangent[axis] = snorm8::fromFloat( vertex._arrTangent[axis] ).getRaw();
            }
            attributes._arrTangent[3] = vertex._arrTangent[3] < 0.0f ? int8{ -127 } : int8{ 127 }; // 바이탄젠트 부호 — 0 은 +로 둔다(MikkTSpace 가 0 을 내지 않는다)
            Memory::copy( attributes._arrUV, vertex._arrUV, sizeof( attributes._arrUV ) );
            for ( uint32 channel = 0; channel < 4; ++channel )
                attributes._arrColor[channel] = half::fromFloat( vertex._arrColor[channel] ); // R-15 — 정점 색은 half(65504 넘는 값은 무한대가 된다)
            Memory::copy( pOutAttribute + static_cast<size_t>( vertexIndex ) * sizeof( RHIPackedMeshAttributes ), &attributes, sizeof( attributes ) );
        }
    }
} // namespace sw
