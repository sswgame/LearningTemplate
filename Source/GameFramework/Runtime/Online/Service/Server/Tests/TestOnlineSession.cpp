#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

// OnlineSessionTest — 게임의 온라인 세션: 등록된 키트 클라이언트를 만들어 형식으로 찾고, 설정이 빠지면 조용히 넘어가지 않으며, 루프백 서버에 붙어 Hello 를 마치고 끊는다(nogpu).

using namespace sw;

namespace
{
    constexpr int64 kNowMs = 1700000000000ll;

    /** @brief 등록을 보는 가짜 키트 클라이언트입니다. 테스트 로그인과 같은 프로토콜 항목을 쓴다. */
    class SessionTestClient final : public IOnlineClientService
    {
    public:
        OnlineServiceClient* _pBound{ nullptr };

        void                       initialize( OnlineServiceClient* pClient ) { _pBound = pClient; }
        const OnlineProtocolEntry& getProtocol() const override { return test::getTestLoginProtocol(); }
        void                       onServicePush( uint16 kind, BitReader& body ) override
        {
            (void)kind;
            (void)body;
        }
    };

    class UnregisteredTestClient final : public IOnlineClientService
    {
    public:
        const OnlineProtocolEntry& getProtocol() const override { return test::getTestLoginProtocol(); }
        void                       onServicePush( uint16 kind, BitReader& body ) override
        {
            (void)kind;
            (void)body;
        }
    };

    struct OnlineSessionTest
    {
        const OnlineClientRegistrar<SessionTestClient> _registrar{ "SessionTestClient" };
        GameSettings                                   _settings{};
        OnlineSession                                  _session{ _settings };

        OnlineSessionTest() { SW_EXPECT_TRUE( _session.initializeSubsystem() == SubsystemInitResult::Succeeded ); }
        ~OnlineSessionTest() { _session.shutdownSubsystem(); }
    };
} // namespace

/** @brief [OnlineSessionTest] 등록된 키트 클라이언트를 만들어 서비스 클라이언트에 묶고 형식으로 찾는다. 등록하지 않은 형식은 없다 */
SW_TEST_CASE_F( OnlineSessionTest, BuildsRegisteredClients )
{
    SessionTestClient* const pClient = _session.find<SessionTestClient>();
    SW_REQUIRE_TRUE( pClient != nullptr );
    SW_EXPECT_TRUE( pClient->_pBound == &_session.getServiceClient() );
    SW_EXPECT_TRUE( _session.find<UnregisteredTestClient>() == nullptr );
    SW_EXPECT_TRUE( _session.getClientCount() >= 1 );
}

/** @brief [OnlineSessionTest] 주소나 빌드 글이 없으면 연결하지 않고 까닭을 돌려준다 */
SW_TEST_CASE_F( OnlineSessionTest, ConnectNeedsSettings )
{
    const expected<void> noAddress = _session.connect();
    SW_REQUIRE_FALSE( noAddress.has_value() );
    SW_EXPECT_TRUE( noAddress.error().message().find( "onlineServerAddress" ) != string_view::npos );

    _settings._onlineServerAddress = "127.0.0.1:7100";
    const expected<void> noBuild   = _session.connect();
    SW_REQUIRE_FALSE( noBuild.has_value() );
    SW_EXPECT_TRUE( noBuild.error().message().find( "onlineGameBuild" ) != string_view::npos );
    SW_EXPECT_TRUE( _session.getState() == OnlineClientState::Disconnected );
}

/** @brief [OnlineSessionTest] 루프백 서버에 붙어 Hello 를 마치고, 끊으면 다시 돌지 않는다 */
SW_TEST_CASE_F( OnlineSessionTest, ConnectsAndDisconnects )
{
    // 서버 쪽 테스트 로그인이 같은 영역이므로 세션의 가짜 클라이언트와 해시가 맞다
    test::OnlineTestFixture      rig;
    unique_ptr<IStreamTransport> transport = rig._network.createTransport();
    {
        const auto result = _session.connectTo( transport.get(), OnlineClientSecurity::plaintextLoopback(), NetAddress::makeLoopback( rig._server._port ) );
        SW_REQUIRE_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
    }
    for ( int32 round = 0; round < 8; ++round )
    {
        _session.updateAt( MonotonicMilliseconds{ kNowMs } );
        rig._server.update( kNowMs );
    }
    SW_EXPECT_TRUE( _session.isReady() );

    _session.disconnect();
    SW_EXPECT_FALSE( _session.isReady() );
    _session.update(); // 연결이 없으면 바로 돌아온다
    SW_EXPECT_FALSE( _session.isReady() );
}
