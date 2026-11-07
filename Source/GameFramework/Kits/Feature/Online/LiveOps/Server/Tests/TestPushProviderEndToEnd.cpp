// PushProviderEndToEndTest — 설정이 고른 실제 푸시 제공자를 로컬 가짜 끝점까지 잇는다. 제공자 파일 → 환경 변수 비밀 → 발송기 → FCM(실제 HTTP/1.1 · OpenSSL 서명) · APNs(실제 HTTP/2 · TLS ALPN h2 · ES256) → 가짜 서버 → 결과.
// 키는 케이스마다 새로 만들고 실제 계정은 쓰지 않는다. APNs 는 이 파일의 가짜 서버가 처음으로 전송까지 지난다 - `TestPushProviders.cpp` 는 HTTP/2 서버가 없어 요청 만들기 · 응답 분류만 본다(nogpu).
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushProviderSet.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/HTTP/HTTP2Frame.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/HTTP/Hpack.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Provider/OnlineProviderFile.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPLink.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPServer.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineEndpointTestUtil.h"
#include "GameFramework/Runtime/Online/Service/Tests/OnlineHostTestUtil.h"
#include "GameFramework/Runtime/Online/Store/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr AccountID   kAccount   = 42;
    constexpr const utf8* kFcmEnv    = "SW_TEST_PUSH_FCM_ACCOUNT";
    constexpr const utf8* kApnsEnv   = "SW_TEST_PUSH_APNS_KEY";
    constexpr const utf8* kApnsToken = "abcd1234";
    constexpr const utf8* kFcmToken  = "fcm-device-token";
    constexpr const utf8* kTopic     = "com.x.game";

    /** @brief 가짜 APNs 가 받은 요청 하나입니다. */
    struct ApnsRequestRecord
    {
        string _path{};
        string _authorization{};
        string _topic{};
        string _pushType{};
        string _body{};
    };

    /**
     * @brief HTTP/2 서버 쪽을 최소로 말하는 가짜 APNs 입니다(TLS 1.3 · ALPN h2). `update` 를 부른 스레드에서 답합니다.
     * @details 연결 서문과 SETTINGS 를 받고, 스트림마다 HEADERS · DATA 를 모아 END_STREAM 에서 답합니다. 쓰는 것은 제품 코드의 프레임 · HPACK 함수입니다(서버 상태 기계만 여기 있다).
     */
    class FakeApnsServer final : public IStreamHandler
    {
    public:
        vector<ApnsRequestRecord> _listRequest{};
        int32                     _status{ 200 };

        ~FakeApnsServer() override
        {
            if ( _transport != nullptr )
                _transport->shutdown();
        }

        void start()
        {
            INetSecurityProvider& security = game::getSubsystem<INetSecurityProvider>();
            TLSContextConfig      settings;
            settings._alpnProtocol = "h2";
            SW_REQUIRE_TRUE( security.createSelfSignedCertificate( "localhost", 1, settings._certificatePem, settings._privateKeyPem ) );
            _trustFile = test::writeScratchText( "apns-trust.pem", settings._certificatePem );
            string error;
            _tlsContext = security.createTLSContext( settings, error );
            SW_REQUIRE_TRUE_MSG( _tlsContext != nullptr, error.c_str() );
            _transport = StreamTransportFactory::createPlatformTransport();
            SW_REQUIRE_TRUE( _transport != nullptr );
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_REQUIRE_TRUE( _transport->initialize( this, transportSettings ) );
            SW_REQUIRE_TRUE( _transport->listen( NetAddress::makeLoopback( 0 ) ) );
        }

        void          update() { (void)_transport->pollIO( 0 ); }
        uint16        getPort() const { return _transport->getListenPort(); }
        const string& getTrustFile() const { return _trustFile; }

        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            unique_ptr<Peer> peer = make_unique<Peer>();
            peer->_link           = HTTPLink::open( handle, _tlsContext.get() );
            SW_REQUIRE_TRUE( peer->_link != nullptr );
            _mapPeer[handle.packed()] = std::move( peer );
        }

        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            const auto peerIt = _mapPeer.find( handle.packed() );
            if ( peerIt == _mapPeer.end() )
                return;
            Peer& peer = *peerIt->second;
            SW_EXPECT_TRUE_MSG( peer._link->readReceived( *_transport, pData, size, peer._inBytes ), peer._link->getFailureText() );
            vector<uint8> outBytes;
            processFrames( peer, outBytes );
            if ( outBytes.empty() == false )
                SW_EXPECT_TRUE( peer._link->writePlain( *_transport, outBytes.data(), outBytes.size() ) );
        }

        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)reason;
            _mapPeer.erase( handle.packed() );
        }

    private:
        struct Stream
        {
            vector<HTTPHeader> _listHeader{};
            string             _body{};
        };

        struct Peer
        {
            unique_ptr<HTTPLink>          _link{};
            HpackDecoder                  _decoder{};
            vector<uint8>                 _inBytes{};
            unordered_map<uint32, Stream> _mapStream{};
            uint8                         _bPrefaceRead{ SW_FALSE };
        };

        static string findHeaderValue( const vector<HTTPHeader>& listHeader, string_view name )
        {
            for ( const HTTPHeader& header : listHeader )
            {
                if ( header._name == name )
                    return header._value;
            }
            return {};
        }

        void processFrames( Peer& peer, vector<uint8>& outBytes )
        {
            const string_view preface = HTTP2FrameHeader::kConnectionPreface;
            if ( peer._bPrefaceRead == SW_FALSE )
            {
                if ( peer._inBytes.size() < preface.size() )
                    return;
                SW_EXPECT_TRUE( string_view( reinterpret_cast<const utf8*>( peer._inBytes.data() ), preface.size() ) == preface );
                peer._inBytes.erase( peer._inBytes.begin(), peer._inBytes.begin() + static_cast<ptrdiff_t>( preface.size() ) );
                peer._bPrefaceRead = SW_TRUE;
                SW_EXPECT_TRUE( HTTP2FrameHeader::writeSettings( {}, false, outBytes ) );
            }
            HTTP2FrameHeader header;
            while ( HTTP2FrameHeader::readHeader( peer._inBytes.data(), peer._inBytes.size(), header ) )
            {
                const size_t total = HTTP2FrameHeader::kHeaderSize + header._length;
                if ( peer._inBytes.size() < total )
                    return;
                handleFrame( peer, header, peer._inBytes.data() + HTTP2FrameHeader::kHeaderSize, outBytes );
                peer._inBytes.erase( peer._inBytes.begin(), peer._inBytes.begin() + static_cast<ptrdiff_t>( total ) );
            }
        }

        void handleFrame( Peer& peer, const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes )
        {
            const HTTP2FrameType type = static_cast<HTTP2FrameType>( header._type );
            if ( type == HTTP2FrameType::Settings )
            {
                if ( ( header._flags & HTTP2FrameHeader::kFlagAck ) == 0 )
                    SW_EXPECT_TRUE( HTTP2FrameHeader::writeSettings( {}, true, outBytes ) );
                return;
            }
            if ( type == HTTP2FrameType::Ping )
            {
                uint8 arrOpaque[8] = {};
                Memory::copy( arrOpaque, pBody, sizeof( arrOpaque ) );
                SW_EXPECT_TRUE( HTTP2FrameHeader::writePing( arrOpaque, true, outBytes ) );
                return;
            }
            if ( type == HTTP2FrameType::Headers )
            {
                string error;
                SW_EXPECT_TRUE_MSG( peer._decoder.decode( pBody, header._length, peer._mapStream[header._streamID]._listHeader, error ), error.c_str() );
            }
            else if ( type == HTTP2FrameType::Data )
            {
                peer._mapStream[header._streamID]._body.append( reinterpret_cast<const utf8*>( pBody ), header._length );
            }
            else
            {
                return; // WINDOW_UPDATE 와 그 밖의 프레임은 답이 없다
            }
            if ( ( header._flags & HTTP2FrameHeader::kFlagEndStream ) != 0 )
                respond( peer, header._streamID, outBytes );
        }

        void respond( Peer& peer, uint32 streamID, vector<uint8>& outBytes )
        {
            const auto streamIt = peer._mapStream.find( streamID );
            if ( streamIt == peer._mapStream.end() )
                return;
            const vector<HTTPHeader>& listHeader = streamIt->second._listHeader;
            ApnsRequestRecord&        record     = _listRequest.emplace_back();
            record._path                         = findHeaderValue( listHeader, ":path" );
            record._authorization                = findHeaderValue( listHeader, "authorization" );
            record._topic                        = findHeaderValue( listHeader, "apns-topic" );
            record._pushType                     = findHeaderValue( listHeader, "apns-push-type" );
            record._body                         = streamIt->second._body;
            peer._mapStream.erase( streamIt );
            vector<uint8> block;
            SW_EXPECT_TRUE( HpackEncoder::encode( {
                                                      HTTPHeader{ ":status", to_string( _status ) }
            },
                                                  block ) );
            SW_EXPECT_TRUE( HTTP2FrameHeader::writeHeaders( streamID, block, true, HTTP2FrameHeader::kDefaultMaxFrameSize, outBytes ) );
        }

    private:
        unordered_map<uint64, unique_ptr<Peer>> _mapPeer{};
        unique_ptr<ITLSContext>                 _tlsContext{};
        unique_ptr<IStreamTransport>            _transport{};
        string                                  _trustFile{};
    };

    /** @brief 가짜 FCM(토큰 주소와 보내기 주소)입니다. 평문 HTTP 입니다 - TLS 길은 APNs 와 영수증 끝단이 지난다. */
    struct FakeFcmEndpoint
    {
        HTTPServer     _server{};
        vector<string> _listAuthorization{};
        vector<string> _listSendBody{};
        int32          _tokenRequestCount{ 0 };

        void start()
        {
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            SW_REQUIRE_TRUE( _server.initialize( StreamTransportFactory::createPlatformTransport(), transportSettings, HTTPServerConfig{}, HTTPRequestFunction::create<&FakeFcmEndpoint::onRequest>( this ) ) );
        }

        string makeBaseURL() { return "http://127.0.0.1:" + to_string( static_cast<int32>( _server.getListenPort() ) ); }

        void onRequest( const HTTPServerRequest& request, HTTPServerResponse& outResponse )
        {
            string body;
            if ( request._path == "/token" )
            {
                ++_tokenRequestCount;
                body = R"({"access_token":"fcm-token","expires_in":3600})";
            }
            else
            {
                const HTTPHeader* const pAuthorization = request.findHeader( "Authorization" );
                _listAuthorization.push_back( pAuthorization != nullptr ? pAuthorization->_value : string() );
                _listSendBody.emplace_back( request.getBodyText() );
                body = R"({"name":"projects/proj/messages/1"})";
            }
            outResponse._bodyBytes.assign( body.begin(), body.end() );
        }
    };

    /** @brief 제공자 파일 → 제공자 묶음 → 발송기 → 가짜 서버 둘입니다. 선언 순서가 수명이다 - 발송기가 묶음보다, 묶음이 끝점보다 먼저 내려간다. */
    struct PushEndToEndTest
    {
        FakeApnsServer                              _apns{};
        FakeFcmEndpoint                             _fcm{};
        unique_ptr<test::ScopedEnvironmentVariable> _fcmEnv{};
        unique_ptr<test::ScopedEnvironmentVariable> _apnsEnv{};
        SystemMonotonicClock                        _clock{};
        MemoryServiceDatabase                       _database{};
        MemoryServiceStore                          _store{ &_database };
        PushProviderSet                             _providers{};
        PushNotificationDispatcher                  _dispatcher{};

        PushEndToEndTest()
        {
            _apns.start();
            _fcm.start();
            INetSecurityProvider& security = game::getSubsystem<INetSecurityProvider>();
            string                fcmKey;
            string                apnsKey;
            NetPublicKey          publicKey;
            SW_REQUIRE_TRUE( security.createSigningKeyPair( NetSignatureAlgorithm::RsaPkcs1SHA256, fcmKey, publicKey ) );
            SW_REQUIRE_TRUE( security.createSigningKeyPair( NetSignatureAlgorithm::EcdsaP256Sha256, apnsKey, publicKey ) );
            _fcmEnv  = make_unique<test::ScopedEnvironmentVariable>( kFcmEnv, R"({"project_id":"proj","client_email":"a@b","private_key":")" + test::escapeJSONText( fcmKey ) + R"("})" );
            _apnsEnv = make_unique<test::ScopedEnvironmentVariable>( kApnsEnv, apnsKey );
        }

        ~PushEndToEndTest() { _providers.shutdown(); }

        [[nodiscard]] string writeProvidersFile()
        {
            const string fcmURL = _fcm.makeBaseURL();
            const string text   = formatToString( R"({"providers":[
                {"kind":"fcm","serviceAccountEnvironment":"%#","tokenUrl":"%#/token","sendUrl":"%#"},
                {"kind":"apns","keyEnvironment":"%#","keyId":"key","teamId":"team","topic":"%#","sendUrl":"https://localhost:%#"}
            ]})",
                                                  kFcmEnv, fcmURL, fcmURL, kApnsEnv, kTopic, _apns.getPort() );
            return test::writeScratchText( "push.json", text );
        }

        /** @brief 운영의 라이브 운영 키트 조립과 같은 순서로 파일에서 제공자를 세워 발송기에 올립니다. */
        [[nodiscard]] bool assemble( string& outError )
        {
            const std::optional<AbsolutePath> providersFile = AbsolutePath::tryMake( writeProvidersFile() );
            const std::optional<AbsolutePath> trustFile     = AbsolutePath::tryMake( _apns.getTrustFile() );
            vector<PushProviderConfig>        listConfig;
            const bool                        bRead = OnlineProviderFile::read<PushProviderConfig>( providersFile, trustFile, "pushProvidersFile", "pushTrustFile", &PushProviderSet::readEntry, listConfig, outError );
            if ( bRead == false || _providers.initialize( listConfig, OnlineProviderFile::getTrustText( trustFile ), _clock, outError ) == false )
                return false;
            _dispatcher.initialize( &_store, PushDispatcherConfig{} );
            for ( int32 index = 0; index < _providers.getProviderCount(); ++index )
            {
                SW_EXPECT_TRUE( _dispatcher.registerProvider( _providers.getProvider( index ) ) );
            }
            return true;
        }

        static UnixMilliseconds getNow() { return WallClock::nowUnixMilliseconds(); }

        void pump()
        {
            while ( _store.pollCompletions() > 0 )
            {
            }
            _dispatcher.update( getNow() );
            _apns.update();
            _fcm._server.update();
        }

        void addDevice( string_view providerID, string_view token )
        {
            PushDeviceRegistration registration;
            registration._providerID   = string( providerID );
            registration._token        = string( token );
            registration._locale       = "ko-KR";
            registration._registeredMs = getNow().count();
            _dispatcher.registerDevice( kAccount, registration, 0 );
            SW_EXPECT_TRUE( test::pumpUntil( [this]()
            { pump(); }, [this]()
            { return _dispatcher.getPendingWorkCount() == 0; } ) );
        }

        /** @brief 계정에 알림 하나를 보내고 맡긴 배달이 모두 끝날 때까지 돌립니다. */
        void notifyAndWait()
        {
            PushNotificationMessage message;
            message._titleKey = "title.key";
            message._bodyKey  = "body.key";
            message._deepLink = "mailbox";
            SW_EXPECT_TRUE( _dispatcher.notifyAccount( kAccount, message, getNow() ) );
            SW_EXPECT_TRUE( test::pumpUntil( [this]()
            { pump(); }, [this]()
            { return _dispatcher.getPendingWorkCount() == 0 && _dispatcher.getInFlightCount() == 0; } ) );
        }
    };
} // namespace

/** @brief [PushEndToEndTest] 파일이 고른 두 제공자가 기기마다 실제 전송으로 닿는다: FCM 은 서명으로 받은 토큰, APNs 는 HTTP/2 스트림에 ES256 제공자 토큰 · 토픽 · 경로를 싣는다 */
SW_TEST_CASE_F( PushEndToEndTest, ProvidersReachTheirEndpoints )
{
    string error;
    SW_REQUIRE_TRUE_MSG( assemble( error ), error.c_str() );
    SW_EXPECT_EQUAL( 2, _providers.getProviderCount() );
    addDevice( "fcm", kFcmToken );
    addDevice( "apns", kApnsToken );
    notifyAndWait();

    SW_EXPECT_EQUAL( uint64( 2 ), _dispatcher.getStats()._deliveredCount );
    SW_EXPECT_EQUAL( 1, _fcm._tokenRequestCount );
    SW_REQUIRE_EQUAL( size_t( 1 ), _fcm._listSendBody.size() );
    SW_EXPECT_EQUAL( string( "Bearer fcm-token" ), _fcm._listAuthorization[0] );
    JSONDocument document;
    SW_REQUIRE_TRUE( document.tryParse( _fcm._listSendBody[0] ) );
    string fcmDeviceToken;
    SW_EXPECT_TRUE( document.getRoot().findPathText( "message.token", fcmDeviceToken ) );
    SW_EXPECT_EQUAL( string( kFcmToken ), fcmDeviceToken );

    SW_REQUIRE_EQUAL( size_t( 1 ), _apns._listRequest.size() );
    const ApnsRequestRecord& request = _apns._listRequest[0];
    SW_EXPECT_EQUAL( string( "/3/device/" ) + kApnsToken, request._path );
    SW_EXPECT_EQUAL( string( kTopic ), request._topic );
    SW_EXPECT_EQUAL( string( "alert" ), request._pushType );
    SW_EXPECT_TRUE( StringUtil::startsWith( request._authorization, "bearer " ) );
    SW_EXPECT_TRUE( request._body.find( "body.key" ) != string::npos );
}

/** @brief [PushEndToEndTest] APNs 410 이 전송을 지나 돌아오면 기기를 지워 다음 알림은 그 기기에 가지 않는다 */
SW_TEST_CASE_F( PushEndToEndTest, ApnsGoneRemovesTheDevice )
{
    string error;
    SW_REQUIRE_TRUE_MSG( assemble( error ), error.c_str() );
    addDevice( "apns", kApnsToken );
    _apns._status = 410;
    notifyAndWait();
    SW_EXPECT_EQUAL( uint64( 1 ), _dispatcher.getStats()._invalidTokenCount );
    SW_EXPECT_EQUAL( size_t( 1 ), _apns._listRequest.size() );

    notifyAndWait();
    SW_EXPECT_EQUAL( size_t( 1 ), _apns._listRequest.size() );
}

/** @brief [PushEndToEndTest] 비밀 환경 변수가 없으면 가짜로 대신 뜨지 않고 묶음 전체가 실패한다 */
SW_TEST_CASE_F( PushEndToEndTest, BadSettingsStopAssembly )
{
    SW_EXPECT_LOG( "Server.SecretNotSet" );
    _apnsEnv.reset();
    string error;
    SW_EXPECT_FALSE( assemble( error ) );
    SW_EXPECT_TRUE( error.find( kApnsEnv ) != string::npos );
    SW_EXPECT_EQUAL( 0, _providers.getProviderCount() );
}

/** @brief [PushKitAssemblyTest] 라이브 운영 키트 조립: 설정 옵션이 고른 제공자로 서버가 서고 오프라인 푸시 창을 내놓는다. 신뢰 파일이 빠지면 서지 않는다 */
SW_TEST_CASE( PushKitAssemblyTest, KitOptionsSelectTheProviders )
{
    FakeApnsServer apns;
    apns.start();
    string       apnsKey;
    NetPublicKey publicKey;
    SW_REQUIRE_TRUE( game::getSubsystem<INetSecurityProvider>().createSigningKeyPair( NetSignatureAlgorithm::EcdsaP256Sha256, apnsKey, publicKey ) );
    const test::ScopedEnvironmentVariable apnsEnv{ kApnsEnv, apnsKey };
    const string                          providersFile = test::writeScratchText( "kit-push.json", formatToString( R"({"providers":[{"kind":"apns","keyEnvironment":"%#","keyId":"key","teamId":"team","topic":"%#","sendUrl":"https://localhost:%#"}]})", kApnsEnv, kTopic, apns.getPort() ) );

    constexpr const utf8* kServerJSON    = R"({ "_listStore": [ { "_name": "local", "_driver": "memory" } ], "_listCache": [ { "_name": "local", "_driver": "memory" } ] })";
    const auto            makeOnlineJSON = [&providersFile]( string_view trustOption )
    {
        return formatToString( R"({ "_storeName": "local", "_cacheName": "local", "_listKit": [ { "_name": "liveops", "_settings": { "_pushProvidersFile": "%#"%# } } ] })", providersFile, trustOption );
    };
    const int64 nowMs = WallClock::nowUnixMilliseconds().count();

    {
        test::OnlineRuntimeFixture noTrust;
        string                     error;
        SW_EXPECT_FALSE( noTrust.fromConfig( kServerJSON, makeOnlineJSON( "" ), nowMs, error ) );
        SW_EXPECT_TRUE( error.find( "pushTrustFile" ) != string::npos );
    }

    test::OnlineRuntimeFixture rig;
    string                     error;
    SW_REQUIRE_TRUE_MSG( rig.fromConfig( kServerJSON, makeOnlineJSON( formatToString( R"(, "_pushTrustFile": "%#")", apns.getTrustFile() ) ), nowMs, error ), error.c_str() );
    SW_EXPECT_TRUE( rig._runtime.getContext()._kits.find<IOfflinePushSender>() != nullptr );
}

/** @brief [PushKitAssemblyTest] 저장소에 든 견본(`Shooter3D.push.json`)이 같은 읽기를 지난다 - 견본이 코드와 어긋나 낡지 않게 한다 */
SW_TEST_CASE( PushKitAssemblyTest, ExternalSampleConfigReads )
{
    string                            error;
    const std::optional<AbsolutePath> providersFile = AbsolutePath::tryResolve( "Config/Server/Online/Providers/Shooter3D.push.json", ProcessRoots::get()._project );
    SW_REQUIRE_TRUE( providersFile.has_value() );
    vector<PushProviderConfig> listConfig;
    SW_REQUIRE_TRUE_MSG( OnlineProviderFile::read<PushProviderConfig>( providersFile, std::nullopt, "pushProvidersFile", "pushTrustFile", &PushProviderSet::readEntry, listConfig, error ), error.c_str() );
    SW_EXPECT_EQUAL( size_t( 2 ), listConfig.size() );
}
