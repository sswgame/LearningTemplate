/**
 * @file RespawnRemoteTarget.h
 * @brief 부활 고르기의 원격 호출 대상입니다. 클라이언트는 정책 종류와 지점 id 해시만 보내고, 서버의 `RespawnFlow` 가 서버 상태 · 서버 세계로 판정합니다.
 * @details 반사 함수 호출(`GF_NetClientServer` 의 `RemoteCall` — 언리얼 `UFUNCTION( Server, Reliable, WithValidation )`)에 대상으로 등록한다(기억에 의존).
 *          - **신뢰 경계**: 자리 · 비용 · 잔액 · 횟수는 받지 않는다. 제자리 부활 자리는 서버가 아는 죽은 자리에서 서버의 세계 질의로, 비용은 서버 지갑으로 정한다.
 *          - **검증 짝**(`validateRequestRespawn`): 열거 밖 정책 값은 정상 클라이언트가 보낼 수 없는 조작이라 거짓 — 호출 층이 연결을 끊는다.
 *            게임 규칙의 거절(살아 있음 · 고르기 단계 아님 · 허용 안 된 정책 · 모르는 지점 · 돈 부족 · 횟수 끝 · 안전 지점 없음)은 결과 값(`RespawnRequestResult`)으로 돌려준다.
 *          - **호출률**: 함수 상한 `MaxCallsPerSecond` 를 넘은 호출은 몸을 부르지 않는다(호출 층의 토큰 버킷).
 *          수명: 서버의 대상은 흐름 · 세계를 빌린다(`_pFlow` · `_context`) — 게임이 걸음마다 세계 칸을 갱신하고, 흐름보다 먼저 등록을 푼다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnFlow.h"

namespace sw
{
    /**
     * @struct RespawnRemoteTarget
     * @brief 부활 고르기 하나의 원격 대상입니다. 서버는 흐름을 빌려 판정하고, 클라이언트는 받은 판정을 남깁니다.
     */
    REFLECT()
    struct SW_GF_API RespawnRemoteTarget
    {
        REFLECT_BODY();

        PROPERTY()
        RespawnRequestResult _lastResult{ RespawnRequestResult::Accepted }; ///< 서버 — 마지막 판정, 클라이언트 — 마지막으로 받은 판정
        PROPERTY()
        int32 _resultCount{ 0 };

        RespawnFlow*                                                       _pFlow{ nullptr }; ///< 서버 — 판정하는 흐름. 클라이언트는 비어 있다
        RespawnContext                                                     _context{};        ///< 서버 — 판정에 쓰는 세계(게임이 걸음마다 갱신한다)
        Delegate<void( const RespawnRemoteTarget&, RespawnRequestResult )> _onResult{};       ///< 서버 — 판정을 알릴 곳(게임의 서버 링크가 `receiveRespawnResult` 를 부른다)

        /** @brief 부활 자리를 고릅니다(서버에서 판정). 정책이면 규칙에 있는 그 정책으로, `PlayerChoice` 면 @p pointHash 의 지점으로 부활합니다. */
        FUNCTION( Server, Reliable, Validate, MaxCallsPerSecond = 4 )
        void requestRespawn( RespawnPolicyKind kind, uint32 pointHash );
        /** @brief 정책 값이 열거 안인지 봅니다(밖이면 조작이다). 지점 해시는 어떤 값이든 꼴이 맞다 — 모르는 지점은 규칙의 거절이다. */
        FUNCTION()
        bool validateRequestRespawn( RespawnPolicyKind kind, uint32 pointHash ) const;

        /** @brief 서버가 판정을 알립니다. */
        FUNCTION( Client, Reliable )
        void receiveRespawnResult( RespawnRequestResult result );
    };
} // namespace sw
