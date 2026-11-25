/**
 * @file NetPacketSealer.h
 * @brief UDP 연결의 AEAD 봉인과 열기, 핸드셰이크의 키 유도와 증명 태그, 키 확인 태그, 재전송 방지 창을 맡습니다(언리얼 AESGCMHandlerComponent 처럼 패킷 봉인 한 단위).
 * @details 데이터 패킷과 끊기 패킷의 본문은 `[종류][연결 값 4바이트, 리틀 엔디언][패킷 번호 8바이트, 빅 엔디언][AEAD + 태그]` 형식이다.
 *          AAD는 프로토콜 ID(리틀 엔디언 4바이트)와 이 본문 헤더를 이은 값이고, nonce 는 방향 IV XOR 패킷 번호다(`NetSessionKeys::makeNonce`).
 *          열 때는 재전송 방지 윈도우를 먼저 보고, **복호가 성공한 뒤에만** 번호를 표시한다. 먼저 표시하면 위조 패킷이 진짜 번호를 소모시킨다.
 *
 *          패킷 형식(체크섬 헤더, 패킷 종류)과 연결 상태는 `NetEndpoint` 가 맡고, 이 클래스는 바이트와 키만 다룬다.
 *          **스레드**: 잠금이 없다. 봉인(`seal`)은 `NetEndpoint` 의 잠금 안에서 그 값 멤버로 한다(아무 스레드나 끊기 패킷을 봉인한다).
 *          열기(`open`)는 받는 방향 상태(`NetReceiveCryptContext`)만 만지는 static 이라, 그 상태를 쥔 `update` 스레드가 잠금 밖에서 연다(감사 N-8).
 *
 *          재전송 방지 창(`NetReplayWindow`): 가장 높은 번호에서 1024 안쪽의 처음 보는 번호만 받습니다(IPsec · DTLS · WireGuard 의 슬라이딩 창).
 *          순서는 `isAcceptable`(복호 전에 싸게 거른다) → 복호 · 검증 → `markReceived`. 검증 전에 표시하면 위조 패킷이 진짜 번호를 미리 태워 진짜를 거절하게 만든다.
 *          세션 키(`NetSessionKeys`): X25519 공유 비밀을 HKDF-SHA256 으로 방향마다 키 32 + IV 12 로 펴고, nonce 는 IV XOR 패킷 번호입니다(TLS 1.3 · QUIC 과 같은 모양).
 *          소금 = 로그인 키트가 준 세션 비밀(없으면 0 32 바이트) — 세션 비밀을 모르는 중간자는 같은 키를 못 만든다. 정보 = "sw-net-v1" ‖ 클라이언트 소금 ‖ 서버 소금 ‖ 프로토콜 id
 *          (모두 LE) — 같은 공유 비밀이라도 다른 연결 · 다른 판에서는 다른 키다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/NetSecurityTypes.h"

namespace sw
{
    class BitWriter;
    class INetAEAD;
    class INetSecurityProvider;
} // namespace sw

namespace sw
{
    class SW_API NetReplayWindow
    {
    public:
        static constexpr uint64 kWindowSize = 1024;

        NetReplayWindow();

        /** @brief 처음 보는 번호이고 창보다 옛것이 아니면 true 입니다. */
        bool isAcceptable( uint64 packetNumber ) const;
        /** @brief 검증이 통과한 번호를 표시합니다. 더 높은 번호면 창이 앞으로 간다. */
        void markReceived( uint64 packetNumber );
        void reset();

        uint64 getHighest() const { return _highest; }
        bool   hasReceived() const { return _bHasReceived == SW_TRUE; }

    private:
        static constexpr uint64 kBitsPerWord = 64; ///< `_arrBit` 한 칸의 비트 수
        static constexpr int32  kWordCount   = static_cast<int32>( kWindowSize / kBitsPerWord );

        bool isMarked( uint64 packetNumber ) const;

    private:
        uint64 _arrBit[kWordCount];
        uint64 _highest;
        uint8  _bHasReceived;
    };
} // namespace sw

namespace sw
{
    /** @brief 한 방향(보내는 쪽 기준)의 키와 IV 입니다. */
    struct NetDirectionKeys
    {
        uint8 _arrKey[NetSecurityConstant::kAEADKeySize]{};
        uint8 _arrIv[NetSecurityConstant::kAEADNonceSize]{};
    };
} // namespace sw

namespace sw
{
    /** @brief 한 연결의 두 방향 키입니다. 연결이 끝나면 `wipe`. */
    struct SW_API NetSessionKeys
    {
        NetDirectionKeys _clientToServer{};
        NetDirectionKeys _serverToClient{};

        void wipe();

        /** @brief 키 확인 태그에 쓰는 패킷 번호입니다 — 데이터 패킷은 0 부터 오르므로 겹치지 않는다. */
        static constexpr uint64 kKeyConfirmPacketNumber = ~0ull;

        /** @brief 공유 비밀(kX25519KeySize 바이트) → 방향별 키 · IV 입니다. @p pSessionSecret 이 없으면 소금은 0 32 바이트. */
        [[nodiscard]] static bool computeSessionKeys( INetSecurityProvider& provider, const uint8* pSharedSecret, const NetSessionSecret* pSessionSecret, uint64 clientSalt,
                                                      uint64 serverSalt, uint32 protocolID, NetSessionKeys& outKeys );
        /** @brief 세션 비밀을 가졌음을 증명하는 키(연결 응답의 증명 태그)입니다 — HKDF( 비밀, 소금 없음, "sw-net-proof-v1" ) → kAEADKeySize 바이트. */
        [[nodiscard]] static bool computeProofKey( INetSecurityProvider& provider, const NetSessionSecret& secret, uint8* pOutKey );
        /** @brief nonce = IV XOR (패킷 번호를 빅엔디언 8 바이트로 IV 뒤 8 바이트에)입니다. */
        static void makeNonce( const uint8* pIv, uint64 packetNumber, uint8* pOutNonce );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 연결 하나의 받는 방향 암호 상태입니다 — 받는 키로 초기화한 AEAD, 받는 방향 IV, 재전송 방지 윈도우.
     * @details 보내는 방향과 나눈 까닭: 복호를 엔드포인트 잠금 밖에서 하려면 받는 쪽 상태를 `update` 스레드 하나가 쥐어야 한다(`NetEndpoint` 가 키 유도 뒤 옮겨 간다).
     *          보내는 쪽은 아무 스레드의 끊기 · 보내기가 잠금 안에서 쓴다. GameNetworkingSockets 가 연결마다 `m_cryptContextSend` · `m_cryptContextRecv` 를 따로 두는 것과 같다.
     *          소멸시키는 번역 단위는 `INetSecurityProvider.h`(`INetAEAD` 정의)를 include 해야 한다.
     */
    struct NetReceiveCryptContext
    {
        unique_ptr<INetAEAD> _aead{};                                       ///< 받는 방향 키로 초기화한 AEAD. 없으면 키가 없다.
        NetReplayWindow      _replayWindow{};                               ///< 받은 패킷 번호의 재전송 방지 윈도우.
        uint8                _arrIv[NetSecurityConstant::kAEADNonceSize]{}; ///< 받는 방향 IV(nonce = IV XOR 패킷 번호).

        /** @brief IV 를 지우고 AEAD 를 놓고 윈도우를 처음으로 돌립니다(연결이 끝났을 때). */
        SW_API void wipe();
    };

    /**
     * @brief 연결 하나의 암호 상태입니다. 암호화 방식일 때만 만듭니다.
     * @details 소멸시키는 번역 단위는 `INetSecurityProvider.h`(`INetAEAD` 정의)를 include 해야 한다.
     */
    struct NetSlotSecurity
    {
        NetX25519KeyPair       _keyPair{};  ///< 이쪽의 일회용 키. 클라이언트는 키를 만든 뒤 지우고, 서버는 수락을 다시 보낼 때 쓰도록 공개 키만 남긴다.
        NetSessionKeys         _keys{};     ///< 방향별 키와 IV.
        unique_ptr<INetAEAD>   _sendAEAD{}; ///< 보내는 방향 키로 초기화한 AEAD.
        NetReceiveCryptContext _receive{};  ///< 받는 방향 상태. 키를 만든 뒤 `NetEndpoint` 가 `update` 스레드 소유로 옮겨 가므로 그 뒤에는 비어 있다.
        uint64                 _sendPacketNumber{ 0 };
        uint64                 _principalID{ 0 };                                      ///< 서버 전용. 인증기가 지정한 주체.
        uint8                  _arrKeyConfirmTag[NetSecurityConstant::kAEADTagSize]{}; ///< 서버 전용. 수락을 다시 보낼 때 같은 태그를 쓰기 위해 보관한다.
        uint8                  _bKeysReady{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetPacketSealer
     * @brief 암호화 방식의 패킷 본문을 봉인하고 엽니다. 봉인은 스크래치 버퍼를 다시 쓰는 멤버이고, 열기는 부른 쪽 버퍼에 평문을 덧붙이는 static 입니다.
     */
    class SW_API NetPacketSealer
    {
    public:
        static constexpr int32 kSecureHeadSize    = 1 + 4 + 8;                                                                     ///< 종류 1바이트, 연결 값 4바이트, 패킷 번호 8바이트.
        static constexpr int32 kAadSize           = 4 + kSecureHeadSize;                                                           ///< 프로토콜 ID 4바이트와 본문 헤더.
        static constexpr int32 kKeyConfirmAadSize = 4 + 8;                                                                         ///< 서버가 지정한 슬롯 번호와 서버 솔트.
        static constexpr int32 kMaxProofAadSize   = 16 + NetSecurityConstant::kX25519KeySize + NetSecurityConstant::kMaxTokenSize; ///< 두 솔트, 공개 키, 토큰.

        /** @brief `open` 의 결과입니다. 실패 종류마다 `NetEndpoint` 가 다른 카운터를 올립니다. */
        enum class OpenResult : uint8
        {
            Opened = 0,          ///< 열었다. 평문은 `getOpened`.
            Malformed,           ///< 본문이 헤더와 태그보다 짧다.
            Replayed,            ///< 재전송 방지 윈도우가 거절했거나 예약 번호(`kKeyConfirmPacketNumber`)다.
            AuthenticationFailed ///< 태그가 맞지 않는다(변조나 다른 키).
        };

        NetPacketSealer();
        ~NetPacketSealer();

        NetPacketSealer( const NetPacketSealer& )            = delete;
        NetPacketSealer& operator=( const NetPacketSealer& ) = delete;

        /**
         * @brief @p pPlain 을 봉인해 본문 헤더와 암호문을 @p outWriter 에 씁니다(비운 뒤 쓴다). 보내는 패킷 번호는 실패해도 하나 오릅니다(예약 번호에 닿으면 오르지 않고 늘 거절 — 감겨 nonce 를 다시 쓰지 않는다).
         * @param bServer 서버면 서버에서 클라이언트로 가는 키를 쓴다.
         * @return 봉인에 실패하면 false 이고 @p outWriter 는 건드리지 않는다.
         */
        [[nodiscard]] bool seal( NetSlotSecurity& security, uint8 typeByte, uint32 connectionToken, uint32 protocolID, bool bServer, const uint8* pPlain, int32 plainSize,
                                 BitWriter& outWriter );
        /**
         * @brief 본문(체크섬 헤더 뒤)을 엽니다. 재전송 방지 윈도우와 태그를 검사하고, 연 뒤에만 번호를 표시합니다.
         * @details 연 평문을 @p inoutPlainBytes 뒤에 덧붙인다(열지 못하면 길이를 그대로 둔다). @p receive 만 만지므로 그것을 쥔 스레드가 잠금 없이 부른다.
         */
        static OpenResult open( NetReceiveCryptContext& receive, uint32 protocolID, const uint8* pBody, int32 bodySize, vector<uint8>& inoutPlainBytes );

        /**
         * @brief 서버 쪽 키를 만듭니다. 일회용 키 쌍을 만들고 공유 비밀, 세션 키(@p pSecret 이 있으면 그것이 솔트), 방향별 AEAD 를 만든 뒤 개인 키를 지웁니다.
         * @return 하나라도 실패하면 false 입니다(작은 차수 공개 키 같은 경우).
         */
        [[nodiscard]] static bool deriveServerKeys( INetSecurityProvider& provider, NetAEADAlgorithm algorithm, const uint8* pClientPublicKey, const NetSessionSecret* pSecret,
                                                    uint64 clientSalt, uint64 serverSalt, uint32 protocolID, NetSlotSecurity& outSecurity );
        /** @brief 클라이언트 쪽 키를 만듭니다. 이미 만든 일회용 키와 서버 공개 키로 공유 비밀, 세션 키, 방향별 AEAD 를 만듭니다. 개인 키는 지우지 않습니다. */
        [[nodiscard]] static bool deriveClientKeys( INetSecurityProvider& provider, NetAEADAlgorithm algorithm, const uint8* pServerPublicKey, const NetSessionSecret* pSecret,
                                                    uint64 clientSalt, uint64 serverSalt, uint32 protocolID, NetSlotSecurity& inoutSecurity );

        /** @brief 서버가 키 확인 태그(서버에서 클라이언트로 가는 키로 빈 데이터를 봉인한 값)를 `_arrKeyConfirmTag` 에 만듭니다. */
        [[nodiscard]] static bool sealKeyConfirmTag( NetSlotSecurity& security, uint32 serverIndex, uint64 serverSalt );
        /** @brief 클라이언트가 수락에 든 키 확인 태그를 검사합니다. 틀리면 서버가 다른 키(중간자이거나 틀린 비밀)를 가진 것입니다. */
        static bool isKeyConfirmTagValid( NetSlotSecurity& security, uint32 serverIndex, uint64 serverSalt, const uint8* pTag );

        /**
         * @brief 토큰 결속의 증명 태그를 @p pOutTag 에 만듭니다. 세션 비밀로 유도한 키로 빈 데이터를 봉인한 태그이고,
         *        AAD가 두 솔트, 클라이언트 공개 키, 토큰이라 다른 응답에 옮겨 붙일 수 없습니다. 실패하면 @p pOutTag 를 건드리지 않습니다.
         */
        [[nodiscard]] static bool computeProofTag( INetSecurityProvider& provider, NetAEADAlgorithm algorithm, const NetSessionSecret& secret, uint64 clientSalt, uint64 serverSalt,
                                                   const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, uint8* pOutTag );
        /** @brief 증명 태그를 검사합니다. */
        static bool isProofTagValid( INetSecurityProvider& provider, NetAEADAlgorithm algorithm, const NetSessionSecret& secret, uint64 clientSalt, uint64 serverSalt,
                                     const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, const uint8* pTag );

    private:
        vector<uint8> _listSealScratch; ///< 봉인한 암호문과 태그.
    };
} // namespace sw
