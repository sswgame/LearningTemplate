/**
 * @file NetPacketSealer.h
 * @brief UDP 연결의 AEAD 봉인과 열기, 핸드셰이크의 키 유도와 증명 태그, 키 확인 태그를 맡습니다.
 * @details 데이터 패킷과 끊기 패킷의 본문은 `[종류][연결 값 4바이트, 리틀 엔디언][패킷 번호 8바이트, 빅 엔디언][AEAD + 태그]` 형식이다.
 *          AAD는 프로토콜 ID(리틀 엔디언 4바이트)와 이 본문 헤더를 이은 값이고, nonce 는 방향 IV XOR 패킷 번호다(`NetSessionKeyUtil::makeNonce`).
 *          열 때는 재전송 방지 윈도우를 먼저 보고, **복호가 성공한 뒤에만** 번호를 표시한다. 먼저 표시하면 위조 패킷이 진짜 번호를 소모시킨다.
 *
 *          패킷 형식(체크섬 헤더, 패킷 종류)과 연결 상태는 `NetHost` 가 맡고, 이 클래스는 바이트와 키만 다룬다.
 *          **스레드**: 잠금이 없다. `NetHost` 의 값 멤버로 그 잠금 안에서만 쓴다. 스크래치 버퍼를 다시 쓰므로 패킷마다 할당하지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/NetReplayWindow.h"
#include "Core/Network/Security/NetSecurityTypes.h"
#include "Core/Network/Security/NetSessionKeyUtil.h"

namespace sw
{
    class BitWriter;
    class INetAead;
    class INetSecurityProvider;

    /**
     * @brief 연결 하나의 암호 상태입니다. 암호화 방식일 때만 만듭니다.
     * @details 소멸시키는 번역 단위는 `INetSecurityProvider.h`(`INetAead` 정의)를 include 해야 한다.
     */
    struct NetSlotSecurity
    {
        NetX25519KeyPair     _keyPair{};      ///< 이쪽의 일회용 키. 클라이언트는 키를 만든 뒤 지우고, 서버는 수락을 다시 보낼 때 쓰도록 공개 키만 남긴다.
        NetSessionKeys       _keys{};         ///< 방향별 키와 IV.
        unique_ptr<INetAead> _sendAead{};     ///< 보내는 방향 키로 초기화한 AEAD.
        unique_ptr<INetAead> _receiveAead{};  ///< 받는 방향 키로 초기화한 AEAD.
        NetReplayWindow      _replayWindow{}; ///< 받은 패킷 번호의 재전송 방지 윈도우.
        uint64               _sendPacketNumber{ 0 };
        uint64               _principalID{ 0 };                                      ///< 서버 전용. 인증기가 지정한 주체.
        uint8                _arrKeyConfirmTag[NetSecurityConstant::kAeadTagSize]{}; ///< 서버 전용. 수락을 다시 보낼 때 같은 태그를 쓰기 위해 보관한다.
        uint8                _bKeysReady{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetPacketSealer
     * @brief 암호화 방식의 패킷 본문을 봉인하고 엽니다. 연 평문은 다음 `open` 까지 `getOpened` 에 남습니다.
     */
    class NetPacketSealer
    {
    public:
        static constexpr int32 kSecureHeadSize    = 1 + 4 + 8;                                                                     ///< 종류 1바이트, 연결 값 4바이트, 패킷 번호 8바이트.
        static constexpr int32 kAadSize           = 4 + kSecureHeadSize;                                                           ///< 프로토콜 ID 4바이트와 본문 헤더.
        static constexpr int32 kKeyConfirmAadSize = 4 + 8;                                                                         ///< 서버가 지정한 슬롯 번호와 서버 솔트.
        static constexpr int32 kMaxProofAadSize   = 16 + NetSecurityConstant::kX25519KeySize + NetSecurityConstant::kMaxTokenSize; ///< 두 솔트, 공개 키, 토큰.

        /** @brief `open` 의 결과입니다. 실패 종류마다 `NetHost` 가 다른 카운터를 올립니다. */
        enum class OpenResult : uint8
        {
            Opened = 0,          ///< 열었다. 평문은 `getOpened`.
            Malformed,           ///< 본문이 헤더와 태그보다 짧다.
            Replayed,            ///< 재전송 방지 윈도우가 거절했다.
            AuthenticationFailed ///< 태그가 맞지 않는다(변조나 다른 키).
        };

        NetPacketSealer();
        ~NetPacketSealer();

        NetPacketSealer( const NetPacketSealer& )            = delete;
        NetPacketSealer& operator=( const NetPacketSealer& ) = delete;

        /**
         * @brief @p pPlain 을 봉인해 본문 헤더와 암호문을 @p outWriter 에 씁니다(비운 뒤 쓴다). 보내는 패킷 번호는 실패해도 하나 오릅니다.
         * @param bServer 서버면 서버에서 클라이언트로 가는 키를 쓴다.
         * @return 봉인에 실패하면 false 이고 @p outWriter 는 건드리지 않는다.
         */
        [[nodiscard]] bool seal( NetSlotSecurity& security, uint8 typeByte, uint32 connectionToken, uint32 protocolID, bool bServer, const uint8* pPlain, int32 plainSize,
                                 BitWriter& outWriter );
        /** @brief 본문(체크섬 헤더 뒤)을 엽니다. 재전송 방지 윈도우와 태그를 검사하고, 연 뒤에만 번호를 표시합니다. */
        OpenResult open( NetSlotSecurity& security, uint32 protocolID, bool bServer, const uint8* pBody, int32 bodySize );
        /** @brief 마지막으로 연 평문입니다. */
        const vector<uint8>& getOpened() const { return _listOpenScratch; }

        /**
         * @brief 서버 쪽 키를 만듭니다. 일회용 키 쌍을 만들고 공유 비밀, 세션 키(@p pSecret 이 있으면 그것이 솔트), 방향별 AEAD 를 만든 뒤 개인 키를 지웁니다.
         * @return 하나라도 실패하면 false 입니다(작은 차수 공개 키 같은 경우).
         */
        [[nodiscard]] static bool deriveServerKeys( INetSecurityProvider& provider, NetAeadAlgorithm algorithm, const uint8* pClientPublicKey, const NetSessionSecret* pSecret,
                                                    uint64 clientSalt, uint64 serverSalt, uint32 protocolID, NetSlotSecurity& outSecurity );
        /** @brief 클라이언트 쪽 키를 만듭니다. 이미 만든 일회용 키와 서버 공개 키로 공유 비밀, 세션 키, 방향별 AEAD 를 만듭니다. 개인 키는 지우지 않습니다. */
        [[nodiscard]] static bool deriveClientKeys( INetSecurityProvider& provider, NetAeadAlgorithm algorithm, const uint8* pServerPublicKey, const NetSessionSecret* pSecret,
                                                    uint64 clientSalt, uint64 serverSalt, uint32 protocolID, NetSlotSecurity& inoutSecurity );

        /** @brief 서버가 키 확인 태그(서버에서 클라이언트로 가는 키로 빈 데이터를 봉인한 값)를 `_arrKeyConfirmTag` 에 만듭니다. */
        [[nodiscard]] static bool sealKeyConfirmTag( NetSlotSecurity& security, uint32 serverIndex, uint64 serverSalt );
        /** @brief 클라이언트가 수락에 든 키 확인 태그를 검사합니다. 틀리면 서버가 다른 키(중간자이거나 틀린 비밀)를 가진 것입니다. */
        static bool isKeyConfirmTagValid( NetSlotSecurity& security, uint32 serverIndex, uint64 serverSalt, const uint8* pTag );

        /**
         * @brief 토큰 결속의 증명 태그를 @p pOutTag 에 만듭니다. 세션 비밀로 유도한 키로 빈 데이터를 봉인한 태그이고,
         *        AAD가 두 솔트, 클라이언트 공개 키, 토큰이라 다른 응답에 옮겨 붙일 수 없습니다. 실패하면 @p pOutTag 를 건드리지 않습니다.
         */
        [[nodiscard]] static bool computeProofTag( INetSecurityProvider& provider, NetAeadAlgorithm algorithm, const NetSessionSecret& secret, uint64 clientSalt, uint64 serverSalt,
                                                   const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, uint8* pOutTag );
        /** @brief 증명 태그를 검사합니다. */
        static bool isProofTagValid( INetSecurityProvider& provider, NetAeadAlgorithm algorithm, const NetSessionSecret& secret, uint64 clientSalt, uint64 serverSalt,
                                     const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, const uint8* pTag );

    private:
        vector<uint8> _listSealScratch; ///< 봉인한 암호문과 태그.
        vector<uint8> _listOpenScratch; ///< 연 평문.
    };
} // namespace sw
