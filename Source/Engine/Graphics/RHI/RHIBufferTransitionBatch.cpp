#include "pch.h"

#include "Engine/Graphics/RHI/RHIBufferTransitionBatch.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    void RHIBufferTransitionBatch::add( RHIBufferHandle buffer, RHIBufferState state )
    {
        if ( buffer == 0 )
            return;
        // 한 단계의 목록은 짧다(컬링 슬롯 수 × 셋) — 선형 탐색이 맵보다 싸다.
        for ( RHIBufferTransition& transition : _listPending )
        {
            if ( transition._buffer == buffer )
            {
                transition._state = state;
                return;
            }
        }
        _listPending.push_back( RHIBufferTransition{ buffer, state } );
    }

    void RHIBufferTransitionBatch::flush( IRHICommandList& commands )
    {
        if ( _listPending.empty() )
            return;
        commands.transitionBuffers( _listPending.data(), static_cast<uint32>( _listPending.size() ) );
        _listPending.clear();
    }
} // namespace sw
