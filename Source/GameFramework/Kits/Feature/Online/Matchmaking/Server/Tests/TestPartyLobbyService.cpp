// PartyLobbyServiceTest — 파티 · 로비(메모리 캐시): 파티 만들기 · 초대 · 수락 · 정원 · 떠나기 · 파티장 넘어감 · 내보내기 · 깨진 티켓, 로비 만들기 · 목록 · 참가 · 팀 나눔 · 준비 · 시작 · 다시 열기,
// 다른 로비로 옮기면 앞 로비에서 빠지는 것, 계정이 떠날 때. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"
#include "GameFramework/Runtime/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"

using namespace sw;

namespace
{
    constexpr AccountID kAlice = 11;
    constexpr AccountID kBob   = 12;
    constexpr AccountID kCarol = 13;
    constexpr AccountID kDave  = 14;
    constexpr int64     kNowMs = 1700000000000ll;

    /** @brief 파티 찾기의 답을 받습니다. */
    struct PartyFoundSink
    {
        PartySnapshot      _party{};
        int32              _count{ 0 };
        MatchmakingOutcome _result{ MatchmakingResult::Ok };

        void onFound( uint64 /*tag*/, MatchmakingOutcome result, const PartySnapshot& party )
        {
            ++_count;
            _result = result;
            _party  = party;
        }
    };

    struct PartyFixture
    {
        ManualClock             _clock;
        MemoryEphemeralDatabase _cacheData;
        MemoryEphemeralStore    _cache;
        EphemeralStoreRouter    _router;
        PartyLobbyService       _service;

        explicit PartyFixture( int32 maxPartySize = 3 )
            : _clock{ MonotonicMilliseconds{ 1000 } }
            , _cacheData{ _clock }
            , _cache{ &_cacheData }
            , _router{}
            , _service{}
        {
            _router.initialize( &_cache );
            SW_EXPECT_TRUE( _service.initialize( &_router, 1, maxPartySize ) );
        }

        ~PartyFixture()
        {
            _service.shutdown();
            _router.shutdown();
        }

        void pump()
        {
            while ( _router.pump() > 0 )
            {
            }
        }

        /** @brief 쌓인 완료 중 태그가 @p tag 인 것을 꺼냅니다. 태그 0(내부 요청)은 버립니다. */
        PartyLobbyCompletion take( uint64 tag )
        {
            pump();
            vector<PartyLobbyCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            PartyLobbyCompletion found;
            int32                count = 0;
            for ( PartyLobbyCompletion& completion : listCompletion )
            {
                if ( completion._requestTag != tag )
                    continue;
                found = std::move( completion );
                ++count;
            }
            SW_EXPECT_EQUAL( 1, count );
            return found;
        }

        vector<PartyLobbyNotification> takeNotifications()
        {
            pump();
            vector<PartyLobbyNotification> listNotification;
            _service.pollNotifications( listNotification );
            return listNotification;
        }

        uint64 makeParty( AccountID leaderID )
        {
            _service.createParty( leaderID, 1 );
            const PartyLobbyCompletion created = take( 1 );
            SW_EXPECT_TRUE( created._result == MatchmakingResult::Ok );
            return created._party._partyID;
        }

        MatchmakingOutcome invite( AccountID leaderID, AccountID targetID )
        {
            _service.inviteToParty( leaderID, targetID, 2 );
            return take( 2 )._result;
        }

        MatchmakingOutcome accept( AccountID accountID, uint64 partyID )
        {
            _service.acceptPartyInvite( accountID, partyID, 3 );
            return take( 3 )._result;
        }

        void addMember( uint64 partyID, AccountID leaderID, AccountID memberID )
        {
            SW_EXPECT_TRUE( invite( leaderID, memberID ) == MatchmakingResult::Ok );
            SW_EXPECT_TRUE( accept( memberID, partyID ) == MatchmakingResult::Ok );
        }

        PartyFoundSink find( AccountID accountID )
        {
            PartyFoundSink sink;
            _service.findPartyOfAccount( accountID, 9, PartyLobbyService::PartyFoundDelegate::create<&PartyFoundSink::onFound>( &sink ) );
            pump();
            return sink;
        }

        uint64 makeLobby( AccountID ownerID, string_view modeID = "duel", int32 maxMemberCount = 4 )
        {
            LobbySnapshot request;
            request._name           = "Room";
            request._modeID         = string( modeID );
            request._maxMemberCount = maxMemberCount;
            _service.createLobby( ownerID, request, UnixMilliseconds{ kNowMs }, 4 );
            const PartyLobbyCompletion created = take( 4 );
            SW_EXPECT_TRUE( created._result == MatchmakingResult::Ok );
            return created._lobby._lobbyID;
        }

        PartyLobbyCompletion join( AccountID accountID, uint64 lobbyID )
        {
            _service.joinLobby( accountID, lobbyID, 5 );
            return take( 5 );
        }

        MatchmakingOutcome ready( AccountID accountID, uint64 lobbyID, bool bReady = true )
        {
            _service.setLobbyReady( accountID, lobbyID, bReady, 6 );
            return take( 6 )._result;
        }

        MatchmakingOutcome start( AccountID accountID, uint64 lobbyID )
        {
            _service.startLobby( accountID, lobbyID, 7 );
            return take( 7 )._result;
        }

        vector<LobbySnapshot> list( string_view modeID = "duel" )
        {
            _service.listLobbies( modeID, 8 );
            return take( 8 )._listLobby;
        }
    };
} // namespace

/** @brief [PartyLobbyServiceTest] 최대 인원이 범위 밖이면 시작하지 않는다 — 조용히 맞춰 돌지 않는다 */
SW_TEST_CASE( PartyLobbyServiceTest, RefusesBadMaxPartySize )
{
    SW_EXPECT_LOG( "PartyLobbyService.PartyLobbyServiceRefusedStartMax" );
    PartyFixture      rig;
    PartyLobbyService service;
    SW_EXPECT_FALSE( service.initialize( &rig._router, 2, PartyLobbyLimit::kMinPartySize - 1 ) );
    SW_EXPECT_FALSE( service.initialize( &rig._router, 2, PartyLobbyLimit::kMaxPartySize + 1 ) );

    PartyFoundSink sink; // 시작하지 못한 서비스는 찾기에 Unavailable 로 답한다
    service.findPartyOfAccount( kAlice, 9, PartyLobbyService::PartyFoundDelegate::create<&PartyFoundSink::onFound>( &sink ) );
    SW_EXPECT_EQUAL( 1, sink._count );
    SW_EXPECT_TRUE( sink._result.hasError( OnlineError::kUnavailable ) );
}

/** @brief [PartyLobbyServiceTest] 파티: 만들면 파티장이고 찾을 수 있다. 이미 파티가 있으면 AlreadyInParty, 파티가 없으면 NotInParty 다 */
SW_TEST_CASE( PartyLobbyServiceTest, CreateAndFindParty )
{
    PartyFixture rig;
    const uint64 partyID = rig.makeParty( kAlice );
    SW_EXPECT_EQUAL( uint64( 1 ), partyID >> 32 );
    const vector<PartyLobbyNotification> listNotification = rig.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listNotification.size() ); // 만든 사람도 받는다 — "내 파티" 의 원본이 알림이다
    SW_EXPECT_EQUAL( kAlice, listNotification[0]._recipientID );

    const PartyFoundSink found = rig.find( kAlice );
    SW_EXPECT_EQUAL( 1, found._count );
    SW_EXPECT_TRUE( found._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( kAlice, found._party.getLeaderID() );
    SW_EXPECT_EQUAL( 3, found._party._maxMemberCount );

    rig._service.createParty( kAlice, 1 );
    SW_EXPECT_TRUE( rig.take( 1 )._result == MatchmakingResult::AlreadyInParty );
    SW_EXPECT_TRUE( rig.find( kBob )._result == MatchmakingResult::NotInParty );
    rig._service.leaveParty( kBob, 2 );
    SW_EXPECT_TRUE( rig.take( 2 )._result == MatchmakingResult::NotInParty );
}

/** @brief [PartyLobbyServiceTest] 초대 · 수락: 파티장만 초대한다. 자신 · 0 · 멤버는 초대할 수 없고, 초대 없이 수락할 수 없으며, 정원이 차면 PartyFull 이고 인덱스가 남지 않는다 */
SW_TEST_CASE( PartyLobbyServiceTest, InviteAndAcceptRules )
{
    PartyFixture rig;
    const uint64 partyID = rig.makeParty( kAlice );

    SW_EXPECT_TRUE( rig.invite( kAlice, kAlice ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.invite( kAlice, kInvalidAccountID ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.invite( kBob, kCarol ) == MatchmakingResult::NotInParty );
    SW_EXPECT_TRUE( rig.accept( kBob, partyID ) == MatchmakingResult::InviteMissing );

    (void)rig.takeNotifications();
    SW_EXPECT_TRUE( rig.invite( kAlice, kBob ) == MatchmakingResult::Ok );
    const vector<PartyLobbyNotification> listInvite = rig.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listInvite.size() );
    SW_EXPECT_EQUAL( kBob, listInvite[0]._recipientID );
    SW_EXPECT_EQUAL( partyID, listInvite[0]._invite._partyID );
    SW_EXPECT_EQUAL( kAlice, listInvite[0]._invite._inviterID );

    SW_EXPECT_TRUE( rig.accept( kBob, partyID ) == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( size_t( 2 ), rig.find( kAlice )._party._listMemberID.size() );
    SW_EXPECT_TRUE( rig.invite( kBob, kCarol ) == MatchmakingResult::NotPartyLeader );
    SW_EXPECT_TRUE( rig.invite( kAlice, kBob ) == MatchmakingResult::AlreadyInParty );

    SW_EXPECT_TRUE( rig.invite( kAlice, kCarol ) == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( rig.invite( kAlice, kDave ) == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( rig.accept( kCarol, partyID ) == MatchmakingResult::Ok ); // 셋이 차서
    SW_EXPECT_TRUE( rig.accept( kDave, partyID ) == MatchmakingResult::PartyFull );
    SW_EXPECT_TRUE( rig.invite( kAlice, kDave ) == MatchmakingResult::PartyFull );

    rig.pump();
    SW_EXPECT_TRUE( rig.find( kDave )._result == MatchmakingResult::NotInParty ); // 잡아 둔 인덱스를 되돌렸다
    const uint64 davePartyID = rig.makeParty( kDave );                            // 다른 파티를 만들 수 있다
    SW_EXPECT_TRUE( davePartyID != partyID );
}

/** @brief [PartyLobbyServiceTest] 줄을 서 있는 파티에는 들어올 수 없다. 멤버가 떠나면 티켓이 깨졌다고 알린다 */
SW_TEST_CASE( PartyLobbyServiceTest, QueuedPartyLocksMembership )
{
    PartyFixture rig;
    const uint64 partyID = rig.makeParty( kAlice );
    rig.addMember( partyID, kAlice, kBob );
    SW_EXPECT_TRUE( rig.invite( kAlice, kCarol ) == MatchmakingResult::Ok );

    constexpr uint64 kTicket = 0x100000001ull;
    rig._service.setPartyTicket( partyID, kTicket );
    rig.pump();
    SW_EXPECT_EQUAL( kTicket, rig.find( kAlice )._party._queuedTicketID );
    SW_EXPECT_TRUE( rig.accept( kCarol, partyID ) == MatchmakingResult::AlreadyQueued );
    rig.pump();
    SW_EXPECT_TRUE( rig.find( kCarol )._result == MatchmakingResult::NotInParty );

    rig._service.leaveParty( kBob, 2 );
    SW_EXPECT_TRUE( rig.take( 2 )._result == MatchmakingResult::Ok );
    vector<uint64> listBroken;
    rig._service.pollBrokenTickets( listBroken );
    SW_REQUIRE_EQUAL( size_t( 1 ), listBroken.size() );
    SW_EXPECT_EQUAL( kTicket, listBroken[0] );
    SW_EXPECT_EQUAL( uint64( 0 ), rig.find( kAlice )._party._queuedTicketID );
}

/** @brief [PartyLobbyServiceTest] 떠나기: 파티장이 떠나면 다음 사람이 파티장이고, 떠난 사람은 멤버가 빈 알림을 받으며 다시 파티를 만들 수 있다. 마지막 사람이 떠나면 파티가 사라진다 */
SW_TEST_CASE( PartyLobbyServiceTest, LeaderLeavingPromotesNextMember )
{
    PartyFixture rig;
    const uint64 partyID = rig.makeParty( kAlice );
    rig.addMember( partyID, kAlice, kBob );
    (void)rig.takeNotifications();

    rig._service.leaveParty( kAlice, 2 );
    const PartyLobbyCompletion left = rig.take( 2 );
    SW_EXPECT_TRUE( left._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( kBob, left._party.getLeaderID() );

    const vector<PartyLobbyNotification> listNotification = rig.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listNotification.size() );
    SW_EXPECT_EQUAL( kBob, listNotification[0]._recipientID );
    SW_EXPECT_EQUAL( kAlice, listNotification[1]._recipientID );
    SW_EXPECT_TRUE( listNotification[1]._party._listMemberID.empty() );

    SW_EXPECT_TRUE( rig.find( kAlice )._result == MatchmakingResult::NotInParty );
    SW_EXPECT_EQUAL( kBob, rig.find( kBob )._party.getLeaderID() );

    rig._service.leaveParty( kBob, 2 );
    SW_EXPECT_TRUE( rig.take( 2 )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( rig.find( kBob )._result == MatchmakingResult::NotInParty );
    SW_EXPECT_TRUE( rig.accept( kAlice, partyID ) == MatchmakingResult::InviteMissing );
}

/** @brief [PartyLobbyServiceTest] 내보내기: 파티장만, 자신은 안 되고, 멤버가 아니면 NotInParty 다. 내보내진 사람의 인덱스가 지워진다 */
SW_TEST_CASE( PartyLobbyServiceTest, KickRules )
{
    PartyFixture rig;
    const uint64 partyID = rig.makeParty( kAlice );
    rig.addMember( partyID, kAlice, kBob );

    rig._service.kickFromParty( kBob, kAlice, 2 );
    SW_EXPECT_TRUE( rig.take( 2 )._result == MatchmakingResult::NotPartyLeader );
    rig._service.kickFromParty( kAlice, kAlice, 2 );
    SW_EXPECT_TRUE( rig.take( 2 )._result.hasError( OnlineError::kInvalidRequest ) );
    rig._service.kickFromParty( kAlice, kCarol, 2 );
    SW_EXPECT_TRUE( rig.take( 2 )._result == MatchmakingResult::NotInParty );
    rig._service.kickFromParty( kAlice, kBob, 2 );
    SW_EXPECT_TRUE( rig.take( 2 )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( rig.find( kBob )._result == MatchmakingResult::NotInParty );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.find( kAlice )._party._listMemberID.size() );
}

/** @brief [PartyLobbyServiceTest] 파티 기록이 시한으로 사라지면 인덱스도 끝나 파티가 없는 것으로 본다 */
SW_TEST_CASE( PartyLobbyServiceTest, PartyExpires )
{
    PartyFixture rig;
    (void)rig.makeParty( kAlice );
    rig._clock.advanceMilliseconds( PartyLobbyLimit::kPartyTTLMs + 1 );
    SW_EXPECT_TRUE( rig.find( kAlice )._result == MatchmakingResult::NotInParty );
    (void)rig.makeParty( kAlice );
}

/** @brief [PartyLobbyServiceTest] 로비 만들기 규칙: 이름 · 정원 하한 · 모드 id · 설정 키 글자를 본다. 만든 사람이 방장이고 목록에 나온다 */
SW_TEST_CASE( PartyLobbyServiceTest, CreateLobbyValidatesAndLists )
{
    PartyFixture  rig;
    LobbySnapshot request;
    request._name           = "Room";
    request._modeID         = "duel";
    request._maxMemberCount = 4;

    LobbySnapshot bad = request;
    bad._name         = "";
    rig._service.createLobby( kAlice, bad, UnixMilliseconds{ kNowMs }, 4 );
    SW_EXPECT_TRUE( rig.take( 4 )._result.hasError( OnlineError::kInvalidRequest ) );
    bad                 = request;
    bad._maxMemberCount = PartyLobbyLimit::kMinLobbySize - 1;
    rig._service.createLobby( kAlice, bad, UnixMilliseconds{ kNowMs }, 4 );
    SW_EXPECT_TRUE( rig.take( 4 )._result.hasError( OnlineError::kInvalidRequest ) );
    bad         = request;
    bad._modeID = "Bad Mode";
    rig._service.createLobby( kAlice, bad, UnixMilliseconds{ kNowMs }, 4 );
    SW_EXPECT_TRUE( rig.take( 4 )._result.hasError( OnlineError::kInvalidRequest ) );
    bad = request;
    bad._listSetting.push_back( LobbySetting{ "Bad Key", "v" } );
    rig._service.createLobby( kAlice, bad, UnixMilliseconds{ kNowMs }, 4 );
    SW_EXPECT_TRUE( rig.take( 4 )._result.hasError( OnlineError::kInvalidRequest ) );

    request._listSetting.push_back( LobbySetting{ "map.name", "desert" } );
    rig._service.createLobby( kAlice, request, UnixMilliseconds{ kNowMs }, 4 );
    const PartyLobbyCompletion created = rig.take( 4 );
    SW_EXPECT_TRUE( created._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( kAlice, created._lobby.getOwnerID() );
    SW_EXPECT_EQUAL( kNowMs, created._lobby._createdMs );

    const vector<LobbySnapshot> listLobby = rig.list();
    SW_REQUIRE_EQUAL( size_t( 1 ), listLobby.size() );
    SW_EXPECT_EQUAL( created._lobby._lobbyID, listLobby[0]._lobbyID );
    SW_REQUIRE_EQUAL( size_t( 1 ), listLobby[0]._listSetting.size() );
    SW_EXPECT_EQUAL( string( "desert" ), listLobby[0]._listSetting[0]._value );

    SW_EXPECT_TRUE( rig.list( "other" ).empty() );
    rig._service.listLobbies( "Bad Mode", 8 );
    SW_EXPECT_TRUE( rig.take( 8 )._result == MatchmakingResult::UnknownMode );
}

/** @brief [PartyLobbyServiceTest] 참가: 인원이 적은 팀으로 들어가고, 두 번 들어와도 한 번이며, 정원이 차면 LobbyFull, 없는 로비는 NotInLobby 다 */
SW_TEST_CASE( PartyLobbyServiceTest, JoinBalancesTeamsAndHonoursCapacity )
{
    PartyFixture rig;
    const uint64 lobbyID = rig.makeLobby( kAlice, "duel", 3 );

    const PartyLobbyCompletion second = rig.join( kBob, lobbyID );
    SW_EXPECT_TRUE( second._result == MatchmakingResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 2 ), second._lobby._listMember.size() );
    SW_EXPECT_EQUAL( 0, second._lobby._listMember[0]._team );
    SW_EXPECT_EQUAL( 1, second._lobby._listMember[1]._team );

    SW_EXPECT_EQUAL( size_t( 2 ), rig.join( kBob, lobbyID )._lobby._listMember.size() );
    const PartyLobbyCompletion third = rig.join( kCarol, lobbyID );
    SW_REQUIRE_EQUAL( size_t( 3 ), third._lobby._listMember.size() );
    SW_EXPECT_EQUAL( 0, third._lobby._listMember[2]._team );

    SW_EXPECT_TRUE( rig.join( kDave, lobbyID )._result == MatchmakingResult::LobbyFull );
    SW_EXPECT_TRUE( rig.join( kDave, 999 )._result == MatchmakingResult::NotInLobby );
}

/** @brief [PartyLobbyServiceTest] 시작: 방장만, 다른 사람이 모두 준비해야 하고, 혼자는 시작할 수 없다. 시작하면 배정 요청이 나오고 새 참가는 막힌다. 다시 열면 또 시작할 수 있다 */
SW_TEST_CASE( PartyLobbyServiceTest, StartRequiresReadyAndReopens )
{
    PartyFixture rig;
    const uint64 lobbyID = rig.makeLobby( kAlice );

    SW_EXPECT_TRUE( rig.start( kAlice, lobbyID ) == MatchmakingResult::LobbyNotReady ); // 혼자다
    (void)rig.join( kBob, lobbyID );
    SW_EXPECT_TRUE( rig.start( kBob, lobbyID ) == MatchmakingResult::NotLobbyOwner );
    SW_EXPECT_TRUE( rig.start( kAlice, lobbyID ) == MatchmakingResult::LobbyNotReady );
    SW_EXPECT_TRUE( rig.ready( kCarol, lobbyID ) == MatchmakingResult::NotInLobby );
    SW_EXPECT_TRUE( rig.ready( kBob, lobbyID ) == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( rig.ready( kBob, lobbyID, false ) == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( rig.start( kAlice, lobbyID ) == MatchmakingResult::LobbyNotReady );
    SW_EXPECT_TRUE( rig.ready( kBob, lobbyID ) == MatchmakingResult::Ok );

    vector<LobbyStartRequest> listStart;
    rig._service.pollLobbyStarts( listStart );
    SW_EXPECT_TRUE( listStart.empty() );
    SW_EXPECT_TRUE( rig.start( kAlice, lobbyID ) == MatchmakingResult::Ok );
    rig._service.pollLobbyStarts( listStart );
    SW_REQUIRE_EQUAL( size_t( 1 ), listStart.size() );
    SW_EXPECT_EQUAL( lobbyID, listStart[0]._lobby._lobbyID );
    SW_EXPECT_TRUE( listStart[0]._lobby._state == LobbyState::Starting );

    SW_EXPECT_TRUE( rig.join( kCarol, lobbyID )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.start( kAlice, lobbyID ).hasError( OnlineError::kInvalidRequest ) );

    rig._service.reopenLobby( lobbyID );
    rig.pump();
    SW_EXPECT_TRUE( rig.join( kCarol, lobbyID )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( rig.ready( kCarol, lobbyID ) == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( rig.start( kAlice, lobbyID ) == MatchmakingResult::Ok );
}

/** @brief [PartyLobbyServiceTest] 떠나기: 방장이 떠나면 다음 사람이 방장이다. 마지막 사람이 떠나면 목록에서도 사라진다 */
SW_TEST_CASE( PartyLobbyServiceTest, LobbyOwnerLeavingPromotesAndLastLeavingRemoves )
{
    PartyFixture rig;
    const uint64 lobbyID = rig.makeLobby( kAlice );
    (void)rig.join( kBob, lobbyID );

    rig._service.leaveLobby( kCarol, lobbyID, 2 );
    SW_EXPECT_TRUE( rig.take( 2 )._result == MatchmakingResult::NotInLobby );
    rig._service.leaveLobby( kAlice, lobbyID, 2 );
    const PartyLobbyCompletion left = rig.take( 2 );
    SW_EXPECT_TRUE( left._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( kBob, left._lobby.getOwnerID() );

    rig._service.leaveLobby( kBob, lobbyID, 2 );
    SW_EXPECT_TRUE( rig.take( 2 )._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( rig.list().empty() );
    SW_EXPECT_TRUE( rig.join( kCarol, lobbyID )._result == MatchmakingResult::NotInLobby );
}

/** @brief [PartyLobbyServiceTest] 다른 로비에 들어가거나 새 로비를 만들면 앞 로비에서 빠진다 — 유령 멤버가 남아 앞 로비를 시작하지 못하는 일이 없다 */
SW_TEST_CASE( PartyLobbyServiceTest, MovingToAnotherLobbyLeavesThePrevious )
{
    PartyFixture rig;
    const uint64 first  = rig.makeLobby( kAlice );
    const uint64 second = rig.makeLobby( kCarol );
    (void)rig.join( kBob, first );
    SW_EXPECT_TRUE( rig.ready( kBob, first ) == MatchmakingResult::Ok );
    (void)rig.join( kDave, first );

    (void)rig.join( kDave, second ); // 준비하지 않은 사람이 옮겼다
    rig.pump();
    SW_EXPECT_TRUE( rig.start( kAlice, first ) == MatchmakingResult::Ok );

    const uint64 own = rig.makeLobby( kBob, "other" ); // 만들어도 옮긴 것이다
    rig.pump();
    for ( const LobbySnapshot& lobby : rig.list() )
    {
        if ( lobby._lobbyID == first )
            SW_EXPECT_FALSE( lobby.hasMember( kBob ) );
    }
    SW_EXPECT_TRUE( own != first );

    (void)rig.join( kDave, second ); // 같은 로비에 다시 들어와도 빠지지 않는다
    rig.pump();
    for ( const LobbySnapshot& lobby : rig.list() )
    {
        if ( lobby._lobbyID == second )
            SW_EXPECT_TRUE( lobby.hasMember( kDave ) );
    }
}

/** @brief [PartyLobbyServiceTest] 계정이 떠나면 파티와 로비에서 함께 빠진다 */
SW_TEST_CASE( PartyLobbyServiceTest, RemoveAccountLeavesPartyAndLobby )
{
    PartyFixture rig;
    const uint64 partyID = rig.makeParty( kAlice );
    rig.addMember( partyID, kAlice, kBob );
    const uint64 lobbyID = rig.makeLobby( kAlice );
    (void)rig.join( kBob, lobbyID );

    rig._service.removeAccount( kBob );
    rig.pump();
    SW_EXPECT_TRUE( rig.find( kBob )._result == MatchmakingResult::NotInParty );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.find( kAlice )._party._listMemberID.size() );
    const vector<LobbySnapshot> listLobby = rig.list();
    SW_REQUIRE_EQUAL( size_t( 1 ), listLobby.size() );
    SW_EXPECT_FALSE( listLobby[0].hasMember( kBob ) );
}
