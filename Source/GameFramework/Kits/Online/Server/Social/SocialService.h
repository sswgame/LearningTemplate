/**
 * @file SocialService.h
 * @brief 친구 로직 — 관계 바꾸기(저장소 일: 양쪽 줄 + 양쪽 개수를 판 조건으로 한 트랜잭션, 충돌이면 다시), 이 서버 계정의 관계 메모리(차단 조회), 접속 상태(캐시 · 버스). 전송을 모릅니다.
 * @details - 관계 = 방향 있는 레코드 둘(`social_link` `<나>/<상대>`) + 사람마다 개수(`social_count`). 두 사람이 동시에 서로 신청해도 하나만 이기고(진 쪽은 다시 읽어
 *            "상대가 이미 신청함" → 자동 수락), 상한이 경합으로 넘지 않는다.
 *          - 이 서버에 붙은 계정의 관계는 메모리에 올린다(그 계정의 첫 요청 — 클라이언트는 로그인 뒤 `kListLinks` 를 먼저 부른다). 다른 서버가 바꾸면
 *            버스 `social.links` 로 그 계정을 다시 읽는다. `isBlockedLocal` 은 채팅 정책 · 귓속말이 부른다(게임 조립이 잇는다 — 키트끼리 모른다).
 *          - 접속 상태는 캐시 `social/rp/<계정>`(시한 90 초, 30 초마다 연장) + 버스 `social.presence` — 받은 서버마다 그 계정을 친구로 둔 **이 서버의** 계정에게 알린다.
 *          - 이름으로 친구 신청은 계정 이름 색인(`IAccountNameIndex` — 정식 계정만)을 같은 저장소 일 안에서 읽는다(찾기 + 쓰기가 왕복 하나).
 *          - 서비스 스레드 하나. 결과는 `drainCompletions`(꼬리표 — 0 은 안에서 건 일이라 완료가 없다), 알림은 `drainNotifications`(받는 이 포함).
 *          Nakama Friends(상태 친구 · 보낸 신청 · 받은 신청 · 막음, 방향 있는 행 둘)와 같은 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Server/Social/SocialLinkRules.h"
#include "GameFramework/Kits/Online/Social/SocialTypes.h"

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;
    class IAccountNameIndex;
    class IServerBus;
    class IServiceStore;

    /** @brief 빌려 쓰는 것들입니다. */
    struct SocialServiceDependencies
    {
        IServiceStore*           _pStore{ nullptr };     ///< 필수
        EphemeralStoreRouter*    _pRouter{ nullptr };    ///< 접속 상태 — null 이면 이 서버 계정만
        IServerBus*              _pBus{ nullptr };       ///< null 이면 서버 한 대
        const IAccountNameIndex* _pNameIndex{ nullptr }; ///< 이름으로 친구 신청 — null 이면 NotFound
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. */
    struct SocialCompletion
    {
        vector<SocialLink>     _listLink{};     ///< 관계 목록
        vector<SocialPresence> _listPresence{}; ///< 친구 접속 상태
        uint64                 _requestTag{ 0 };
        AccountId              _otherId{ kInvalidAccountId }; ///< 관계 바꾸기의 상대(이름으로 찾았으면 찾은 계정)
        SocialResult           _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 버스 주제입니다(내보내는 클래스의 정적 멤버로 두지 않는다 — 지연 로드 모듈의 데이터 가져오기 금지). */
    struct SocialBus
    {
        static constexpr const utf8* kLinksTopic    = "social.links";    ///< 몸 = varuint 계정 둘(관계가 바뀐 두 사람)
        static constexpr const utf8* kPresenceTopic = "social.presence"; ///< 몸 = 접속 상태(`SocialProtocol::writePresence`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SocialService
     * @brief 친구 로직입니다(서비스 스레드).
     */
    class SW_GF_API SocialService
    {
    public:
        SocialService();
        ~SocialService();

        SocialService( const SocialService& )            = delete;
        SocialService& operator=( const SocialService& ) = delete;

        void initialize( const SocialServiceDependencies& dependencies );
        /** @brief 기다리는 캐시 읽기를 취소하고 메모리를 비웁니다. 맡긴 저장소 일은 부르는 쪽이 먼저 거둔다(호스트 · 저장소를 내린 뒤). */
        void shutdown();
        /** @brief 접속 상태 시한을 연장합니다. */
        void tick( int64 nowMs );

        // 관계(비동기 — 완료는 꼬리표로)
        void changeLink( SocialLinkOperation operation, AccountId accountId, AccountId otherId, int64 nowMs, uint64 requestTag );
        /** @brief 정식 계정의 표시 이름으로 친구 신청합니다(완료의 `_otherId` 가 찾은 계정). */
        void requestFriendByName( AccountId accountId, string_view displayName, int64 nowMs, uint64 requestTag );
        /** @brief 관계 목록을 저장소에서 읽습니다(이 서버 메모리도 그것으로 바꾼다). */
        void listLinks( AccountId accountId, uint64 requestTag );

        // 접속 상태
        void setPresence( AccountId accountId, SocialPresenceStatus status, string_view activity, int64 nowMs, uint64 requestTag );
        void queryFriendPresence( AccountId accountId, uint64 requestTag );
        /** @brief 계정이 이 서버를 떠났다 — 오프라인을 알리고 캐시 · 메모리에서 뺀다. */
        void removeAccount( AccountId accountId );

        /** @brief 이 서버에 붙은 @p ownerId 가 @p otherId 를 막았는가입니다(관계를 아직 읽지 않았으면 false). */
        bool isBlockedLocal( AccountId ownerId, AccountId otherId ) const;
        /** @brief 다른 서버가 낸 버스 메시지입니다(`SocialBus` 주제 — 자기 서버 것은 부르는 쪽이 거른다). */
        void handleBusMessage( string_view topic, const vector<uint8>& bytes );

        void  drainCompletions( vector<SocialCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void  drainNotifications( vector<SocialNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }
        int32 getPendingCount() const { return _pendingCount; }

        /** @brief 일의 `complete` 가 부릅니다(키트 안). */
        void applyLinkChange( uint64 requestTag, AccountId accountId, AccountId otherId, const SocialLinkDecision& decision );
        void applyLinksLoaded( AccountId accountId, uint64 requestTag, bool bReadOk, vector<SocialLink>&& listLink );

    private:
        struct LocalAccount
        {
            vector<SocialLink>   _listLink{};
            string               _activity{};
            int64                _presenceWrittenMs{ 0 };
            SocialPresenceStatus _status{ SocialPresenceStatus::Offline };
            uint8                _bLoaded{ SW_FALSE };
            uint8                _bLoading{ SW_FALSE };
        };

        struct PresenceQuery
        {
            vector<SocialPresence> _listPresence{};
            uint64                 _requestTag{ 0 };
            int32                  _outstandingCount{ 0 };
        };

        struct PresenceRead
        {
            uint64    _queryId{ 0 };
            AccountId _friendId{ kInvalidAccountId };
        };

        LocalAccount& ensureLocal( AccountId accountId );
        void          startLoad( AccountId accountId, uint64 requestTag );
        void          reloadIfLocal( AccountId accountId );
        void          submitLinkWork( SocialLinkOperation operation, AccountId accountId, AccountId otherId, string_view displayName, int64 nowMs, uint64 requestTag );
        void          publishPresence( AccountId accountId, const LocalAccount& local );
        void          notifyFriendsOfPresence( const SocialPresence& presence );
        void          writePresenceRecord( AccountId accountId, const LocalAccount& local );
        void          onPresenceReply( const EphemeralReply& reply );
        void          pushCompletion( uint64 requestTag, SocialResult result );

        unordered_map<AccountId, LocalAccount> _mapAccountToLocal;
        unordered_map<uint64, PresenceRead>    _mapCacheRequestToRead;
        unordered_map<uint64, PresenceQuery>   _mapQuery;
        EventBuffer<SocialCompletion>          _completionBuffer;
        EventBuffer<SocialNotification>        _notificationBuffer;
        SocialServiceDependencies              _dependencies;
        uint64                                 _nextQueryId;
        int32                                  _pendingCount;
    };
} // namespace sw
