#include "pch.h"

#include "Core/Network/Connection/NetEndpoint.h"

#include "Core/Concurrency/thread.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Process/Process.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/String/StringBuilder.h"

#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

namespace sw
{
    namespace
    {
        struct NetEndpointInternal
        {
            static constexpr int32 kTypeBits   = 3;
            static constexpr int32 kHeaderSize = 8; ///< 프로토콜 ID 4바이트와 체크섬 4바이트.

            /**
             * @brief 채널 상한을 넘는 메시지면 `NetConnection` 과 같은 오류를 남기고 true 입니다. 엔드포인트 잠금을 잡기 전에 부른다 —
             *        연결의 같은 로그는 엔드포인트 잠금 안에서 남게 되어 잎 잠금 규칙(`mutex`)에 어긋난다.
             */
            [[nodiscard]] static bool reportOversizeMessage( NetChannelType channel, const uint8* pData, int32 size )
            {
                const bool bCheckable = size > 0 && pData != nullptr && channel != NetChannelType::Count;
                if ( bCheckable == false || size <= NetConnection::getMaxMessageSize( channel ) )
                    return false;
                SW_LOG_ERROR_ID( "Net.MessageTooLarge", "NetConnection: a %# byte message exceeds the %# byte limit of channel %# - not sent", size,
                                 NetConnection::getMaxMessageSize( channel ), static_cast<int32>( channel ) );
                return true;
            }

            /** @brief `sendControl` 이 쓴 순서(아래 32비트 먼저)로 64비트 값을 읽습니다. 두 번의 읽기를 한 식에 두면 순서가 정해지지 않아 문장으로 나눈다. */
            static uint64 readLowFirstUint64( BitReader& reader )
            {
                const uint64 low  = reader.readBits( 32 );
                const uint64 high = reader.readBits( 32 );
                return low | ( high << 32 );
            }
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
                    case NetDisconnectReason::ReceiveOverflow:
                    case NetDisconnectReason::ReliableOverflow:
                        break;
                }
                return NetDisconnectReason::Rejected;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NetEndpoint::NetEndpoint()
        : _mutex{}
        , _slotTable{}
        , _challenge{}
        , _sealer{}
        , _listEvent{}
        , _pendingBatch{}
        , _packetWriter{}
        , _connectPromise{}
        , _listFinished{}
        , _updateThread{}
        , _listReceived{}
        , _flushBatch{}
        , _listDeliver{}
        , _listSealed{}
        , _openedBytes{}
        , _listReceiveCrypt{}
        , _listClosedReceiveCrypt{}
        , _plainWriter{}
        , _credentials{}
        , _settings{}
        , _sentKindStats{}
        , _listDueSlot{}
        , _pTransport{ nullptr }
        , _rejectedPacketCount{ 0 }
        , _authenticationFailureCount{ 0 }
        , _replayRejectedCount{ 0 }
        , _mismatchLogCount{ 0 }
        , _slotVisitCount{ 0 }
        , _lastUpdateTime{ -1.0 }
        , _nextDebuggerPollTime{ 0.0 }
        , _nextUpdateTime{ std::numeric_limits<float64>::lowest() }
        , _protocolID{ 0 }
        , _updateDepth{ 0 }
        , _clientIndex{ constant::invalid_index::kInt32 }
        , _receiveCursor{ 0 }
        , _bServer{ SW_FALSE }
        , _bConnectPending{ SW_FALSE }
        , _bHasCredentials{ SW_FALSE }
        , _bDebugHold{ SW_FALSE }
        , _bWaiting{ SW_FALSE }
        , _bWakeRequested{ SW_FALSE }
    {
    }

    NetEndpoint::~NetEndpoint() = default;

    void NetEndpoint::initialize( INetTransport* pTransport, const NetEndpointConfig& settings )
    {
        vector<FinishedConnect> listFinished;
        {
            ScopedLock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested );
            _pTransport = pTransport;
            _settings   = settings;
            _challenge.initialize( settings._saltSeed );
            resetSlots( 0 );
            _packetWriter.reserve( kNetMaxPacketSize );
            _pendingBatch.clear();
            _pendingBatch._bytes.reserve( static_cast<size_t>( kNetMaxPacketSize ) * 4 );
            _listEvent.clear();
            _rejectedPacketCount        = 0;
            _authenticationFailureCount = 0;
            _replayRejectedCount        = 0;
            _mismatchLogCount           = 0;
            _clientIndex                = constant::invalid_index::kInt32;
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

    NetEndpoint::PacketType NetEndpoint::peekPacketType( const uint8* pData, int32 size )
    {
        if ( pData == nullptr || size <= NetEndpointInternal::kHeaderSize )
            return PacketType::Count;
        BitReader    reader( pData + NetEndpointInternal::kHeaderSize, size - NetEndpointInternal::kHeaderSize );
        const uint32 type = reader.readBits( NetEndpointInternal::kTypeBits );
        return type < static_cast<uint32>( PacketType::Count ) ? static_cast<PacketType>( type ) : PacketType::Count;
    }

    uint32 NetEndpoint::computePacketChecksum( uint32 headerID, const uint8* pBody, int32 size )
    {
        // FNV-1a 32비트. 헤더 값(프로토콜 ID)을 먼저 섞으므로, 다른 게임이나 버전의 패킷은 체크섬부터 맞지 않는다.
        uint32 hash = HashUtil::kFNVOffset32;
        for ( int32 shift = 0; shift < 32; shift += 8 )
        {
            hash = HashUtil::fnvStep32( hash, ( headerID >> shift ) & 0xFFu );
        }
        return HashUtil::appendBytes32( hash, pBody, size > 0 ? static_cast<size_t>( size ) : 0 );
    }

    uint32 NetEndpoint::computeFeatureMask() const
    {
        if ( isEncrypted() == false )
            return 0;
        // 토큰 결속은 서버에 인증기가 있을 때, 클라이언트가 자격을 제시할 때 켠다. 한쪽만 켜면 프로토콜 ID가 달라져 SecurityMismatch 가 된다.
        const bool bTokenBound = _bServer == SW_TRUE ? _settings._security._pAuthenticator != nullptr : _bHasCredentials == SW_TRUE;
        return NetProtocolFeature::kEncrypted | ( bTokenBound ? NetProtocolFeature::kTokenBound : 0u );
    }

    bool NetEndpoint::listen()
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _pTransport == nullptr )
            return false;
        if ( isEncrypted() && _settings._security._pProvider == nullptr )
        {
            SW_LOG_ERROR_ID( "NetEndpoint.NetEndpointEncryptedModeNeeds", "NetEndpoint: encrypted mode needs a security provider (the INetSecurityProvider subsystem GameFramework publishes)" );
            return false;
        }
#if defined( SW_SHIPPING )
        if ( isEncrypted() && _settings._security._pAuthenticator == nullptr )
        {
            // 인증기 없는 암호화(일회용 키)는 도청만 막고 중간자 공격은 막지 못하므로 개발 빌드에서만 쓴다.
            SW_LOG_ERROR_ID( "NetEndpoint.NetEndpointShippingServerMust", "NetEndpoint: a shipping server must bind encrypted connections to session tokens (set NetSecurityConfig::_pAuthenticator)" );
            return false;
        }
#endif
        _bServer    = SW_TRUE;
        _protocolID = NetProtocol::makeProtocolID( _settings._gameID, _settings._wireVersion, computeFeatureMask() );
        resetSlots( MathUtil::max( 1, _settings._maxConnections ) );
        return true;
    }

    bool NetEndpoint::connect( const NetAddress& serverAddress )
    {
        return connectLocked( serverAddress, nullptr );
    }

    bool NetEndpoint::connect( const NetAddress& serverAddress, const NetConnectCredentials& credentials )
    {
        return connectLocked( serverAddress, &credentials );
    }

    bool NetEndpoint::connectLocked( const NetAddress& serverAddress, const NetConnectCredentials* pCredentials )
    {
        vector<FinishedConnect> listFinished;
        bool                    bStarted = false;
        INetTransport*          pWake    = nullptr;
        {
            ScopedLock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested ); // 앞의 비동기 연결은 여기서 끝난다.
            _credentials     = pCredentials != nullptr ? *pCredentials : NetConnectCredentials{};
            _bHasCredentials = pCredentials != nullptr && pCredentials->_tokenSize > 0 ? SW_TRUE : SW_FALSE;
            bStarted         = startConnect( serverAddress );
            listFinished.swap( _listFinished );
            pWake = consumeWakeRequest();
        }
        deliverFinished( listFinished );
        wakeTransport( pWake );
        return bStarted;
    }

    TaskFuture<NetConnectResult> NetEndpoint::connectAsync( const NetAddress& serverAddress )
    {
        return connectAsyncWith( serverAddress, nullptr );
    }

    TaskFuture<NetConnectResult> NetEndpoint::connectAsync( const NetAddress& serverAddress, const NetConnectCredentials& credentials )
    {
        return connectAsyncWith( serverAddress, &credentials );
    }

    TaskFuture<NetConnectResult> NetEndpoint::connectAsyncWith( const NetAddress& serverAddress, const NetConnectCredentials* pCredentials )
    {
        vector<FinishedConnect>      listFinished;
        TaskFuture<NetConnectResult> future;
        INetTransport*               pWake = nullptr;
        {
            ScopedLock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested );
            _connectPromise  = TaskPromise<NetConnectResult>{};
            future           = _connectPromise.getFuture();
            _bConnectPending = SW_TRUE;
            _credentials     = pCredentials != nullptr ? *pCredentials : NetConnectCredentials{};
            _bHasCredentials = pCredentials != nullptr && pCredentials->_tokenSize > 0 ? SW_TRUE : SW_FALSE;
            if ( startConnect( serverAddress ) == false )
                finishConnect( NetDisconnectReason::Rejected );
            listFinished.swap( _listFinished );
            pWake = consumeWakeRequest();
        }
        deliverFinished( listFinished );
        wakeTransport( pWake );
        return future;
    }

    bool NetEndpoint::startConnect( const NetAddress& serverAddress )
    {
        if ( _pTransport == nullptr || serverAddress.isValid() == false )
            return false;
        if ( _bHasCredentials == SW_TRUE && _credentials._tokenSize > NetSecurityConstant::kMaxTokenSize )
        {
            SW_LOG_ERROR_ID( "NetEndpoint.NetEndpointSessionTokenBytes", "NetEndpoint: session token of %# bytes exceeds the limit of %# bytes", _credentials._tokenSize, NetSecurityConstant::kMaxTokenSize );
            return false;
        }
        _bServer    = SW_FALSE;
        _protocolID = NetProtocol::makeProtocolID( _settings._gameID, _settings._wireVersion, computeFeatureMask() );
        resetSlots( 1 );
        _slotTable.bindAddress( 0, serverAddress );
        NetEndpointSlot& slot = _slotTable.get( 0 );
        _slotTable.open( 0, NetConnectionState::Connecting );
        slot._clientSalt       = _challenge.nextSalt();
        slot._connectStartTime = -1.0; // 첫 update 에서 시각을 정한다.
        slot._lastSendTime     = -1.0;
        if ( isEncrypted() )
        {
            slot._security = make_unique<NetSlotSecurity>();
            if ( _settings._security._pProvider == nullptr || _settings._security._pProvider->makeX25519KeyPair( slot._security->_keyPair ) == false )
            {
                SW_LOG_ERROR_ID( "NetEndpoint.NetEndpointCannotCreateKey", "NetEndpoint: cannot create a key pair - is NetSecurityConfig::_pProvider set?" );
                closeSlot( 0, NetDisconnectReason::Rejected, false );
                return false;
            }
        }
        requestWake( std::numeric_limits<float64>::lowest() ); // 연결 요청은 지금 나간다 — 연결이 없어 상한까지 자던 스레드를 깨운다
        return true;
    }

    void NetEndpoint::sendFramed( const NetAddress& to, uint32 headerID )
    {
        // 보낼 배치의 바이트 뒤에 바로 만든다(헤더와 본문). 잠금을 푼 뒤 `sendBatch` 가 보낸다.
        const vector<uint8>& body     = _packetWriter.getBytes();
        const int32          bodySize = _packetWriter.getByteCount();
        const size_t         offset   = _pendingBatch._bytes.size();
        _pendingBatch._bytes.resize( offset + static_cast<size_t>( NetEndpointInternal::kHeaderSize + bodySize ) );
        uint8* pPacket = _pendingBatch._bytes.data() + offset;
        if ( bodySize > 0 )
            std::memcpy( pPacket + NetEndpointInternal::kHeaderSize, body.data(), static_cast<size_t>( bodySize ) );
        LittleEndianUtil::writeUint32( _pendingBatch._bytes.data() + offset, headerID );
        LittleEndianUtil::writeUint32( _pendingBatch._bytes.data() + offset + 4, computePacketChecksum( headerID, pPacket + NetEndpointInternal::kHeaderSize, bodySize ) );
        _pendingBatch._listDatagram.push_back( OutgoingDatagram{ to, static_cast<int32>( offset ), NetEndpointInternal::kHeaderSize + bodySize } );
    }

    void NetEndpoint::sendBatch( INetTransport* pTransport, const OutgoingBatch& batch )
    {
        if ( pTransport == nullptr )
            return;
        // 실패는 손실과 같다(재전송이 맡는다). 묶어서 보내면 UDP 소켓이 시스템 호출 한 번으로 보낸다(`INetTransport::sendBatch`).
        constexpr size_t kChunkSize = static_cast<size_t>( kReceiveBatchSize );
        NetDatagramView  arrView[kChunkSize];
        const size_t     total = batch._listDatagram.size();
        for ( size_t first = 0; first < total; first += kChunkSize )
        {
            const size_t chunkCount = total - first < kChunkSize ? total - first : kChunkSize;
            for ( size_t index = 0; index < chunkCount; ++index )
            {
                const OutgoingDatagram& datagram = batch._listDatagram[first + index];
                arrView[index]                   = NetDatagramView{ datagram._to, batch._bytes.data() + datagram._offset, datagram._size };
            }
            (void)pTransport->sendBatch( arrView, static_cast<int32>( chunkCount ) );
        }
    }

    void NetEndpoint::takePending( OutgoingBatch& outBatch, vector<FinishedConnect>& outListFinished )
    {
        outBatch.clear();
        outBatch._listDatagram.swap( _pendingBatch._listDatagram );
        outBatch._bytes.swap( _pendingBatch._bytes );
        outListFinished.clear();
        outListFinished.swap( _listFinished );
    }

    void NetEndpoint::deliverFinished( vector<FinishedConnect>& listFinished )
    {
        for ( FinishedConnect& finished : listFinished )
        {
            finished._promise.setValue( finished._result );
        }
        listFinished.clear();
    }

    void NetEndpoint::pushEvent( const NetEndpointEvent& event )
    {
        _listEvent.push_back( event );
        if ( _bServer == SW_FALSE )
            finishConnect( event._kind == NetEndpointEvent::Kind::Connected ? NetDisconnectReason::None : event._reason );
    }

    void NetEndpoint::finishConnect( NetDisconnectReason reason )
    {
        if ( _bConnectPending == SW_FALSE )
            return;
        _bConnectPending = SW_FALSE;
        FinishedConnect finished{
            std::move( _connectPromise ), NetConnectResult{ reason, reason == NetDisconnectReason::None ? _clientIndex : constant::invalid_index::kInt32 }
        };
        _listFinished.push_back( std::move( finished ) );
    }

    void NetEndpoint::sendControl( const NetAddress& to, PacketType type, uint64 valueA, uint64 valueB )
    {
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( type ), NetEndpointInternal::kTypeBits );
        writer.writeBits( static_cast<uint32>( valueA ), 32 );
        writer.writeBits( static_cast<uint32>( valueA >> 32 ), 32 );
        writer.writeBits( static_cast<uint32>( valueB ), 32 );
        writer.writeBits( static_cast<uint32>( valueB >> 32 ), 32 );
        sendFramed( to, isHandshakeFramed( type ) ? NetProtocol::kHandshakeID : _protocolID );
    }

    void NetEndpoint::sendDenied( const NetAddress& to, NetDisconnectReason reason, uint64 clientSalt )
    {
        sendControl( to, PacketType::Denied, static_cast<uint64>( reason ) | ( static_cast<uint64>( _protocolID ) << 32 ), clientSalt );
    }

    void NetEndpoint::sendChallengeResponse( NetEndpointSlot& slot )
    {
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( PacketType::ChallengeResponse ), NetEndpointInternal::kTypeBits );
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
                uint8 arrTag[NetSecurityConstant::kAEADTagSize]{};
                (void)NetPacketSealer::computeProofTag( *_settings._security._pProvider, _settings._security._algorithm, _credentials._secret, slot._clientSalt,
                                                        slot._serverSalt, slot._security->_keyPair._arrPublicKey, _credentials._arrToken, _credentials._tokenSize,
                                                        arrTag ); // 실패하면 0 태그가 나가고, 서버가 증명 실패로 거절한다.
                writer.writeBytes( arrTag, NetSecurityConstant::kAEADTagSize );
            }
        }
        sendFramed( slot._address, _protocolID );
    }

    void NetEndpoint::sendAccepted( int32 slotIndex )
    {
        const NetEndpointSlot& slot   = _slotTable.get( slotIndex );
        BitWriter&             writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( PacketType::Accepted ), NetEndpointInternal::kTypeBits );
        writer.writeBits( static_cast<uint32>( slotIndex ), 32 );
        writer.writeBits( 0u, 32 );
        writer.writeBits( static_cast<uint32>( slot._serverSalt ), 32 );
        writer.writeBits( static_cast<uint32>( slot._serverSalt >> 32 ), 32 );
        if ( isEncrypted() && slot._security != nullptr )
        {
            writer.writeBytes( slot._security->_keyPair._arrPublicKey, NetSecurityConstant::kX25519KeySize );
            writer.writeBytes( slot._security->_arrKeyConfirmTag, NetSecurityConstant::kAEADTagSize );
        }
        sendFramed( slot._address, _protocolID );
    }

    bool NetEndpoint::acceptSecureResponse( BitReader& reader, const NetAddress& from, uint64 clientSalt, uint64 challenge, unique_ptr<NetSlotSecurity>& outSecurity )
    {
        INetSecurityProvider& provider    = *_settings._security._pProvider;
        const bool            bTokenBound = _settings._security._pAuthenticator != nullptr;
        uint8                 arrClientPublic[NetSecurityConstant::kX25519KeySize]{};
        uint8                 arrProof[NetSecurityConstant::kAEADTagSize]{};
        vector<uint8>         listToken;
        bool                  bRead = reader.readBytes( arrClientPublic, NetSecurityConstant::kX25519KeySize );
        if ( bTokenBound )
            bRead = bRead && reader.readBlob( listToken, NetSecurityConstant::kMaxTokenSize ) && reader.readBytes( arrProof, NetSecurityConstant::kAEADTagSize );
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

    bool NetEndpoint::acceptSecureAccepted( BitReader& reader, NetEndpointSlot& slot, int32 slotIndex, uint32 serverIndex )
    {
        NetSlotSecurity& security = *slot._security;
        uint8            arrServerPublic[NetSecurityConstant::kX25519KeySize]{};
        uint8            arrTag[NetSecurityConstant::kAEADTagSize]{};
        // 키 확인으로 서버가 같은 키(같은 세션 비밀과 같은 공유 비밀)를 가졌는지 본다. 다르면 중간자이거나 비밀이 틀린 것이다.
        const bool bOk = reader.readBytes( arrServerPublic, NetSecurityConstant::kX25519KeySize ) && reader.readBytes( arrTag, NetSecurityConstant::kAEADTagSize ) &&
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

    int32 NetEndpoint::sendSealed( NetEndpointSlot& slot, PacketType type, const uint8* pPlain, int32 plainSize )
    {
        if ( _sealer.seal( *slot._security, static_cast<uint8>( type ), static_cast<uint32>( slot.getConnectionToken() ), _protocolID, _bServer == SW_TRUE, pPlain, plainSize,
                           _packetWriter ) == false )
            return 0;
        sendFramed( slot._address, _protocolID );
        return NetEndpointInternal::kHeaderSize + _packetWriter.getByteCount();
    }

    void NetEndpoint::sendPayload( float64 nowSeconds, NetEndpointSlot& slot )
    {
        if ( slot._security != nullptr )
        {
            // 암호화 방식에서는 NetConnection 패킷을 평문으로 만들어 봉인한다. 예산에서 암호화 헤더와 태그 크기를 뺀다.
            _plainWriter.clear();
            _plainWriter.writeBits( _bDebugHold == SW_TRUE ? 1u : 0u, 1 ); // 디버거 대기 비트. 평문 첫 비트이므로 함께 봉인된다.
            const int32 maxBytes =
                slot._sendCredit > 0.0 ? kNetMaxPacketSize - NetEndpointInternal::kHeaderSize - NetPacketSealer::kSecureHeadSize - NetSecurityConstant::kAEADTagSize : 0;
            slot._connection->writePacket( nowSeconds, _plainWriter, maxBytes, _settings._keepAliveInterval, _settings._maxUnreliableAgeSeconds );
            const int32 sentBytes = sendSealed( slot, PacketType::Payload, _plainWriter.getBytes().data(), _plainWriter.getByteCount() );
            slot._sendCredit -= static_cast<float64>( sentBytes );
            slot._lastSendTime = nowSeconds;
            return;
        }
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( PacketType::Payload ), NetEndpointInternal::kTypeBits );
        // 연결 값(두 솔트를 섞은 값)으로 같은 주소의 옛 연결이나 위조 패킷을 걸러 낸다.
        writer.writeBits( static_cast<uint32>( slot.getConnectionToken() ), 32 );
        writer.writeBits( _bDebugHold == SW_TRUE ? 1u : 0u, 1 ); // 디버거 대기 비트.
        // 대역폭 토큰을 다 썼으면 헤더(확인과 연결 유지)만 보내고, 메시지는 다음 전송으로 미룬다.
        const int32 maxBytes = slot._sendCredit > 0.0 ? kNetMaxPacketSize - NetEndpointInternal::kHeaderSize : 0;
        slot._connection->writePacket( nowSeconds, writer, maxBytes, _settings._keepAliveInterval, _settings._maxUnreliableAgeSeconds );
        sendFramed( slot._address, _protocolID );
        slot._sendCredit -= static_cast<float64>( writer.getByteCount() + NetEndpointInternal::kHeaderSize );
        slot._lastSendTime = nowSeconds;
    }

    void NetEndpoint::refillSendCredit( float64 nowSeconds, NetEndpointSlot& slot ) const
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

    void NetEndpoint::updateDebugHold( float64 nowSeconds )
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
                    _nextDebuggerPollTime = nowSeconds + NetEndpointInternal::kDebuggerPollSeconds;
                }
                break;
            }
        }
        // 가상 시계. 알리는 중에 update 사이가 크게 벌어졌다면 프로세스가 중단점이나 일시정지로 멈췄던 것이다. 그동안 상대가 보낸 것을 받지 못한 것은
        // 상대 탓이 아니므로, 멈춘 시간만큼 마지막 수신 시각과 연결 시작 시각을 민다. 그러면 재개한 쪽이 상대를 타임아웃으로 끊지 않는다.
        const float64 stalledSeconds = _lastUpdateTime < 0.0 ? 0.0 : nowSeconds - _lastUpdateTime;
        if ( _bDebugHold == SW_TRUE && stalledSeconds > NetEndpointInternal::kDebugStallSeconds )
        {
            for ( NetEndpointSlot& slot : _slotTable )
            {
                slot._lastReceiveTime += stalledSeconds;
                if ( slot._connectStartTime >= 0.0 )
                    slot._connectStartTime += stalledSeconds;
            }
            _slotTable.markAllDue(); // 기한을 시계에서 셈했으니 민 시계로 다시 센다
        }
#endif
        _lastUpdateTime = nowSeconds;
    }

    float64 NetEndpoint::computeTimeoutSeconds( const NetEndpointSlot& slot ) const
    {
#if defined( SW_SHIPPING )
        (void)slot;
        return _settings._timeoutSeconds; // 배포 빌드는 알림을 믿지 않는다. 클라이언트가 비트를 켜서 슬롯을 붙잡는 것을 막기 위해서다.
#else
        return slot._bRemoteDebugHold == SW_TRUE ? MathUtil::max( _settings._timeoutSeconds, _settings._debugHoldTimeoutSeconds ) : _settings._timeoutSeconds;
#endif
    }

    bool NetEndpoint::isDebugHoldAnnounced() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _bDebugHold == SW_TRUE;
    }

    float64 NetEndpoint::computeWaitSeconds( float64 nowSeconds, float64 nextUpdateTime, float64 maxWaitSeconds )
    {
        return MathUtil::clamp( nextUpdateTime - nowSeconds, 0.0, MathUtil::max( 0.0, maxWaitSeconds ) );
    }

    bool NetEndpoint::waitForReceive( float64 nowSeconds, float64 maxWaitSeconds )
    {
        INetTransport* pTransport  = nullptr;
        float64        waitSeconds = 0.0;
        {
            ScopedLock<mutex> lock{ _mutex };
            pTransport  = _pTransport;
            waitSeconds = computeWaitSeconds( nowSeconds, _nextUpdateTime, maxWaitSeconds );
            _bWaiting   = SW_TRUE; // 이 뒤에 앞당기는 쪽은 깨운다(`requestWake`) — 앞이면 위의 자는 시간에 이미 들어갔다
        }
        bool bReceived = false;
        if ( pTransport != nullptr )
            bReceived = pTransport->waitForReceive( waitSeconds );
        else
            ThreadUtil::sleepForSeconds( MathUtil::max( 0.0, maxWaitSeconds ) ); // 전송이 없으면 기한도 없다(update 가 아무것도 하지 않는다)
        ScopedLock<mutex> lock{ _mutex };
        _bWaiting       = SW_FALSE;
        _bWakeRequested = SW_FALSE;
        return bReceived;
    }

    void NetEndpoint::wake()
    {
        INetTransport* pTransport = nullptr;
        {
            ScopedLock<mutex> lock{ _mutex };
            pTransport = _pTransport;
        }
        wakeTransport( pTransport );
    }

    float64 NetEndpoint::getNextUpdateTime() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _nextUpdateTime;
    }

    void NetEndpoint::requestWake( float64 dueTime )
    {
        if ( dueTime >= _nextUpdateTime )
            return; // 이미 그 전에 깬다 — 메시지마다 깨우지 않는다
        _nextUpdateTime = dueTime;
        if ( _bWaiting == SW_TRUE )
            _bWakeRequested = SW_TRUE;
    }

    INetTransport* NetEndpoint::consumeWakeRequest()
    {
        if ( _bWakeRequested == SW_FALSE )
            return nullptr;
        _bWakeRequested = SW_FALSE;
        return _pTransport;
    }

    void NetEndpoint::wakeTransport( INetTransport* pTransport )
    {
        if ( pTransport != nullptr )
            pTransport->wake();
    }

    void NetEndpoint::update( float64 nowSeconds )
    {
        const uint32 depth = _updateDepth.fetch_add( 1, std::memory_order_acq_rel );
        SW_ASSERT( depth == 0 && "NetEndpoint::update called from two threads at once" );
        (void)depth;
        _updateThread.assumeHeld(); // 한 번에 한 스레드만 update 를 돈다(위 단언) — 그 스레드가 update 스레드 역할이다
        INetTransport* pTransport = nullptr;
        {
            ScopedLock<mutex> lock{ _mutex };
            pTransport = _pTransport;
        }
        if ( pTransport != nullptr )
        {
            // 1) 받기(잠금 밖). 받은 버퍼는 다시 쓴다.
            pTransport->update( nowSeconds );
            int32 receivedCount = 0;
            while ( receivedCount < kMaxDatagramPerUpdate )
            {
                const int32 wantCount = kMaxDatagramPerUpdate - receivedCount < kReceiveBatchSize ? kMaxDatagramPerUpdate - receivedCount : kReceiveBatchSize;
                if ( static_cast<int32>( _listReceived.size() ) < receivedCount + wantCount )
                    _listReceived.resize( static_cast<size_t>( receivedCount + wantCount ) );
                const int32 gotCount = pTransport->receiveBatch( _listReceived.data() + receivedCount, wantCount );
                if ( gotCount <= 0 )
                    break;
                receivedCount += gotCount;
            }
            // 2) 처리, 타임아웃, 보낼 패킷 생성(잠금 안). 받은 것은 일괄 받기 크기(`kReceiveBatchSize`)씩 잠금을 따로 잡아 처리한다 —
            //    복호까지 512 개를 한 잠금에서 하면 그동안 게임 스레드 · 복제 워커의 `sendMessage` 가 모두 기다린다(감사 N-8). 묶음 사이에 그들이 끼어든다.
            //    암호화 데이터는 잠금 안에서 슬롯 · 연결 값만 확인하고, 잠금을 놓고 열고(AEAD — 패킷마다 가장 무거운 일), 다시 잠금을 잡아 연결에 넘긴다.
            {
                ScopedLock<mutex> lock{ _mutex };
                updateDebugHold( nowSeconds );
                releaseClosedReceiveCrypt();
            }
            for ( int32 first = 0; first < receivedCount; first += kReceiveBatchSize )
            {
                const int32 last = MathUtil::min( receivedCount, first + kReceiveBatchSize );
                _listSealed.clear();
                _openedBytes.clear();
                uint32 protocolID = 0;
                {
                    ScopedLock<mutex> lock{ _mutex };
                    protocolID = _protocolID;
                    for ( int32 index = first; index < last; ++index )
                    {
                        const NetReceivedDatagram& datagram = _listReceived[static_cast<size_t>( index )];
                        handlePacket( nowSeconds, datagram._from, datagram._buffer.data(), static_cast<int32>( datagram._buffer.size() ) );
                    }
                }
                if ( _listSealed.empty() )
                    continue;
                openSealedPackets( protocolID );
                ScopedLock<mutex> lock{ _mutex };
                for ( const SealedDatagram& sealed : _listSealed )
                {
                    handleOpenedPacket( nowSeconds, sealed );
                }
            }
            vector<DeferredWarning> listWarning;
            {
                ScopedLock<mutex> lock{ _mutex };
                updateSlots( nowSeconds );
                takePending( _flushBatch, _listDeliver );
                listWarning.swap( _listDeferredWarning );
            }
            for ( const DeferredWarning& warning : listWarning ) // 잠금을 놓은 뒤에 쓴다 — 잠금은 잎이다
            {
                switch ( warning._kind )
                {
                    case DeferredWarningKind::ConnectionRefused:
                    {
                        SW_LOG_WARNING_ID( "Net.ConnectionRefused", "%#", warning._text.c_str() );
                        break;
                    }
                    case DeferredWarningKind::ServerRefused:
                    {
                        SW_LOG_WARNING_ID( "Net.ServerRefused", "%#", warning._text.c_str() );
                        break;
                    }
                }
            }
            // 3) 보내기와 비동기 연결 결과 전달(잠금 밖).
            sendBatch( pTransport, _flushBatch );
            deliverFinished( _listDeliver );
        }
        _updateDepth.fetch_sub( 1, std::memory_order_acq_rel );
    }

    void NetEndpoint::updateSlots( float64 nowSeconds )
    {
        // 기한이 된 연결만 본다(감사 N-8). 기한은 다녀간 뒤 `computeNextDueTime` 이 정하고, 받은 패킷 · 넣은 메시지가 `markDue` 로 앞당긴다.
        // 언리얼은 틱마다 모든 연결을 `TickFlush` 하고, 여기서는 보낼 시각 · 타임아웃이 된 것만 돈다 — 깨어날 때마다 정원 N 을 훑지 않는다.
        _slotTable.collectDueSlots( nowSeconds, _listDueSlot );
        _slotVisitCount += static_cast<uint64>( _listDueSlot.size() );
        for ( const int32 index : _listDueSlot )
        {
            NetEndpointSlot& slot = _slotTable.get( index );
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
                    const bool    bCanCarry     = slot._sendCredit > 0.0;
                    const bool    bHasData      = bCanCarry ? slot._connection->hasDataToSend( nowSeconds ) : slot._connection->isAckPending();
                    const float64 timeSinceSend = slot._lastSendTime < 0.0 ? 1.0e9 : nowSeconds - slot._lastSendTime;
                    const bool    bDue          = timeSinceSend >= _settings._sendInterval && ( timeSinceSend >= _settings._keepAliveInterval || bHasData );
                    if ( bDue == false )
                        break;
                    sendPayload( nowSeconds, slot );
                    // 토큰이 남고 실을 것이 더 있으면 같은 전송에서 더 보낸다. 큰 신뢰 메시지나 몰린 스냅샷의 전송 속도는 대역폭 상한이 정한다.
                    for ( int32 packetCount = 1; packetCount < kMaxPacketsPerSend && slot._sendCredit > 0.0 && slot._connection->hasDataToSend( nowSeconds );
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
            if ( slot._state == NetConnectionState::Connecting || slot._state == NetConnectionState::Connected )
                _slotTable.setDueTime( index, computeNextDueTime( nowSeconds, slot, _settings, computeTimeoutSeconds( slot ) ) );
        }
        // 디버거 확인 시각은 넣지 않는다 — 그 비트는 데이터 패킷에만 실리고, 연결이 있으면 유지 간격마다 돈다.
        _nextUpdateTime = _slotTable.getNextDueTime();
    }

    float64 NetEndpoint::computeNextDueTime( float64 nowSeconds, const NetEndpointSlot& slot, const NetEndpointConfig& settings, float64 timeoutSeconds )
    {
        if ( slot._lastSendTime < 0.0 )
            return nowSeconds; // 아직 보낸 적이 없다 — 다음 갱신에서 다시 본다
        if ( slot._state == NetConnectionState::Connecting )
            return MathUtil::min( slot._connectStartTime + settings._connectTimeout, slot._lastSendTime + settings._connectRetryInterval );
        // 한가한(보낼 것 · 확인할 것 · 확인을 기다리는 신뢰 메시지가 없는) 연결은 유지 간격, 아니면 보내기 간격마다 본다.
        // 신뢰 메시지의 재전송 시각과 토큰 충전은 시계로만 오므로, 그것이 남아 있는 동안은 보내기 간격마다 다시 묻는다.
        const bool    bIdle     = slot._connection->hasDataToSend( nowSeconds ) == false && slot._connection->getPendingReliableCount() == 0;
        const float64 sendAt    = slot._lastSendTime + ( bIdle ? MathUtil::max( settings._sendInterval, settings._keepAliveInterval ) : settings._sendInterval );
        const float64 timeoutAt = slot._lastReceiveTime + timeoutSeconds;
        return MathUtil::min( sendAt, timeoutAt );
    }

    void NetEndpoint::handlePacket( float64 nowSeconds, const NetAddress& from, const uint8* pData, int32 size )
    {
        const uint32 headerID        = size > NetEndpointInternal::kHeaderSize ? LittleEndianUtil::readUint32( pData ) : 0u;
        const bool   bRejectedPacket = size <= NetEndpointInternal::kHeaderSize || ( headerID != _protocolID && headerID != NetProtocol::kHandshakeID ) ||
                                     LittleEndianUtil::readUint32( pData + 4 ) !=
                                         computePacketChecksum( headerID, pData + NetEndpointInternal::kHeaderSize, size - NetEndpointInternal::kHeaderSize );
        if ( bRejectedPacket )
        {
            ++_rejectedPacketCount; // 다른 버전, 다른 게임, 깨진 패킷.
            return;
        }
        BitReader        reader( pData + NetEndpointInternal::kHeaderSize, size - NetEndpointInternal::kHeaderSize );
        const PacketType type = static_cast<PacketType>( reader.readBits( NetEndpointInternal::kTypeBits ) );
        if ( type >= PacketType::Count || isHandshakeFramed( type ) != ( headerID == NetProtocol::kHandshakeID ) )
        {
            ++_rejectedPacketCount;
            return;
        }
        const optional<int32> slotIndex = _slotTable.findByAddress( from );
        if ( slotIndex.has_value() )
            _slotTable.markDue( *slotIndex ); // 받은 것이 확인 · 응답 · 상태를 바꿨을 수 있다 — 이번 갱신에서 그 슬롯을 본다
        if ( isEncrypted() && ( type == PacketType::Payload || type == PacketType::Disconnect ) )
        {
            queueSealedPacket( nowSeconds, type, slotIndex, pData + NetEndpointInternal::kHeaderSize, size - NetEndpointInternal::kHeaderSize );
            return;
        }
        if ( type == PacketType::Payload )
        {
            handlePayloadPacket( nowSeconds, slotIndex, reader );
            return;
        }
        ControlPacket packet;
        packet._from       = from;
        packet._nowSeconds = nowSeconds;
        packet._slotIndex  = slotIndex;
        packet._valueA     = NetEndpointInternal::readLowFirstUint64( reader );
        packet._valueB     = NetEndpointInternal::readLowFirstUint64( reader );
        if ( reader.hasOverflowed() )
        {
            ++_rejectedPacketCount;
            return;
        }
        handleControlPacket( type, packet, reader );
    }

    void NetEndpoint::handlePayloadPacket( float64 nowSeconds, const optional<int32> slotIndex, BitReader& reader )
    {
        const uint32     token = reader.readBits( 32 );
        NetEndpointSlot* pSlot = slotIndex.has_value() ? &_slotTable.get( *slotIndex ) : nullptr;
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
        NetEndpointSlot& slot = *pSlot;
        if ( token != static_cast<uint32>( slot.getConnectionToken() ) )
        {
            ++_rejectedPacketCount;
            return;
        }
        const uint8 bRemoteDebugHold = reader.readBits( 1 ) != 0 ? SW_TRUE : SW_FALSE;
        if ( slot._connection->readPacket( nowSeconds, reader ) )
        {
            slot._lastReceiveTime  = nowSeconds;
            slot._bRemoteDebugHold = bRemoteDebugHold;
        }
        if ( slot._connection->hasReceiveOverflowed() )
            closeSlot( *slotIndex, NetDisconnectReason::ReceiveOverflow, true );
    }

    void NetEndpoint::handleControlPacket( PacketType type, const ControlPacket& packet, BitReader& reader )
    {
        const optional<int32> slotIndex = packet._slotIndex;
        switch ( type )
        {
            case PacketType::ConnectRequest:
            {
                handleConnectRequest( packet );
                return;
            }
            case PacketType::Challenge:
            {
                if ( _bServer || slotIndex.has_value() == false )
                    return;
                NetEndpointSlot& slot = _slotTable.get( *slotIndex );
                if ( slot._state != NetConnectionState::Connecting || packet._valueA != slot._clientSalt )
                    return;
                slot._serverSalt = packet._valueB;
                sendChallengeResponse( slot );
                slot._lastSendTime = packet._nowSeconds;
                return;
            }
            case PacketType::ChallengeResponse:
            {
                handleChallengeResponse( packet, reader );
                return;
            }
            case PacketType::Accepted:
            {
                if ( _bServer || slotIndex.has_value() == false )
                    return;
                NetEndpointSlot& slot = _slotTable.get( *slotIndex );
                if ( slot._state != NetConnectionState::Connecting || packet._valueB != slot._serverSalt )
                    return;
                if ( slot._security != nullptr && acceptSecureAccepted( reader, slot, *slotIndex, static_cast<uint32>( packet._valueA ) ) == false )
                    return; // 키 확인이 틀렸다. 연결은 그 함수 안에서 닫았다.
                if ( slot._security != nullptr )
                    installReceiveCrypt( *slotIndex, slot );
                slot._state           = NetConnectionState::Connected;
                slot._lastReceiveTime = packet._nowSeconds;
                slot._lastSendTime    = -1.0;
                slot._connection->setFlowIdentity( slot.getConnectionToken(), false ); // 트레이스 흐름 id(`NetFlowLog`)의 연결 신원
                _clientIndex = static_cast<int32>( packet._valueA );
                pushEvent( NetEndpointEvent{ 0, NetDisconnectReason::None, NetEndpointEvent::Kind::Connected } );
                return;
            }
            case PacketType::Denied:
            {
                if ( _bServer || slotIndex.has_value() == false )
                    return;
                const NetEndpointSlot& slot = _slotTable.get( *slotIndex );
                if ( slot._state != NetConnectionState::Connecting || packet._valueB != slot._clientSalt )
                    return; // 이 요청에 대한 거절이 아니다(옛 요청이나 위조).
                const NetDisconnectReason reason = NetEndpointInternal::readDeniedReason( packet._valueA );
                if ( reason != NetDisconnectReason::ServerFull )
                {
                    StringBuilder<constant::buffer::kMax512> text;
                    text.appendFormat( "NetEndpoint: server %# refused the connection (%#) — it speaks protocol 0x%08x, this client 0x%08x (game 0x%08x)",
                                       packet._from.toString().c_str(), toString( reason ), static_cast<uint32>( packet._valueA >> 32 ), _protocolID, _settings._gameID );
                    _listDeferredWarning.push_back( DeferredWarning{ DeferredWarningKind::ServerRefused, string( text.c_str() ) } ); // 엔드포인트 잠금 안이다 — 로그는 `update` 가 잠금을 놓은 뒤에 쓴다
                }
                closeSlot( *slotIndex, reason, false );
                return;
            }
            case PacketType::Disconnect:
            {
                if ( slotIndex.has_value() && packet._valueA == _slotTable.get( *slotIndex ).getConnectionToken() )
                    closeSlot( *slotIndex, NetDisconnectReason::Remote, false );
                return;
            }
            case PacketType::Payload:
            case PacketType::Count:
            {
                return;
            }
        }
    }

    void NetEndpoint::handleConnectRequest( const ControlPacket& packet )
    {
        if ( _bServer == SW_FALSE )
            return;
        const uint32 requestProtocolID = static_cast<uint32>( packet._valueB );
        if ( requestProtocolID != _protocolID )
        {
            const uint32        requestGameID = static_cast<uint32>( packet._valueB >> 32 );
            NetDisconnectReason reason        = requestGameID == _settings._gameID ? NetDisconnectReason::VersionMismatch : NetDisconnectReason::Rejected;
            // 같은 게임, 같은 버전인데 기능 마스크만 다르면 한쪽만 암호화나 토큰 결속을 켠 것이다.
            for ( uint32 mask = 0; reason == NetDisconnectReason::VersionMismatch && mask <= NetProtocolFeature::kAllMask; ++mask )
            {
                if ( NetProtocol::makeProtocolID( _settings._gameID, _settings._wireVersion, mask ) == requestProtocolID )
                    reason = NetDisconnectReason::SecurityMismatch;
            }
            ++_mismatchLogCount;
            if ( ( _mismatchLogCount & ( _mismatchLogCount - 1 ) ) == 0 )
            {
                StringBuilder<constant::buffer::kMax512> text;
                text.appendFormat( "NetEndpoint: refused a connection from %# (%#) — it speaks protocol 0x%08x (game 0x%08x), this server 0x%08x (game 0x%08x); %# such requests so far",
                                   packet._from.toString().c_str(), toString( reason ), requestProtocolID, requestGameID, _protocolID, _settings._gameID, _mismatchLogCount );
                _listDeferredWarning.push_back( DeferredWarning{ DeferredWarningKind::ConnectionRefused, string( text.c_str() ) } ); // 엔드포인트 잠금 안이다 — 로그는 `update` 가 잠금을 놓은 뒤에 쓴다
            }
            sendDenied( packet._from, reason, packet._valueA );
            return;
        }
        if ( packet._slotIndex.has_value() )
        {
            // 이 주소는 이미 연결돼 있다. 같은 요청의 늦은 재전송이면 수락을 다시 보내고, 솔트가 다르면(새로 시작한 클라이언트) 옛 연결이 끝날 때까지 답하지 않는다.
            if ( _slotTable.get( *packet._slotIndex )._clientSalt == packet._valueA )
                sendAccepted( *packet._slotIndex );
            return;
        }
        if ( _slotTable.findFreeSlot().has_value() == false )
        {
            // 게임 스레드가 끊김을 꺼내면 곧 빈다 — 가득 참으로 거절하지 않고 답하지 않는다(클라이언트가 재요청 간격마다 다시 묻는다).
            // 끊고 바로 다시 붙는 클라이언트가 자기 옛 슬롯이 놓이기 전에 도착해 ServerFull 로 끝나던 경쟁이다.
            if ( _slotTable.countReleasePending() == 0 )
                sendDenied( packet._from, NetDisconnectReason::ServerFull, packet._valueA );
            return;
        }
        // 슬롯을 할당하지 않고 챌린지 값만 돌려준다. 그 값은 이 주소로 보낸 패킷에만 있으므로, 되돌려 준 응답이 와야 주소가 진짜임이 확인된다.
        sendControl( packet._from, PacketType::Challenge, packet._valueA, _challenge.makeToken( packet._from, packet._valueA, packet._nowSeconds ) );
    }

    void NetEndpoint::handleChallengeResponse( const ControlPacket& packet, BitReader& reader )
    {
        if ( _bServer == SW_FALSE )
            return;
        if ( packet._slotIndex.has_value() )
        {
            // 이미 연결됐다. 수락을 받지 못한 클라이언트가 요청을 반복한 경우다.
            NetEndpointSlot& slot = _slotTable.get( *packet._slotIndex );
            if ( slot._clientSalt == packet._valueA && slot._serverSalt == packet._valueB )
            {
                // 확장 바이트(공개 키와 토큰)는 다시 읽지 않고, 보관해 둔 키 확인 태그로 같은 수락을 보낸다.
                slot._lastReceiveTime = packet._nowSeconds;
                sendAccepted( *packet._slotIndex );
            }
            return;
        }
        // 챌린지 값을 다시 만들어 비교한다. 이번 윈도우나 바로 앞 윈도우에서 만든 값만 유효하다(5~10초).
        if ( _challenge.isTokenValid( packet._from, packet._valueA, packet._valueB, packet._nowSeconds ) == false )
        {
            ++_rejectedPacketCount; // 위조되었거나 만료된 응답.
            return;
        }
        const optional<int32> freeSlot = _slotTable.findFreeSlot();
        if ( freeSlot.has_value() == false )
        {
            if ( _slotTable.countReleasePending() == 0 ) // 곧 빌 슬롯이 있으면 답하지 않는다 — 클라이언트가 응답을 다시 보낸다(`handleConnectRequest` 와 같다)
                sendDenied( packet._from, NetDisconnectReason::ServerFull, packet._valueA );
            return;
        }
        const int32 freeIndex = *freeSlot;
        // 암호화 방식에서는 주소가 확인된(챌린지를 통과한) 응답에만 X25519 를 계산한다. 토큰 결속이면 증명이 맞아야 슬롯을 할당한다.
        unique_ptr<NetSlotSecurity> security;
        if ( isEncrypted() && acceptSecureResponse( reader, packet._from, packet._valueA, packet._valueB, security ) == false )
            return; // 거절은 그 함수 안에서 이미 보냈다.
        NetEndpointSlot& slot = _slotTable.get( freeIndex );
        slot                  = NetEndpointSlot{};
        _slotTable.bindAddress( freeIndex, packet._from );
        slot._clientSalt = packet._valueA;
        slot._serverSalt = packet._valueB;
        _slotTable.open( freeIndex, NetConnectionState::Connected );
        slot._lastReceiveTime = packet._nowSeconds;
        slot._lastSendTime    = -1.0;
        slot._connection->setFlowIdentity( slot.getConnectionToken(), true ); // 트레이스 흐름 id(`NetFlowLog`)의 연결 신원
        slot._security = std::move( security );
        // 키 확인 태그는 서버에서 클라이언트로 가는 키로 빈 데이터를 봉인한 값이다. 수락을 다시 보낼 때도 같은 태그를 쓴다.
        if ( slot._security != nullptr )
        {
            (void)NetPacketSealer::sealKeyConfirmTag( *slot._security, static_cast<uint32>( freeIndex ), slot._serverSalt ); // 실패하면 0 태그라 클라이언트가 키 확인에서 끊는다.
            installReceiveCrypt( freeIndex, slot );
        }
        pushEvent( NetEndpointEvent{ freeIndex, NetDisconnectReason::None, NetEndpointEvent::Kind::Connected } );
        sendAccepted( freeIndex );
    }

    void NetEndpoint::queueSealedPacket( float64 nowSeconds, PacketType type, const optional<int32> slotIndex, const uint8* pBody, int32 bodySize )
    {
        // 본문 형식은 [종류와 0][연결 값 4바이트][패킷 번호 8바이트][AEAD + 태그]다. 바이트 정렬이므로 BitReader 를 거치지 않는다.
        const uint32     token  = bodySize >= 5 ? LittleEndianUtil::readUint32( pBody + 1 ) : 0u;
        NetEndpointSlot* pSlot  = slotIndex.has_value() ? &_slotTable.get( *slotIndex ) : nullptr;
        const bool       bReady = pSlot != nullptr && pSlot->_state == NetConnectionState::Connected && pSlot->_security != nullptr &&
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
        SealedDatagram sealed;
        sealed._pBody           = pBody;
        sealed._bodySize        = bodySize;
        sealed._slotIndex       = *slotIndex;
        sealed._connectionToken = pSlot->getConnectionToken();
        sealed._type            = type;
        _listSealed.push_back( sealed );
    }

    void NetEndpoint::openSealedPackets( uint32 protocolID )
    {
        // 잠금이 없다 — 받는 키(`_listReceiveCrypt`)와 평문 버퍼는 이 스레드만 만진다. 그사이 다른 스레드가 연결을 닫아도 키는 다음 `update` 가 지우므로
        // 여기서 쓰는 키가 사라지지 않고, 닫힌 연결의 평문은 `handleOpenedPacket` 이 연결 값을 보고 버린다.
        for ( SealedDatagram& sealed : _listSealed )
        {
            const size_t index  = static_cast<size_t>( sealed._slotIndex );
            const bool   bKeyed = index < _listReceiveCrypt.size() && _listReceiveCrypt[index]._connectionToken == sealed._connectionToken &&
                                _listReceiveCrypt[index]._context._aead != nullptr;
            if ( bKeyed == false )
                continue;
            const size_t plainStart = _openedBytes.size();
            sealed._result          = NetPacketSealer::open( _listReceiveCrypt[index]._context, protocolID, sealed._pBody, sealed._bodySize, _openedBytes );
            sealed._plainOffset     = static_cast<int32>( plainStart );
            sealed._plainSize       = static_cast<int32>( _openedBytes.size() - plainStart );
        }
    }

    void NetEndpoint::handleOpenedPacket( float64 nowSeconds, const SealedDatagram& sealed )
    {
        const int32 slotIndex       = sealed._slotIndex;
        const bool  bSameConnection = _slotTable.isValid( slotIndex ) && _slotTable.get( slotIndex )._state == NetConnectionState::Connected &&
                                     _slotTable.get( slotIndex ).getConnectionToken() == sealed._connectionToken;
        if ( bSameConnection == false || sealed._result.has_value() == false )
        {
            ++_rejectedPacketCount; // 연 사이에 닫혔거나 다른 연결이 그 슬롯을 받았다.
            return;
        }
        switch ( *sealed._result )
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
        NetEndpointSlot& slot = _slotTable.get( slotIndex );
        if ( sealed._type == PacketType::Disconnect )
        {
            if ( sealed._plainSize == 0 )
                closeSlot( slotIndex, NetDisconnectReason::Remote, false );
            return;
        }
        BitReader   plainReader( _openedBytes.data() + sealed._plainOffset, sealed._plainSize );
        const uint8 bRemoteDebugHold = plainReader.readBits( 1 ) != 0 ? SW_TRUE : SW_FALSE;
        if ( slot._connection->readPacket( nowSeconds, plainReader ) )
        {
            slot._lastReceiveTime  = nowSeconds;
            slot._bRemoteDebugHold = bRemoteDebugHold;
        }
        if ( slot._connection->hasReceiveOverflowed() )
            closeSlot( slotIndex, NetDisconnectReason::ReceiveOverflow, true );
    }

    void NetEndpoint::installReceiveCrypt( int32 slotIndex, NetEndpointSlot& slot )
    {
        const size_t index = static_cast<size_t>( slotIndex );
        if ( _listReceiveCrypt.size() <= index )
            _listReceiveCrypt.resize( index + 1 );
        ReceiveCryptSlot&       entry   = _listReceiveCrypt[index];
        NetReceiveCryptContext& receive = slot._security->_receive;
        entry._context.wipe(); // 아직 지우지 못한 옛 연결의 키(닫힘을 남긴 뒤 다음 update 전)
        entry._context._aead = std::move( receive._aead );
        std::memcpy( entry._context._arrIv, receive._arrIv, sizeof( entry._context._arrIv ) );
        entry._connectionToken = slot.getConnectionToken();
        receive.wipe(); // 슬롯에는 보내는 쪽만 남는다 — 다른 스레드가 슬롯을 닫아도 받는 키는 건드리지 않는다
    }

    void NetEndpoint::releaseClosedReceiveCrypt()
    {
        for ( const ClosedReceiveCryptEntry& closed : _listClosedReceiveCrypt )
        {
            const size_t index = static_cast<size_t>( closed._slotIndex );
            if ( index < _listReceiveCrypt.size() && _listReceiveCrypt[index]._connectionToken == closed._connectionToken )
            {
                _listReceiveCrypt[index]._context.wipe();
                _listReceiveCrypt[index]._connectionToken = 0;
            }
        }
        _listClosedReceiveCrypt.clear();
    }

    void NetEndpoint::addClosedReceiveCrypt( int32 slotIndex, const NetEndpointSlot& slot )
    {
        if ( slot._security != nullptr )
            _listClosedReceiveCrypt.push_back( ClosedReceiveCryptEntry{ slot.getConnectionToken(), slotIndex } );
    }

    void NetEndpoint::resetSlots( int32 slotCount )
    {
        for ( const NetLiveSlot& live : _slotTable.getLiveSlots() )
        {
            addClosedReceiveCrypt( live._slotIndex, _slotTable.get( live._slotIndex ) );
        }
        _slotTable.reset( slotCount );
    }

    void NetEndpoint::closeSlot( int32 slotIndex, NetDisconnectReason reason, bool bNotifyRemote )
    {
        NetEndpointSlot& slot = _slotTable.get( slotIndex );
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
        addClosedReceiveCrypt( slotIndex, slot ); // 받는 키는 `update` 스레드가 지운다
        _slotTable.unbindAddress( slotIndex );
        _slotTable.close( slotIndex, bWasVisible ); // 보인 끊김은 게임 스레드가 사건을 꺼낼 때까지 번호를 다시 주지 않는다(`releaseDisconnectedSlots`)
        slot._bRemoteDebugHold = SW_FALSE;
        if ( slot._security != nullptr )
        {
            slot._security->_keys.wipe();
            slot._security->_keyPair.wipe();
            slot._security.reset();
        }
        if ( bWasVisible )
            pushEvent( NetEndpointEvent{ slotIndex, reason, NetEndpointEvent::Kind::Disconnected } );
        if ( _bServer == SW_FALSE )
            _clientIndex = constant::invalid_index::kInt32;
    }

    bool NetEndpoint::sendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size )
    {
        if ( NetEndpointInternal::reportOversizeMessage( channel, pData, size ) )
            return false;
        bool           bQueued = false;
        INetTransport* pWake   = nullptr;
        {
            ScopedLock<mutex> lock{ _mutex };
            bQueued = sendMessageLocked( connectionID, channel, pData, size, WindowFullPolicy::Close );
            pWake   = consumeWakeRequest();
        }
        wakeTransport( pWake );
        return bQueued;
    }

    bool NetEndpoint::trySendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size )
    {
        if ( NetEndpointInternal::reportOversizeMessage( channel, pData, size ) )
            return false;
        bool           bQueued = false;
        INetTransport* pWake   = nullptr;
        {
            ScopedLock<mutex> lock{ _mutex };
            bQueued = sendMessageLocked( connectionID, channel, pData, size, WindowFullPolicy::Report );
            pWake   = consumeWakeRequest();
        }
        wakeTransport( pWake );
        return bQueued;
    }

    bool NetEndpoint::sendMessageLocked( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size, WindowFullPolicy policy )
    {
        NetEndpointSlot* pSlot   = _slotTable.findConnected( connectionID );
        const bool       bQueued = pSlot != nullptr && pSlot->_connection->sendMessage( channel, pData, size );
        if ( bQueued )
        {
            _sentKindStats.add( pData, size );
            _slotTable.markDue( connectionID );                            // 보낼 것이 생겼다 — 다음 유지 시각을 기다리지 않는다
            requestWake( pSlot->_lastSendTime + _settings._sendInterval ); // 보내기 간격이 차는 때 — 자는 네트워크 스레드가 그때 깬다
            return true;
        }
        const bool bReliable    = channel == NetChannelType::ReliableOrdered || channel == NetChannelType::ReliableUnordered;
        const bool bCloseOnFull = pSlot != nullptr && bReliable && policy == WindowFullPolicy::Close && pSlot->_connection->isReliableWindowFull( size );
        if ( bCloseOnFull )
        {
            SW_LOG_WARNING_ID( "Net.ReliableOverflow", "NetEndpoint: the reliable window of connection %# is full (the peer does not acknowledge) - closing it instead of dropping a reliable message",
                               connectionID );
            closeSlot( connectionID, NetDisconnectReason::ReliableOverflow, true );
        }
        return false;
    }

    bool NetEndpoint::sendNotifiedMessage( int32 connectionID, const uint8* pData, int32 size, uint32 tag )
    {
        bool           bQueued = false;
        INetTransport* pWake   = nullptr;
        {
            ScopedLock<mutex> lock{ _mutex };
            NetEndpointSlot*  pSlot = _slotTable.findConnected( connectionID );
            bQueued                 = pSlot != nullptr && pSlot->_connection->sendNotifiedMessage( pData, size, tag );
            if ( bQueued )
            {
                _sentKindStats.add( pData, size );
                _slotTable.markDue( connectionID );
                requestWake( pSlot->_lastSendTime + _settings._sendInterval );
            }
            pWake = consumeWakeRequest();
        }
        wakeTransport( pWake );
        return bQueued;
    }

    void NetEndpoint::addSentKindStats( NetMessageKindStatsTable& outTable ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outTable.addTable( _sentKindStats );
    }

    int32 NetEndpoint::broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptID )
    {
        if ( NetEndpointInternal::reportOversizeMessage( channel, pData, size ) )
            return 0;
        int32          sentCount = 0;
        INetTransport* pWake     = nullptr;
        {
            ScopedLock<mutex>          lock{ _mutex };
            const vector<NetLiveSlot>& listLive = _slotTable.getLiveSlots();
            // 뒤에서부터 돈다 — 창이 차 닫힌 연결(`sendMessageLocked`)은 목록에서 빠지고 뒤 칸이 당겨지는데, 뒤 칸은 이미 보냈다.
            for ( size_t position = listLive.size(); position > 0; --position )
            {
                const int32 index = listLive[position - 1]._slotIndex;
                if ( index != exceptID && sendMessageLocked( index, channel, pData, size, WindowFullPolicy::Close ) )
                    ++sentCount;
            }
            pWake = consumeWakeRequest();
        }
        wakeTransport( pWake ); // 연결 수만큼이 아니라 한 번
        return sentCount;
    }

    bool NetEndpoint::receiveMessage( int32& outConnectionID, NetChannelType& outChannel, vector<uint8>& outBuffer )
    {
        ScopedLock<mutex> lock{ _mutex };
        const int32       slotCount = _slotTable.getCount();
        for ( int32 step = 0; step < slotCount; ++step )
        {
            const int32      index = ( _receiveCursor + step ) % slotCount;
            NetEndpointSlot& slot  = _slotTable.get( index );
            if ( slot._state != NetConnectionState::Connected )
                continue;
            for ( int32 channel = 0; channel < static_cast<int32>( NetChannelType::Count ); ++channel )
            {
                if ( slot._connection->receiveMessage( static_cast<NetChannelType>( channel ), outBuffer ) )
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

    void NetEndpoint::disconnect( int32 connectionID )
    {
        // 끊김 알림은 바로 보낸다(곧 엔드포인트를 없앨 수 있다). 이 스레드의 사본으로 잠금 밖에서 보낸다.
        OutgoingBatch           batch;
        vector<FinishedConnect> listFinished;
        INetTransport*          pTransport = nullptr;
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _slotTable.isValid( connectionID ) )
                closeSlot( connectionID, NetDisconnectReason::Requested, true );
            takePending( batch, listFinished );
            pTransport = _pTransport;
        }
        sendBatch( pTransport, batch );
        deliverFinished( listFinished );
    }

    void NetEndpoint::disconnectAll()
    {
        OutgoingBatch           batch;
        vector<FinishedConnect> listFinished;
        INetTransport*          pTransport = nullptr;
        {
            ScopedLock<mutex> lock{ _mutex };
            const int32       slotCount = _slotTable.getCount();
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

    void NetEndpoint::releaseDisconnectedSlots( const vector<NetEndpointEvent>& listEvent )
    {
        for ( const NetEndpointEvent& event : listEvent )
        {
            if ( event._kind == NetEndpointEvent::Kind::Disconnected && _slotTable.isValid( event._connectionID ) )
                _slotTable.release( event._connectionID );
        }
    }

    void NetEndpoint::pollEvents( vector<NetEndpointEvent>& outListEvent )
    {
        ScopedLock<mutex> lock{ _mutex };
        releaseDisconnectedSlots( _listEvent );
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    void NetEndpoint::pollInbound( NetInbound& outInbound )
    {
        outInbound.clear();
        ScopedLock<mutex> lock{ _mutex };
        outInbound._listEvent.swap( _listEvent );
        releaseDisconnectedSlots( outInbound._listEvent );
        for ( const NetLiveSlot& live : _slotTable.getLiveSlots() ) // 정원이 아니라 살아 있는 연결만(번호 순)
        {
            const int32      index = live._slotIndex;
            NetEndpointSlot& slot  = _slotTable.get( index );
            if ( slot._state != NetConnectionState::Connected )
                continue;
            const size_t firstNotice = outInbound._listDeliveryNotice.size();
            slot._connection->pollDeliveryNotices( outInbound._listDeliveryNotice );
            for ( size_t noticeIndex = firstNotice; noticeIndex < outInbound._listDeliveryNotice.size(); ++noticeIndex )
            {
                outInbound._listDeliveryNotice[noticeIndex]._connectionID = index;
            }
            for ( int32 channel = 0; channel < static_cast<int32>( NetChannelType::Count ); ++channel )
            {
                // 메시지를 아레나 뒤에 바로 덧붙인다(중간 버퍼를 거치지 않는다).
                int32 offset = static_cast<int32>( outInbound._bytes.size() );
                while ( slot._connection->receiveMessageAppend( static_cast<NetChannelType>( channel ), outInbound._bytes ) )
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

    bool NetEndpoint::isServer() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _bServer != SW_FALSE;
    }

    int32 NetEndpoint::getClientIndex() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _clientIndex;
    }

    uint64 NetEndpoint::getRejectedPacketCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _rejectedPacketCount;
    }

    uint64 NetEndpoint::getConnectionPrincipal( int32 connectionID ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _bServer == SW_FALSE )
            return 0;
        const NetEndpointSlot* pSlot = _slotTable.findConnected( connectionID );
        return pSlot != nullptr && pSlot->_security != nullptr ? pSlot->_security->_principalID : 0;
    }

    uint64 NetEndpoint::getAuthenticationFailureCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _authenticationFailureCount;
    }

    uint64 NetEndpoint::getReplayRejectedCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _replayRejectedCount;
    }

    uint64 NetEndpoint::getSlotVisitCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _slotVisitCount;
    }

    uint32 NetEndpoint::getProtocolID() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _protocolID;
    }

    int32 NetEndpoint::getMaxBytesPerSecond() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _settings._maxBytesPerSecond;
    }

    bool NetEndpoint::isConnectionSaturated( int32 connectionID ) const
    {
        ScopedLock<mutex>      lock{ _mutex };
        const NetEndpointSlot* pSlot = _slotTable.findConnected( connectionID );
        if ( pSlot == nullptr )
            return false;
        return pSlot->_sendCredit <= 0.0 || pSlot->_connection->getQueuedUnreliableCount() > 0;
    }

    bool NetEndpoint::getConnectionStats( int32 connectionID, NetConnectionStats& outStats ) const
    {
        ScopedLock<mutex>      lock{ _mutex };
        const NetEndpointSlot* pSlot = _slotTable.findConnected( connectionID );
        if ( pSlot == nullptr )
            return false;
        outStats                       = pSlot->_connection->getStats();
        outStats._pendingReliableCount = pSlot->_connection->getPendingReliableCount();
        return true;
    }

    NetConnectionState NetEndpoint::getConnectionState( int32 connectionID ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _slotTable.isValid( connectionID ) ? _slotTable.get( connectionID )._state : NetConnectionState::Disconnected;
    }

    const NetConnection* NetEndpoint::findConnection( int32 connectionID ) const
    {
        ScopedLock<mutex>      lock{ _mutex };
        const NetEndpointSlot* pSlot = _slotTable.findConnected( connectionID );
        return pSlot != nullptr ? pSlot->_connection.get() : nullptr;
    }

    NetAddress NetEndpoint::getConnectionAddress( int32 connectionID ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _slotTable.isValid( connectionID ) ? _slotTable.get( connectionID )._address : NetAddress{};
    }

    int32 NetEndpoint::getConnectedCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _slotTable.countConnected();
    }

    void NetEndpoint::collectConnected( vector<int32>& outListConnection ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        _slotTable.collectConnected( outListConnection );
    }
} // namespace sw
