// TradeServerTest — 거래 바인딩(패킷 → 거래 서비스 → 응답 · 알림): 깨진 몸 · 모르는 메서드 · 로그인 안 한 요청, 위조 값(남의 거래, 범위 밖 수량), 상대 찾기(이 서버 · 접속 상태 관리자 · 없음),
// 정산 응답과 알림의 잔액, 떠난 계정, 다른 서버로 넘기는 알림, 기능 플래그, 저장소 실패. 진짜 `TradeService`(메모리 저장소 + 원장)를 루프백 호스트에 올린다 — 로그인은 테스트 로그인이고,
// 디렉터리 · 접속 상태는 가짜다. 상태 기계 표는 TradeStateMachineTest, 저장 규칙은 TradeServiceTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeSessionRecord.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeServer.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeService.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStateMachine.h"
#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

using namespace sw;

namespace
{
    constexpr int64     kNowMs     = 1700000000000ll;
    constexpr AccountID kAlice     = 11;
    constexpr AccountID kBob       = 12;
    constexpr AccountID kCarol     = 13;
    constexpr AccountID kRemote    = 14; ///< 디렉터리에는 있지만 이 서버에 연결이 없는 계정(다른 서버)
    constexpr uint64    kServerID  = 1;
    constexpr int32     kAliceSlot = 0;
    constexpr int32     kBobSlot   = 1;
    constexpr int32     kCarolSlot = 2;

    /** @brief 표시 이름 → 계정의 가짜 디렉터리입니다. */
    class FakeTradeDirectory final : public IAccountDirectory
    {
    public:
        void add( string_view name, AccountID accountID )
        {
            AccountIdentity& identity = _listIdentity.emplace_back();
            identity._displayName     = string( name );
            identity._accountID       = accountID;
        }

        [[nodiscard]] bool findIdentity( AccountID accountID, AccountIdentity& outIdentity ) const override
        {
            for ( const AccountIdentity& identity : _listIdentity )
            {
                if ( identity._accountID != accountID )
                    continue;
                outIdentity = identity;
                return true;
            }
            return false;
        }
        [[nodiscard]] bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const override
        {
            for ( const AccountIdentity& identity : _listIdentity )
            {
                if ( identity._displayName != displayName )
                    continue;
                outIdentity = identity;
                return true;
            }
            return false;
        }
        bool isAccountOnline( AccountID accountID ) const override
        {
            AccountIdentity identity;
            return findIdentity( accountID, identity );
        }

    private:
        vector<AccountIdentity> _listIdentity{};
    };

    /** @brief 찾기를 들고 있다가 테스트가 답하는 가짜 접속 상태 관리자입니다. 원격 알림은 받는 계정만 적습니다. */
    class FakeTradePresence final : public IAccountPresence
    {
    public:
        vector<AccountID> _listRelayedAccount{};

        uint64 submitFindByDisplayName( string_view displayName, const AccountPresenceDelegate& onFound, const Subscription& owner ) override
        {
            (void)displayName;
            (void)owner;
            const uint64 requestID = ++_lastRequestID;
            _listFind.push_back( PendingFind{ onFound, requestID } );
            return requestID;
        }
        uint64 submitFindByAccount( AccountID accountID, const AccountPresenceDelegate& onFound, const Subscription& owner ) override
        {
            (void)accountID;
            return submitFindByDisplayName( "", onFound, owner );
        }
        bool relayRemotePush( const OnlinePushRelay& relay ) override
        {
            _listRelayedAccount.push_back( relay._accountID );
            return true;
        }

        size_t getPendingCount() const { return _listFind.size(); }

        /** @brief 맨 앞 찾기에 @p accountID 로 답합니다(0 이면 못 찾음). */
        void answerFront( AccountID accountID )
        {
            const PendingFind find = _listFind.front();
            _listFind.erase( _listFind.begin() );
            AccountPresenceResult result;
            result._requestID           = find._requestID;
            result._identity._accountID = accountID;
            find._onFound( result );
        }

    protected:
        void releaseSubscription( uint64 id ) override { (void)id; }

    private:
        struct PendingFind
        {
            AccountPresenceDelegate _onFound;
            uint64                  _requestID;
        };

        vector<PendingFind> _listFind{};
        uint64              _lastRequestID{ 0 };
    };

    /** @brief 클라이언트 쪽 거래 알림을 받아 적습니다. 키트 클라이언트 대신 맨 요청을 보내므로 영역만 맞춘다. */
    class TradePushSink final : public IOnlineClientService
    {
    public:
        vector<uint16>      _listKind{};
        vector<TradeNotice> _listReply{};

        const OnlineProtocolEntry& getProtocol() const override { return TradeSessionRecord::kOnlineEntry; }
        void                       onServicePush( uint16 kind, BitReader& body ) override
        {
            TradeNotice reply;
            SW_EXPECT_TRUE( TradeSessionRecord::MessageTypes::read( body, reply ) == FixedReadResult::Ok );
            _listKind.push_back( kind );
            _listReply.push_back( std::move( reply ) );
        }
    };

    /** @brief 맨 요청의 답입니다. 몸은 콜백 뒤에도 쓰게 복사한다. */
    struct TradeResponseSink
    {
        vector<uint8> _bodyBytes{};
        int32         _count{ 0 };
        uint16        _errorCode{ OnlineError::kOk };
        uint8         _bOk : 1;

        TradeResponseSink()
            : _bOk{ SW_FALSE }
        {
        }

        void onResponse( const OnlineResponse& response )
        {
            ++_count;
            _errorCode = response._errorCode;
            _bOk       = response.isOk() ? SW_TRUE : SW_FALSE;
            _bodyBytes.assign( response._pBody, response._pBody + response._bodySize );
        }

        /** @brief 성공 응답의 몸을 읽습니다. */
        TradeReply readReply() const
        {
            TradeReply reply;
            BitReader  reader( _bodyBytes.data(), static_cast<int32>( _bodyBytes.size() ) );
            SW_EXPECT_TRUE( _bOk == SW_TRUE );
            SW_EXPECT_TRUE( TradeSessionRecord::MessageTypes::read( reader, reply ) == FixedReadResult::Ok );
            return reply;
        }
    };

    /**
     * @brief 거래 서비스 + 바인딩 + 루프백 서버 + 세 계정의 연결입니다.
     * @details 서버를 먼저 내린다(저장소 → 호스트 → 남은 완료) — 완료가 서비스를 부르므로 서비스의 `shutdown` 은 그 뒤다.
     */
    struct TradeServerFixture
    {
        DefaultTradePolicy                  _policy{};
        RemoteConfig                        _remoteConfig{};
        TradeService                        _tradeService{};
        FakeTradeDirectory                  _directory{};
        FakeTradePresence                   _presence{};
        TradeServer                         _tradeServer{};
        TradePushSink                       _arrSink[3]{};
        TradeResponseSink                   _response{};
        unique_ptr<test::OnlineTestFixture> _pRig{};

        /** @brief @p bWithPresence 면 서버 여러 대의 모양이다(상대를 접속 상태 관리자에게도 묻는다). */
        explicit TradeServerFixture( bool bWithPresence = false, bool bWithRemoteConfig = false )
        {
            _pRig = make_unique<test::OnlineTestFixture>( false );
            _tradeService.initialize( &_pRig->_server._store, &_policy, nullptr, kServerID, TradeServiceConfig{} );
            _tradeServer.initialize( &_tradeService, &_directory, bWithPresence ? &_presence : nullptr );
            _directory.add( "alice", kAlice );
            _directory.add( "bob", kBob );
            _directory.add( "carol", kCarol );
            _directory.add( "remote", kRemote );
            SW_EXPECT_TRUE( _pRig->_server._host.registerServerService( &_tradeServer ) );
            if ( bWithRemoteConfig )
                startWithRemoteConfig();
            else
                _pRig->_server.start();
            for ( TradePushSink& sink : _arrSink )
            {
                (void)_pRig->_clients.connect( _pRig->_server._port, { &sink } );
            }
            pump();
            _pRig->_clients.login( kAliceSlot, kAlice );
            _pRig->_clients.login( kBobSlot, kBob );
            _pRig->_clients.login( kCarolSlot, kCarol );
            pump();
        }

        ~TradeServerFixture()
        {
            _pRig.reset();
            _tradeServer.shutdown();
            _tradeService.shutdown();
        }

        /** @brief `OnlineTestServer::start` 와 같은 설정에 원격 설정만 더해 받기 시작합니다. */
        void startWithRemoteConfig()
        {
            test::OnlineTestServer& server = _pRig->_server;
            OnlineServiceHostConfig settings;
            settings._transportSettings._ioThreadCount = 0;
            settings._listenAddress                    = NetAddress::makeLoopback( server._port );
            settings._pServiceStore                    = &server._store;
            settings._pEphemeralStore                  = &server._cache;
            settings._pServerBusTransport              = &server._bus;
            settings._pRemoteConfig                    = &_remoteConfig;
            settings._requestBurstPerRemote            = 1000;
            settings._requestBurstPerAccount           = 1000;
            {
                const auto result = server._host.initialize( server._transport.get(), settings );
                SW_EXPECT_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
            }
        }

        void pump() { test::updateAll( { &_pRig->_server }, _pRig->_clients, kNowMs, 12 ); }

        void grant( AccountID accountID, string_view assetID, int64 amount )
        {
            LedgerTransferRequest request;
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( accountID ), string( assetID ), amount } );
            request._journalKey = "test/grant/" + to_string( accountID ) + "/" + string( assetID );
            request._reason     = "test.grant";
            LedgerTransferResult outcome;
            SW_EXPECT_TRUE( Ledger::executeTransfer( _pRig->_database, request, outcome ) == LedgerResult::Ok );
        }

        int64 balanceOf( AccountID accountID, string_view assetID )
        {
            LedgerBalance balance;
            SW_EXPECT_TRUE( Ledger::readBalance( _pRig->_database, LedgerHolder::makeAccount( accountID ), assetID, balance ) == ServiceStoreResult::Ok );
            return balance._amount;
        }

        /** @brief 맨 요청을 보내고 돌립니다. 답은 `_response` 입니다. */
        const TradeResponseSink& sendRaw( int32 slot, uint16 method, const BitWriter& body )
        {
            _response = TradeResponseSink{};
            NetRequestOptions options;
            options._idempotencyKey = NetIdempotencyKey::makeRandom(); // 표의 호출 규칙이다. 빠진 요청은 kInvalidRequest 다
            (void)_pRig->_clients.getClient( slot ).sendRequest( method, body, options, OnlineResponseDelegate::create<&TradeResponseSink::onResponse>( &_response ) );
            pump();
            return _response;
        }

        template <typename TMessage>
        const TradeResponseSink& send( int32 slot, uint16 method, const TMessage& message )
        {
            BitWriter body;
            SW_EXPECT_TRUE( TradeSessionRecord::MessageTypes::write( body, message ) );
            return sendRaw( slot, method, body );
        }

        /** @brief 답 하나를 받아야 하는 요청입니다. 공통 범주로 끝났으면 빈 응답이고 이유는 `_response._errorCode` 이다. */
        template <typename TMessage>
        TradeReply call( int32 slot, uint16 method, const TMessage& message )
        {
            const TradeResponseSink& response = send( slot, method, message );
            SW_EXPECT_EQUAL( 1, response._count );
            if ( response._bOk == SW_FALSE )
                return TradeReply{};
            return response.readReply();
        }

        TradeReply invite( int32 slot, string_view peerName )
        {
            TradeInviteRequest request;
            request._peerDisplayName = string( peerName );
            return call( slot, TradeMethod::Invite::kID, request );
        }

        TradeReply respond( int32 slot, uint64 tradeID, bool bAccept = true )
        {
            TradeRespondRequest request;
            request._tradeID = tradeID;
            request._bAccept = bAccept ? SW_TRUE : SW_FALSE;
            return call( slot, TradeMethod::Respond::kID, request );
        }

        TradeReply offer( int32 slot, uint64 tradeID, string_view assetID, int64 amount )
        {
            TradeLeg leg;
            leg._assetID = string( assetID );
            leg._amount  = amount;
            TradeOfferRequest request;
            request._tradeID = tradeID;
            request._listLeg.push_back( leg );
            return call( slot, TradeMethod::SetOffer::kID, request );
        }

        TradeReply lock( int32 slot, uint64 tradeID )
        {
            TradeIDRequest request;
            request._tradeID = tradeID;
            return call( slot, TradeMethod::Lock::kID, request );
        }

        TradeReply confirm( int32 slot, const TradeSnapshot& snapshot, AccountID accountID )
        {
            const int32         sideIndex = snapshot.findSideIndex( accountID ).value();
            TradeConfirmRequest request;
            request._tradeID          = snapshot._tradeID;
            request._seenOwnRevision  = snapshot._arrSide[sideIndex]._offerRevision;
            request._seenPeerRevision = snapshot._arrSide[1 - sideIndex]._offerRevision;
            return call( slot, TradeMethod::Confirm::kID, request );
        }

        void clearPushes()
        {
            for ( TradePushSink& sink : _arrSink )
            {
                sink._listKind.clear();
                sink._listReply.clear();
            }
        }
    };

    /** @brief 답을 살핍니다. */
    struct TradeServerTestUtil
    {
        static BitWriter makeJunk( int32 byteCount )
        {
            const vector<uint8> listByte( static_cast<size_t>( byteCount ), uint8( 0xFF ) );
            BitWriter           writer;
            writer.writeBytes( listByte.data(), byteCount );
            return writer;
        }

        /** @brief 잔액 목록에서 자산의 값입니다. 없으면 비어 있습니다. */
        static optional<int64> findBalance( const vector<TradeBalance>& listBalance, string_view assetID )
        {
            for ( const TradeBalance& balance : listBalance )
            {
                if ( balance._assetID == assetID )
                    return balance._amount;
            }
            return nullopt;
        }
    };
} // namespace

/** @brief [TradeServerTest] 신청: 응답은 신청한 쪽, 초대 알림은 받는 쪽에만 간다(신청한 쪽은 응답으로 안다) */
SW_TEST_CASE( TradeServerTest, InvitePushesOnlyToThePeer )
{
    TradeServerFixture rig;
    const TradeReply   reply = rig.invite( kAliceSlot, "bob" );
    SW_EXPECT_TRUE( reply._result == TradeResult::Ok );
    SW_EXPECT_TRUE( reply._snapshot._state == TradeState::Invited );
    SW_EXPECT_NOT_EQUAL( uint64( 0 ), reply._snapshot._tradeID );
    SW_EXPECT_EQUAL( kAlice, reply._snapshot._arrSide[0]._accountID );
    SW_EXPECT_EQUAL( kBob, reply._snapshot._arrSide[1]._accountID );

    SW_EXPECT_TRUE( rig._arrSink[kAliceSlot]._listKind.empty() );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._arrSink[kBobSlot]._listKind.size() );
    SW_EXPECT_EQUAL( TradeMethod::Invited::kKind, rig._arrSink[kBobSlot]._listKind[0] );
    SW_EXPECT_EQUAL( reply._snapshot._tradeID, rig._arrSink[kBobSlot]._listReply[0]._snapshot._tradeID );
    SW_EXPECT_TRUE( rig._arrSink[kCarolSlot]._listKind.empty() );
}

/** @brief [TradeServerTest] 찾지 못한 상대: 서버 한 대(접속 상태 없음)면 바로 PeerOffline 이다. 자기 자신 신청은 Invalid 다 */
SW_TEST_CASE( TradeServerTest, UnknownPeerAndSelf )
{
    TradeServerFixture rig;
    SW_EXPECT_TRUE( rig.invite( kAliceSlot, "nobody" )._result == TradeResult::PeerOffline );
    SW_EXPECT_TRUE( rig.invite( kAliceSlot, "" )._result == TradeResult::PeerOffline );
    (void)rig.invite( kAliceSlot, "alice" );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig._response._errorCode ); // 자기 자신에게는 신청할 수 없다
    SW_EXPECT_EQUAL( 0, rig._tradeService.getOpenTradeCount() );
}

/** @brief [TradeServerTest] 패킷: 메서드마다 깨진 · 빈 몸은 kInvalidRequest 이고 거래가 생기거나 바뀌지 않는다. 다른 타입의 몸도 같다. 모르는 메서드도 같다 */
SW_TEST_CASE( TradeServerTest, MalformedBodiesAreRefused )
{
    TradeServerFixture rig;
    const uint64       tradeID = rig.invite( kAliceSlot, "bob" )._snapshot._tradeID;
    rig.clearPushes();
    for ( const uint16 method : { TradeMethod::Invite::kID, TradeMethod::Respond::kID, TradeMethod::SetOffer::kID, TradeMethod::Lock::kID, TradeMethod::Confirm::kID, TradeMethod::Cancel::kID } )
    {
        SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( kBobSlot, method, BitWriter{} )._errorCode );
        SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( kBobSlot, method, TradeServerTestUtil::makeJunk( 24 ) )._errorCode );
    }
    TradeIDRequest idRequest;
    idRequest._tradeID = tradeID;
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.send( kBobSlot, TradeMethod::Respond::kID, idRequest )._errorCode ); // 수락 칸이 없는 다른 메시지
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( kBobSlot, static_cast<uint16>( OnlineMethodRange::kTrade + 0x70 ), BitWriter{} )._errorCode );

    SW_EXPECT_TRUE( rig._arrSink[kAliceSlot]._listKind.empty() );
    SW_EXPECT_TRUE( rig.respond( kBobSlot, tradeID )._snapshot._state == TradeState::Open ); // 앞의 어느 요청도 초대를 건드리지 않았다
}

/** @brief [TradeServerTest] 로그인하지 않은 연결은 호스트가 kUnauthenticated 로 막아 바인딩에 닿지 않는다 */
SW_TEST_CASE( TradeServerTest, AnonymousConnectionIsRefused )
{
    TradeServerFixture rig;
    TradePushSink      anonymousSink;
    const int32        anonymous = rig._pRig->_clients.connect( rig._pRig->_server._port, { &anonymousSink } );
    rig.pump();
    TradeInviteRequest request;
    request._peerDisplayName = "bob";
    SW_EXPECT_EQUAL( OnlineError::kUnauthenticated, rig.send( anonymous, TradeMethod::Invite::kID, request )._errorCode );
    SW_EXPECT_EQUAL( 0, rig._tradeService.getOpenTradeCount() );
    rig._pRig.reset(); // 받기를 내린 뒤 지역 알림 받이가 사라진다
}

/** @brief [TradeServerTest] 위조: 없는 거래는 NotFound, 남의 거래는 NotParty 이고, 음수 · 0 · 범위 밖 수량은 Invalid, 항목이 너무 많으면 TooManyLegs 다. 어느 것도 거래를 바꾸지 않는다 */
SW_TEST_CASE( TradeServerTest, ForgedValuesAreRefused )
{
    TradeServerFixture rig;
    rig.grant( kAlice, "cur.gold", 100 );
    const uint64 tradeID = rig.invite( kAliceSlot, "bob" )._snapshot._tradeID;
    SW_EXPECT_TRUE( rig.respond( kBobSlot, tradeID )._result == TradeResult::Ok );

    SW_EXPECT_TRUE( rig.lock( kBobSlot, tradeID + 1000 )._result == TradeResult::NotFound );
    SW_EXPECT_TRUE( rig.lock( kBobSlot, 0 )._result != TradeResult::Ok );
    SW_EXPECT_TRUE( rig.lock( kCarolSlot, tradeID )._result == TradeResult::NotParty );
    SW_EXPECT_TRUE( rig.offer( kCarolSlot, tradeID, "cur.gold", 1 )._result == TradeResult::NotParty );

    (void)rig.offer( kAliceSlot, tradeID, "cur.gold", -5 );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig._response._errorCode );
    (void)rig.offer( kAliceSlot, tradeID, "cur.gold", 0 );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig._response._errorCode );
    (void)rig.offer( kAliceSlot, tradeID, "cur.gold", LedgerConstant::kMaxAmount + 1 );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig._response._errorCode );

    TradeOfferRequest tooMany;
    tooMany._tradeID = tradeID;
    for ( int32 index = 0; index <= TradeConstant::kMaxLegsPerSide; ++index )
    {
        TradeLeg& leg = tooMany._listLeg.emplace_back();
        leg._assetID  = "cur.c" + to_string( index );
        leg._amount   = 1;
    }
    SW_EXPECT_TRUE( rig.call( kAliceSlot, TradeMethod::SetOffer::kID, tooMany )._result == TradeResult::TooManyLegs );

    const TradeReply same = rig.lock( kAliceSlot, tradeID );
    SW_EXPECT_TRUE( same._result == TradeResult::Ok ); // 거절된 제시는 없던 일이라 빈 제시를 잠근다
    SW_EXPECT_TRUE( same._snapshot._arrSide[0]._listLeg.empty() );
    SW_EXPECT_EQUAL( int64( 100 ), rig.balanceOf( kAlice, "cur.gold" ) );
}

/** @brief [TradeServerTest] 낡은 확정: 본 버전이 지금 버전과 다르면 StaleOffer 이고 아무것도 옮기지 않는다(바꿔치기 사기) */
SW_TEST_CASE( TradeServerTest, StaleConfirmMovesNothing )
{
    TradeServerFixture rig;
    rig.grant( kAlice, "cur.gold", 100 );
    const uint64 tradeID = rig.invite( kAliceSlot, "bob" )._snapshot._tradeID;
    SW_EXPECT_TRUE( rig.respond( kBobSlot, tradeID )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( rig.offer( kAliceSlot, tradeID, "cur.gold", 10 )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( rig.lock( kAliceSlot, tradeID )._result == TradeResult::Ok );
    const TradeSnapshot locked = rig.lock( kBobSlot, tradeID )._snapshot;

    TradeConfirmRequest request;
    request._tradeID          = tradeID;
    request._seenOwnRevision  = locked._arrSide[1]._offerRevision;
    request._seenPeerRevision = locked._arrSide[0]._offerRevision + 1;
    SW_EXPECT_TRUE( rig.call( kBobSlot, TradeMethod::Confirm::kID, request )._result == TradeResult::StaleOffer );
    SW_EXPECT_EQUAL( int64( 100 ), rig.balanceOf( kAlice, "cur.gold" ) );
}

/** @brief [TradeServerTest] 정산: 양쪽을 옮기고, 응답에는 요청한 쪽, 닫힘 알림에는 각자의 이동 뒤 잔액만 실린다. 남의 잔액이 섞이지 않는다 */
SW_TEST_CASE( TradeServerTest, SettlementCarriesOwnBalances )
{
    TradeServerFixture rig;
    rig.grant( kAlice, "cur.gold", 100 );
    rig.grant( kBob, "cur.gem", 50 );
    const uint64 tradeID = rig.invite( kAliceSlot, "bob" )._snapshot._tradeID;
    SW_EXPECT_TRUE( rig.respond( kBobSlot, tradeID )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( rig.offer( kAliceSlot, tradeID, "cur.gold", 40 )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( rig.offer( kBobSlot, tradeID, "cur.gem", 20 )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( rig.lock( kAliceSlot, tradeID )._result == TradeResult::Ok );
    const TradeSnapshot locked = rig.lock( kBobSlot, tradeID )._snapshot;
    const TradeSnapshot first  = rig.confirm( kAliceSlot, locked, kAlice )._snapshot;
    SW_EXPECT_FALSE( first.isClosed() );
    rig.clearPushes();

    const TradeReply settled = rig.confirm( kBobSlot, first, kBob );
    SW_REQUIRE_TRUE( settled._result == TradeResult::Ok );
    SW_EXPECT_TRUE( settled._snapshot._state == TradeState::Settled );
    SW_EXPECT_TRUE( TradeServerTestUtil::findBalance( settled._listBalance, "cur.gold" ) == int64( 40 ) );
    SW_EXPECT_TRUE( TradeServerTestUtil::findBalance( settled._listBalance, "cur.gem" ) == int64( 30 ) );

    SW_REQUIRE_EQUAL( size_t( 1 ), rig._arrSink[kAliceSlot]._listKind.size() );
    SW_EXPECT_EQUAL( TradeMethod::Closed::kKind, rig._arrSink[kAliceSlot]._listKind[0] );
    const TradeNotice& alicePush = rig._arrSink[kAliceSlot]._listReply[0];
    SW_EXPECT_TRUE( alicePush._snapshot._state == TradeState::Settled );
    SW_EXPECT_TRUE( TradeServerTestUtil::findBalance( alicePush._listBalance, "cur.gold" ) == int64( 60 ) );
    SW_EXPECT_TRUE( TradeServerTestUtil::findBalance( alicePush._listBalance, "cur.gem" ) == int64( 20 ) );

    SW_REQUIRE_EQUAL( size_t( 1 ), rig._arrSink[kBobSlot]._listKind.size() );
    SW_EXPECT_TRUE( TradeServerTestUtil::findBalance( rig._arrSink[kBobSlot]._listReply[0]._listBalance, "cur.gold" ) == int64( 40 ) );
    SW_EXPECT_TRUE( rig._arrSink[kCarolSlot]._listKind.empty() );

    SW_EXPECT_EQUAL( int64( 60 ), rig.balanceOf( kAlice, "cur.gold" ) );
    SW_EXPECT_EQUAL( int64( 40 ), rig.balanceOf( kBob, "cur.gold" ) );
}

/** @brief [TradeServerTest] 제시가 바뀌면 두 당사자에게 갱신 알림이 가고, 거절하면 닫힘 알림과 이유가 간다 */
SW_TEST_CASE( TradeServerTest, UpdatesAndClosePush )
{
    TradeServerFixture rig;
    rig.grant( kAlice, "cur.gold", 100 );
    const uint64 tradeID = rig.invite( kAliceSlot, "bob" )._snapshot._tradeID;
    SW_EXPECT_TRUE( rig.respond( kBobSlot, tradeID )._result == TradeResult::Ok );
    rig.clearPushes();

    SW_EXPECT_TRUE( rig.offer( kAliceSlot, tradeID, "cur.gold", 10 )._result == TradeResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._arrSink[kBobSlot]._listKind.size() );
    SW_EXPECT_EQUAL( TradeMethod::Update::kKind, rig._arrSink[kBobSlot]._listKind[0] );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._arrSink[kBobSlot]._listReply[0]._snapshot._arrSide[0]._listLeg.size() );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._arrSink[kAliceSlot]._listKind.size() );
    rig.clearPushes();

    TradeIDRequest cancel;
    cancel._tradeID = tradeID;
    SW_EXPECT_TRUE( rig.call( kBobSlot, TradeMethod::Cancel::kID, cancel )._result == TradeResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._arrSink[kAliceSlot]._listKind.size() );
    SW_EXPECT_EQUAL( TradeMethod::Closed::kKind, rig._arrSink[kAliceSlot]._listKind[0] );
    SW_EXPECT_TRUE( rig._arrSink[kAliceSlot]._listReply[0]._snapshot._closeReason == TradeCloseReason::CancelledByParty );
}

/** @brief [TradeServerTest] 계정이 떠나면 그 계정의 열린 거래를 닫고 상대에게 PartyLeft 로 알린다 */
SW_TEST_CASE( TradeServerTest, LeavingClosesTheTrade )
{
    TradeServerFixture rig;
    const uint64       tradeID = rig.invite( kAliceSlot, "bob" )._snapshot._tradeID;
    SW_EXPECT_TRUE( rig.respond( kBobSlot, tradeID )._result == TradeResult::Ok );
    rig.clearPushes();

    rig._pRig->_clients.drop( kBobSlot );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._arrSink[kAliceSlot]._listKind.size() );
    SW_EXPECT_EQUAL( TradeMethod::Closed::kKind, rig._arrSink[kAliceSlot]._listKind[0] );
    SW_EXPECT_TRUE( rig._arrSink[kAliceSlot]._listReply[0]._snapshot._closeReason == TradeCloseReason::PartyLeft );
    SW_EXPECT_EQUAL( 0, rig._tradeService.getOpenTradeCount() );
}

/** @brief [TradeServerTest] 서버 여러 대: 이 서버에 없는 이름은 접속 상태 관리자에게 묻고, 답이 오는 다음 틱에 응답한다. 못 찾으면 PeerOffline, 모르는 찾기 번호는 무시한다 */
SW_TEST_CASE( TradeServerTest, PresenceLookupFinishesTheInvite )
{
    TradeServerFixture rig( true );
    TradeInviteRequest request;
    request._peerDisplayName = "elsewhere";

    SW_EXPECT_EQUAL( 0, rig.send( kAliceSlot, TradeMethod::Invite::kID, request )._count ); // 찾기를 기다린다
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._presence.getPendingCount() );
    rig._presence.answerFront( kInvalidAccountID );
    rig.pump();
    SW_EXPECT_EQUAL( 1, rig._response._count );
    SW_EXPECT_TRUE( rig._response.readReply()._result == TradeResult::PeerOffline );

    SW_EXPECT_EQUAL( 0, rig.send( kAliceSlot, TradeMethod::Invite::kID, request )._count );
    rig._presence.answerFront( kBob );
    rig.pump();
    SW_EXPECT_EQUAL( 1, rig._response._count );
    const TradeReply reply = rig._response.readReply();
    SW_EXPECT_TRUE( reply._result == TradeResult::Ok );
    SW_EXPECT_EQUAL( kBob, reply._snapshot._arrSide[1]._accountID );
}

/** @brief [TradeServerTest] 찾기를 기다리는 사이 신청한 계정이 떠나면 신청을 거둔다 — 뒤늦은 답으로 떠난 계정의 거래가 열려 아무도 닫지 못하면 상대가 시한까지 "거래 중" 이 된다 */
SW_TEST_CASE( TradeServerTest, LookupOfADepartedAccountOpensNothing )
{
    TradeServerFixture rig( true );
    TradeInviteRequest request;
    request._peerDisplayName = "elsewhere";
    (void)rig.send( kAliceSlot, TradeMethod::Invite::kID, request );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._presence.getPendingCount() );

    rig._pRig->_clients.drop( kAliceSlot );
    rig.pump();
    rig._presence.answerFront( kBob );
    rig.pump();
    SW_EXPECT_EQUAL( 0, rig._tradeService.getOpenTradeCount() );
    SW_EXPECT_TRUE( rig._arrSink[kBobSlot]._listKind.empty() );

    SW_EXPECT_TRUE( rig.invite( kBobSlot, "carol" )._result == TradeResult::Ok ); // 밥은 묶이지 않았다
}

/** @brief [TradeServerTest] 상대가 이 서버에 연결돼 있지 않으면 알림을 접속 상태 관리자에게 넘긴다 */
SW_TEST_CASE( TradeServerTest, PushForAnotherServerGoesThroughPresence )
{
    TradeServerFixture rig( true );
    const TradeReply   reply = rig.invite( kAliceSlot, "remote" );
    SW_EXPECT_TRUE( reply._result == TradeResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._presence._listRelayedAccount.size() );
    SW_EXPECT_EQUAL( kRemote, rig._presence._listRelayedAccount[0] );
}

/** @brief [TradeServerTest] 저장소 실패는 kUnavailable 이고 같은 요청을 다시 하면 된다 */
SW_TEST_CASE( TradeServerTest, StoreFailureIsUnavailable )
{
    TradeServerFixture rig;
    const uint64       tradeID = rig.invite( kAliceSlot, "bob" )._snapshot._tradeID;
    rig._pRig->_database.armFault( ServiceStoreFault::RejectCommit );
    TradeRespondRequest request;
    request._tradeID = tradeID;
    request._bAccept = SW_TRUE;
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, rig.send( kBobSlot, TradeMethod::Respond::kID, request )._errorCode );
    SW_EXPECT_TRUE( rig.respond( kBobSlot, tradeID )._result == TradeResult::Ok );
}

/** @brief [TradeServerTest] 기능 플래그가 꺼지면 모든 요청이 kFeatureDisabled 이고 서비스에 닿지 않는다. 플래그가 없으면 기본은 켬이다 */
SW_TEST_CASE( TradeServerTest, FeatureFlagBlocksRequests )
{
    TradeServerFixture rig( false, true );
    SW_EXPECT_TRUE( rig.invite( kAliceSlot, "bob" )._result == TradeResult::Ok ); // 플래그 없음 = 켬

    RemoteConfigValue value;
    value._type    = RemoteConfigValueType::Flag;
    value._integer = 0;
    ServiceAuditEntry audit;
    audit._actor   = "gm.0000000000000001";
    audit._action  = "config.set";
    audit._subject = "config";
    audit._timeMs  = 1;
    rig._remoteConfig.submitSet( rig._pRig->_server._store, TradeServer::kFeatureFlag, value, audit );
    rig.pump();

    TradeInviteRequest request;
    request._peerDisplayName = "carol";
    SW_EXPECT_EQUAL( OnlineError::kFeatureDisabled, rig.send( kBobSlot, TradeMethod::Invite::kID, request )._errorCode );
    SW_EXPECT_EQUAL( OnlineError::kFeatureDisabled, rig.sendRaw( kBobSlot, TradeMethod::Lock::kID, TradeServerTestUtil::makeJunk( 4 ) )._errorCode ); // 몸을 읽기 전에 막는다
    SW_EXPECT_EQUAL( 1, rig._tradeService.getOpenTradeCount() );
}

/** @brief [TradeServerTest] 다시 시도할 공통 범주(저장소 일시 장애)는 요청 층이 기억하지 않아 같은 멱등 키의 거래 명령 재시도가 서비스에 다시 닿고, 끝난 결과는 기억한다 */
SW_TEST_CASE( TradeServerTest, RetryableReplyReachesTheServiceAgain )
{
    test::ReplyRetentionFixture<TradeSessionRecord, TradeReply> rig;
    TradeReply                                                  finished;
    finished._result = TradeResult::Ok;
    rig.expectFailureReachesService( TradeMethod::Invite::kID, OnlineError::kUnavailable, finished );
}
