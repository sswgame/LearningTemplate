// 파티 · 로비 — 만들기 · 초대 · 수락 · 장 넘김 · 마지막이 나가면 기록과 색인이 사라짐, 한 계정 한 파티(서버 둘이 동시에 수락), 내보내기와 줄 선 표 깨짐,
// 로비 들어가기 · 정원 · 준비 · 시작 요청, 로비 나가기의 방장 넘김 · 비면 목록에서 빠짐 · 만료로 사라진 로비는 목록을 읽을 때 지움.
#include "pch.h"

#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 서버 하나의 파티 · 로비 — 캐시 앞 · 라우터 · 로직. */
    struct PartyNode
    {
        MemoryEphemeralStore           _cache;
        EphemeralStoreRouter           _router;
        PartyLobbyService              _service;
        vector<PartyLobbyCompletion>   _listCompletion;
        vector<PartyLobbyNotification> _listNotification;

        PartyNode( MemoryEphemeralDatabase* pCacheDatabase, uint64 serverID )
            : _cache{ pCacheDatabase }
            , _router{}
            , _service{}
            , _listCompletion{}
            , _listNotification{}
        {
            _pCacheDatabase = pCacheDatabase;
            _router.initialize( &_cache );
            _service.initialize( &_router, serverID );
        }

        ~PartyNode()
        {
            _service.shutdown();
            _router.shutdown();
        }

        void step()
        {
            for ( int32 round = 0; round < 12; ++round )
            {
                (void)_router.pump();
            }
            _service.drainCompletions( _listCompletion );
            _service.drainNotifications( _listNotification );
        }

        const PartyLobbyCompletion& last() const { return _listCompletion.back(); }

        /** @brief 캐시에 키가 있는가입니다(시험이 기록 · 색인을 직접 본다). */
        bool hasKey( string_view key )
        {
            MemoryEphemeralStore probe{ _pCacheDatabase }; // 따로 앞 하나 — 라우터의 답을 가져가지 않는다
            (void)probe.submit( EphemeralRequest::makeGet( key ) );
            vector<EphemeralReply> listReply;
            (void)probe.pollReplies( listReply );
            return listReply.size() == 1 && listReply[0]._result == EphemeralResult::Ok;
        }

        MemoryEphemeralDatabase* _pCacheDatabase{ nullptr };
    };
} // namespace

SW_TEST_CASE( PartyLobbyTest, PartyLifecycleWithLeaderHandover )
{
    MemoryEphemeralDatabase cacheDatabase;
    PartyNode               node( &cacheDatabase, 1 );
    node._service.createParty( 10, 1 );
    node.step();
    SW_ASSERT_TRUE( node.last()._result == MatchmakingResult::Ok );
    const uint64 partyID = node.last()._party._partyID;
    SW_EXPECT_EQUAL( node.last()._party.getLeaderID(), AccountID( 10 ) );
    node._service.createParty( 10, 2 );
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::AlreadyInParty );

    node._service.acceptPartyInvite( 11, partyID, 3 ); // 초대 없이
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::InviteMissing );
    node._service.inviteToParty( 11, 12, 4 ); // 파티가 없는 사람의 초대
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::NotInParty );
    node._service.inviteToParty( 10, 11, 5 );
    node.step();
    SW_ASSERT_TRUE( node.last()._result == MatchmakingResult::Ok );
    SW_ASSERT_TRUE( node._listNotification.empty() == false );
    SW_EXPECT_EQUAL( node._listNotification.back()._recipientID, AccountID( 11 ) );
    SW_EXPECT_EQUAL( node._listNotification.back()._pushKind, MatchmakingMethod::kPushPartyInvite );
    SW_EXPECT_EQUAL( node._listNotification.back()._invite._partyID, partyID );
    node._service.acceptPartyInvite( 11, partyID, 6 );
    node.step();
    SW_ASSERT_TRUE( node.last()._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( node.last()._party._listMemberID.size(), size_t( 2 ) );
    SW_EXPECT_FALSE( node.hasKey( PartyLobbyService::makeInviteKey( 11, partyID ) ) ); // 쓴 초대는 지운다

    node._listNotification.clear();
    node._service.leaveParty( 10, 7 ); // 장이 나감 — 11 이 장
    node.step();
    SW_ASSERT_TRUE( node.last()._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( node.last()._party.getLeaderID(), AccountID( 11 ) );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 2 ) ); // 남은 11 · 떠난 10(빈 회원)
    SW_EXPECT_EQUAL( node._listNotification[1]._recipientID, AccountID( 10 ) );
    SW_EXPECT_TRUE( node._listNotification[1]._party._listMemberID.empty() );
    node._service.leaveParty( 11, 8 ); // 마지막 — 기록 · 색인이 사라짐
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::Ok );
    SW_EXPECT_FALSE( node.hasKey( PartyLobbyService::makePartyKey( partyID ) ) );
    SW_EXPECT_FALSE( node.hasKey( PartyLobbyService::makeAccountPartyKey( 11 ) ) );
    node._service.createParty( 11, 9 );
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::Ok );
}

SW_TEST_CASE( PartyLobbyTest, OneAccountOnePartyAcrossServers )
{
    MemoryEphemeralDatabase cacheDatabase;
    PartyNode               first( &cacheDatabase, 1 );
    PartyNode               second( &cacheDatabase, 2 );
    first._service.createParty( 10, 1 );
    second._service.createParty( 20, 1 );
    first.step();
    second.step();
    const uint64 firstParty  = first.last()._party._partyID;
    const uint64 secondParty = second.last()._party._partyID;
    SW_EXPECT_NOT_EQUAL( firstParty, secondParty ); // 서버 id 가 id 의 위 32 비트
    first._service.inviteToParty( 10, 30, 2 );
    second._service.inviteToParty( 20, 30, 2 );
    first.step();
    second.step();
    first._service.acceptPartyInvite( 30, firstParty, 3 );
    second._service.acceptPartyInvite( 30, secondParty, 3 ); // 같은 계정이 다른 서버에서 동시에
    first.step();
    second.step();
    const bool bFirstOk  = first.last()._result == MatchmakingResult::Ok;
    const bool bSecondOk = second.last()._result == MatchmakingResult::Ok;
    SW_EXPECT_TRUE( bFirstOk != bSecondOk ); // 하나만
    SW_EXPECT_TRUE( ( bFirstOk ? second.last()._result : first.last()._result ) == MatchmakingResult::AlreadyInParty );
}

SW_TEST_CASE( PartyLobbyTest, KickBreaksTheQueuedTicketAndFreesTheIndex )
{
    MemoryEphemeralDatabase cacheDatabase;
    PartyNode               node( &cacheDatabase, 1 );
    node._service.createParty( 10, 1 );
    node.step();
    const uint64 partyID = node.last()._party._partyID;
    node._service.inviteToParty( 10, 11, 2 );
    node._service.inviteToParty( 10, 12, 3 );
    node.step();
    node._service.acceptPartyInvite( 11, partyID, 4 );
    node.step();
    node._service.setPartyTicket( partyID, 77 );
    node.step();
    node._service.acceptPartyInvite( 12, partyID, 5 ); // 줄 선 동안은 못 들어온다
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::AlreadyQueued );

    node._service.kickFromParty( 11, 10, 6 ); // 장이 아니다
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::NotPartyLeader );
    node._service.kickFromParty( 10, 11, 7 );
    node.step();
    SW_ASSERT_TRUE( node.last()._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( node.last()._party._listMemberID.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node.last()._party._queuedTicketID, uint64( 0 ) );
    vector<uint64> listBroken;
    node._service.drainBrokenTickets( listBroken );
    SW_ASSERT_EQUAL( listBroken.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listBroken[0], uint64( 77 ) );
    node._service.createParty( 11, 8 ); // 내보내진 사람의 색인도 지워졌다
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::Ok );
}

SW_TEST_CASE( PartyLobbyTest, LobbyJoinReadyAndStartRequest )
{
    MemoryEphemeralDatabase cacheDatabase;
    PartyNode               node( &cacheDatabase, 1 );
    LobbySnapshot           request;
    request._name           = "friday night";
    request._modeID         = "duo";
    request._maxMemberCount = 2;
    node._service.createLobby( 10, request, 1000, 1 );
    node.step();
    SW_ASSERT_TRUE( node.last()._result == MatchmakingResult::Ok );
    const uint64 lobbyID = node.last()._lobby._lobbyID;
    SW_EXPECT_EQUAL( node.last()._lobby.getOwnerID(), AccountID( 10 ) );
    node._service.listLobbies( "duo", 2 );
    node.step();
    SW_ASSERT_EQUAL( node.last()._listLobby.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( node.last()._listLobby[0]._name.c_str(), "friday night" );

    request._name = "";
    node._service.createLobby( 10, request, 1000, 3 ); // 이름 없음
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::Invalid );

    node._service.joinLobby( 11, lobbyID, 4 );
    node._service.joinLobby( 12, lobbyID, 5 ); // 같은 순간 둘 — 비교 후 쓰기가 하나를 다시 읽게 하고, 정원 2
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::LobbyFull );
    SW_EXPECT_EQUAL( node._listCompletion[node._listCompletion.size() - 2]._lobby._listMember[1]._team, 1 ); // 인원이 적은 편
    node._service.startLobby( 10, lobbyID, 6 );                                                              // 아직 준비 안 됨
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::LobbyNotReady );
    node._service.startLobby( 11, lobbyID, 7 ); // 방장이 아니다
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::NotLobbyOwner );
    node._service.setLobbyReady( 11, lobbyID, true, 8 );
    node.step();
    node._service.startLobby( 10, lobbyID, 9 );
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::Ok );
    vector<LobbyStartRequest> listStart;
    node._service.drainLobbyStarts( listStart );
    SW_ASSERT_EQUAL( listStart.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listStart[0]._lobby._state == LobbyState::Starting );
    node._service.joinLobby( 13, lobbyID, 10 ); // 시작한 로비
    node.step();
    SW_EXPECT_TRUE( node.last()._result != MatchmakingResult::Ok );

    node._service.reopenLobby( lobbyID ); // 배정 실패 — 다시 Open
    node.step();
    node._service.listLobbies( "duo", 11 );
    node.step();
    SW_ASSERT_EQUAL( node.last()._listLobby.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( node.last()._listLobby[0]._state == LobbyState::Open );
}

SW_TEST_CASE( PartyLobbyTest, LobbyLeaveHandsOverOwnerAndListDropsGoneLobbies )
{
    MemoryEphemeralDatabase cacheDatabase;
    cacheDatabase.setManualTimeMs( 0 );
    PartyNode     node( &cacheDatabase, 1 );
    LobbySnapshot request;
    request._name           = "practice";
    request._modeID         = "arena";
    request._maxMemberCount = 4;
    LobbySetting setting;
    setting._key   = "map";
    setting._value = "harbor";
    request._listSetting.push_back( setting );
    node._service.createLobby( 10, request, 0, 1 );
    node.step();
    const uint64 lobbyID = node.last()._lobby._lobbyID;
    SW_ASSERT_EQUAL( node.last()._lobby._listSetting.size(), size_t( 1 ) );
    node._service.joinLobby( 11, lobbyID, 2 );
    node.step();

    node._listNotification.clear();
    node._service.leaveLobby( 10, lobbyID, 3 ); // 방장이 나감 — 11 이 방장
    node.step();
    SW_ASSERT_TRUE( node.last()._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( node.last()._lobby.getOwnerID(), AccountID( 11 ) );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( node._listNotification[1]._recipientID, AccountID( 10 ) );
    SW_EXPECT_TRUE( node._listNotification[1]._lobby._listMember.empty() );
    node._service.setLobbyReady( 12, lobbyID, true, 4 ); // 회원이 아니다
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::NotInLobby );
    node._service.leaveLobby( 11, lobbyID, 5 ); // 마지막 — 기록 · 목록에서 사라짐
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::Ok );
    SW_EXPECT_FALSE( node.hasKey( PartyLobbyService::makeLobbyKey( lobbyID ) ) );
    node._service.listLobbies( "arena", 6 );
    node.step();
    SW_EXPECT_TRUE( node.last()._result == MatchmakingResult::Ok );
    SW_EXPECT_TRUE( node.last()._listLobby.empty() );

    node._service.createLobby( 20, request, 0, 7 );
    node.step();
    node._service.createLobby( 21, request, 10, 8 );
    node.step();
    const uint64 newerLobbyID = node.last()._lobby._lobbyID;
    node._service.joinLobby( 22, newerLobbyID, 9 ); // 바뀌면 만료가 연장된다
    cacheDatabase.advanceTimeMs( PartyLobbyLimit::kLobbyTtlMs - 1 );
    node.step();
    cacheDatabase.advanceTimeMs( 2 ); // 첫 로비만 만료
    node._service.listLobbies( "arena", 10 );
    node.step();
    SW_ASSERT_EQUAL( node.last()._listLobby.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node.last()._listLobby[0]._lobbyID, newerLobbyID );
}

SW_TEST_CASE( PartyLobbyTest, RemoveAccountLeavesPartyAndLobby )
{
    MemoryEphemeralDatabase cacheDatabase;
    PartyNode               node( &cacheDatabase, 1 );
    node._service.createParty( 10, 1 );
    LobbySnapshot request;
    request._name           = "room";
    request._modeID         = "duo";
    request._maxMemberCount = 2;
    node._service.createLobby( 10, request, 0, 2 );
    node.step();
    const uint64 lobbyID = node.last()._lobby._lobbyID;
    node._service.removeAccount( 10 ); // 끊김
    node.step();
    SW_EXPECT_FALSE( node.hasKey( PartyLobbyService::makeAccountPartyKey( 10 ) ) );
    SW_EXPECT_FALSE( node.hasKey( PartyLobbyService::makeLobbyKey( lobbyID ) ) );
    SW_EXPECT_EQUAL( node._service.getPendingCount(), 0 );
}
