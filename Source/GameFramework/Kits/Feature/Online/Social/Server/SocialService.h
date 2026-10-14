/**
 * @file SocialService.h
 * @brief 친구 로직입니다. 관계 바꾸기, 이 서버 계정의 관계 메모리(차단 조회), 접속 상태(캐시와 버스)를 처리합니다. 전송은 모릅니다.
 * @details 관계 바꾸기는 저장소 작업이며, 양쪽 줄과 양쪽 개수를 버전 조건으로 묶은 한 트랜잭션이다. 충돌이 나면 다시 시도한다.
 *          - 관계는 방향이 있는 레코드 둘(`social_link` 의 `<나>/<상대>`)과 사람마다의 개수(`social_count`)로 이루어진다.
 *            두 사람이 동시에 서로 신청해도 하나만 이기고, 진 쪽은 다시 읽어 "상대가 이미 신청함" 을 보고 자동 수락한다. 상한은 경합이 나도 넘지 않는다.
 *          - 이 서버에 접속한 계정의 관계는 메모리에 올린다. 그 계정의 첫 요청에서 올리며, 클라이언트는 로그인 뒤 `kListLinks` 를 먼저 부른다.
 *            다른 서버가 관계를 바꾸면 버스 `social.links` 로 그 계정을 다시 읽는다.
 *            `isBlockedLocal` 은 채팅 정책과 귓속말이 부른다. 키트끼리는 서로 모르므로 서버 조립(`SocialOnlineServerKit`)이 기반의 `IAccountBlockList` 로 내놓는다.
 *          - 접속 상태는 캐시 `social/rp/<계정>` 에 시한 90초로 저장하고 30초마다 연장한다. 버스 `social.presence` 로 알리면 받은 서버마다 그 계정을 친구로 둔 **이 서버의** 계정에게 전달한다.
 *          - 이름으로 친구 신청은 계정 이름 인덱스(`IAccountNameIndex`, 정식 계정만)를 같은 저장소 작업 안에서 읽는다. 찾기와 쓰기가 한 번의 왕복으로 끝난다.
 *          - 서비스 스레드 하나에서 쓴다. 결과는 `drainCompletions` 로 회수하며 요청 태그 0은 내부에서 건 작업이라 완료가 없다. 알림은 `drainNotifications` 로 회수하며 받는 사람을 포함한다.
 *
 *          Nakama Friends(친구, 보낸 신청, 받은 신청, 막음 상태와 방향 있는 행 둘)와 같은 구조이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialTypes.h"

namespace sw
{
    enum class SocialLinkOperation : uint8;

    struct EphemeralReply;
    struct SocialLinkDecision;

    class EphemeralStoreRouter;
    class IAccountNameIndex;
    class IServerBus;
    class IServiceStore;

    /** @brief 빌려 쓰는 객체들입니다. */
    struct SocialServiceDependencies
    {
        IServiceStore*           _pStore{ nullptr };     ///< 필수이다
        EphemeralStoreRouter*    _pRouter{ nullptr };    ///< 접속 상태에 쓴다. null 이면 이 서버의 계정만 다룬다
        IServerBus*              _pBus{ nullptr };       ///< null 이면 서버가 한 대이다
        const IAccountNameIndex* _pNameIndex{ nullptr }; ///< 이름으로 친구 신청할 때 쓴다. null 이면 NotFound 로 끝낸다
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. */
    struct SocialCompletion
    {
        vector<SocialLink>     _listLink{};     ///< 관계 목록이다
        vector<SocialPresence> _listPresence{}; ///< 친구의 접속 상태이다
        uint64                 _requestTag{ 0 };
        AccountID              _otherID{ kInvalidAccountID }; ///< 관계를 바꾼 상대이다. 이름으로 찾았으면 찾은 계정이다
        SocialResult           _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 버스 주제입니다. 내보내는 클래스의 정적 멤버로 두지 않습니다. 지연 로드 모듈에서는 데이터를 가져올 수 없기 때문입니다. */
    struct SocialBus
    {
        static constexpr const utf8* kLinksTopic    = "social.links";    ///< 본문은 varuint 계정 둘이다. 관계가 바뀐 두 사람이다
        static constexpr const utf8* kPresenceTopic = "social.presence"; ///< 본문은 접속 상태(`SocialProtocol::writePresence`)이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class SocialService
     * @brief 친구 로직입니다. 서비스 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API SocialService
    {
    public:
        SocialService();
        ~SocialService();

        SocialService( const SocialService& )            = delete;
        SocialService& operator=( const SocialService& ) = delete;

        void initialize( const SocialServiceDependencies& dependencies );
        /** @brief 기다리는 캐시 읽기를 취소하고 메모리를 비웁니다. 맡긴 저장소 작업은 호출하는 쪽이 먼저 회수해야 하며, 호스트와 저장소를 종료한 뒤에 합니다. */
        void shutdown();
        /** @brief 접속 상태의 시한을 연장합니다. */
        void tick( int64 nowMs );

        // 관계 함수들은 비동기이며 완료는 요청 태그로 받는다
        void changeLink( SocialLinkOperation operation, AccountID accountID, AccountID otherID, int64 nowMs, uint64 requestTag );
        /** @brief 정식 계정의 표시 이름으로 친구 신청합니다. 완료의 `_otherID` 가 찾은 계정입니다. */
        void requestFriendByName( AccountID accountID, string_view displayName, int64 nowMs, uint64 requestTag );
        /** @brief 관계 목록을 저장소에서 읽습니다. 이 서버의 메모리도 그 결과로 바꿉니다. */
        void listLinks( AccountID accountID, uint64 requestTag );

        // 접속 상태
        void setPresence( AccountID accountID, SocialPresenceStatus status, string_view activity, int64 nowMs, uint64 requestTag );
        void queryFriendPresence( AccountID accountID, uint64 requestTag );
        /** @brief 계정이 이 서버를 떠났을 때 부릅니다. 오프라인을 알리고 캐시와 메모리에서 뺍니다. */
        void removeAccount( AccountID accountID );

        /** @brief 이 서버에 접속한 @p ownerID 가 @p otherID 를 막았으면 true 입니다. 관계를 아직 읽지 않았으면 false 입니다. */
        bool isBlockedLocal( AccountID ownerID, AccountID otherID ) const;
        /** @brief 다른 서버가 낸 버스 메시지를 처리합니다. `SocialBus` 주제이며 자기 서버가 낸 것은 호출하는 쪽이 거릅니다. */
        void handleBusMessage( string_view topic, const vector<uint8>& bytes );

        void  drainCompletions( vector<SocialCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void  drainNotifications( vector<SocialNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }
        int32 getPendingCount() const { return _pendingCount; }

        /** @brief 작업의 `complete` 가 호출합니다. 키트 안에서만 씁니다. */
        void applyLinkChange( uint64 requestTag, AccountID accountID, AccountID otherID, const SocialLinkDecision& decision );
        void applyLinksLoaded( AccountID accountID, uint64 requestTag, bool bReadOk, vector<SocialLink>&& listLink );

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
            uint64    _queryID{ 0 };
            AccountID _friendID{ kInvalidAccountID };
        };

        LocalAccount& ensureLocal( AccountID accountID );
        void          startLoad( AccountID accountID, uint64 requestTag );
        void          reloadIfLocal( AccountID accountID );
        void          submitLinkWork( SocialLinkOperation operation, AccountID accountID, AccountID otherID, string_view displayName, int64 nowMs, uint64 requestTag );
        void          publishPresence( AccountID accountID, const LocalAccount& local );
        void          notifyFriendsOfPresence( const SocialPresence& presence );
        void          writePresenceRecord( AccountID accountID, const LocalAccount& local );
        void          onPresenceReply( const EphemeralReply& reply );
        void          pushCompletion( uint64 requestTag, SocialResult result );

        unordered_map<AccountID, LocalAccount> _mapAccountToLocal;
        unordered_map<uint64, PresenceRead>    _mapCacheRequestToRead;
        unordered_map<uint64, PresenceQuery>   _mapQuery;
        EventBuffer<SocialCompletion>          _completionBuffer;
        EventBuffer<SocialNotification>        _notificationBuffer;
        SocialServiceDependencies              _dependencies;
        uint64                                 _nextQueryID;
        int32                                  _pendingCount;
    };
} // namespace sw
