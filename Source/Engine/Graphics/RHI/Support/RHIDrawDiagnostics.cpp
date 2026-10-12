#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIDrawDiagnostics.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "RHIDrawDiagnostics" );

    namespace
    {
        struct RHIDrawDiagnosticsInternal
        {
            /** @brief 백엔드 이름마다 이미 센 횟수의 표입니다. 기록 스레드가 여럿이라 잠급니다(잘못된 드로우를 만난 길에서만 부른다). */
            static inline mutex                         s_mutex;
            static inline unordered_map<string, uint32> s_mapBackendToCount;

            /** @brief 그 백엔드의 센 횟수를 하나 올리고 올리기 전 값을 돌려줍니다. */
            static uint32 countReport( const utf8* pBackendName )
            {
                std::scoped_lock<mutex> lock{ s_mutex };
                uint32&                 count = s_mapBackendToCount[string( pBackendName )];
                return count++;
            }
        };
    } // namespace

    void RHIDrawDiagnostics::reportDestroyedVertexBuffer( const utf8* pBackendName, RHIBufferHandle handle )
    {
        const uint32 count = RHIDrawDiagnosticsInternal::countReport( pBackendName );
        if ( count < kMaxReportedDraw )
            SW_LOG_ERROR( "%# draw skipped: its vertex buffer (handle %#) was destroyed while bound - rebind before drawing", pBackendName, handle );
    }
} // namespace sw
