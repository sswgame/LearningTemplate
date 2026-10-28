/**
 * @file NetChallenge.h
 * @brief 연결 핸드셰이크의 상태 없는 챌린지 값과 솔트를 만듭니다.
 * @details 서버는 연결 요청에 슬롯을 잡지 않고 챌린지 값만 돌려준다. 값은 비밀 키, 주소, 클라이언트 솔트, 시간 윈도우를 섞어 만들고,
 *          그 값을 되돌려 주는 응답이 값을 만든 윈도우나 그다음 윈도우 안(`kWindowSeconds` 의 1~2배)에 와야 유효하다.
 *          값은 128 비트 키의 SipHash-2-4(키 있는 PRF, `SecureUtil::sipHash24`)이고, 솔트도 다른 키의 SipHash 이고, 두 키 모두 운영체제 난수에서 뽑는다(솔트는 선에 실린다).
 *          위조한 주소로 슬롯을 채우는 공격을 막는다. 64 비트 값이라 긴 메시지 인증에는 쓰지 않는다.
 *
 *          **스레드**: 잠금이 없다. `NetEndpoint` 의 값 멤버로 그 잠금 안에서만 쓴다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/SecureUtil.h"
#include "Core/Math/DeterministicRandom.h"

namespace sw
{
    struct NetAddress;

    /**
     * @class NetChallenge
     * @brief 챌린지 값의 비밀 키와 솔트 수열을 쥡니다. 서버는 챌린지 값을, 클라이언트는 자기 솔트를 여기서 받습니다.
     */
    class SW_API NetChallenge
    {
    public:
        static constexpr float64 kWindowSeconds = 5.0; ///< 챌린지 값의 시간 윈도우. 값을 만든 윈도우와 그다음 윈도우 동안 유효하다.

        NetChallenge();

        /** @brief 솔트 수열을 시드하고 챌린지 키를 정합니다. @p saltSeed 가 0이면 둘 다 운영체제 난수입니다(위조 연결을 막는다). 0 이 아니면 키도 시드에서 만든다(테스트 · 결정적 시뮬레이션). */
        void initialize( uint64 saltSeed );

        /** @brief 다음 솔트입니다 — 솔트 키의 SipHash(순번). 0 이 되지 않고, 앞 솔트들에서 맞힐 수 없다. */
        uint64 nextSalt();
        /** @brief @p nowSeconds 가 속한 윈도우의 챌린지 값입니다. 0 이 되지 않습니다. */
        uint64 makeToken( const NetAddress& address, uint64 clientSalt, float64 nowSeconds ) const;
        /** @brief @p token 이 이번 윈도우나 바로 앞 윈도우에서 만든 값인지 봅니다. */
        bool isTokenValid( const NetAddress& address, uint64 clientSalt, uint64 token, float64 nowSeconds ) const;

        static int64 computeWindow( float64 nowSeconds );

    private:
        uint64 makeTokenForWindow( const NetAddress& address, uint64 clientSalt, int64 window ) const;

        SipHashKey _saltKey;   ///< 솔트 PRF 의 비밀 키. 솔트는 (키, 순번) 의 SipHash 라 받은 솔트에서 다음 솔트를 맞힐 수 없다
        SipHashKey _key;       ///< 서버 전용. 챌린지 값의 비밀 키(솔트 키와 따로 뽑는다).
        uint64     _saltCount; ///< 지금까지 낸 솔트 수(PRF 입력)
    };
} // namespace sw
