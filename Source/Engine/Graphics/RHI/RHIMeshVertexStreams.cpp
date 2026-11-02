#include "pch.h"

#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    uint32 RHIMeshVertexStreams::computeBufferBytes( RHIMeshVertexFormat format, uint32 vertexCapacity )
    {
        return vertexCapacity * ( constant::render::kMeshPositionStride + constant::render::getMeshAttributeStride( format ) );
    }

    void RHIMeshVertexStreams::bind( IRHICommandList& cmd, RHIBufferHandle vertexBuffer, uint32 attributeOffset, RHIMeshVertexFormat format )
    {
        cmd.setVertexBuffer( constant::render::kMeshPositionSlot, vertexBuffer, constant::render::kMeshPositionStride, 0 );
        cmd.setVertexBuffer( constant::render::kMeshAttributeSlot, vertexBuffer, constant::render::getMeshAttributeStride( format ), attributeOffset );
    }

    int8 RHIMeshVertexStreams::packSnorm8( float32 value )
    {
        const float32 scaled = MathUtil::clamp( value, -1.0f, 1.0f ) * 127.0f;
        return static_cast<int8>( scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f );
    }

    void RHIMeshVertexStreams::write( RHIMeshVertexFormat format, const RHIVertex* pSource, uint32 count, uint8* pOutPosition, uint8* pOutAttribute )
    {
        for ( uint32 vertexIndex = 0; vertexIndex < count; ++vertexIndex )
        {
            const RHIVertex& vertex = pSource[vertexIndex];
            Memory::copy( pOutPosition + static_cast<size_t>( vertexIndex ) * constant::render::kMeshPositionStride, vertex._arrPosition, constant::render::kMeshPositionStride );
            if ( format == RHIMeshVertexFormat::Full )
            {
                RHIFullMeshAttributes attributes{};
                Memory::copy( attributes._arrNormal, vertex._arrNormal, sizeof( attributes._arrNormal ) );
                Memory::copy( attributes._arrUv, vertex._arrUv, sizeof( attributes._arrUv ) );
                Memory::copy( attributes._arrColor, vertex._arrColor, sizeof( attributes._arrColor ) );
                Memory::copy( attributes._arrTangent, vertex._arrTangent, sizeof( attributes._arrTangent ) );
                Memory::copy( pOutAttribute + static_cast<size_t>( vertexIndex ) * sizeof( RHIFullMeshAttributes ), &attributes, sizeof( attributes ) );
                continue;
            }
            RHIPackedMeshAttributes attributes{};
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                attributes._arrNormal[axis]  = packSnorm8( vertex._arrNormal[axis] );
                attributes._arrTangent[axis] = packSnorm8( vertex._arrTangent[axis] );
            }
            attributes._arrTangent[3] = vertex._arrTangent[3] < 0.0f ? int8{ -127 } : int8{ 127 }; // 바이탄젠트 부호 — 0 은 +로 둔다(MikkTSpace 가 0 을 내지 않는다)
            Memory::copy( attributes._arrUv, vertex._arrUv, sizeof( attributes._arrUv ) );
            Memory::copy( attributes._arrColor, vertex._arrColor, sizeof( attributes._arrColor ) );
            Memory::copy( pOutAttribute + static_cast<size_t>( vertexIndex ) * sizeof( RHIPackedMeshAttributes ), &attributes, sizeof( attributes ) );
        }
    }
} // namespace sw
