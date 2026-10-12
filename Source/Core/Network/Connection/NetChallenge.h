/**
 * @file NetChallenge.h
 * @brief 연결 핸드셰이크의 상태 없는 챌린지 값과 솔트를 만듭니다.
 * @details 서버는 연결 요청에 슬롯을 잡지 않고 챌린지 값만 돌려준다. 값은 비밀 키, 주소, 클라이언트 솔트, 시간 윈도우를 섞어 만들고,
 *          그 값을 되돌려 주는 응답이 값을 만든 윈도우나 그다음 윈도우 안(`kWindowSeconds` 의 1~2배)에 와야 유효하다.
 *          값은 키를 섞는 splitmix 이므로 암호학적 MAC은 아니다. 위조한 주소로 슬롯을 채우는 공격을 막는 데까지만 쓴다.
 *
 *          **스레드**: 잠금이 없다. `NetHost` 의 값 멤버로 그 잠금 안에서만 쓴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

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

        /** @brief 솔트 수열을 시드하고 비밀 키를 그 첫 값으로 정합니다. @p saltSeed 가 0이면 운영체제 난수를 씁니다(위조 연결을 막는다). */
        void initialize( uint64 saltSeed );

        /** @brief 솔트 수열의 다음 값입니다. */
        uint64 nextSalt();
        /** @brief @p nowSeconds 가 속한 윈도우의 챌린지 값입니다. 0 이 되지 않습니다. */
        uint64 makeToken( const NetAddress& address, uint64 clientSalt, float64 nowSeconds ) const;
        /** @brief @p token 이 이번 윈도우나 바로 앞 윈도우에서 만든 값인지 봅니다. */
        bool isTokenValid( const NetAddress& address, uint64 clientSalt, uint64 token, float64 nowSeconds ) const;

        static int64 computeWindow( float64 nowSeconds );

    private:
        uint64 makeTokenForWindow( const NetAddress& address, uint64 clientSalt, int64 window ) const;

        uint64 _saltState;
        uint64 _secret; ///< 서버 전용. 챌린지 값의 비밀 키(솔트 시드에서 만든다).
    };
} // namespace sw
