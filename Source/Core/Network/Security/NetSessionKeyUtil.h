/**
 * @file NetSessionKeyUtil.h
 * @brief UDP 세션 키 유도 — X25519 공유 비밀을 HKDF-SHA256 으로 방향마다 키 32 + IV 12 로 펴고, nonce 는 IV XOR 패킷 번호입니다(TLS 1.3 · QUIC 과 같은 모양).
 * @details 소금 = 로그인 키트가 준 세션 비밀(없으면 0 32 바이트) — 세션 비밀을 모르는 중간자는 같은 키를 못 만든다. 정보 = "sw-net-v1" ‖ 클라이언트 소금 ‖ 서버 소금 ‖ 프로토콜 id
 *          (모두 LE) — 같은 공유 비밀이라도 다른 연결 · 다른 판에서는 다른 키다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Network/Security/NetSecurityTypes.h"

namespace sw
{
    class INetSecurityProvider;
} // namespace sw

namespace sw
{
    /** @brief 한 방향(보내는 쪽 기준)의 키와 IV 입니다. */
    struct NetDirectionKeys
    {
        uint8 _arrKey[NetSecurityConstant::kAeadKeySize]{};
        uint8 _arrIv[NetSecurityConstant::kAeadNonceSize]{};
    };
} // namespace sw

namespace sw
{
    /** @brief 한 연결의 두 방향 키입니다. 연결이 끝나면 `wipe`. */
    struct NetSessionKeys
    {
        NetDirectionKeys _clientToServer{};
        NetDirectionKeys _serverToClient{};

        SW_API void wipe();
    };
} // namespace sw

namespace sw
{
    struct SW_API NetSessionKeyUtil
    {
        /** @brief 키 확인 태그에 쓰는 패킷 번호입니다 — 데이터 패킷은 0 부터 오르므로 겹치지 않는다. */
        static constexpr uint64 kKeyConfirmPacketNumber = ~0ull;

        /** @brief 공유 비밀(kX25519KeySize 바이트) → 방향별 키 · IV 입니다. @p pSessionSecret 이 없으면 소금은 0 32 바이트. */
        [[nodiscard]] static bool computeSessionKeys( INetSecurityProvider& provider, const uint8* pSharedSecret, const NetSessionSecret* pSessionSecret, uint64 clientSalt,
                                                      uint64 serverSalt, uint32 protocolId, NetSessionKeys& outKeys );
        /** @brief 세션 비밀을 가졌음을 증명하는 키(연결 응답의 증명 태그)입니다 — HKDF( 비밀, 소금 없음, "sw-net-proof-v1" ) → kAeadKeySize 바이트. */
        [[nodiscard]] static bool computeProofKey( INetSecurityProvider& provider, const NetSessionSecret& secret, uint8* pOutKey );
        /** @brief nonce = IV XOR (패킷 번호를 빅엔디언 8 바이트로 IV 뒤 8 바이트에)입니다. */
        static void makeNonce( const uint8* pIv, uint64 packetNumber, uint8* pOutNonce );
    };
} // namespace sw
