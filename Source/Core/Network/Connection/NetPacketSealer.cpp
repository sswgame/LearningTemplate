#include "pch.h"

#include "Core/Network/Connection/NetPacketSealer.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Security/INetSecurityProvider.h"

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
             * @brief 증명 태그의 AAD를 @p pOutAad(`kMaxProofAadSize` 바이트)에 만들고 길이를 반환합니다. 토큰 길이가 범위 밖이면 -1 입니다.
             * @details 클라이언트 솔트, 서버 솔트(챌린지 값), 클라이언트 공개 키, 토큰을 이어 붙인다. 스택 버퍼이므로 핸드셰이크마다 할당하지 않는다.
             */
            static int32 makeProofAad( uint64 clientSalt, uint64 serverSalt, const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, uint8* pOutAad )
            {
                if ( tokenSize < 0 || NetSecurityConstant::kMaxTokenSize < tokenSize )
                    return -1;
                LittleEndianUtil::writeUint64( pOutAad, clientSalt );
                LittleEndianUtil::writeUint64( pOutAad + 8, serverSalt );
                std::memcpy( pOutAad + 16, pPublicKey, NetSecurityConstant::kX25519KeySize );
                if ( tokenSize > 0 )
                    std::memcpy( pOutAad + 16 + NetSecurityConstant::kX25519KeySize, pToken, static_cast<size_t>( tokenSize ) );
                return 16 + NetSecurityConstant::kX25519KeySize + tokenSize;
            }

            /** @brief 세션 비밀로 증명 AEAD 를 만듭니다. 유도한 키는 지웁니다. */
            static unique_ptr<INetAead> createProofAead( INetSecurityProvider& provider, NetAeadAlgorithm algorithm, const NetSessionSecret& secret )
            {
                uint8                arrProofKey[NetSecurityConstant::kAeadKeySize]{};
                unique_ptr<INetAead> proof;
                if ( NetSessionKeyUtil::computeProofKey( provider, secret, arrProofKey ) )
                    proof = provider.createAead( algorithm, arrProofKey );
                std::memset( arrProofKey, 0, sizeof( arrProofKey ) );
                return proof;
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
        uint8                   arrNonce[NetSecurityConstant::kAeadNonceSize]{};
        const NetDirectionKeys& keys = bServer ? security._keys._serverToClient : security._keys._clientToServer;
        NetSessionKeyUtil::makeNonce( keys._arrIv, packetNumber, arrNonce );
        _listSealScratch.resize( static_cast<size_t>( plainSize + NetSecurityConstant::kAeadTagSize ) );
        if ( security._sendAead->seal( arrNonce, arrAad, kAadSize, pPlain, plainSize, _listSealScratch.data() ) == false )
            return false;
        outWriter.clear();
        outWriter.writeBytes( arrHead, kSecureHeadSize );
        outWriter.writeBytes( _listSealScratch.data(), static_cast<int32>( _listSealScratch.size() ) );
        return true;
    }

    NetPacketSealer::OpenResult NetPacketSealer::open( NetSlotSecurity& security, uint32 protocolID, bool bServer, const uint8* pBody, int32 bodySize )
    {
        if ( bodySize < kSecureHeadSize + NetSecurityConstant::kAeadTagSize )
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
        uint8                   arrNonce[NetSecurityConstant::kAeadNonceSize]{};
        const NetDirectionKeys& keys = bServer ? security._keys._clientToServer : security._keys._serverToClient;
        NetSessionKeyUtil::makeNonce( keys._arrIv, packetNumber, arrNonce );
        const int32 cipherSize = bodySize - kSecureHeadSize;
        _listOpenScratch.resize( static_cast<size_t>( cipherSize - NetSecurityConstant::kAeadTagSize ) );
        if ( security._receiveAead->open( arrNonce, arrAad, kAadSize, pBody + kSecureHeadSize, cipherSize, _listOpenScratch.data() ) == false )
            return OpenResult::AuthenticationFailed;
        security._replayWindow.markReceived( packetNumber ); // 복호가 성공한 뒤에만 표시한다. 먼저 표시하면 위조 패킷이 진짜 번호를 소모시킨다.
        return OpenResult::Opened;
    }

    bool NetPacketSealer::deriveServerKeys( INetSecurityProvider& provider, NetAeadAlgorithm algorithm, const uint8* pClientPublicKey, const NetSessionSecret* pSecret,
                                            uint64 clientSalt, uint64 serverSalt, uint32 protocolID, NetSlotSecurity& outSecurity )
    {
        uint8      arrShared[NetSecurityConstant::kX25519KeySize]{};
        const bool bKeys = provider.makeX25519KeyPair( outSecurity._keyPair ) &&
                           provider.computeX25519SharedSecret( outSecurity._keyPair._arrPrivateKey, pClientPublicKey, arrShared ) &&
                           NetSessionKeyUtil::computeSessionKeys( provider, arrShared, pSecret, clientSalt, serverSalt, protocolID, outSecurity._keys );
        std::memset( arrShared, 0, sizeof( arrShared ) );
        outSecurity._keyPair.wipe(); // 서버는 수락을 다시 보낼 때 쓰도록 공개 키만 남긴다.
        if ( bKeys == false )
            return false;
        outSecurity._sendAead    = provider.createAead( algorithm, outSecurity._keys._serverToClient._arrKey );
        outSecurity._receiveAead = provider.createAead( algorithm, outSecurity._keys._clientToServer._arrKey );
        return outSecurity._sendAead != nullptr && outSecurity._receiveAead != nullptr;
    }

    bool NetPacketSealer::deriveClientKeys( INetSecurityProvider& provider, NetAeadAlgorithm algorithm, const uint8* pServerPublicKey, const NetSessionSecret* pSecret,
                                            uint64 clientSalt, uint64 serverSalt, uint32 protocolID, NetSlotSecurity& inoutSecurity )
    {
        uint8      arrShared[NetSecurityConstant::kX25519KeySize]{};
        const bool bKeys = provider.computeX25519SharedSecret( inoutSecurity._keyPair._arrPrivateKey, pServerPublicKey, arrShared ) &&
                           NetSessionKeyUtil::computeSessionKeys( provider, arrShared, pSecret, clientSalt, serverSalt, protocolID, inoutSecurity._keys );
        std::memset( arrShared, 0, sizeof( arrShared ) );
        if ( bKeys == false )
            return false;
        inoutSecurity._sendAead    = provider.createAead( algorithm, inoutSecurity._keys._clientToServer._arrKey );
        inoutSecurity._receiveAead = provider.createAead( algorithm, inoutSecurity._keys._serverToClient._arrKey );
        return inoutSecurity._sendAead != nullptr && inoutSecurity._receiveAead != nullptr;
    }

    bool NetPacketSealer::sealKeyConfirmTag( NetSlotSecurity& security, uint32 serverIndex, uint64 serverSalt )
    {
        uint8 arrNonce[NetSecurityConstant::kAeadNonceSize]{};
        uint8 arrAad[kKeyConfirmAadSize]{};
        NetSessionKeyUtil::makeNonce( security._keys._serverToClient._arrIv, NetSessionKeyUtil::kKeyConfirmPacketNumber, arrNonce );
        NetPacketSealerInternal::makeKeyConfirmAad( serverIndex, serverSalt, arrAad );
        return security._sendAead->seal( arrNonce, arrAad, kKeyConfirmAadSize, nullptr, 0, security._arrKeyConfirmTag );
    }

    bool NetPacketSealer::isKeyConfirmTagValid( NetSlotSecurity& security, uint32 serverIndex, uint64 serverSalt, const uint8* pTag )
    {
        uint8 arrNonce[NetSecurityConstant::kAeadNonceSize]{};
        uint8 arrAad[kKeyConfirmAadSize]{};
        NetSessionKeyUtil::makeNonce( security._keys._serverToClient._arrIv, NetSessionKeyUtil::kKeyConfirmPacketNumber, arrNonce );
        NetPacketSealerInternal::makeKeyConfirmAad( serverIndex, serverSalt, arrAad );
        return security._receiveAead->open( arrNonce, arrAad, kKeyConfirmAadSize, pTag, NetSecurityConstant::kAeadTagSize, nullptr );
    }

    bool NetPacketSealer::computeProofTag( INetSecurityProvider& provider, NetAeadAlgorithm algorithm, const NetSessionSecret& secret, uint64 clientSalt, uint64 serverSalt,
                                           const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, uint8* pOutTag )
    {
        uint8       arrAad[kMaxProofAadSize]{};
        const int32 aadSize = NetPacketSealerInternal::makeProofAad( clientSalt, serverSalt, pPublicKey, pToken, tokenSize, arrAad );
        if ( aadSize < 0 )
            return false;
        const unique_ptr<INetAead> proof = NetPacketSealerInternal::createProofAead( provider, algorithm, secret );
        if ( proof == nullptr )
            return false;
        const uint8 arrZeroNonce[NetSecurityConstant::kAeadNonceSize]{};
        return proof->seal( arrZeroNonce, arrAad, aadSize, nullptr, 0, pOutTag );
    }

    bool NetPacketSealer::isProofTagValid( INetSecurityProvider& provider, NetAeadAlgorithm algorithm, const NetSessionSecret& secret, uint64 clientSalt, uint64 serverSalt,
                                           const uint8* pPublicKey, const uint8* pToken, int32 tokenSize, const uint8* pTag )
    {
        uint8       arrAad[kMaxProofAadSize]{};
        const int32 aadSize = NetPacketSealerInternal::makeProofAad( clientSalt, serverSalt, pPublicKey, pToken, tokenSize, arrAad );
        if ( aadSize < 0 )
            return false;
        const unique_ptr<INetAead> proof = NetPacketSealerInternal::createProofAead( provider, algorithm, secret );
        const uint8                arrZeroNonce[NetSecurityConstant::kAeadNonceSize]{};
        return proof != nullptr && proof->open( arrZeroNonce, arrAad, aadSize, pTag, NetSecurityConstant::kAeadTagSize, nullptr );
    }
} // namespace sw
