#include "pch.h"

#include "Core/Network/Connection/NetHost.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/LittleEndianUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Process/Process.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace sw
{
    namespace
    {
        struct NetHostInternal
        {
            static constexpr int32 kTypeBits   = 3;
            static constexpr int32 kHeaderSize = 8; ///< 프로토콜 ID 4바이트와 체크섬 4바이트.
#if !defined( SW_SHIPPING )
            /** @brief 디버거 대기 중 `update` 사이가 이 값보다 벌어지면 프로세스가 멈췄던 것으로 보고 연결 시계를 민다. 평소 간격(수 ms)보다 훨씬 길게 잡는다. */
            static constexpr float64 kDebugStallSeconds = 0.5;
            /** @brief 디버거가 붙었는지 다시 확인하는 간격입니다. Linux 에서는 파일을 읽어 확인합니다. */
            static constexpr float64 kDebuggerPollSeconds = 1.0;
#endif

            /** @brief 거절 패킷이 싣는 이유를 읽습니다. 모르는 값은 Rejected 로 읽으므로, 다른 버전이 이유를 늘려도 클라이언트는 연결이 끊깁니다. */
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
        , _slotTable{}
        , _challenge{}
        , _sealer{}
        , _listEvent{}
        , _pendingBatch{}
        , _packetWriter{}
        , _connectPromise{}
        , _listFinished{}
        , _listReceived{}
        , _flushBatch{}
        , _listDeliver{}
        , _plainWriter{}
        , _credentials{}
        , _settings{}
        , _sentKindTally{}
        , _pTransport{ nullptr }
        , _rejectedPacketCount{ 0 }
        , _authenticationFailureCount{ 0 }
        , _replayRejectedCount{ 0 }
        , _mismatchLogCount{ 0 }
        , _lastUpdateTime{ -1.0 }
        , _nextDebuggerPollTime{ 0.0 }
        , _protocolID{ 0 }
        , _updateDepth{ 0 }
        , _clientIndex{ -1 }
        , _receiveCursor{ 0 }
        , _bServer{ SW_FALSE }
        , _bConnectPending{ SW_FALSE }
        , _bHasCredentials{ SW_FALSE }
        , _bDebugHold{ SW_FALSE }
    {
    }

    NetHost::~NetHost() = default;

    void NetHost::initialize( INetTransport* pTransport, const NetHostConfig& settings )
    {
        vector<FinishedConnect> listFinished;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested );
            _pTransport = pTransport;
            _settings   = settings;
            _challenge.initialize( settings._saltSeed );
            _slotTable.reset( 0 );
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
            _bDebugHold                 = SW_FALSE;
            _lastUpdateTime             = -1.0;
            _nextDebuggerPollTime       = 0.0;
            _credentials                = NetConnectCredentials{};
            _plainWriter.reserve( kNetMaxPacketSize );
            _protocolID = NetProtocol::makeProtocolID( settings._gameID, settings._wireVersion, computeFeatureMask() ); // `listen` 과 `connect` 가 역할에 맞춰 다시 계산한다.
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

    uint32 NetHost::computePacketChecksum( uint32 headerID, const uint8* pBody, int32 size )
    {
        // FNV-1a 32비트. 헤더 값(프로토콜 ID)을 먼저 섞으므로, 다른 게임이나 버전의 패킷은 체크섬부터 맞지 않는다.
        uint32 hash = HashUtil::kFnvOffset32;
        for ( int32 shift = 0; shift < 32; shift += 8 )
        {
            hash = HashUtil::fnvStep32( hash, ( headerID >> shift ) & 0xFFu );
        }
        return HashUtil::appendBytes32( hash, pBody, size > 0 ? static_cast<size_t>( size ) : 0 );
    }

    uint32 NetHost::computeFeatureMask() const
    {
        if ( isEncrypted() == false )
            return 0;
        // 토큰 결속은 서버에 인증기가 있을 때, 클라이언트가 자격을 제시할 때 켠다. 한쪽만 켜면 프로토콜 ID가 달라져 SecurityMismatch 가 된다.
        const bool bTokenBound = _bServer == SW_TRUE ? _settings._security._pAuthenticator != nullptr : _bHasCredentials == SW_TRUE;
        return NetProtocolFeature::kEncrypted | ( bTokenBound ? NetProtocolFeature::kTokenBound : 0u );
    }

    bool NetHost::listen()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _pTransport == nullptr )
            return false;
        if ( isEncrypted() && _settings._security._pProvider == nullptr )
        {
            SW_LOG_ERROR( "NetHost: encrypted mode needs a security provider (NetSecurity::getProvider())" );
            return false;
        }
#if defined( SW_SHIPPING )
        if ( isEncrypted() && _settings._security._pAuthenticator == nullptr )
        {
            // 인증기 없는 암호화(일회용 키)는 도청만 막고 중간자 공격은 막지 못하므로 개발 빌드에서만 쓴다.
            SW_LOG_ERROR( "NetHost: a shipping server must bind encrypted connections to session tokens (set NetSecurityConfig::_pAuthenticator)" );
            return false;
        }
#endif
        _bServer    = SW_TRUE;
        _protocolID = NetProtocol::makeProtocolID( _settings._gameID, _settings._wireVersion, computeFeatureMask() );
        _slotTable.reset( MathUtil::max( 1, _settings._maxConnections ) );
        return true;
    }

    bool NetHost::connect( const NetAddress& serverAddress )
    {
        return connectLocked( serverAddress, nullptr );
    }

    bool NetHost::connect( const NetAddress& serverAddress, const NetConnectCredentials& credentials )
    {
        return connectLocked( serverAddress, &credentials );
    }

    bool NetHost::connectLocked( const NetAddress& serverAddress, const NetConnectCredentials* pCredentials )
    {
        vector<FinishedConnect> listFinished;
        bool                    bStarted = false;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested ); // 앞의 비동기 연결은 여기서 끝난다.
            _credentials     = pCredentials != nullptr ? *pCredentials : NetConnectCredentials{};
            _bHasCredentials = pCredentials != nullptr && pCredentials->_tokenSize > 0 ? SW_TRUE : SW_FALSE;
            bStarted         = startConnect( serverAddress );
            listFinished.swap( _listFinished );
        }
        deliverFinished( listFinished );
        return bStarted;
    }

    TaskFuture<NetConnectResult> NetHost::connectAsync( const NetAddress& serverAddress )
    {
        return connectAsyncWith( serverAddress, nullptr );
    }

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
        _protocolID = NetProtocol::makeProtocolID( _settings._gameID, _settings._wireVersion, computeFeatureMask() );
        _slotTable.reset( 1 );
        _slotTable.bindAddress( 0, serverAddress );
        NetHostSlot& slot      = _slotTable.get( 0 );
        slot._clientSalt       = _challenge.nextSalt();
        slot._state            = NetConnectionState::Connecting;
        slot._connectStartTime = -1.0; // 첫 update 에서 시각을 정한다.
        slot._lastSendTime     = -1.0;
        if ( isEncrypted() )
        {
            slot._security = make_unique<NetSlotSecurity>();
            if ( _settings._security._pProvider == nullptr || _settings._security._pProvider->makeX25519KeyPair( slot._security->_keyPair ) == false )
            {
                SW_LOG_ERROR( "NetHost: cannot create a key pair - is NetSecurityConfig::_pProvider set?" );
                closeSlot( 0, NetDisconnectReason::Rejected, false );
                return false;
            }
        }
        return true;
    }

    void NetHost::sendFramed( const NetAddress& to, uint32 headerID )
    {
        // 보낼 배치의 바이트 뒤에 바로 만든다(헤더와 본문). 잠금을 푼 뒤 `sendBatch` 가 보낸다.
        const vector<uint8>& body     = _packetWriter.getBytes();
        const int32          bodySize = _packetWriter.getByteCount();
        const size_t         offset   = _pendingBatch._bytes.size();
        _pendingBatch._bytes.resize( offset + static_cast<size_t>( NetHostInternal::kHeaderSize + bodySize ) );
        uint8* pPacket = _pendingBatch._bytes.data() + offset;
        if ( bodySize > 0 )
            std::memcpy( pPacket + NetHostInternal::kHeaderSize, body.data(), static_cast<size_t>( bodySize ) );
        LittleEndianUtil::writeUint32( _pendingBatch._bytes.data() + offset, headerID );
        LittleEndianUtil::writeUint32( _pendingBatch._bytes.data() + offset + 4, computePacketChecksum( headerID, pPacket + NetHostInternal::kHeaderSize, bodySize ) );
        _pendingBatch._listDatagram.push_back( OutgoingDatagram{ to, static_cast<int32>( offset ), NetHostInternal::kHeaderSize + bodySize } );
    }

    void NetHost::sendBatch( INetTransport* pTransport, const OutgoingBatch& batch )
    {
        if ( pTransport == nullptr )
            return;
        for ( const OutgoingDatagram& datagram : batch._listDatagram )
        {
            (void)pTransport->send( datagram._to, batch._bytes.data() + datagram._offset, datagram._size ); // 실패는 손실과 같다(재전송이 맡는다).
        }
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
        {
            finished._promise.setValue( finished._result );
        }
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
        sendFramed( to, isHandshakeFramed( type ) ? NetProtocol::kHandshakeID : _protocolID );
    }

    void NetHost::sendDenied( const NetAddress& to, NetDisconnectReason reason, uint64 clientSalt )
    {
        sendControl( to, PacketType::Denied, static_cast<uint64>( reason ) | ( static_cast<uint64>( _protocolID ) << 32 ), clientSalt );
    }

    void NetHost::sendChallengeResponse( NetHostSlot& slot )
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
                // 증명은 세션 비밀로 유도한 키로 빈 데이터를 봉인한 태그다. AAD가 이 응답의 솔트, 챌린지 값, 공개 키, 토큰이라 다른 응답에 옮겨 붙일 수 없다.
                uint8 arrTag[NetSecurityConstant::kAeadTagSize]{};
                (void)NetPacketSealer::computeProofTag( *_settings._security._pProvider, _settings._security._algorithm, _credentials._secret, slot._clientSalt,
                                                        slot._serverSalt, slot._security->_keyPair._arrPublicKey, _credentials._arrToken, _credentials._tokenSize,
                                                        arrTag ); // 실패하면 0 태그가 나가고, 서버가 증명 실패로 거절한다.
                writer.writeBytes( arrTag, NetSecurityConstant::kAeadTagSize );
            }
        }
        sendFramed( slot._address, _protocolID );
    }

    void NetHost::sendAccepted( int32 slotIndex )
    {
        const NetHostSlot& slot   = _slotTable.get( slotIndex );
        BitWriter&         writer = _packetWriter;
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
        sendFramed( slot._address, _protocolID );
    }

    bool NetHost::acceptSecureResponse( BitReader& reader, const NetAddress& from, uint64 clientSalt, uint64 challenge, unique_ptr<NetSlotSecurity>& outSecurity )
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
        uint64           principalID = 0;
        if ( bTokenBound )
        {
            const int32 tokenSize = static_cast<int32>( listToken.size() );
            const bool  bKnown    = _settings._security._pAuthenticator->findSessionSecret( listToken.data(), tokenSize, secret, principalID ) &&
                                NetPacketSealer::isProofTagValid( provider, _settings._security._algorithm, secret, clientSalt, challenge, arrClientPublic, listToken.data(),
                                                                  tokenSize, arrProof );
            if ( bKnown == false )
            {
                ++_authenticationFailureCount;
                sendDenied( from, NetDisconnectReason::AuthenticationFailed, clientSalt );
                return false;
            }
        }
        unique_ptr<NetSlotSecurity> security = make_unique<NetSlotSecurity>();
        if ( NetPacketSealer::deriveServerKeys( provider, _settings._security._algorithm, arrClientPublic, bTokenBound ? &secret : nullptr, clientSalt, challenge, _protocolID,
                                                *security ) == false )
        {
            ++_authenticationFailureCount; // 작은 차수 공개 키 같은 경우.
            sendDenied( from, NetDisconnectReason::AuthenticationFailed, clientSalt );
            return false;
        }
        security->_principalID = principalID;
        security->_bKeysReady  = SW_TRUE;
        outSecurity            = std::move( security );
        return true;
    }

    bool NetHost::acceptSecureAccepted( BitReader& reader, NetHostSlot& slot, int32 slotIndex, uint32 serverIndex )
    {
        NetSlotSecurity& security = *slot._security;
        uint8            arrServerPublic[NetSecurityConstant::kX25519KeySize]{};
        uint8            arrTag[NetSecurityConstant::kAeadTagSize]{};
        // 키 확인으로 서버가 같은 키(같은 세션 비밀과 같은 공유 비밀)를 가졌는지 본다. 다르면 중간자이거나 비밀이 틀린 것이다.
        const bool bOk = reader.readBytes( arrServerPublic, NetSecurityConstant::kX25519KeySize ) && reader.readBytes( arrTag, NetSecurityConstant::kAeadTagSize ) &&
                         NetPacketSealer::deriveClientKeys( *_settings._security._pProvider, _settings._security._algorithm, arrServerPublic,
                                                            _bHasCredentials == SW_TRUE ? &_credentials._secret : nullptr, slot._clientSalt, slot._serverSalt, _protocolID,
                                                            security ) &&
                         NetPacketSealer::isKeyConfirmTagValid( security, serverIndex, slot._serverSalt, arrTag );
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

    int32 NetHost::sendSealed( NetHostSlot& slot, PacketType type, const uint8* pPlain, int32 plainSize )
    {
        if ( _sealer.seal( *slot._security, static_cast<uint8>( type ), static_cast<uint32>( slot.getConnectionToken() ), _protocolID, _bServer == SW_TRUE, pPlain, plainSize,
                           _packetWriter ) == false )
            return 0;
        sendFramed( slot._address, _protocolID );
        return NetHostInternal::kHeaderSize + _packetWriter.getByteCount();
    }

    void NetHost::sendPayload( float64 nowSeconds, NetHostSlot& slot )
    {
        if ( slot._security != nullptr )
        {
            // 암호화 방식에서는 NetConnection 패킷을 평문으로 만들어 봉인한다. 예산에서 암호화 헤더와 태그 크기를 뺀다.
            _plainWriter.clear();
            _plainWriter.writeBits( _bDebugHold == SW_TRUE ? 1u : 0u, 1 ); // 디버거 대기 비트. 평문 첫 비트이므로 함께 봉인된다.
            const int32 maxBytes =
                slot._sendCredit > 0.0 ? kNetMaxPacketSize - NetHostInternal::kHeaderSize - NetPacketSealer::kSecureHeadSize - NetSecurityConstant::kAeadTagSize : 0;
            slot._connection.writePacket( nowSeconds, _plainWriter, maxBytes, _settings._keepAliveInterval, _settings._maxUnreliableAgeSeconds );
            const int32 sentBytes = sendSealed( slot, PacketType::Payload, _plainWriter.getBytes().data(), _plainWriter.getByteCount() );
            slot._sendCredit -= static_cast<float64>( sentBytes );
            slot._lastSendTime = nowSeconds;
            return;
        }
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( PacketType::Payload ), NetHostInternal::kTypeBits );
        // 연결 값(두 솔트를 섞은 값)으로 같은 주소의 옛 연결이나 위조 패킷을 걸러 낸다.
        writer.writeBits( static_cast<uint32>( slot.getConnectionToken() ), 32 );
        writer.writeBits( _bDebugHold == SW_TRUE ? 1u : 0u, 1 ); // 디버거 대기 비트.
        // 대역폭 토큰을 다 썼으면 헤더(확인과 연결 유지)만 보내고, 메시지는 다음 전송으로 미룬다.
        const int32 maxBytes = slot._sendCredit > 0.0 ? kNetMaxPacketSize - NetHostInternal::kHeaderSize : 0;
        slot._connection.writePacket( nowSeconds, writer, maxBytes, _settings._keepAliveInterval, _settings._maxUnreliableAgeSeconds );
        sendFramed( slot._address, _protocolID );
        slot._sendCredit -= static_cast<float64>( writer.getByteCount() + NetHostInternal::kHeaderSize );
        slot._lastSendTime = nowSeconds;
    }

    void NetHost::refillSendCredit( float64 nowSeconds, NetHostSlot& slot ) const
    {
        const float64 rate = static_cast<float64>( MathUtil::max( kNetMaxPacketSize, _settings._maxBytesPerSecond ) );
        // 쌓아 둘 수 있는 토큰은 보내기 간격 두 번어치(최소 패킷 하나)다. 한가했다고 한꺼번에 몰아 보내 회선을 넘치게 하지 않기 위해서다.
        const float64 capacity = MathUtil::max( static_cast<float64>( kNetMaxPacketSize ), rate * _settings._sendInterval * 2.0 );
        if ( slot._lastCreditTime < 0.0 )
            slot._sendCredit = capacity;
        else
            slot._sendCredit = MathUtil::min( capacity, slot._sendCredit + rate * MathUtil::max( 0.0, nowSeconds - slot._lastCreditTime ) );
        slot._lastCreditTime = nowSeconds;
    }

    void NetHost::updateDebugHold( float64 nowSeconds )
    {
#if defined( SW_SHIPPING )
        (void)nowSeconds;
#else
        switch ( _settings._debugHold )
        {
            case NetDebugHold::Off:
            {
                _bDebugHold = SW_FALSE;
                break;
            }
            case NetDebugHold::Always:
            {
                _bDebugHold = SW_TRUE;
                break;
            }
            case NetDebugHold::WhenDebuggerAttached:
            {
                if ( nowSeconds >= _nextDebuggerPollTime )
                {
                    _bDebugHold           = Process::isDebuggerPresent() ? SW_TRUE : SW_FALSE;
                    _nextDebuggerPollTime = nowSeconds + NetHostInternal::kDebuggerPollSeconds;
                }
                break;
            }
        }
        // 가상 시계. 알리는 중에 update 사이가 크게 벌어졌다면 프로세스가 중단점이나 일시정지로 멈췄던 것이다. 그동안 상대가 보낸 것을 받지 못한 것은
        // 상대 탓이 아니므로, 멈춘 시간만큼 마지막 수신 시각과 연결 시작 시각을 민다. 그러면 재개한 쪽이 상대를 타임아웃으로 끊지 않는다.
        const float64 stalledSeconds = _lastUpdateTime < 0.0 ? 0.0 : nowSeconds - _lastUpdateTime;
        if ( _bDebugHold == SW_TRUE && stalledSeconds > NetHostInternal::kDebugStallSeconds )
        {
            for ( NetHostSlot& slot : _slotTable )
            {
                slot._lastReceiveTime += stalledSeconds;
                if ( slot._connectStartTime >= 0.0 )
                    slot._connectStartTime += stalledSeconds;
            }
        }
#endif
        _lastUpdateTime = nowSeconds;
    }

    float64 NetHost::computeTimeoutSeconds( const NetHostSlot& slot ) const
    {
#if defined( SW_SHIPPING )
        (void)slot;
        return _settings._timeoutSeconds; // 배포 빌드는 알림을 믿지 않는다. 클라이언트가 비트를 켜서 슬롯을 붙잡는 것을 막기 위해서다.
#else
        return slot._bRemoteDebugHold == SW_TRUE ? MathUtil::max( _settings._timeoutSeconds, _settings._debugHoldTimeoutSeconds ) : _settings._timeoutSeconds;
#endif
    }

    bool NetHost::isDebugHoldAnnounced() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _bDebugHold == SW_TRUE;
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

    void NetHost::update( float64 nowSeconds )
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
            // 1) 받기(잠금 밖). 받은 버퍼는 다시 쓴다.
            pTransport->update( nowSeconds );
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
            // 2) 처리, 타임아웃, 보낼 패킷 생성(잠금 안).
            {
                std::scoped_lock<mutex> lock{ _mutex };
                updateDebugHold( nowSeconds );
                for ( int32 index = 0; index < receivedCount; ++index )
                {
                    const ReceivedDatagram& datagram = _listReceived[static_cast<size_t>( index )];
                    handlePacket( nowSeconds, datagram._from, datagram._buffer.data(), static_cast<int32>( datagram._buffer.size() ) );
                }
                updateSlots( nowSeconds );
                takePending( _flushBatch, _listDeliver );
            }
            // 3) 보내기와 비동기 연결 결과 전달(잠금 밖).
            sendBatch( pTransport, _flushBatch );
            deliverFinished( _listDeliver );
        }
        _updateDepth.fetch_sub( 1, std::memory_order_acq_rel );
    }

    void NetHost::updateSlots( float64 nowSeconds )
    {
        const int32 slotCount = _slotTable.getCount();
        for ( int32 index = 0; index < slotCount; ++index )
        {
            NetHostSlot& slot = _slotTable.get( index );
            switch ( slot._state )
            {
                case NetConnectionState::Connecting:
                {
                    if ( slot._connectStartTime < 0.0 )
                        slot._connectStartTime = nowSeconds;
                    if ( nowSeconds - slot._connectStartTime > _settings._connectTimeout )
                    {
                        closeSlot( index, NetDisconnectReason::Timeout, false );
                        break;
                    }
                    if ( slot._lastSendTime < 0.0 || nowSeconds - slot._lastSendTime >= _settings._connectRetryInterval )
                    {
                        // 챌린지를 받았으면 응답을, 아니면 요청을 다시 보낸다.
                        if ( slot._serverSalt != 0 )
                            sendChallengeResponse( slot );
                        else
                            sendControl( slot._address, PacketType::ConnectRequest, slot._clientSalt,
                                         ( static_cast<uint64>( _settings._gameID ) << 32 ) | _protocolID );
                        slot._lastSendTime = nowSeconds;
                    }
                    break;
                }
                case NetConnectionState::Connected:
                {
                    if ( nowSeconds - slot._lastReceiveTime > computeTimeoutSeconds( slot ) )
                    {
                        closeSlot( index, NetDisconnectReason::Timeout, false );
                        break;
                    }
                    // 보낼 것이 있으면 `_sendInterval` 마다, 없으면 `_keepAliveInterval` 마다만 보낸다(연결 유지, RTT, 상대의 확인용). 대역폭 토큰을 다 썼으면
                    // 메시지는 기다리고 확인할 것만 "보낼 것"으로 센다.
                    refillSendCredit( nowSeconds, slot );
                    const bool    bCanCarry = slot._sendCredit > 0.0;
                    const bool    bHasData  = bCanCarry ? slot._connection.hasDataToSend( nowSeconds ) : slot._connection.isAckPending();
                    const float64 sinceSend = slot._lastSendTime < 0.0 ? 1.0e9 : nowSeconds - slot._lastSendTime;
                    const bool    bDue      = sinceSend >= _settings._sendInterval && ( sinceSend >= _settings._keepAliveInterval || bHasData );
                    if ( bDue == false )
                        break;
                    sendPayload( nowSeconds, slot );
                    // 토큰이 남고 실을 것이 더 있으면 같은 전송에서 더 보낸다. 큰 신뢰 메시지나 몰린 스냅샷의 전송 속도는 대역폭 상한이 정한다.
                    for ( int32 packetCount = 1; packetCount < kMaxPacketsPerSend && slot._sendCredit > 0.0 && slot._connection.hasDataToSend( nowSeconds );
                          ++packetCount )
                    {
                        sendPayload( nowSeconds, slot );
                    }
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

    void NetHost::handlePacket( float64 nowSeconds, const NetAddress& from, const uint8* pData, int32 size )
    {
        const uint32 headerID        = size > NetHostInternal::kHeaderSize ? LittleEndianUtil::readUint32( pData ) : 0u;
        const bool   bRejectedPacket = size <= NetHostInternal::kHeaderSize || ( headerID != _protocolID && headerID != NetProtocol::kHandshakeID ) ||
                                     LittleEndianUtil::readUint32( pData + 4 ) !=
                                         computePacketChecksum( headerID, pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize );
        if ( bRejectedPacket )
        {
            ++_rejectedPacketCount; // 다른 버전, 다른 게임, 깨진 패킷.
            return;
        }
        BitReader        reader( pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize );
        const PacketType type = static_cast<PacketType>( reader.readBits( NetHostInternal::kTypeBits ) );
        if ( type >= PacketType::Count || isHandshakeFramed( type ) != ( headerID == NetProtocol::kHandshakeID ) )
        {
            ++_rejectedPacketCount;
            return;
        }
        const int32 slotIndex = _slotTable.findByAddress( from );
        if ( isEncrypted() && ( type == PacketType::Payload || type == PacketType::Disconnect ) )
        {
            handleSealedPacket( nowSeconds, type, slotIndex, pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize );
            return;
        }
        if ( type == PacketType::Payload )
        {
            const uint32 token = reader.readBits( 32 );
            NetHostSlot* pSlot = _slotTable.isValid( slotIndex ) ? &_slotTable.get( slotIndex ) : nullptr;
            if ( pSlot == nullptr || pSlot->_state != NetConnectionState::Connected )
            {
                // 서버는 수락했지만 클라이언트가 Accepted 를 받지 못한 경우다. 수락에만 서버가 지정한 번호가 있으므로, 데이터 패킷으로는 연결로 인정하지 않는다.
                // 응답을 바로 다시 보내면 서버가 수락을 다시 보낸다. 이 패킷에 든 신뢰 메시지는 확인하지 않았으므로 서버가 다시 보낸다.
                const bool bClientAwaitingAccept = _bServer == SW_FALSE && pSlot != nullptr && pSlot->_state == NetConnectionState::Connecting && pSlot->_serverSalt != 0 &&
                                                   token == static_cast<uint32>( pSlot->getConnectionToken() );
                if ( bClientAwaitingAccept )
                {
                    sendChallengeResponse( *pSlot );
                    pSlot->_lastSendTime = nowSeconds;
                    return;
                }
                ++_rejectedPacketCount;
                return;
            }
            NetHostSlot& slot = *pSlot;
            if ( token != static_cast<uint32>( slot.getConnectionToken() ) )
            {
                ++_rejectedPacketCount;
                return;
            }
            const uint8 bRemoteDebugHold = reader.readBits( 1 ) != 0 ? SW_TRUE : SW_FALSE;
            if ( slot._connection.readPacket( nowSeconds, reader ) )
            {
                slot._lastReceiveTime  = nowSeconds;
                slot._bRemoteDebugHold = bRemoteDebugHold;
            }
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
                const uint32 requestProtocolID = static_cast<uint32>( valueB );
                if ( requestProtocolID != _protocolID )
                {
                    const uint32        requestGameID = static_cast<uint32>( valueB >> 32 );
                    NetDisconnectReason reason        = requestGameID == _settings._gameID ? NetDisconnectReason::VersionMismatch : NetDisconnectReason::Rejected;
                    // 같은 게임, 같은 버전인데 기능 마스크만 다르면 한쪽만 암호화나 토큰 결속을 켠 것이다.
                    for ( uint32 mask = 0; reason == NetDisconnectReason::VersionMismatch && mask <= NetProtocolFeature::kAllMask; ++mask )
                    {
                        if ( NetProtocol::makeProtocolID( _settings._gameID, _settings._wireVersion, mask ) == requestProtocolID )
                            reason = NetDisconnectReason::SecurityMismatch;
                    }
                    ++_mismatchLogCount;
                    if ( ( _mismatchLogCount & ( _mismatchLogCount - 1 ) ) == 0 )
                        SW_LOG_WARNING( "NetHost: refused a connection from %# (%#) — it speaks protocol 0x%08x (game 0x%08x), this server 0x%08x (game 0x%08x); %# such requests so far",
                                        from.toString().c_str(), toString( reason ), requestProtocolID, requestGameID, _protocolID, _settings._gameID,
                                        _mismatchLogCount );
                    sendDenied( from, reason, valueA );
                    return;
                }
                if ( _slotTable.isValid( slotIndex ) )
                {
                    // 이 주소는 이미 연결돼 있다. 같은 요청의 늦은 재전송이면 수락을 다시 보내고, 솔트가 다르면(새로 시작한 클라이언트) 옛 연결이 끝날 때까지 답하지 않는다.
                    const NetHostSlot& slot = _slotTable.get( slotIndex );
                    if ( slot._clientSalt == valueA )
                        sendAccepted( slotIndex );
                    return;
                }
                if ( _slotTable.findFreeSlot() < 0 )
                {
                    sendDenied( from, NetDisconnectReason::ServerFull, valueA );
                    return;
                }
                // 슬롯을 할당하지 않고 챌린지 값만 돌려준다. 그 값은 이 주소로 보낸 패킷에만 있으므로, 되돌려 준 응답이 와야 주소가 진짜임이 확인된다.
                sendControl( from, PacketType::Challenge, valueA, _challenge.makeToken( from, valueA, nowSeconds ) );
                return;
            }
            case PacketType::Challenge:
            {
                if ( _bServer || _slotTable.isValid( slotIndex ) == false )
                    return;
                NetHostSlot& slot = _slotTable.get( slotIndex );
                if ( slot._state != NetConnectionState::Connecting || valueA != slot._clientSalt )
                    return;
                slot._serverSalt = valueB;
                sendChallengeResponse( slot );
                slot._lastSendTime = nowSeconds;
                return;
            }
            case PacketType::ChallengeResponse:
            {
                if ( _bServer == SW_FALSE )
                    return;
                if ( _slotTable.isValid( slotIndex ) )
                {
                    // 이미 연결됐다. 수락을 받지 못한 클라이언트가 요청을 반복한 경우다.
                    NetHostSlot& slot = _slotTable.get( slotIndex );
                    if ( slot._clientSalt == valueA && slot._serverSalt == valueB )
                    {
                        // 확장 바이트(공개 키와 토큰)는 다시 읽지 않고, 보관해 둔 키 확인 태그로 같은 수락을 보낸다.
                        slot._lastReceiveTime = nowSeconds;
                        sendAccepted( slotIndex );
                    }
                    return;
                }
                // 챌린지 값을 다시 만들어 비교한다. 이번 윈도우나 바로 앞 윈도우에서 만든 값만 유효하다(5~10초).
                if ( _challenge.isTokenValid( from, valueA, valueB, nowSeconds ) == false )
                {
                    ++_rejectedPacketCount; // 위조되었거나 만료된 응답.
                    return;
                }
                const int32 freeIndex = _slotTable.findFreeSlot();
                if ( freeIndex < 0 )
                {
                    sendDenied( from, NetDisconnectReason::ServerFull, valueA );
                    return;
                }
                // 암호화 방식에서는 주소가 확인된(챌린지를 통과한) 응답에만 X25519 를 계산한다. 토큰 결속이면 증명이 맞아야 슬롯을 할당한다.
                unique_ptr<NetSlotSecurity> security;
                if ( isEncrypted() && acceptSecureResponse( reader, from, valueA, valueB, security ) == false )
                    return; // 거절은 그 함수 안에서 이미 보냈다.
                NetHostSlot& slot = _slotTable.get( freeIndex );
                slot              = NetHostSlot{};
                _slotTable.bindAddress( freeIndex, from );
                slot._clientSalt      = valueA;
                slot._serverSalt      = valueB;
                slot._state           = NetConnectionState::Connected;
                slot._lastReceiveTime = nowSeconds;
                slot._lastSendTime    = -1.0;
                slot._connection.reset();
                slot._security = std::move( security );
                // 키 확인 태그는 서버에서 클라이언트로 가는 키로 빈 데이터를 봉인한 값이다. 수락을 다시 보낼 때도 같은 태그를 쓴다.
                if ( slot._security != nullptr )
                    (void)NetPacketSealer::sealKeyConfirmTag( *slot._security, static_cast<uint32>( freeIndex ), slot._serverSalt ); // 실패하면 0 태그라 클라이언트가 키 확인에서 끊는다.
                pushEvent( NetHostEvent{ freeIndex, NetDisconnectReason::None, NetHostEvent::Kind::Connected } );
                sendAccepted( freeIndex );
                return;
            }
            case PacketType::Accepted:
            {
                if ( _bServer || _slotTable.isValid( slotIndex ) == false )
                    return;
                NetHostSlot& slot = _slotTable.get( slotIndex );
                if ( slot._state != NetConnectionState::Connecting || valueB != slot._serverSalt )
                    return;
                if ( slot._security != nullptr && acceptSecureAccepted( reader, slot, slotIndex, static_cast<uint32>( valueA ) ) == false )
                    return; // 키 확인이 틀렸다. 연결은 그 함수 안에서 닫았다.
                slot._state           = NetConnectionState::Connected;
                slot._lastReceiveTime = nowSeconds;
                slot._lastSendTime    = -1.0;
                slot._connection.reset();
                _clientIndex = static_cast<int32>( valueA );
                pushEvent( NetHostEvent{ 0, NetDisconnectReason::None, NetHostEvent::Kind::Connected } );
                return;
            }
            case PacketType::Denied:
            {
                if ( _bServer || _slotTable.isValid( slotIndex ) == false )
                    return;
                const NetHostSlot& slot = _slotTable.get( slotIndex );
                if ( slot._state != NetConnectionState::Connecting || valueB != slot._clientSalt )
                    return; // 이 요청에 대한 거절이 아니다(옛 요청이나 위조).
                const NetDisconnectReason reason = NetHostInternal::readDeniedReason( valueA );
                if ( reason != NetDisconnectReason::ServerFull )
                    SW_LOG_WARNING( "NetHost: server %# refused the connection (%#) — it speaks protocol 0x%08x, this client 0x%08x (game 0x%08x)",
                                    from.toString().c_str(), toString( reason ), static_cast<uint32>( valueA >> 32 ), _protocolID, _settings._gameID );
                closeSlot( slotIndex, reason, false );
                return;
            }
            case PacketType::Disconnect:
            {
                if ( _slotTable.isValid( slotIndex ) == false )
                    return;
                const NetHostSlot& slot = _slotTable.get( slotIndex );
                if ( valueA == slot.getConnectionToken() )
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

    void NetHost::handleSealedPacket( float64 nowSeconds, PacketType type, int32 slotIndex, const uint8* pBody, int32 bodySize )
    {
        // 본문 형식은 [종류와 0][연결 값 4바이트][패킷 번호 8바이트][AEAD + 태그]다. 바이트 정렬이므로 BitReader 를 거치지 않는다.
        const uint32 token  = bodySize >= 5 ? LittleEndianUtil::readUint32( pBody + 1 ) : 0u;
        NetHostSlot* pSlot  = _slotTable.isValid( slotIndex ) ? &_slotTable.get( slotIndex ) : nullptr;
        const bool   bReady = pSlot != nullptr && pSlot->_state == NetConnectionState::Connected && pSlot->_security != nullptr &&
                            pSlot->_security->_bKeysReady == SW_TRUE && token == static_cast<uint32>( pSlot->getConnectionToken() );
        if ( bReady == false )
        {
            // 평문 방식과 같다. 클라이언트가 Accepted 를 받지 못했으면 응답을 다시 보내(서버가 수락을 다시 보낸다), 아니면 버린다.
            const bool bClientAwaitingAcceptPayload = type == PacketType::Payload && _bServer == SW_FALSE && pSlot != nullptr && pSlot->_state == NetConnectionState::Connecting && pSlot->_serverSalt != 0 &&
                                                      token == static_cast<uint32>( pSlot->getConnectionToken() );
            if ( bClientAwaitingAcceptPayload )
            {
                sendChallengeResponse( *pSlot );
                pSlot->_lastSendTime = nowSeconds;
                return;
            }
            ++_rejectedPacketCount;
            return;
        }
        switch ( _sealer.open( *pSlot->_security, _protocolID, _bServer == SW_TRUE, pBody, bodySize ) )
        {
            case NetPacketSealer::OpenResult::Opened:
            {
                break;
            }
            case NetPacketSealer::OpenResult::Malformed:
            {
                ++_rejectedPacketCount;
                return;
            }
            case NetPacketSealer::OpenResult::Replayed:
            {
                ++_replayRejectedCount;
                return;
            }
            case NetPacketSealer::OpenResult::AuthenticationFailed:
            {
                ++_authenticationFailureCount; // 변조이므로 세고 버린다. 연결은 유지한다.
                return;
            }
        }
        const vector<uint8>& listPlain = _sealer.getOpened();
        if ( type == PacketType::Disconnect )
        {
            if ( listPlain.empty() )
                closeSlot( slotIndex, NetDisconnectReason::Remote, false );
            return;
        }
        BitReader   plainReader( listPlain.data(), static_cast<int32>( listPlain.size() ) );
        const uint8 bRemoteDebugHold = plainReader.readBits( 1 ) != 0 ? SW_TRUE : SW_FALSE;
        if ( pSlot->_connection.readPacket( nowSeconds, plainReader ) )
        {
            pSlot->_lastReceiveTime  = nowSeconds;
            pSlot->_bRemoteDebugHold = bRemoteDebugHold;
        }
    }

    void NetHost::closeSlot( int32 slotIndex, NetDisconnectReason reason, bool bNotifyRemote )
    {
        NetHostSlot& slot = _slotTable.get( slotIndex );
        if ( slot._state == NetConnectionState::Disconnected )
            return;
        if ( bNotifyRemote && slot._state == NetConnectionState::Connected )
        {
            // 끊김 알림은 유실될 수 있으므로 몇 번 보낸다(못 받아도 상대는 타임아웃으로 안다). 암호화 방식에서는 빈 평문을 봉인하므로, 엿본 솔트로 위조할 수 없다.
            for ( int32 repeat = 0; repeat < 3; ++repeat )
            {
                if ( slot._security == nullptr )
                    sendControl( slot._address, PacketType::Disconnect, slot.getConnectionToken(), 0 );
                else if ( slot._security->_bKeysReady == SW_TRUE )
                    (void)sendSealed( slot, PacketType::Disconnect, nullptr, 0 );
            }
        }
        const bool bWasVisible = slot._state == NetConnectionState::Connected || _bServer == SW_FALSE;
        _slotTable.unbindAddress( slotIndex );
        slot._state            = NetConnectionState::Disconnected;
        slot._bRemoteDebugHold = SW_FALSE;
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

    bool NetHost::sendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return sendMessageLocked( connectionID, channel, pData, size );
    }

    bool NetHost::sendMessageLocked( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size )
    {
        NetHostSlot* pSlot   = _slotTable.findConnected( connectionID );
        const bool   bQueued = pSlot != nullptr && pSlot->_connection.sendMessage( channel, pData, size );
        if ( bQueued )
            _sentKindTally.add( pData, size );
        return bQueued;
    }

    void NetHost::copySentKindTally( NetMessageKindTallyTable& outTable ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outTable = _sentKindTally;
    }

    int32 NetHost::broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptID )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        int32                   sentCount = 0;
        const int32             slotCount = _slotTable.getCount();
        for ( int32 index = 0; index < slotCount; ++index )
        {
            if ( index != exceptID && sendMessageLocked( index, channel, pData, size ) )
                ++sentCount;
        }
        return sentCount;
    }

    bool NetHost::receiveMessage( int32& outConnectionID, NetChannelType& outChannel, vector<uint8>& outBuffer )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const int32             slotCount = _slotTable.getCount();
        for ( int32 step = 0; step < slotCount; ++step )
        {
            const int32  index = ( _receiveCursor + step ) % slotCount;
            NetHostSlot& slot  = _slotTable.get( index );
            if ( slot._state != NetConnectionState::Connected )
                continue;
            for ( int32 channel = 0; channel < static_cast<int32>( NetChannelType::Count ); ++channel )
            {
                if ( slot._connection.receiveMessage( static_cast<NetChannelType>( channel ), outBuffer ) )
                {
                    outConnectionID = index;
                    outChannel      = static_cast<NetChannelType>( channel );
                    _receiveCursor  = index; // 다음에는 같은 연결부터 이어서 비운다.
                    return true;
                }
            }
        }
        _receiveCursor = slotCount > 0 ? ( _receiveCursor + 1 ) % slotCount : 0;
        return false;
    }

    void NetHost::disconnect( int32 connectionID )
    {
        // 끊김 알림은 바로 보낸다(곧 호스트를 없앨 수 있다). 이 스레드의 사본으로 잠금 밖에서 보낸다.
        OutgoingBatch           batch;
        vector<FinishedConnect> listFinished;
        INetTransport*          pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _slotTable.isValid( connectionID ) )
                closeSlot( connectionID, NetDisconnectReason::Requested, true );
            takePending( batch, listFinished );
            pTransport = _pTransport;
        }
        sendBatch( pTransport, batch );
        deliverFinished( listFinished );
    }

    void NetHost::disconnectAll()
    {
        OutgoingBatch           batch;
        vector<FinishedConnect> listFinished;
        INetTransport*          pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const int32             slotCount = _slotTable.getCount();
            for ( int32 index = 0; index < slotCount; ++index )
            {
                closeSlot( index, NetDisconnectReason::Requested, true );
            }
            takePending( batch, listFinished );
            pTransport = _pTransport;
        }
        sendBatch( pTransport, batch );
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
        const int32 slotCount = _slotTable.getCount();
        for ( int32 index = 0; index < slotCount; ++index )
        {
            NetHostSlot& slot = _slotTable.get( index );
            if ( slot._state != NetConnectionState::Connected )
                continue;
            for ( int32 channel = 0; channel < static_cast<int32>( NetChannelType::Count ); ++channel )
            {
                // 메시지를 아레나 뒤에 바로 덧붙인다(중간 버퍼를 거치지 않는다).
                int32 offset = static_cast<int32>( outInbound._bytes.size() );
                while ( slot._connection.receiveMessageAppend( static_cast<NetChannelType>( channel ), outInbound._bytes ) )
                {
                    const int32       end = static_cast<int32>( outInbound._bytes.size() );
                    NetInboundMessage message;
                    message._connectionID = index;
                    message._offset       = offset;
                    message._size         = end - offset;
                    message._channel      = static_cast<NetChannelType>( channel );
                    outInbound._listMessage.push_back( message );
                    offset = end;
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

    uint64 NetHost::getConnectionPrincipal( int32 connectionID ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bServer == SW_FALSE )
            return 0;
        const NetHostSlot* pSlot = _slotTable.findConnected( connectionID );
        return pSlot != nullptr && pSlot->_security != nullptr ? pSlot->_security->_principalID : 0;
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

    uint32 NetHost::getProtocolID() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _protocolID;
    }

    int32 NetHost::getMaxBytesPerSecond() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _settings._maxBytesPerSecond;
    }

    bool NetHost::isConnectionSaturated( int32 connectionID ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const NetHostSlot*      pSlot = _slotTable.findConnected( connectionID );
        if ( pSlot == nullptr )
            return false;
        return pSlot->_sendCredit <= 0.0 || pSlot->_connection.getQueuedUnreliableCount() > 0;
    }

    bool NetHost::getConnectionStats( int32 connectionID, NetConnectionStats& outStats ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const NetHostSlot*      pSlot = _slotTable.findConnected( connectionID );
        if ( pSlot == nullptr )
            return false;
        outStats = pSlot->_connection.getStats();
        return true;
    }

    NetConnectionState NetHost::getConnectionState( int32 connectionID ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _slotTable.isValid( connectionID ) ? _slotTable.get( connectionID )._state : NetConnectionState::Disconnected;
    }

    const NetConnection* NetHost::findConnection( int32 connectionID ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const NetHostSlot*      pSlot = _slotTable.findConnected( connectionID );
        return pSlot != nullptr ? &pSlot->_connection : nullptr;
    }

    NetAddress NetHost::getConnectionAddress( int32 connectionID ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _slotTable.isValid( connectionID ) ? _slotTable.get( connectionID )._address : NetAddress{};
    }

    int32 NetHost::getConnectedCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _slotTable.countConnected();
    }

    void NetHost::collectConnected( vector<int32>& outListConnection ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _slotTable.collectConnected( outListConnection );
    }
} // namespace sw
