#include "pch.h"

#include "Core/Network/Connection/NetHost.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/NetTransport.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

namespace sw
{
    namespace
    {
        struct NetHostInternal
        {
            static constexpr int32 kTypeBits          = 3;
            static constexpr int32 kHeaderSize        = 8;         ///< 프로토콜 id 4 + 체크섬 4
            static constexpr int32 kSecureHeadSize    = 1 + 4 + 8; ///< 암호화 몸의 머리 — 종류 바이트 · 연결 값 · 패킷 번호(BE)
            static constexpr int32 kAadSize           = 4 + kSecureHeadSize;
            static constexpr int32 kKeyConfirmAadSize = 4 + 8; ///< 서버가 준 자리 번호 ‖ 서버 소금

            /** @brief 증명 태그의 AAD — 클라이언트 소금 ‖ 도전 값 ‖ 클라이언트 공개 키 ‖ 토큰(다른 응답에 옮겨 붙이지 못한다). */
            static vector<uint8> makeProofAad( uint64 clientSalt, uint64 challenge, const uint8* pPublicKey, const uint8* pToken, int32 tokenSize )
            {
                vector<uint8> listAad( 16u + static_cast<size_t>( NetSecurityConstant::kX25519KeySize ) + static_cast<size_t>( tokenSize ) );
                for ( int32 index = 0; index < 8; ++index )
                {
                    listAad[static_cast<size_t>( index )]     = static_cast<uint8>( clientSalt >> ( index * 8 ) );
                    listAad[static_cast<size_t>( 8 + index )] = static_cast<uint8>( challenge >> ( index * 8 ) );
                }
                std::memcpy( listAad.data() + 16, pPublicKey, NetSecurityConstant::kX25519KeySize );
                if ( tokenSize > 0 )
                    std::memcpy( listAad.data() + 16 + NetSecurityConstant::kX25519KeySize, pToken, static_cast<size_t>( tokenSize ) );
                return listAad;
            }

            /** @brief 키 확인 태그의 AAD — 서버가 준 자리 번호 4 LE ‖ 서버 소금 8 LE. */
            static void makeKeyConfirmAad( uint32 serverIndex, uint64 serverSalt, uint8* pOutAad )
            {
                for ( int32 index = 0; index < 4; ++index )
                    pOutAad[index] = static_cast<uint8>( serverIndex >> ( index * 8 ) );
                for ( int32 index = 0; index < 8; ++index )
                    pOutAad[4 + index] = static_cast<uint8>( serverSalt >> ( index * 8 ) );
            }

            /** @brief 데이터 · 끊기 패킷의 AAD — 프로토콜 id 4 LE ‖ 몸의 머리(종류 · 연결 값 · 패킷 번호). */
            static void makePacketAad( uint32 protocolId, const uint8* pBodyHead, uint8* pOutAad )
            {
                for ( int32 index = 0; index < 4; ++index )
                    pOutAad[index] = static_cast<uint8>( protocolId >> ( index * 8 ) );
                std::memcpy( pOutAad + 4, pBodyHead, kSecureHeadSize );
            }

            static void writeUint32( vector<uint8>& outBytes, size_t offset, uint32 value )
            {
                for ( int32 index = 0; index < 4; ++index )
                    outBytes[offset + static_cast<size_t>( index )] = static_cast<uint8>( value >> ( index * 8 ) );
            }

            static uint64 makeAddressKey( const NetAddress& address ) { return ( static_cast<uint64>( address._ipv4 ) << 16 ) | address._port; }

            /** @brief 운영체제 난수 64 비트 — 도전 값이 다른 실행 · 다른 호스트와 겹치지 않고 미리 알 수 없게. */
            static uint64 makeRandomSeed()
            {
                std::random_device randomDevice;
                const uint64       seed = ( static_cast<uint64>( randomDevice() ) << 32 ) | static_cast<uint64>( randomDevice() );
                return seed != 0 ? seed : HashUtil::kGoldenRatio64;
            }

            static uint32 readUint32( const uint8* pData )
            {
                return static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) | ( static_cast<uint32>( pData[2] ) << 16 ) |
                       ( static_cast<uint32>( pData[3] ) << 24 );
            }

            /** @brief 거절 패킷이 싣는 이유입니다. 모르는 값은 Rejected 로 읽는다(다른 판이 이유를 늘려도 클라이언트는 끊긴다). */
            static NetDisconnectReason readDeniedReason( uint64 value )
            {
                switch ( static_cast<NetDisconnectReason>( value & 0xFFu ) )
                {
                    case NetDisconnectReason::ServerFull:
                        return NetDisconnectReason::ServerFull;
                    case NetDisconnectReason::VersionMismatch:
                        return NetDisconnectReason::VersionMismatch;
                    case NetDisconnectReason::SecurityMismatch:
                        return NetDisconnectReason::SecurityMismatch;
                    case NetDisconnectReason::AuthenticationFailed:
                        return NetDisconnectReason::AuthenticationFailed;
                    case NetDisconnectReason::None:
                    case NetDisconnectReason::Requested:
                    case NetDisconnectReason::Remote:
                    case NetDisconnectReason::Timeout:
                    case NetDisconnectReason::Rejected:
                        break;
                }
                return NetDisconnectReason::Rejected;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NetHost::NetHost()
        : _mutex{}
        , _listSlot{}
        , _listEvent{}
        , _pendingBatch{}
        , _packetWriter{}
        , _mapSlotByAddress{}
        , _connectPromise{}
        , _listFinished{}
        , _listReceived{}
        , _flushBatch{}
        , _listDeliver{}
        , _listDrainScratch{}
        , _plainWriter{}
        , _listSealScratch{}
        , _listOpenScratch{}
        , _credentials{}
        , _settings{}
        , _pTransport{ nullptr }
        , _saltState{ 0 }
        , _rejectedPacketCount{ 0 }
        , _authenticationFailureCount{ 0 }
        , _replayRejectedCount{ 0 }
        , _mismatchLogCount{ 0 }
        , _challengeSecret{ 0 }
        , _protocolId{ 0 }
        , _updateDepth{ 0 }
        , _clientIndex{ -1 }
        , _receiveCursor{ 0 }
        , _bServer{ SW_FALSE }
        , _bConnectPending{ SW_FALSE }
        , _bHasCredentials{ SW_FALSE }
    {
    }

    NetHost::~NetHost() = default;

    void NetHost::initialize( INetTransport* pTransport, const NetHostSettings& settings )
    {
        vector<FinishedConnect> listFinished;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested );
            _pTransport      = pTransport;
            _settings        = settings;
            _saltState       = settings._saltSeed != 0 ? settings._saltSeed : NetHostInternal::makeRandomSeed();
            _challengeSecret = nextSalt();
            _listSlot.clear();
            _mapSlotByAddress.clear();
            _packetWriter.reserve( kNetMaxPacketSize );
            _pendingBatch.clear();
            _pendingBatch._bytes.reserve( static_cast<size_t>( kNetMaxPacketSize ) * 4 );
            _listEvent.clear();
            _rejectedPacketCount        = 0;
            _authenticationFailureCount = 0;
            _replayRejectedCount        = 0;
            _mismatchLogCount           = 0;
            _clientIndex                = -1;
            _bServer                    = SW_FALSE;
            _bHasCredentials            = SW_FALSE;
            _credentials                = NetConnectCredentials{};
            _plainWriter.reserve( kNetMaxPacketSize );
            _protocolId = NetProtocol::makeProtocolId( settings._gameId, settings._wireVersion, computeFeatureMask() ); // listen · connect 가 역할에 맞춰 다시 셈한다
            listFinished.swap( _listFinished );
        }
        deliverFinished( listFinished );
    }

    NetHost::PacketType NetHost::peekPacketType( const uint8* pData, int32 size )
    {
        if ( pData == nullptr || size <= NetHostInternal::kHeaderSize )
            return PacketType::Count;
        BitReader    reader( pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize );
        const uint32 type = reader.readBits( NetHostInternal::kTypeBits );
        return type < static_cast<uint32>( PacketType::Count ) ? static_cast<PacketType>( type ) : PacketType::Count;
    }

    uint32 NetHost::computePacketChecksum( uint32 headerId, const uint8* pBody, int32 size )
    {
        // FNV-1a 32 — 머리 값(프로토콜 id)을 먼저 섞어 다른 게임 · 판의 패킷은 체크섬부터 틀린다.
        uint32 hash = HashUtil::kFnvOffset32;
        for ( int32 shift = 0; shift < 32; shift += 8 )
            hash = ( hash ^ ( ( headerId >> shift ) & 0xFFu ) ) * HashUtil::kFnvPrime32;
        for ( int32 index = 0; index < size; ++index )
            hash = ( hash ^ pBody[index] ) * HashUtil::kFnvPrime32;
        return hash;
    }

    uint32 NetHost::computeFeatureMask() const
    {
        if ( isEncrypted() == false )
            return 0;
        // 토큰 결속 — 서버는 인증기가 있을 때, 클라이언트는 자격을 내밀 때. 한쪽만이면 프로토콜 id 가 달라 SecurityMismatch 로 갈린다.
        const bool bTokenBound = _bServer == SW_TRUE ? _settings._security._pAuthenticator != nullptr : _bHasCredentials == SW_TRUE;
        return NetProtocolFeature::kEncrypted | ( bTokenBound ? NetProtocolFeature::kTokenBound : 0u );
    }

    int64 NetHost::computeChallengeWindow( float64 time ) { return static_cast<int64>( MathUtil::floor( time / kChallengeWindowSeconds ) ); }

    uint64 NetHost::makeChallengeToken( const NetAddress& address, uint64 clientSalt, int64 window ) const
    {
        uint64 value = HashUtil::mix64( _challengeSecret ^ NetHostInternal::makeAddressKey( address ) );
        value        = HashUtil::mix64( value ^ clientSalt );
        value        = HashUtil::mix64( value ^ static_cast<uint64>( window ) ^ ( _challengeSecret << 1 ) );
        return value | 1u; // 0 은 "아직 도전을 못 받았다" 의 뜻
    }

    uint64 NetHost::nextSalt()
    {
        // splitmix64
        _saltState += HashUtil::kGoldenRatio64;
        uint64 value = _saltState;
        return HashUtil::mix64( value );
    }

    bool NetHost::listen()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _pTransport == nullptr )
            return false;
        if ( isEncrypted() && _settings._security._pProvider == nullptr )
        {
            SW_LOG_ERROR( "NetHost: encrypted mode needs a security provider (EngineNetSecurity::getProvider())" );
            return false;
        }
#if defined( SW_SHIPPING )
        if ( isEncrypted() && _settings._security._pAuthenticator == nullptr )
        {
            // 인증기 없는 암호화(일회 키)는 도청만 막고 중간자는 못 막는다 — 개발 빌드 전용이다.
            SW_LOG_ERROR( "NetHost: a shipping server must bind encrypted connections to session tokens (set NetSecuritySettings::_pAuthenticator)" );
            return false;
        }
#endif
        _bServer    = SW_TRUE;
        _protocolId = NetProtocol::makeProtocolId( _settings._gameId, _settings._wireVersion, computeFeatureMask() );
        _listSlot.clear();
        _listSlot.resize( static_cast<size_t>( MathUtil::max( 1, _settings._maxConnections ) ) );
        _mapSlotByAddress.clear();
        _mapSlotByAddress.reserve( _listSlot.size() * 2 );
        return true;
    }

    bool NetHost::connect( const NetAddress& serverAddress ) { return connectLocked( serverAddress, nullptr ); }

    bool NetHost::connect( const NetAddress& serverAddress, const NetConnectCredentials& credentials ) { return connectLocked( serverAddress, &credentials ); }

    bool NetHost::connectLocked( const NetAddress& serverAddress, const NetConnectCredentials* pCredentials )
    {
        vector<FinishedConnect> listFinished;
        bool                    bStarted = false;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested ); // 앞의 비동기 연결은 끝난다
            _credentials     = pCredentials != nullptr ? *pCredentials : NetConnectCredentials{};
            _bHasCredentials = pCredentials != nullptr && pCredentials->_tokenSize > 0 ? SW_TRUE : SW_FALSE;
            bStarted         = startConnect( serverAddress );
            listFinished.swap( _listFinished );
        }
        deliverFinished( listFinished );
        return bStarted;
    }

    TaskFuture<NetConnectResult> NetHost::connectAsync( const NetAddress& serverAddress ) { return connectAsyncWith( serverAddress, nullptr ); }

    TaskFuture<NetConnectResult> NetHost::connectAsync( const NetAddress& serverAddress, const NetConnectCredentials& credentials )
    {
        return connectAsyncWith( serverAddress, &credentials );
    }

    TaskFuture<NetConnectResult> NetHost::connectAsyncWith( const NetAddress& serverAddress, const NetConnectCredentials* pCredentials )
    {
        vector<FinishedConnect>      listFinished;
        TaskFuture<NetConnectResult> future;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested );
            _connectPromise  = TaskPromise<NetConnectResult>{};
            future           = _connectPromise.getFuture();
            _bConnectPending = SW_TRUE;
            _credentials     = pCredentials != nullptr ? *pCredentials : NetConnectCredentials{};
            _bHasCredentials = pCredentials != nullptr && pCredentials->_tokenSize > 0 ? SW_TRUE : SW_FALSE;
            if ( startConnect( serverAddress ) == false )
                finishConnect( NetDisconnectReason::Rejected );
            listFinished.swap( _listFinished );
        }
        deliverFinished( listFinished );
        return future;
    }

    bool NetHost::startConnect( const NetAddress& serverAddress )
    {
        if ( _pTransport == nullptr || serverAddress.isValid() == false )
            return false;
        if ( _bHasCredentials == SW_TRUE && _credentials._tokenSize > NetSecurityConstant::kMaxTokenSize )
        {
            SW_LOG_ERROR( "NetHost: session token of %# bytes exceeds the limit of %# bytes", _credentials._tokenSize, NetSecurityConstant::kMaxTokenSize );
            return false;
        }
        _bServer    = SW_FALSE;
        _protocolId = NetProtocol::makeProtocolId( _settings._gameId, _settings._wireVersion, computeFeatureMask() );
        _listSlot.clear();
        _listSlot.resize( 1 );
        _mapSlotByAddress.clear();
        bindAddress( 0, serverAddress );
        Slot& slot             = _listSlot[0];
        slot._address          = serverAddress;
        slot._clientSalt       = nextSalt();
        slot._state            = NetConnectionState::Connecting;
        slot._connectStartTime = -1.0; // 첫 update 에서 시각을 잡는다
        slot._lastSendTime     = -1.0;
        if ( isEncrypted() )
        {
            slot._security = make_unique<SlotSecurity>();
            if ( _settings._security._pProvider == nullptr || _settings._security._pProvider->makeX25519KeyPair( slot._security->_keyPair ) == false )
            {
                SW_LOG_ERROR( "NetHost: cannot create a key pair - is NetSecuritySettings::_pProvider set?" );
                closeSlot( 0, NetDisconnectReason::Rejected, false );
                return false;
            }
        }
        return true;
    }

    int32 NetHost::findSlotByAddress( const NetAddress& address ) const
    {
        const auto iter = _mapSlotByAddress.find( NetHostInternal::makeAddressKey( address ) );
        return iter != _mapSlotByAddress.end() ? iter->second : -1;
    }

    void NetHost::bindAddress( int32 slotIndex, const NetAddress& address )
    {
        _mapSlotByAddress[NetHostInternal::makeAddressKey( address )] = slotIndex;
    }

    void NetHost::unbindAddress( int32 slotIndex )
    {
        const auto iter = _mapSlotByAddress.find( NetHostInternal::makeAddressKey( _listSlot[static_cast<size_t>( slotIndex )]._address ) );
        if ( iter != _mapSlotByAddress.end() && iter->second == slotIndex )
            _mapSlotByAddress.erase( iter );
    }

    int32 NetHost::findFreeSlot() const
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._state == NetConnectionState::Disconnected )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void NetHost::sendFramed( const NetAddress& to, uint32 headerId )
    {
        // 보낼 묶음의 바이트 뒤에 바로 짓는다(헤더 + 몸) — 보내기는 잠금을 푼 뒤 `sendBatch` 가.
        const vector<uint8>& body     = _packetWriter.getBytes();
        const int32          bodySize = _packetWriter.getByteCount();
        const size_t         offset   = _pendingBatch._bytes.size();
        _pendingBatch._bytes.resize( offset + static_cast<size_t>( NetHostInternal::kHeaderSize + bodySize ) );
        uint8* pPacket = _pendingBatch._bytes.data() + offset;
        if ( bodySize > 0 )
            std::memcpy( pPacket + NetHostInternal::kHeaderSize, body.data(), static_cast<size_t>( bodySize ) );
        NetHostInternal::writeUint32( _pendingBatch._bytes, offset, headerId );
        NetHostInternal::writeUint32( _pendingBatch._bytes, offset + 4, computePacketChecksum( headerId, pPacket + NetHostInternal::kHeaderSize, bodySize ) );
        _pendingBatch._listDatagram.push_back( OutgoingDatagram{ to, static_cast<int32>( offset ), NetHostInternal::kHeaderSize + bodySize } );
    }

    void NetHost::sendBatch( const OutgoingBatch& batch )
    {
        for ( const OutgoingDatagram& datagram : batch._listDatagram )
            (void)_pTransport->send( datagram._to, batch._bytes.data() + datagram._offset, datagram._size );
    }

    void NetHost::takePending( OutgoingBatch& outBatch, vector<FinishedConnect>& outListFinished )
    {
        outBatch.clear();
        outBatch._listDatagram.swap( _pendingBatch._listDatagram );
        outBatch._bytes.swap( _pendingBatch._bytes );
        outListFinished.clear();
        outListFinished.swap( _listFinished );
    }

    void NetHost::deliverFinished( vector<FinishedConnect>& listFinished )
    {
        for ( FinishedConnect& finished : listFinished )
            finished._promise.setValue( finished._result );
        listFinished.clear();
    }

    void NetHost::pushEvent( const NetHostEvent& event )
    {
        _listEvent.push_back( event );
        if ( _bServer == SW_FALSE )
            finishConnect( event._kind == NetHostEvent::Kind::Connected ? NetDisconnectReason::None : event._reason );
    }

    void NetHost::finishConnect( NetDisconnectReason reason )
    {
        if ( _bConnectPending == SW_FALSE )
            return;
        _bConnectPending = SW_FALSE;
        FinishedConnect finished{
            std::move( _connectPromise ), NetConnectResult{ reason, reason == NetDisconnectReason::None ? _clientIndex : -1 }
        };
        _listFinished.push_back( std::move( finished ) );
    }

    void NetHost::sendControl( const NetAddress& to, PacketType type, uint64 valueA, uint64 valueB )
    {
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( type ), NetHostInternal::kTypeBits );
        writer.writeBits( static_cast<uint32>( valueA ), 32 );
        writer.writeBits( static_cast<uint32>( valueA >> 32 ), 32 );
        writer.writeBits( static_cast<uint32>( valueB ), 32 );
        writer.writeBits( static_cast<uint32>( valueB >> 32 ), 32 );
        sendFramed( to, isHandshakeFramed( type ) ? NetProtocol::kHandshakeId : _protocolId );
    }

    void NetHost::sendDenied( const NetAddress& to, NetDisconnectReason reason, uint64 clientSalt )
    {
        sendControl( to, PacketType::Denied, static_cast<uint64>( reason ) | ( static_cast<uint64>( _protocolId ) << 32 ), clientSalt );
    }

    void NetHost::sendChallengeResponse( Slot& slot )
    {
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( PacketType::ChallengeResponse ), NetHostInternal::kTypeBits );
        writer.writeBits( static_cast<uint32>( slot._clientSalt ), 32 );
        writer.writeBits( static_cast<uint32>( slot._clientSalt >> 32 ), 32 );
        writer.writeBits( static_cast<uint32>( slot._serverSalt ), 32 );
        writer.writeBits( static_cast<uint32>( slot._serverSalt >> 32 ), 32 );
        if ( isEncrypted() && slot._security != nullptr )
        {
            writer.writeBytes( slot._security->_keyPair._arrPublicKey, NetSecurityConstant::kX25519KeySize );
            if ( _bHasCredentials == SW_TRUE )
            {
                writer.writeBlob( _credentials._arrToken, _credentials._tokenSize );
                // 증명 — 세션 비밀로 유도한 키의 빈 봉인. AAD 가 이 응답의 소금 · 도전 값 · 공개 키 · 토큰이라 다른 응답에 옮겨 붙이지 못한다.
                uint8                 arrProofKey[NetSecurityConstant::kAeadKeySize]{};
                uint8                 arrTag[NetSecurityConstant::kAeadTagSize]{};
                const uint8           arrZeroNonce[NetSecurityConstant::kAeadNonceSize]{};
                const vector<uint8>   aad      = NetHostInternal::makeProofAad( slot._clientSalt, slot._serverSalt, slot._security->_keyPair._arrPublicKey, _credentials._arrToken,
                                                                                _credentials._tokenSize );
                INetSecurityProvider& provider = *_settings._security._pProvider;
                if ( NetSessionKeyUtil::computeProofKey( provider, _credentials._secret, arrProofKey ) )
                {
                    unique_ptr<INetAead> proof = provider.createAead( _settings._security._algorithm, arrProofKey );
                    if ( proof != nullptr )
                        (void)proof->seal( arrZeroNonce, aad.data(), static_cast<int32>( aad.size() ), nullptr, 0, arrTag );
                }
                std::memset( arrProofKey, 0, sizeof( arrProofKey ) );
                writer.writeBytes( arrTag, NetSecurityConstant::kAeadTagSize );
            }
        }
        sendFramed( slot._address, _protocolId );
    }

    void NetHost::sendAccepted( int32 slotIndex )
    {
        const Slot& slot   = _listSlot[static_cast<size_t>( slotIndex )];
        BitWriter&  writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( PacketType::Accepted ), NetHostInternal::kTypeBits );
        writer.writeBits( static_cast<uint32>( slotIndex ), 32 );
        writer.writeBits( 0u, 32 );
        writer.writeBits( static_cast<uint32>( slot._serverSalt ), 32 );
        writer.writeBits( static_cast<uint32>( slot._serverSalt >> 32 ), 32 );
        if ( isEncrypted() && slot._security != nullptr )
        {
            writer.writeBytes( slot._security->_keyPair._arrPublicKey, NetSecurityConstant::kX25519KeySize );
            writer.writeBytes( slot._security->_arrKeyConfirmTag, NetSecurityConstant::kAeadTagSize );
        }
        sendFramed( slot._address, _protocolId );
    }

    bool NetHost::acceptSecureResponse( BitReader& reader, const NetAddress& from, uint64 clientSalt, uint64 challenge, unique_ptr<SlotSecurity>& outSecurity )
    {
        INetSecurityProvider& provider    = *_settings._security._pProvider;
        const bool            bTokenBound = _settings._security._pAuthenticator != nullptr;
        uint8                 arrClientPublic[NetSecurityConstant::kX25519KeySize]{};
        uint8                 arrProof[NetSecurityConstant::kAeadTagSize]{};
        vector<uint8>         listToken;
        bool                  bRead = reader.readBytes( arrClientPublic, NetSecurityConstant::kX25519KeySize );
        if ( bTokenBound )
            bRead = bRead && reader.readBlob( listToken, NetSecurityConstant::kMaxTokenSize ) && reader.readBytes( arrProof, NetSecurityConstant::kAeadTagSize );
        if ( bRead == false || reader.hasOverflowed() )
        {
            ++_rejectedPacketCount;
            return false;
        }
        NetSessionSecret secret;
        uint64           principalId = 0;
        if ( bTokenBound )
        {
            uint8                arrProofKey[NetSecurityConstant::kAeadKeySize]{};
            const uint8          arrZeroNonce[NetSecurityConstant::kAeadNonceSize]{};
            const vector<uint8>  aad = NetHostInternal::makeProofAad( clientSalt, challenge, arrClientPublic, listToken.data(), static_cast<int32>( listToken.size() ) );
            unique_ptr<INetAead> proof;
            const bool           bKnown = _settings._security._pAuthenticator->findSessionSecret( listToken.data(), static_cast<int32>( listToken.size() ), secret, principalId ) &&
                                NetSessionKeyUtil::computeProofKey( provider, secret, arrProofKey ) &&
                                ( proof = provider.createAead( _settings._security._algorithm, arrProofKey ) ) != nullptr &&
                                proof->open( arrZeroNonce, aad.data(), static_cast<int32>( aad.size() ), arrProof, NetSecurityConstant::kAeadTagSize, nullptr );
            std::memset( arrProofKey, 0, sizeof( arrProofKey ) );
            if ( bKnown == false )
            {
                ++_authenticationFailureCount;
                sendDenied( from, NetDisconnectReason::AuthenticationFailed, clientSalt );
                return false;
            }
        }
        unique_ptr<SlotSecurity> security = make_unique<SlotSecurity>();
        uint8                    arrShared[NetSecurityConstant::kX25519KeySize]{};
        const bool               bKeys = provider.makeX25519KeyPair( security->_keyPair ) &&
                           provider.computeX25519SharedSecret( security->_keyPair._arrPrivateKey, arrClientPublic, arrShared ) &&
                           NetSessionKeyUtil::computeSessionKeys( provider, arrShared, bTokenBound ? &secret : nullptr, clientSalt, challenge, _protocolId, security->_keys );
        std::memset( arrShared, 0, sizeof( arrShared ) );
        security->_keyPair.wipe(); // 서버는 공개 키만 남긴다(수락 재전송)
        if ( bKeys )
        {
            security->_sendAead    = provider.createAead( _settings._security._algorithm, security->_keys._serverToClient._arrKey );
            security->_receiveAead = provider.createAead( _settings._security._algorithm, security->_keys._clientToServer._arrKey );
        }
        if ( bKeys == false || security->_sendAead == nullptr || security->_receiveAead == nullptr )
        {
            ++_authenticationFailureCount; // 작은 차수 공개 키 등
            sendDenied( from, NetDisconnectReason::AuthenticationFailed, clientSalt );
            return false;
        }
        security->_principalId = principalId;
        security->_bKeysReady  = SW_TRUE;
        outSecurity            = std::move( security );
        return true;
    }

    bool NetHost::acceptSecureAccepted( BitReader& reader, Slot& slot, int32 slotIndex, uint32 serverIndex )
    {
        INetSecurityProvider& provider = *_settings._security._pProvider;
        SlotSecurity&         security = *slot._security;
        uint8                 arrServerPublic[NetSecurityConstant::kX25519KeySize]{};
        uint8                 arrTag[NetSecurityConstant::kAeadTagSize]{};
        uint8                 arrShared[NetSecurityConstant::kX25519KeySize]{};
        bool                  bOk = reader.readBytes( arrServerPublic, NetSecurityConstant::kX25519KeySize ) && reader.readBytes( arrTag, NetSecurityConstant::kAeadTagSize ) &&
                   provider.computeX25519SharedSecret( security._keyPair._arrPrivateKey, arrServerPublic, arrShared ) &&
                   NetSessionKeyUtil::computeSessionKeys( provider, arrShared, _bHasCredentials == SW_TRUE ? &_credentials._secret : nullptr, slot._clientSalt, slot._serverSalt,
                                                          _protocolId, security._keys );
        std::memset( arrShared, 0, sizeof( arrShared ) );
        if ( bOk )
        {
            security._sendAead    = provider.createAead( _settings._security._algorithm, security._keys._clientToServer._arrKey );
            security._receiveAead = provider.createAead( _settings._security._algorithm, security._keys._serverToClient._arrKey );
            bOk                   = security._sendAead != nullptr && security._receiveAead != nullptr;
        }
        if ( bOk )
        {
            // 키 확인 — 서버가 같은 키(같은 세션 비밀 · 같은 공유 비밀)를 가졌는지. 다르면 중간자이거나 비밀이 틀렸다.
            uint8 arrNonce[NetSecurityConstant::kAeadNonceSize]{};
            uint8 arrAad[NetHostInternal::kKeyConfirmAadSize]{};
            NetSessionKeyUtil::makeNonce( security._keys._serverToClient._arrIv, NetSessionKeyUtil::kKeyConfirmPacketNumber, arrNonce );
            NetHostInternal::makeKeyConfirmAad( serverIndex, slot._serverSalt, arrAad );
            bOk = security._receiveAead->open( arrNonce, arrAad, NetHostInternal::kKeyConfirmAadSize, arrTag, NetSecurityConstant::kAeadTagSize, nullptr );
        }
        if ( bOk == false )
        {
            ++_authenticationFailureCount;
            closeSlot( slotIndex, NetDisconnectReason::AuthenticationFailed, false );
            return false;
        }
        security._keyPair.wipe();
        security._bKeysReady = SW_TRUE;
        return true;
    }

    int32 NetHost::sendSealed( Slot& slot, PacketType type, const uint8* pPlain, int32 plainSize )
    {
        SlotSecurity& security     = *slot._security;
        const uint64  packetNumber = security._sendPacketNumber++;
        const uint32  token        = static_cast<uint32>( slot._clientSalt ^ slot._serverSalt );
        uint8         arrHead[NetHostInternal::kSecureHeadSize]{};
        arrHead[0] = static_cast<uint8>( type ); // 종류 3 비트 + 0 5 비트 — BitWriter 는 낮은 비트부터라 받는 쪽 readBits( 3 ) · peekPacketType 이 그대로 읽는다
        for ( int32 index = 0; index < 4; ++index )
            arrHead[1 + index] = static_cast<uint8>( token >> ( index * 8 ) );
        for ( int32 index = 0; index < 8; ++index )
            arrHead[5 + index] = static_cast<uint8>( packetNumber >> ( 56 - index * 8 ) );
        uint8 arrAad[NetHostInternal::kAadSize]{};
        NetHostInternal::makePacketAad( _protocolId, arrHead, arrAad );
        uint8                   arrNonce[NetSecurityConstant::kAeadNonceSize]{};
        const NetDirectionKeys& keys = _bServer == SW_TRUE ? security._keys._serverToClient : security._keys._clientToServer;
        NetSessionKeyUtil::makeNonce( keys._arrIv, packetNumber, arrNonce );
        _listSealScratch.resize( static_cast<size_t>( plainSize + NetSecurityConstant::kAeadTagSize ) );
        if ( security._sendAead->seal( arrNonce, arrAad, NetHostInternal::kAadSize, pPlain, plainSize, _listSealScratch.data() ) == false )
            return 0;
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBytes( arrHead, NetHostInternal::kSecureHeadSize );
        writer.writeBytes( _listSealScratch.data(), static_cast<int32>( _listSealScratch.size() ) );
        sendFramed( slot._address, _protocolId );
        return NetHostInternal::kHeaderSize + writer.getByteCount();
    }

    bool NetHost::openSealed( Slot& slot, const uint8* pBody, int32 bodySize )
    {
        SlotSecurity& security = *slot._security;
        if ( bodySize < NetHostInternal::kSecureHeadSize + NetSecurityConstant::kAeadTagSize )
        {
            ++_rejectedPacketCount;
            return false;
        }
        uint64 packetNumber = 0;
        for ( int32 index = 0; index < 8; ++index )
            packetNumber = ( packetNumber << 8 ) | pBody[5 + index];
        if ( security._replayWindow.isAcceptable( packetNumber ) == false )
        {
            ++_replayRejectedCount;
            return false;
        }
        uint8 arrAad[NetHostInternal::kAadSize]{};
        NetHostInternal::makePacketAad( _protocolId, pBody, arrAad );
        uint8                   arrNonce[NetSecurityConstant::kAeadNonceSize]{};
        const NetDirectionKeys& keys = _bServer == SW_TRUE ? security._keys._clientToServer : security._keys._serverToClient;
        NetSessionKeyUtil::makeNonce( keys._arrIv, packetNumber, arrNonce );
        const int32 cipherSize = bodySize - NetHostInternal::kSecureHeadSize;
        _listOpenScratch.resize( static_cast<size_t>( cipherSize - NetSecurityConstant::kAeadTagSize ) );
        if ( security._receiveAead->open( arrNonce, arrAad, NetHostInternal::kAadSize, pBody + NetHostInternal::kSecureHeadSize, cipherSize, _listOpenScratch.data() ) == false )
        {
            ++_authenticationFailureCount;
            return false;
        }
        security._replayWindow.markReceived( packetNumber ); // 복호가 통과한 뒤에만 — 먼저 표시하면 위조 패킷이 진짜 번호를 태운다
        return true;
    }

    void NetHost::sendPayload( float64 time, Slot& slot )
    {
        if ( slot._security != nullptr )
        {
            // 암호화 — NetConnection 패킷을 평문으로 짓고 봉인한다. 예산에서 암호화 머리 · 태그 몫을 뺀다.
            _plainWriter.clear();
            const int32 maxBytes =
                slot._sendCredit > 0.0 ? kNetMaxPacketSize - NetHostInternal::kHeaderSize - NetHostInternal::kSecureHeadSize - NetSecurityConstant::kAeadTagSize : 0;
            slot._connection.writePacket( time, _plainWriter, maxBytes, _settings._keepAliveInterval );
            const int32 sentBytes = sendSealed( slot, PacketType::Payload, _plainWriter.getBytes().data(), _plainWriter.getByteCount() );
            slot._sendCredit -= static_cast<float64>( sentBytes );
            slot._lastSendTime = time;
            return;
        }
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( PacketType::Payload ), NetHostInternal::kTypeBits );
        // 연결 값(두 소금의 섞음) — 같은 주소의 옛 연결 · 위조 패킷을 거른다.
        const uint64 token = slot._clientSalt ^ slot._serverSalt;
        writer.writeBits( static_cast<uint32>( token ), 32 );
        // 대역폭 몫을 다 썼으면 머리(확인 · 유지)만 — 메시지는 다음 차례에 간다.
        const int32 maxBytes = slot._sendCredit > 0.0 ? kNetMaxPacketSize - NetHostInternal::kHeaderSize : 0;
        slot._connection.writePacket( time, writer, maxBytes, _settings._keepAliveInterval );
        sendFramed( slot._address, _protocolId );
        slot._sendCredit -= static_cast<float64>( writer.getByteCount() + NetHostInternal::kHeaderSize );
        slot._lastSendTime = time;
    }

    void NetHost::refillSendCredit( float64 time, Slot& slot ) const
    {
        const float64 rate = static_cast<float64>( MathUtil::max( kNetMaxPacketSize, _settings._maxBytesPerSecond ) );
        // 쌓아 둘 수 있는 몫은 보내기 간격 두 번어치(최소 패킷 하나) — 한가했다고 몰아 보내 회선을 넘치게 하지 않는다.
        const float64 capacity = MathUtil::max( static_cast<float64>( kNetMaxPacketSize ), rate * _settings._sendInterval * 2.0 );
        if ( slot._lastCreditTime < 0.0 )
            slot._sendCredit = capacity;
        else
            slot._sendCredit = MathUtil::min( capacity, slot._sendCredit + rate * MathUtil::max( 0.0, time - slot._lastCreditTime ) );
        slot._lastCreditTime = time;
    }

    bool NetHost::waitForReceive( float64 timeoutSeconds )
    {
        INetTransport* pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            pTransport = _pTransport;
        }
        if ( pTransport != nullptr )
            return pTransport->waitForReceive( timeoutSeconds );
        std::this_thread::sleep_for( std::chrono::duration<float64>( timeoutSeconds ) );
        return false;
    }

    void NetHost::update( float64 time )
    {
        const uint32 depth = _updateDepth.fetch_add( 1, std::memory_order_acq_rel );
        SW_ASSERT( depth == 0 && "NetHost::update called from two threads at once" );
        (void)depth;
        INetTransport* pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            pTransport = _pTransport;
        }
        if ( pTransport != nullptr )
        {
            // 1) 받기 — 잠금 밖. 받은 버퍼는 다시 쓴다.
            pTransport->update( time );
            int32 receivedCount = 0;
            while ( receivedCount < kMaxDatagramPerUpdate )
            {
                if ( receivedCount == static_cast<int32>( _listReceived.size() ) )
                    _listReceived.emplace_back();
                ReceivedDatagram& datagram = _listReceived[static_cast<size_t>( receivedCount )];
                if ( pTransport->receive( datagram._from, datagram._buffer ) == false )
                    break;
                ++receivedCount;
            }
            // 2) 처리 · 타임아웃 · 보낼 패킷 — 잠금 안.
            {
                std::scoped_lock<mutex> lock{ _mutex };
                for ( int32 index = 0; index < receivedCount; ++index )
                {
                    const ReceivedDatagram& datagram = _listReceived[static_cast<size_t>( index )];
                    handlePacket( time, datagram._from, datagram._buffer.data(), static_cast<int32>( datagram._buffer.size() ) );
                }
                updateSlots( time );
                takePending( _flushBatch, _listDeliver );
            }
            // 3) 보내기 · 비동기 연결 결과 — 잠금 밖.
            sendBatch( _flushBatch );
            deliverFinished( _listDeliver );
        }
        _updateDepth.fetch_sub( 1, std::memory_order_acq_rel );
    }

    void NetHost::updateSlots( float64 time )
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            Slot& slot = _listSlot[index];
            switch ( slot._state )
            {
                case NetConnectionState::Connecting:
                {
                    if ( slot._connectStartTime < 0.0 )
                        slot._connectStartTime = time;
                    if ( time - slot._connectStartTime > _settings._connectTimeout )
                    {
                        closeSlot( static_cast<int32>( index ), NetDisconnectReason::Timeout, false );
                        break;
                    }
                    if ( slot._lastSendTime < 0.0 || time - slot._lastSendTime >= _settings._connectRetryInterval )
                    {
                        // 도전을 받았으면 응답을, 아니면 요청을 되풀이한다.
                        if ( slot._serverSalt != 0 )
                            sendChallengeResponse( slot );
                        else
                            sendControl( slot._address, PacketType::ConnectRequest, slot._clientSalt,
                                         ( static_cast<uint64>( _settings._gameId ) << 32 ) | _protocolId );
                        slot._lastSendTime = time;
                    }
                    break;
                }
                case NetConnectionState::Connected:
                {
                    if ( time - slot._lastReceiveTime > _settings._timeout )
                    {
                        closeSlot( static_cast<int32>( index ), NetDisconnectReason::Timeout, false );
                        break;
                    }
                    // 보낼 것이 있으면 `_sendInterval` 마다, 없으면 `_keepAliveInterval` 마다만(유지 · RTT · 상대의 확인용). 대역폭 몫을 다 썼으면
                    // 메시지는 기다리고 확인할 것만 "보낼 것" 이다.
                    refillSendCredit( time, slot );
                    const bool    bCanCarry = slot._sendCredit > 0.0;
                    const bool    bHasData  = bCanCarry ? slot._connection.hasDataToSend( time ) : slot._connection.isAckPending();
                    const float64 sinceSend = slot._lastSendTime < 0.0 ? 1.0e9 : time - slot._lastSendTime;
                    const bool    bDue      = sinceSend >= _settings._sendInterval && ( sinceSend >= _settings._keepAliveInterval || bHasData );
                    if ( bDue == false )
                        break;
                    sendPayload( time, slot );
                    // 몫이 남고 실을 것이 더 있으면 같은 차례에 더 — 큰 신뢰 메시지 · 몰린 스냅샷의 속도는 상한이 정한다.
                    for ( int32 packetCount = 1; packetCount < kMaxPacketsPerSend && slot._sendCredit > 0.0 && slot._connection.hasDataToSend( time );
                          ++packetCount )
                        sendPayload( time, slot );
                    break;
                }
                case NetConnectionState::Disconnected:
                case NetConnectionState::Disconnecting:
                {
                    break;
                }
            }
        }
    }

    void NetHost::handlePacket( float64 time, const NetAddress& from, const uint8* pData, int32 size )
    {
        const uint32 headerId = size > NetHostInternal::kHeaderSize ? NetHostInternal::readUint32( pData ) : 0u;
        if ( size <= NetHostInternal::kHeaderSize || ( headerId != _protocolId && headerId != NetProtocol::kHandshakeId ) ||
             NetHostInternal::readUint32( pData + 4 ) !=
                 computePacketChecksum( headerId, pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize ) )
        {
            ++_rejectedPacketCount; // 다른 판 · 다른 게임 · 깨진 패킷
            return;
        }
        BitReader        reader( pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize );
        const PacketType type = static_cast<PacketType>( reader.readBits( NetHostInternal::kTypeBits ) );
        if ( type >= PacketType::Count || isHandshakeFramed( type ) != ( headerId == NetProtocol::kHandshakeId ) )
        {
            ++_rejectedPacketCount;
            return;
        }
        const int32 slotIndex = findSlotByAddress( from );
        if ( isEncrypted() && ( type == PacketType::Payload || type == PacketType::Disconnect ) )
        {
            handleSealedPacket( time, type, slotIndex, pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize );
            return;
        }
        if ( type == PacketType::Payload )
        {
            const uint32 token = reader.readBits( 32 );
            if ( isValidSlot( slotIndex ) == false || _listSlot[static_cast<size_t>( slotIndex )]._state != NetConnectionState::Connected )
            {
                // 서버는 수락했지만 클라이언트가 Accepted 를 잃었다 — 데이터 패킷으로는 연결로 치지 않는다(수락에만 서버가 준 번호가 있다).
                // 응답을 바로 다시 보내면 서버가 수락을 다시 보낸다. 이 패킷의 신뢰 메시지는 확인하지 않았으니 서버가 다시 보낸다.
                Slot* pSlot = isValidSlot( slotIndex ) ? &_listSlot[static_cast<size_t>( slotIndex )] : nullptr;
                if ( _bServer == SW_FALSE && pSlot != nullptr && pSlot->_state == NetConnectionState::Connecting && pSlot->_serverSalt != 0 &&
                     token == static_cast<uint32>( pSlot->_clientSalt ^ pSlot->_serverSalt ) )
                {
                    sendChallengeResponse( *pSlot );
                    pSlot->_lastSendTime = time;
                    return;
                }
                ++_rejectedPacketCount;
                return;
            }
            Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
            if ( token != static_cast<uint32>( slot._clientSalt ^ slot._serverSalt ) )
            {
                ++_rejectedPacketCount;
                return;
            }
            if ( slot._connection.readPacket( time, reader ) )
                slot._lastReceiveTime = time;
            return;
        }

        const uint64 valueA = static_cast<uint64>( reader.readBits( 32 ) ) | ( static_cast<uint64>( reader.readBits( 32 ) ) << 32 );
        const uint64 valueB = static_cast<uint64>( reader.readBits( 32 ) ) | ( static_cast<uint64>( reader.readBits( 32 ) ) << 32 );
        if ( reader.hasOverflowed() )
        {
            ++_rejectedPacketCount;
            return;
        }
        switch ( type )
        {
            case PacketType::ConnectRequest:
            {
                if ( _bServer == SW_FALSE )
                    return;
                const uint32 requestProtocolId = static_cast<uint32>( valueB );
                if ( requestProtocolId != _protocolId )
                {
                    const uint32        requestGameId = static_cast<uint32>( valueB >> 32 );
                    NetDisconnectReason reason        = requestGameId == _settings._gameId ? NetDisconnectReason::VersionMismatch : NetDisconnectReason::Rejected;
                    // 같은 게임 · 같은 판인데 기능 마스크만 다르면 — 한쪽만 암호화 · 토큰 결속이다.
                    for ( uint32 mask = 0; reason == NetDisconnectReason::VersionMismatch && mask <= NetProtocolFeature::kAllMask; ++mask )
                    {
                        if ( NetProtocol::makeProtocolId( _settings._gameId, _settings._wireVersion, mask ) == requestProtocolId )
                            reason = NetDisconnectReason::SecurityMismatch;
                    }
                    ++_mismatchLogCount;
                    if ( ( _mismatchLogCount & ( _mismatchLogCount - 1 ) ) == 0 )
                        SW_LOG_WARNING( "NetHost: refused a connection from %# (%#) — it speaks protocol 0x%08x (game 0x%08x), this server 0x%08x (game 0x%08x); %# such requests so far",
                                        from.toString().c_str(), toString( reason ), requestProtocolId, requestGameId, _protocolId, _settings._gameId,
                                        _mismatchLogCount );
                    sendDenied( from, reason, valueA );
                    return;
                }
                if ( isValidSlot( slotIndex ) )
                {
                    // 이 주소는 이미 연결돼 있다 — 같은 요청의 늦은 재전송이면 수락을 다시, 다른 소금(새로 시작한 클라이언트)이면 옛 연결이 끝날 때까지 답하지 않는다.
                    const Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                    if ( slot._clientSalt == valueA )
                        sendAccepted( slotIndex );
                    return;
                }
                if ( findFreeSlot() < 0 )
                {
                    sendDenied( from, NetDisconnectReason::ServerFull, valueA );
                    return;
                }
                // 자리를 잡지 않는다 — 도전 값만 돌려준다. 그 값은 이 주소로 간 패킷에만 있으므로, 되돌려 준 응답이 와야 주소가 진짜다.
                sendControl( from, PacketType::Challenge, valueA, makeChallengeToken( from, valueA, computeChallengeWindow( time ) ) );
                return;
            }
            case PacketType::Challenge:
            {
                if ( _bServer || isValidSlot( slotIndex ) == false )
                    return;
                Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                if ( slot._state != NetConnectionState::Connecting || valueA != slot._clientSalt )
                    return;
                slot._serverSalt = valueB;
                sendChallengeResponse( slot );
                slot._lastSendTime = time;
                return;
            }
            case PacketType::ChallengeResponse:
            {
                if ( _bServer == SW_FALSE )
                    return;
                if ( isValidSlot( slotIndex ) )
                {
                    // 이미 연결됐다 — 수락을 잃은 클라이언트의 되풀이다.
                    Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                    if ( slot._clientSalt == valueA && slot._serverSalt == valueB )
                    {
                        // 확장 바이트(공개 키 · 토큰)는 다시 읽지 않는다 — 기억한 키 확인 태그로 같은 수락을 보낸다.
                        slot._lastReceiveTime = time;
                        sendAccepted( slotIndex );
                    }
                    return;
                }
                // 도전 값을 다시 만들어 맞춰 본다 — 이번 칸이나 바로 앞 칸에 만든 것만(5~10 초).
                const int64 window = computeChallengeWindow( time );
                if ( valueB != makeChallengeToken( from, valueA, window ) && valueB != makeChallengeToken( from, valueA, window - 1 ) )
                {
                    ++_rejectedPacketCount; // 위조 · 만료된 응답
                    return;
                }
                const int32 freeIndex = findFreeSlot();
                if ( freeIndex < 0 )
                {
                    sendDenied( from, NetDisconnectReason::ServerFull, valueA );
                    return;
                }
                // 암호화 — 주소가 확인된(도전을 통과한) 응답에만 X25519 를 계산한다. 토큰 결속이면 증명이 맞아야 자리를 잡는다.
                unique_ptr<SlotSecurity> security;
                if ( isEncrypted() && acceptSecureResponse( reader, from, valueA, valueB, security ) == false )
                    return; // 거절은 그 안에서 보냈다
                Slot& slot    = _listSlot[static_cast<size_t>( freeIndex )];
                slot          = Slot{};
                slot._address = from;
                bindAddress( freeIndex, from );
                slot._clientSalt      = valueA;
                slot._serverSalt      = valueB;
                slot._state           = NetConnectionState::Connected;
                slot._lastReceiveTime = time;
                slot._lastSendTime    = -1.0;
                slot._connection.reset();
                slot._security = std::move( security );
                if ( slot._security != nullptr )
                {
                    // 키 확인 태그 — 서버 → 클라이언트 키의 빈 봉인. 수락을 다시 보낼 때도 같은 태그.
                    uint8 arrNonce[NetSecurityConstant::kAeadNonceSize]{};
                    uint8 arrAad[NetHostInternal::kKeyConfirmAadSize]{};
                    NetSessionKeyUtil::makeNonce( slot._security->_keys._serverToClient._arrIv, NetSessionKeyUtil::kKeyConfirmPacketNumber, arrNonce );
                    NetHostInternal::makeKeyConfirmAad( static_cast<uint32>( freeIndex ), slot._serverSalt, arrAad );
                    (void)slot._security->_sendAead->seal( arrNonce, arrAad, NetHostInternal::kKeyConfirmAadSize, nullptr, 0, slot._security->_arrKeyConfirmTag );
                }
                pushEvent( NetHostEvent{ freeIndex, NetDisconnectReason::None, NetHostEvent::Kind::Connected } );
                sendAccepted( freeIndex );
                return;
            }
            case PacketType::Accepted:
            {
                if ( _bServer || isValidSlot( slotIndex ) == false )
                    return;
                Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                if ( slot._state != NetConnectionState::Connecting || valueB != slot._serverSalt )
                    return;
                if ( slot._security != nullptr && acceptSecureAccepted( reader, slot, slotIndex, static_cast<uint32>( valueA ) ) == false )
                    return; // 키 확인이 틀렸다 — 그 안에서 닫았다
                slot._state           = NetConnectionState::Connected;
                slot._lastReceiveTime = time;
                slot._lastSendTime    = -1.0;
                slot._connection.reset();
                _clientIndex = static_cast<int32>( valueA );
                pushEvent( NetHostEvent{ 0, NetDisconnectReason::None, NetHostEvent::Kind::Connected } );
                return;
            }
            case PacketType::Denied:
            {
                if ( _bServer || isValidSlot( slotIndex ) == false )
                    return;
                const Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                if ( slot._state != NetConnectionState::Connecting || valueB != slot._clientSalt )
                    return; // 이 요청에 대한 거절이 아니다(옛 요청 · 위조)
                const NetDisconnectReason reason = NetHostInternal::readDeniedReason( valueA );
                if ( reason != NetDisconnectReason::ServerFull )
                    SW_LOG_WARNING( "NetHost: server %# refused the connection (%#) — it speaks protocol 0x%08x, this client 0x%08x (game 0x%08x)",
                                    from.toString().c_str(), toString( reason ), static_cast<uint32>( valueA >> 32 ), _protocolId, _settings._gameId );
                closeSlot( slotIndex, reason, false );
                return;
            }
            case PacketType::Disconnect:
            {
                if ( isValidSlot( slotIndex ) == false )
                    return;
                const Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                if ( valueA == ( slot._clientSalt ^ slot._serverSalt ) )
                    closeSlot( slotIndex, NetDisconnectReason::Remote, false );
                return;
            }
            case PacketType::Payload:
            case PacketType::Count:
            {
                return;
            }
        }
    }

    void NetHost::handleSealedPacket( float64 time, PacketType type, int32 slotIndex, const uint8* pBody, int32 bodySize )
    {
        // 몸 = [종류 · 0][연결 값 4][패킷 번호 8][AEAD + 태그] — 바이트 정렬이라 BitReader 를 거치지 않는다.
        const uint32 token  = bodySize >= 5 ? NetHostInternal::readUint32( pBody + 1 ) : 0u;
        Slot*        pSlot  = isValidSlot( slotIndex ) ? &_listSlot[static_cast<size_t>( slotIndex )] : nullptr;
        const bool   bReady = pSlot != nullptr && pSlot->_state == NetConnectionState::Connected && pSlot->_security != nullptr &&
                            pSlot->_security->_bKeysReady == SW_TRUE && token == static_cast<uint32>( pSlot->_clientSalt ^ pSlot->_serverSalt );
        if ( bReady == false )
        {
            // 평문 갈래와 같다 — 클라이언트가 Accepted 를 잃었으면 응답을 다시 보낸다(서버가 수락을 다시 보낸다), 아니면 버린다.
            if ( type == PacketType::Payload && _bServer == SW_FALSE && pSlot != nullptr && pSlot->_state == NetConnectionState::Connecting && pSlot->_serverSalt != 0 &&
                 token == static_cast<uint32>( pSlot->_clientSalt ^ pSlot->_serverSalt ) )
            {
                sendChallengeResponse( *pSlot );
                pSlot->_lastSendTime = time;
                return;
            }
            ++_rejectedPacketCount;
            return;
        }
        if ( openSealed( *pSlot, pBody, bodySize ) == false )
            return; // 변조 · 재전송 — 세고 버린다(연결은 산다)
        if ( type == PacketType::Disconnect )
        {
            if ( _listOpenScratch.empty() )
                closeSlot( slotIndex, NetDisconnectReason::Remote, false );
            return;
        }
        BitReader plainReader( _listOpenScratch.data(), static_cast<int32>( _listOpenScratch.size() ) );
        if ( pSlot->_connection.readPacket( time, plainReader ) )
            pSlot->_lastReceiveTime = time;
    }

    void NetHost::closeSlot( int32 slotIndex, NetDisconnectReason reason, bool bNotifyRemote )
    {
        Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
        if ( slot._state == NetConnectionState::Disconnected )
            return;
        if ( bNotifyRemote && slot._state == NetConnectionState::Connected )
        {
            // 끊김 알림은 잃을 수 있으니 몇 번 보낸다(못 받아도 저쪽은 타임아웃으로 안다). 암호화면 빈 평문의 봉인이라 엿본 소금으로 위조하지 못한다.
            for ( int32 repeat = 0; repeat < 3; ++repeat )
            {
                if ( slot._security == nullptr )
                    sendControl( slot._address, PacketType::Disconnect, slot._clientSalt ^ slot._serverSalt, 0 );
                else if ( slot._security->_bKeysReady == SW_TRUE )
                    (void)sendSealed( slot, PacketType::Disconnect, nullptr, 0 );
            }
        }
        const bool bWasVisible = slot._state == NetConnectionState::Connected || _bServer == SW_FALSE;
        unbindAddress( slotIndex );
        slot._state = NetConnectionState::Disconnected;
        slot._connection.reset();
        if ( slot._security != nullptr )
        {
            slot._security->_keys.wipe();
            slot._security->_keyPair.wipe();
            slot._security.reset();
        }
        if ( bWasVisible )
            pushEvent( NetHostEvent{ slotIndex, reason, NetHostEvent::Kind::Disconnected } );
        if ( _bServer == SW_FALSE )
            _clientIndex = -1;
    }

    bool NetHost::sendMessage( int32 connectionId, NetChannelType channel, const uint8* pData, int32 size )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return sendMessageLocked( connectionId, channel, pData, size );
    }

    bool NetHost::sendMessageLocked( int32 connectionId, NetChannelType channel, const uint8* pData, int32 size )
    {
        if ( isValidSlot( connectionId ) == false || _listSlot[static_cast<size_t>( connectionId )]._state != NetConnectionState::Connected )
            return false;
        return _listSlot[static_cast<size_t>( connectionId )]._connection.sendMessage( channel, pData, size );
    }

    int32 NetHost::broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptId )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        int32                   sentCount = 0;
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( static_cast<int32>( index ) != exceptId && sendMessageLocked( static_cast<int32>( index ), channel, pData, size ) )
                ++sentCount;
        }
        return sentCount;
    }

    bool NetHost::receiveMessage( int32& outConnectionId, NetChannelType& outChannel, vector<uint8>& outBuffer )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const int32             slotCount = static_cast<int32>( _listSlot.size() );
        for ( int32 step = 0; step < slotCount; ++step )
        {
            const int32 index = ( _receiveCursor + step ) % slotCount;
            Slot&       slot  = _listSlot[static_cast<size_t>( index )];
            if ( slot._state != NetConnectionState::Connected )
                continue;
            for ( int32 channel = 0; channel < static_cast<int32>( NetChannelType::Count ); ++channel )
            {
                if ( slot._connection.receiveMessage( static_cast<NetChannelType>( channel ), outBuffer ) )
                {
                    outConnectionId = index;
                    outChannel      = static_cast<NetChannelType>( channel );
                    _receiveCursor  = index; // 같은 연결부터 이어서 비운다
                    return true;
                }
            }
        }
        _receiveCursor = slotCount > 0 ? ( _receiveCursor + 1 ) % slotCount : 0;
        return false;
    }

    void NetHost::disconnect( int32 connectionId )
    {
        // 끊김 알림은 바로 보낸다(곧 호스트를 없앨 수 있다) — 이 스레드의 사본으로, 잠금 밖에서.
        OutgoingBatch           batch;
        vector<FinishedConnect> listFinished;
        INetTransport*          pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( isValidSlot( connectionId ) )
                closeSlot( connectionId, NetDisconnectReason::Requested, true );
            takePending( batch, listFinished );
            pTransport = _pTransport;
        }
        if ( pTransport != nullptr )
        {
            for ( const OutgoingDatagram& datagram : batch._listDatagram )
                (void)pTransport->send( datagram._to, batch._bytes.data() + datagram._offset, datagram._size );
        }
        deliverFinished( listFinished );
    }

    void NetHost::disconnectAll()
    {
        OutgoingBatch           batch;
        vector<FinishedConnect> listFinished;
        INetTransport*          pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( size_t index = 0; index < _listSlot.size(); ++index )
                closeSlot( static_cast<int32>( index ), NetDisconnectReason::Requested, true );
            takePending( batch, listFinished );
            pTransport = _pTransport;
        }
        if ( pTransport != nullptr )
        {
            for ( const OutgoingDatagram& datagram : batch._listDatagram )
                (void)pTransport->send( datagram._to, batch._bytes.data() + datagram._offset, datagram._size );
        }
        deliverFinished( listFinished );
    }

    void NetHost::drainEvents( vector<NetHostEvent>& outListEvent )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    void NetHost::drainInbound( NetInbound& outInbound )
    {
        outInbound.clear();
        std::scoped_lock<mutex> lock{ _mutex };
        outInbound._listEvent.swap( _listEvent );
        const int32 slotCount = static_cast<int32>( _listSlot.size() );
        for ( int32 index = 0; index < slotCount; ++index )
        {
            Slot& slot = _listSlot[static_cast<size_t>( index )];
            if ( slot._state != NetConnectionState::Connected )
                continue;
            for ( int32 channel = 0; channel < static_cast<int32>( NetChannelType::Count ); ++channel )
            {
                while ( slot._connection.receiveMessage( static_cast<NetChannelType>( channel ), _listDrainScratch ) )
                {
                    NetInboundMessage message;
                    message._connectionId = index;
                    message._offset       = static_cast<int32>( outInbound._bytes.size() );
                    message._size         = static_cast<int32>( _listDrainScratch.size() );
                    message._channel      = static_cast<NetChannelType>( channel );
                    outInbound._bytes.insert( outInbound._bytes.end(), _listDrainScratch.begin(), _listDrainScratch.end() );
                    outInbound._listMessage.push_back( message );
                }
            }
        }
    }

    bool NetHost::isServer() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _bServer != SW_FALSE;
    }

    int32 NetHost::getClientIndex() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _clientIndex;
    }

    uint64 NetHost::getRejectedPacketCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _rejectedPacketCount;
    }

    uint64 NetHost::getConnectionPrincipal( int32 connectionId ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bServer == SW_FALSE || isValidSlot( connectionId ) == false )
            return 0;
        const Slot& slot = _listSlot[static_cast<size_t>( connectionId )];
        return slot._state == NetConnectionState::Connected && slot._security != nullptr ? slot._security->_principalId : 0;
    }

    uint64 NetHost::getAuthenticationFailureCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _authenticationFailureCount;
    }

    uint64 NetHost::getReplayRejectedCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _replayRejectedCount;
    }

    uint32 NetHost::getProtocolId() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _protocolId;
    }

    int32 NetHost::getMaxBytesPerSecond() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _settings._maxBytesPerSecond;
    }

    bool NetHost::getConnectionStats( int32 connectionId, NetConnectionStats& outStats ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( isValidSlot( connectionId ) == false || _listSlot[static_cast<size_t>( connectionId )]._state != NetConnectionState::Connected )
            return false;
        outStats = _listSlot[static_cast<size_t>( connectionId )]._connection.getStats();
        return true;
    }

    NetConnectionState NetHost::getConnectionState( int32 connectionId ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return isValidSlot( connectionId ) ? _listSlot[static_cast<size_t>( connectionId )]._state : NetConnectionState::Disconnected;
    }

    const NetConnection* NetHost::findConnection( int32 connectionId ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return isValidSlot( connectionId ) && _listSlot[static_cast<size_t>( connectionId )]._state == NetConnectionState::Connected ? &_listSlot[static_cast<size_t>( connectionId )]._connection : nullptr;
    }

    NetAddress NetHost::getConnectionAddress( int32 connectionId ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return isValidSlot( connectionId ) ? _listSlot[static_cast<size_t>( connectionId )]._address : NetAddress{};
    }

    int32 NetHost::getConnectedCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        int32                   count = 0;
        for ( const Slot& slot : _listSlot )
            count += slot._state == NetConnectionState::Connected ? 1 : 0;
        return count;
    }

    void NetHost::collectConnected( vector<int32>& outListConnection ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outListConnection.clear();
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._state == NetConnectionState::Connected )
                outListConnection.push_back( static_cast<int32>( index ) );
        }
    }
} // namespace sw
