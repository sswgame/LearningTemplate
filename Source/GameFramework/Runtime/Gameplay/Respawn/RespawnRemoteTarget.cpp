#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Respawn/RespawnRemoteTarget.h"

namespace sw
{
    void RespawnRemoteTarget::requestRespawn( RespawnPolicyKind kind, uint32 pointHash )
    {
        // 흐름이 없는 대상(클라이언트 쪽 · 흐름이 사라진 뒤)은 고르기 단계가 아니다 — 없는 흐름을 만들어 판정하지 않는다.
        _lastResult = _pFlow != nullptr ? _pFlow->request( kind, pointHash, _context ) : RespawnRequestResult::NotInSelection;
        ++_resultCount;
        if ( _onResult.isBound() )
            _onResult( *this, _lastResult );
    }

    bool RespawnRemoteTarget::validateRequestRespawn( RespawnPolicyKind kind, uint32 pointHash ) const
    {
        (void)pointHash;
        return kind < RespawnPolicyKind::Count;
    }

    void RespawnRemoteTarget::receiveRespawnResult( RespawnRequestResult result )
    {
        _lastResult = result;
        ++_resultCount;
    }
} // namespace sw
