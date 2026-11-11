#include "pch.h"

#include "Core/Network/Connection/NetPacketSealer.h"

#include "Core/Foundation/Expected.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Serialization/LittleEndianUtil.h"

#include <cstring>

namespace sw
{
    namespace
    {
        struct NetPacketSealerInternal
        {
            /** @brief 키 확인 태그의 AAD입니다. 서버가 지정한 슬롯 번호(4바이트, 리틀 엔디언)와 서버 솔트(8바이트, 리틀 엔디언)를 이었습니다. */
            static void makeKeyConfirmAad( uint32 serverIndex, uint64 serverSalt, uint8* pOutAad )
            {
                LittleEndianUtil::writeUint32( pOutAad, serverIndex );
                LittleEndianUtil::writeUint64( pOutAad + 4, serverSalt );
            }

            /** @brief 데이터 패킷과 끊기 패킷의 AAD입니다. 프로토콜 ID(4바이트, 리틀 엔디언)와 본문 헤더(종류, 연결 값, 패킷 번호)를 이었습니다. */
            static void makePacketAad( uint32 protocolID, const uint8* pBodyHead, uint8* pOutAad )
            {
                LittleEndianUtil::writeUint32( pOutAad, protocolID );
                std::memcpy( pOutAad + 4, pBodyHead, NetPacketSealer::kSecureHeadSize );
            }

            /**
             * @brief 증명 태그의 AAD를 @p pOutAad(`kMaxProofAadSize` 바이트)에 만들고 길이를 반환합니다. 토큰 길이가 범위 밖이면 비어 있습니다.
             * @details 클라이언트 솔트, 서버 솔트(챌린지 값), 클라이언트 공개 키, 토큰을 이어 붙인다. 스택 버퍼이므로 핸드셰이크마다 할당하지 않는다.
             */
            [[nodiscard]] static optional<int32> makeProofAad( uint64 clientSalt, uint64 serverSalt, const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, uint8* pOutAad )
            {
                if ( tokenSize < 0 || NetSecurityConstant::kMaxTokenSize < tokenSize )
                    return nullopt;
                LittleEndianUtil::writeUint64( pOutAad, clientSalt );
                LittleEndianUtil::writeUint64( pOutAad + 8, serverSalt );
                std::memcpy( pOutAad + 16, pPublicKey, NetSecurityConstant::kX25519KeySize );
                if ( tokenSize > 0 )
                    std::memcpy( pOutAad + 16 + NetSecurityConstant::kX25519KeySize, pToken, static_cast<size_t>( tokenSize ) );
                return 16 + NetSecurityConstant::kX25519KeySize + tokenSize;
            }

            /**
             * @brief 증명 태그 = HKDF-SHA256( 증명 키, info = 라벨 ‖ 전사 ) 의 앞 16 바이트입니다. 유도한 키는 지웁니다.
             * @details 예전에는 같은 키 · 고정 nonce(0) 의 AEAD 로 봉인했다 — 같은 비밀로 태그가 두 번 나가면(시간 초과 재시도 · 창 경계의 챌린지 둘)
             *          GCM · ChaCha20-Poly1305 의 인증 키가 복원되어 위조할 수 있었다(감사 N-1). PRF 출력은 nonce 가 없어 재사용이 문제가 아니다
             *          (netcode.io connect token · Steam 티켓이 HMAC · 서명으로 묶는 자리).
             */
            [[nodiscard]] static bool computeProof( INetSecurityProvider& provider, const NetSessionSecret& secret, const uint8* pAad, int32 aadSize, uint8* pOutTag )
            {
                static constexpr char kLabel[] = "sw.net.connect-proof.v2";
                uint8                 arrProofKey[NetSecurityConstant::kAEADKeySize]{};
                uint8                 arrInfo[sizeof( kLabel ) + NetPacketSealer::kMaxProofAadSize]{};
                bool                  bComputed = NetSessionKeys::computeProofKey( provider, secret, arrProofKey );
                if ( bComputed )
                {
                    std::memcpy( arrInfo, kLabel, sizeof( kLabel ) );
                    std::memcpy( arrInfo + sizeof( kLabel ), pAad, static_cast<size_t>( aadSize ) );
                    bComputed = provider.computeHKDFSHA256( arrProofKey, static_cast<int32>( sizeof( arrProofKey ) ), nullptr, 0, arrInfo,
                                                            static_cast<int32>( sizeof( kLabel ) ) + aadSize, pOutTag, NetSecurityConstant::kAEADTagSize );
                }
                std::memset( arrProofKey, 0, sizeof( arrProofKey ) );
                return bComputed;
            }

            /** @brief 길이가 같은 두 바이트 열을 내용과 무관한 시간에 견줍니다(태그 비교가 첫 다른 바이트에서 멈추면 시간으로 한 바이트씩 맞춰 간다). */
            [[nodiscard]] static bool isEqualConstantTime( const uint8* pLeft, const uint8* pRight, int32 size )
            {
                uint8 difference = 0;
                for ( int32 index = 0; index < size; ++index )
                    difference = static_cast<uint8>( difference | ( pLeft[index] ^ pRight[index] ) );
                return difference == 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NetPacketSealer::NetPacketSealer()
        : _listSealScratch{}
        , _listOpenScratch{}
    {
    }

    NetPacketSealer::~NetPacketSealer() = default;

    bool NetPacketSealer::seal( NetSlotSecurity& security, uint8 typeByte, uint32 connectionToken, uint32 protocolID, bool bServer, const uint8* pPlain, int32 plainSize,
                                BitWriter& outWriter )
    {
        const uint64 packetNumber = security._sendPacketNumber++;
        uint8        arrHead[kSecureHeadSize]{};
        arrHead[0] = typeByte; // 종류 3비트와 0 5비트다. BitWriter 는 낮은 비트부터 쓰므로 받는 쪽의 readBits( 3 )과 peekPacketType 이 그대로 읽는다.
        LittleEndianUtil::writeUint32( arrHead + 1, connectionToken );
        for ( int32 index = 0; index < 8; ++index )
        {
            arrHead[5 + index] = static_cast<uint8>( packetNumber >> ( 56 - index * 8 ) );
        }
        uint8 arrAad[kAadSize]{};
        NetPacketSealerInternal::makePacketAad( protocolID, arrHead, arrAad );
        uint8                   arrNonce[NetSecurityConstant::kAEADNonceSize]{};
        const NetDirectionKeys& keys = bServer ? security._keys._serverToClient : security._keys._clientToServer;
        NetSessionKeys::makeNonce( keys._arrIv, packetNumber, arrNonce );
        _listSealScratch.resize( static_cast<size_t>( plainSize + NetSecurityConstant::kAEADTagSize ) );
        if ( security._sendAEAD->seal( arrNonce, arrAad, kAadSize, pPlain, plainSize, _listSealScratch.data() ) == false )
            return false;
        outWriter.clear();
        outWriter.writeBytes( arrHead, kSecureHeadSize );
        outWriter.writeBytes( _listSealScratch.data(), static_cast<int32>( _listSealScratch.size() ) );
        return true;
    }

    NetPacketSealer::OpenResult NetPacketSealer::open( NetSlotSecurity& security, uint32 protocolID, bool bServer, const uint8* pBody, int32 bodySize )
    {
        if ( bodySize < kSecureHeadSize + NetSecurityConstant::kAEADTagSize )
            return OpenResult::Malformed;
        uint64 packetNumber = 0;
        for ( int32 index = 0; index < 8; ++index )
        {
            packetNumber = ( packetNumber << 8 ) | pBody[5 + index];
        }
        if ( security._replayWindow.isAcceptable( packetNumber ) == false )
            return OpenResult::Replayed;
        uint8 arrAad[kAadSize]{};
        NetPacketSealerInternal::makePacketAad( protocolID, pBody, arrAad );
        uint8                   arrNonce[NetSecurityConstant::kAEADNonceSize]{};
        const NetDirectionKeys& keys = bServer ? security._keys._clientToServer : security._keys._serverToClient;
        NetSessionKeys::makeNonce( keys._arrIv, packetNumber, arrNonce );
        const int32 cipherSize = bodySize - kSecureHeadSize;
        _listOpenScratch.resize( static_cast<size_t>( cipherSize - NetSecurityConstant::kAEADTagSize ) );
        if ( security._receiveAEAD->open( arrNonce, arrAad, kAadSize, pBody + kSecureHeadSize, cipherSize, _listOpenScratch.data() ) == false )
            return OpenResult::AuthenticationFailed;
        security._replayWindow.markReceived( packetNumber ); // 복호가 성공한 뒤에만 표시한다. 먼저 표시하면 위조 패킷이 진짜 번호를 소모시킨다.
        return OpenResult::Opened;
    }

    bool NetPacketSealer::deriveServerKeys( INetSecurityProvider& provider, NetAEADAlgorithm algorithm, const uint8* pClientPublicKey, const NetSessionSecret* pSecret,
                                            uint64 clientSalt, uint64 serverSalt, uint32 protocolID, NetSlotSecurity& outSecurity )
    {
        uint8      arrShared[NetSecurityConstant::kX25519KeySize]{};
        const bool bKeys = provider.makeX25519KeyPair( outSecurity._keyPair ) &&
                           provider.computeX25519SharedSecret( outSecurity._keyPair._arrPrivateKey, pClientPublicKey, arrShared ) &&
                           NetSessionKeys::computeSessionKeys( provider, arrShared, pSecret, clientSalt, serverSalt, protocolID, outSecurity._keys );
        std::memset( arrShared, 0, sizeof( arrShared ) );
        outSecurity._keyPair.wipe(); // 서버는 수락을 다시 보낼 때 쓰도록 공개 키만 남긴다.
        if ( bKeys == false )
            return false;
        outSecurity._sendAEAD    = provider.createAEAD( algorithm, outSecurity._keys._serverToClient._arrKey );
        outSecurity._receiveAEAD = provider.createAEAD( algorithm, outSecurity._keys._clientToServer._arrKey );
        return outSecurity._sendAEAD != nullptr && outSecurity._receiveAEAD != nullptr;
    }

    bool NetPacketSealer::deriveClientKeys( INetSecurityProvider& provider, NetAEADAlgorithm algorithm, const uint8* pServerPublicKey, const NetSessionSecret* pSecret,
                                            uint64 clientSalt, uint64 serverSalt, uint32 protocolID, NetSlotSecurity& inoutSecurity )
    {
        uint8      arrShared[NetSecurityConstant::kX25519KeySize]{};
        const bool bKeys = provider.computeX25519SharedSecret( inoutSecurity._keyPair._arrPrivateKey, pServerPublicKey, arrShared ) &&
                           NetSessionKeys::computeSessionKeys( provider, arrShared, pSecret, clientSalt, serverSalt, protocolID, inoutSecurity._keys );
        std::memset( arrShared, 0, sizeof( arrShared ) );
        if ( bKeys == false )
            return false;
        inoutSecurity._sendAEAD    = provider.createAEAD( algorithm, inoutSecurity._keys._clientToServer._arrKey );
        inoutSecurity._receiveAEAD = provider.createAEAD( algorithm, inoutSecurity._keys._serverToClient._arrKey );
        return inoutSecurity._sendAEAD != nullptr && inoutSecurity._receiveAEAD != nullptr;
    }

    bool NetPacketSealer::sealKeyConfirmTag( NetSlotSecurity& security, uint32 serverIndex, uint64 serverSalt )
    {
        uint8 arrNonce[NetSecurityConstant::kAEADNonceSize]{};
        uint8 arrAad[kKeyConfirmAadSize]{};
        NetSessionKeys::makeNonce( security._keys._serverToClient._arrIv, NetSessionKeys::kKeyConfirmPacketNumber, arrNonce );
        NetPacketSealerInternal::makeKeyConfirmAad( serverIndex, serverSalt, arrAad );
        return security._sendAEAD->seal( arrNonce, arrAad, kKeyConfirmAadSize, nullptr, 0, security._arrKeyConfirmTag );
    }

    bool NetPacketSealer::isKeyConfirmTagValid( NetSlotSecurity& security, uint32 serverIndex, uint64 serverSalt, const uint8* pTag )
    {
        uint8 arrNonce[NetSecurityConstant::kAEADNonceSize]{};
        uint8 arrAad[kKeyConfirmAadSize]{};
        NetSessionKeys::makeNonce( security._keys._serverToClient._arrIv, NetSessionKeys::kKeyConfirmPacketNumber, arrNonce );
        NetPacketSealerInternal::makeKeyConfirmAad( serverIndex, serverSalt, arrAad );
        return security._receiveAEAD->open( arrNonce, arrAad, kKeyConfirmAadSize, pTag, NetSecurityConstant::kAEADTagSize, nullptr );
    }

    bool NetPacketSealer::computeProofTag( INetSecurityProvider& provider, NetAEADAlgorithm algorithm, const NetSessionSecret& secret, uint64 clientSalt, uint64 serverSalt,
                                           const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, uint8* pOutTag )
    {
        (void)algorithm; // 증명은 AEAD 가 아니라 PRF 다 — 알고리즘은 세션 봉인에만 쓴다
        uint8                 arrAad[kMaxProofAadSize]{};
        const optional<int32> aadSize = NetPacketSealerInternal::makeProofAad( clientSalt, serverSalt, pPublicKey, pToken, tokenSize, arrAad );
        return aadSize.has_value() && NetPacketSealerInternal::computeProof( provider, secret, arrAad, *aadSize, pOutTag );
    }

    bool NetPacketSealer::isProofTagValid( INetSecurityProvider& provider, NetAEADAlgorithm algorithm, const NetSessionSecret& secret, uint64 clientSalt, uint64 serverSalt,
                                           const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, const uint8* pTag )
    {
        uint8 arrExpected[NetSecurityConstant::kAEADTagSize]{};
        if ( computeProofTag( provider, algorithm, secret, clientSalt, serverSalt, pPublicKey, pToken, tokenSize, arrExpected ) == false )
            return false;
        return NetPacketSealerInternal::isEqualConstantTime( arrExpected, pTag, NetSecurityConstant::kAEADTagSize );
    }
} // namespace sw
