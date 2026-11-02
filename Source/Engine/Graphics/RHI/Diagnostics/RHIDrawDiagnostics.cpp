#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    namespace
    {
        struct RHIDrawDiagnosticsInternal
        {
            /** @brief 백엔드 이름마다 이미 센 횟수의 맵입니다. 기록 스레드가 여럿이라 잠급니다(잘못된 드로우를 만난 경로에서만 부릅니다). */
            static inline mutex                         s_mutex;
            static inline unordered_map<string, uint32> s_mapBackendToCount;

            /** @brief 그 백엔드의 횟수를 1 증가시키고 증가 전 값을 돌려줍니다. */
            static uint32 countReport( const utf8* pBackendName )
            {
                ScopedLock<mutex> lock{ s_mutex };
                uint32&           count = s_mapBackendToCount[string( pBackendName )];
                return count++;
            }
        };
    } // namespace

    void RHIDrawDiagnostics::reportDestroyedVertexBuffer( const utf8* pBackendName, RHIBufferHandle handle )
    {
        const uint32 count = RHIDrawDiagnosticsInternal::countReport( pBackendName );
        if ( count < kMaxReportedDraw )
            SW_LOG_ERROR_ID( "RHI.DrawSkippedVertexBufferDestroyed", "%# draw skipped: its vertex buffer (handle %#) was destroyed while bound - rebind before drawing", pBackendName, handle );
    }

    bool RHIDrawDiagnostics::checkVertexSlotsBound( const utf8* pBackendName, uint32 vertexSlotMask, RHIMeshVertexFormat meshVertexFormat, const RHIVertexBufferBindings& bindings )
    {
        for ( uint32 slot = 0; slot < constant::render::kVertexBufferSlotCount; ++slot )
        {
            if ( ( vertexSlotMask & ( 1u << slot ) ) == 0 )
                continue;
            const RHIVertexBufferBinding& binding        = bindings._arrSlot[slot];
            const uint32                  expectedStride = constant::render::getVertexSlotStride( meshVertexFormat, slot );
            if ( binding._buffer != 0 && binding._stride == expectedStride )
                continue;
            const uint32 count = RHIDrawDiagnosticsInternal::countReport( pBackendName );
            if ( count >= kMaxReportedDraw )
                return false;
            if ( binding._buffer == 0 )
                SW_LOG_ERROR_ID( "RHI.DrawSkippedVertexSlotUnbound",
                                 "%# draw skipped: its vertex shader reads vertex buffer slot %# but no buffer is bound - bind the mesh streams (slots 0 and 2) or instance slot stream (slot 1), or drop VertexInput from a shader that builds its vertices",
                                 pBackendName, slot );
            else
                SW_LOG_ERROR_ID( "RHI.DrawSkippedVertexStrideMismatch",
                                 "%# draw skipped: vertex buffer slot %# has %#-byte elements but the PSO's mesh vertex format (%#) reads %#-byte elements - build the PSO with the mesh's vertex format",
                                 pBackendName, slot, binding._stride, static_cast<uint32>( meshVertexFormat ), expectedStride );
            return false;
        }
        return true;
    }
} // namespace sw
