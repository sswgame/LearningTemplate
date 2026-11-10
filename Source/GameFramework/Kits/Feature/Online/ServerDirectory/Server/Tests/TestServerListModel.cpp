// ServerListModelTest — 서버 목록 상태: 클라이언트가 없거나 연결 전이면 Failed(kUnavailable), 종류가 상한을 넘으면 보내지 않고 Failed(kInvalidRequest),
// 실패마다 목록을 비우고 수(revision)를 올린다. 응답이 오는 길은 루프백 서버(`OnlineHostTestUtil` — 호스트 + 가짜 디렉터리 서비스)로 본다: 응답이 오면 Loaded,
// 묻는 중에 다시 물으면 보내지 않고 거절, 서버가 오류로 답하면 앞서 받은 목록을 비운다.
// 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryClient.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryProtocol.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerListModel.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

using namespace sw;

namespace
{
    /**
     * @brief 서버 쪽 가짜 디렉터리 — 목록 요청(`kListServers`)만 받는다. 아는 종류(`kKnownKind`)면 정해 둔 두 서버를, 아니면 `kUnknownKind` 로 답한다.
     * @details 실제 서버 서비스(`ServerDirectoryServer`)는 서버 키트 DLL 이고 캐시의 서버 레코드를 읽어야 해서, 클라이언트 키트 테스트는 같은 프로토콜 항목의 가짜를 올린다.
     *          `_bHoldAnswers` 를 켜면 답하지 않고 토큰만 들었다가 끄면 다음 서비스 틱에 답한다(묻는 중 상태를 붙잡는다).
     */
    class FakeServerDirectoryService final : public IOnlineServerService
    {
    public:
        static constexpr const utf8* kKnownKind = "dedicated";

        int32 _requestCount{ 0 };
        uint8 _bHoldAnswers : 1;

        FakeServerDirectoryService()
            : _bHoldAnswers{ SW_FALSE }
        {
        }
        const OnlineProtocolEntry& getProtocol() const override { return ServerDirectoryProtocol::kOnlineEntry; }
        bool                       isAnonymousMethod( uint16 method ) const override { return method == ServerDirectoryMethod::ListServers::kID; }

        void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            ++_requestCount;
            ServerListRequest request;
            if ( context._method != ServerDirectoryMethod::ListServers::kID || ServerDirectoryProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
            {
                (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                return;
            }
            _listHeld.push_back( HeldRequest{ context._token, std::move( request._kind ) } );
            if ( _bHoldAnswers == false )
                answerHeld( host );
        }

        void onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override
        {
            (void)nowMs;
            if ( _bHoldAnswers == false )
                answerHeld( host );
        }

        static vector<ServerListEntry> makeKnownList()
        {
            ServerListEntry first;
            first._region      = "kr";
            first._address     = "10.0.0.1";
            first._serverID    = 11;
            first._port        = 7777;
            first._fillPercent = 40;
            first._state       = ServerState::Open;
            ServerListEntry second;
            second._region      = "jp";
            second._address     = "10.0.0.2";
            second._serverID    = 12;
            second._port        = 7778;
            second._fillPercent = 100;
            second._state       = ServerState::Maintenance;
            return { first, second };
        }

    private:
        struct HeldRequest
        {
            NetRequestToken _token;
            string          _kind;
        };

        void answerHeld( OnlineServiceHost& host )
        {
            for ( const HeldRequest& held : _listHeld )
            {
                if ( held._kind != kKnownKind )
                {
                    (void)host.respondError( held._token, ServerDirectoryError::kUnknownKind );
                    continue;
                }
                ServerListReply reply;
                reply._listEntry = makeKnownList();
                (void)host.respondMessage<ServerDirectoryProtocol::MessageTypes>( held._token, reply );
            }
            _listHeld.clear();
        }

    private:
        vector<HeldRequest> _listHeld{};
    };

    /**
     * @brief 가짜 디렉터리를 올린 루프백 서버 + 연결 하나에 디렉터리 클라이언트 · 목록 모델입니다. 연결과 Hello 까지 끝난 채로 선다.
     * @details 선언 순서가 수명이다 — 가짜 서비스는 호스트보다, 디렉터리 클라이언트는 모델보다 오래 산다.
     */
    struct ServerListFixture
    {
        FakeServerDirectoryService _service{};
        ServerDirectoryClient      _directoryClient{};
        test::OnlineTestFixture    _rig{ false };
        ServerListModel            _model{};
        uint8                      _bReady : 1;

        ServerListFixture()
            : _bReady{ SW_FALSE }
        {
            SW_EXPECT_TRUE( _rig._server._host.registerServerService( &_service ) );
            _rig._server.start();
            const int32 clientIndex = _rig._clients.connect( _rig._server._port, { &_directoryClient } );
            _directoryClient.initialize( &_rig._clients.getClient( clientIndex ) );
            _model.initialize( &_directoryClient );
            pump();
            _bReady = _rig._clients.getClient( clientIndex ).isReady();
        }

        void pump() { test::updateAll( { &_rig._server }, _rig._clients, 0 ); }
    };

    bool isSameEntry( const ServerListEntry& left, const ServerListEntry& right )
    {
        return left._region == right._region && left._address == right._address && left._serverID == right._serverID && left._port == right._port &&
               left._fillPercent == right._fillPercent && left._state == right._state;
    }
} // namespace

/** @brief [ServerListModelTest] 클라이언트가 없으면 묻지 않고 Failed(kUnavailable) 로 끝난다 */
SW_TEST_CASE( ServerListModelTest, NoClientFailsAsUnavailable )
{
    ServerListModel model;
    SW_EXPECT_TRUE( model.getState() == ServerListState::Idle );
    SW_EXPECT_FALSE( model.refresh( "dedicated" ) );
    SW_EXPECT_TRUE( model.getState() == ServerListState::Failed );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, model.getErrorCode() );
    SW_EXPECT_EQUAL( uint64{ 1 }, model.getRevision() );
    SW_EXPECT_TRUE( model.getEntries().empty() );
}

/** @brief [ServerListModelTest] 연결 전 클라이언트는 그 호출 안에서 kUnavailable 로 답하고, 상태는 Loading 에 머물지 않는다(다시 물을 수 있다) */
SW_TEST_CASE( ServerListModelTest, UnconnectedClientFailsInsideTheCall )
{
    OnlineServiceClient   serviceClient; // 디렉터리 클라이언트보다 먼저 만들어 나중에 사라진다
    ServerDirectoryClient directoryClient;
    directoryClient.initialize( &serviceClient );
    ServerListModel model;
    model.initialize( &directoryClient );

    SW_EXPECT_FALSE( model.refresh( "dedicated" ) );
    SW_EXPECT_TRUE( model.getState() == ServerListState::Failed );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, model.getErrorCode() );
    SW_EXPECT_EQUAL( uint64{ 2 }, model.getRevision() ); // 로딩 표시 한 번 + 응답 한 번

    SW_EXPECT_FALSE( model.refresh( "dedicated" ) ); // Loading 에 갇히지 않았으므로 다시 보낸다
    SW_EXPECT_EQUAL( uint64{ 4 }, model.getRevision() );
}

/** @brief [ServerListModelTest] 종류가 상한보다 길면 보내지 않고 kInvalidRequest 로 끝난다 */
SW_TEST_CASE( ServerListModelTest, OversizedKindIsRejected )
{
    OnlineServiceClient   serviceClient;
    ServerDirectoryClient directoryClient;
    directoryClient.initialize( &serviceClient );
    ServerListModel model;
    model.initialize( &directoryClient );

    SW_EXPECT_LOG( "OnlineProtocol.MessageOutOfRange" ); // 쓰지 못한 메시지는 오류 로그를 남긴다
    const string longKind( static_cast<size_t>( ServerRecord::kMaxNameSize ) + 1, 'k' );
    SW_EXPECT_FALSE( model.refresh( longKind ) );
    SW_EXPECT_TRUE( model.getState() == ServerListState::Failed );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, model.getErrorCode() );
    SW_EXPECT_TRUE( model.getEntries().empty() );
}

/** @brief [ServerListModelTest] 서버가 목록으로 답하면 Loaded 이고 받은 목록을 그대로 든다(로딩 표시 한 번 + 응답 한 번) */
SW_TEST_CASE( ServerListModelTest, AnsweredListIsLoaded )
{
    ServerListFixture rig;
    SW_REQUIRE_TRUE( rig._bReady );

    SW_EXPECT_TRUE( rig._model.refresh( FakeServerDirectoryService::kKnownKind ) );
    SW_EXPECT_TRUE( rig._model.getState() == ServerListState::Loading );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._model.getRevision() );
    rig.pump();

    SW_EXPECT_TRUE( rig._model.getState() == ServerListState::Loaded );
    SW_EXPECT_EQUAL( OnlineError::kOk, rig._model.getErrorCode() );
    SW_EXPECT_EQUAL( uint64{ 2 }, rig._model.getRevision() );
    const vector<ServerListEntry> listExpected = FakeServerDirectoryService::makeKnownList();
    SW_REQUIRE_EQUAL( listExpected.size(), rig._model.getEntries().size() );
    for ( size_t index = 0; index < listExpected.size(); ++index )
    {
        SW_EXPECT_TRUE( isSameEntry( listExpected[index], rig._model.getEntries()[index] ) );
    }
}

/** @brief [ServerListModelTest] 묻는 중에 다시 물으면 보내지 않고 false 다 — 서버는 요청 하나만 받고, 답이 온 뒤에는 다시 물을 수 있다 */
SW_TEST_CASE( ServerListModelTest, OverlappingRefreshIsRejectedWhileLoading )
{
    ServerListFixture rig;
    SW_REQUIRE_TRUE( rig._bReady );
    rig._service._bHoldAnswers = SW_TRUE;

    SW_EXPECT_TRUE( rig._model.refresh( FakeServerDirectoryService::kKnownKind ) );
    rig.pump(); // 서버가 받고 답을 붙잡는다
    SW_EXPECT_EQUAL( 1, rig._service._requestCount );
    SW_EXPECT_TRUE( rig._model.getState() == ServerListState::Loading );

    SW_EXPECT_FALSE( rig._model.refresh( FakeServerDirectoryService::kKnownKind ) );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._model.getRevision() ); // 거절은 상태를 건드리지 않는다
    rig.pump();
    SW_EXPECT_EQUAL( 1, rig._service._requestCount ); // 겹친 요청은 나가지 않았다

    rig._service._bHoldAnswers = SW_FALSE;
    rig.pump();
    SW_EXPECT_TRUE( rig._model.getState() == ServerListState::Loaded );
    SW_EXPECT_EQUAL( uint64{ 2 }, rig._model.getRevision() );

    SW_EXPECT_TRUE( rig._model.refresh( FakeServerDirectoryService::kKnownKind ) ); // Loaded 뒤에는 다시 묻는다
    rig.pump();
    SW_EXPECT_EQUAL( 2, rig._service._requestCount );
    SW_EXPECT_TRUE( rig._model.getState() == ServerListState::Loaded );
}

/** @brief [ServerListModelTest] 받은 목록이 있어도 서버가 오류(모르는 종류)로 답하면 Failed 이고 낡은 목록을 비운다 */
SW_TEST_CASE( ServerListModelTest, ServerErrorClearsTheLoadedList )
{
    ServerListFixture rig;
    SW_REQUIRE_TRUE( rig._bReady );
    SW_EXPECT_TRUE( rig._model.refresh( FakeServerDirectoryService::kKnownKind ) );
    rig.pump();
    SW_REQUIRE_TRUE( rig._model.getState() == ServerListState::Loaded );
    SW_EXPECT_FALSE( rig._model.getEntries().empty() );

    SW_EXPECT_TRUE( rig._model.refresh( "lobby" ) );
    rig.pump();
    SW_EXPECT_TRUE( rig._model.getState() == ServerListState::Failed );
    SW_EXPECT_EQUAL( ServerDirectoryError::kUnknownKind, rig._model.getErrorCode() );
    SW_EXPECT_TRUE( rig._model.getEntries().empty() );
    SW_EXPECT_EQUAL( uint64{ 4 }, rig._model.getRevision() );
}
