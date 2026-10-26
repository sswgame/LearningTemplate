/**
 * @file OnlinePresence.h
 * @brief 서버 여러 대의 접속 상태를 관리합니다. `IAccountPresence` 구현입니다.
 * @details 이 프로세스에 접속한 계정을 캐시에 기록한다. `presence:<계정>` 키는 서버를 값으로 갖고, `presence.name:<이름 해시>` 키는 계정, 서버, 이름을 값으로 갖는다.
 *          계정이 접속한 서버를 계정 id 나 표시 이름으로 찾을 수 있고, 다른 서버에 접속한 계정에게는 알림을 버스(`push.<서버>`)로 넘긴다.
 *          - 캐시는 원본이 아니다. 시한(기본 60초)을 걸고 주기(기본 30초)마다 다시 기록하므로, 캐시가 비어도(재시작) 다음 주기에 복구된다. 원본은 저장소의 세션 레코드이다.
 *          - 다시 기록할 때는 "지금 값이 내 것일 때만"(CompareAndSet) 쓴다. 다른 서버로 옮겨 간 계정을 덮지 않기 위해서이다.
 *            비교가 어긋나면 "없을 때만" 쓴다. 키가 없는 경우와 남의 값이 있는 경우를 캐시가 구분하지 못하기 때문이다.
 *          - 떠날 때는 "내 것일 때만" 지운다(CompareAndErase). 같은 계정이 이미 다른 서버에 새로 접속했으면 그 기록을 남긴다.
 *          - 알림은 최대 한 번 배달된다(버스). 놓치면 클라이언트가 다시 읽으며, 거래 스냅샷은 저장소에 있다.
 *          - 캐시나 버스가 없는 호스트(서버 한 대)에서는 찾기가 다음 `tick` 에 "없음" 으로 끝나고 원격 알림은 false 를 반환한다. 찾기 결과는 항상 맡긴 델리게이트로 한 번 전달한다.
 *
 *          계정 키트의 `AccountServer` 가 소유하며 호스트 틱 스레드에서 쓴다. Nakama 의 status registry 와 PlayFab 의 presence 에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"

#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class OnlineServiceHost;
    class ServerBusMessage;

    /** @brief 접속 상태 설정입니다. */
    struct OnlinePresenceConfig
    {
        int64 _ttlMs{ 60000 };             ///< 캐시 키의 시한이다
        int64 _refreshIntervalMs{ 30000 }; ///< 캐시에 다시 기록하는 간격이다. 시한의 절반이다
    };
} // namespace sw

namespace sw
{
    /** @brief 접속 상태의 서버 버스 주제입니다. 내보내는 클래스의 정적 멤버로 두지 않습니다(지연 로드 모듈은 데이터를 가져올 수 없습니다). */
    struct OnlinePresenceBus
    {
        static constexpr ServerBusTopicFamily<OnlinePushRelay> kPushTopic{ "push." }; ///< 뒤에 서버 16진. 그 서버에 접속한 계정에게 가는 알림 중계이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlinePresence
     * @brief 접속 상태를 관리합니다. 호스트 틱 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API OnlinePresence final : public IAccountPresence
    {
    public:
        OnlinePresence();
        ~OnlinePresence() override;

        void setSettings( const OnlinePresenceConfig& settings ) { _settings = settings; }
        /** @brief 호스트의 캐시 라우터와 버스를 씁니다. 호스트 `initialize` 뒤에 부르며, 둘 다 있을 때만 켜집니다. 알림 주제 `makePushTopic` 은 호출하는 쪽이 구독합니다. 버스 소비자는 서비스입니다. */
        void attach( OnlineServiceHost* pHost );
        /** @brief 라우터에 맡긴 요청을 취소하고 종료합니다. 호스트가 살아 있을 때 부릅니다. */
        void shutdown();
        /** @brief 호스트가 종료될 때 부릅니다. 호스트를 기다리던 작업을 버립니다. 캐시 응답은 호스트가 이미 회수했습니다. 접속한 계정 목록은 남으며, 다시 `attach` 하면 다시 기록합니다. */
        void detach();
        bool isEnabled() const { return _pHost != nullptr; }

        /** @brief 계정이 이 프로세스에 접속했거나 표시 이름이 바뀌었을 때 부릅니다. */
        void noteOnline( const AccountIdentity& identity );
        /** @brief 계정이 이 프로세스에서 떠났을 때 부릅니다. */
        void noteOffline( AccountID accountID );
        /** @brief 캐시에 다시 기록하는 주기를 처리합니다. */
        void update( int64 nowMs );
        /** @brief 이 서버의 알림 주제(`push.<서버>`) 메시지를 이 빌드의 알림으로 바꿔 접속해 있는 계정에게 넘깁니다. 이 주제가 아니면 false 를 반환합니다. */
        bool handlePushMessage( const ServerBusMessage& message );

        /** @brief 서버 @p serverID 의 알림 주제를 반환합니다. */
        static string makePushTopic( uint64 serverID );
        static string makeAccountKey( AccountID accountID );
        static string makeNameKey( string_view displayName );

        // IAccountPresence
        uint64 submitFindByDisplayName( string_view displayName, const AccountPresenceDelegate& onFound ) override;
        uint64 submitFindByAccount( AccountID accountID, const AccountPresenceDelegate& onFound ) override;
        bool   relayRemotePush( const OnlinePushRelay& relay ) override;
        void   cancel( uint64 requestID ) override;

    private:
        enum class PendingKind : uint8
        {
            FindName = 0,
            FindAccount,
            Push,
            RefreshAccount,
            RefreshName
        };

        struct PendingOperation
        {
            OnlinePushRelay         _relay{};   ///< Push 의 중계 봉투이다
            string                  _nameKey{}; ///< FindName 에서는 찾는 이름(소문자 비교)이고, RefreshName 에서는 키이다
            AccountPresenceDelegate _onFound{}; ///< Find* — 부른 쪽
            AccountID               _accountID{ kInvalidAccountID };
            uint64                  _lookupID{ 0 }; ///< Find* 를 부른 쪽에 준 id 이다
            PendingKind             _pendingKind{ PendingKind::FindName };
        };

        struct DeferredFound
        {
            AccountPresenceResult   _result{};
            AccountPresenceDelegate _onFound{};
        };

        void onCacheReply( const EphemeralReply& reply );
        void submitPending( const EphemeralRequest& request, PendingOperation&& pending );
        void writeAccountEntries( const AccountIdentity& identity, bool bRefresh );
        void finishFindName( const PendingOperation& pending, const EphemeralReply& reply );
        void finishFindAccount( const PendingOperation& pending, const EphemeralReply& reply );
        void deliverDeferred();
        void finishPush( const PendingOperation& pending, const EphemeralReply& reply );
        void finishRefresh( const PendingOperation& pending, const EphemeralReply& reply );
        /** @brief 중계 봉투를 이 빌드의 알림으로 바꿔 이 서버에 접속한 계정에게 보냅니다. 바꾸지 못하면 레지스트리가 로그와 카운터를 남겼다. */
        void          deliverRelay( const OnlinePushRelay& relay );
        vector<uint8> makeServerBytes() const;
        vector<uint8> makeNameBytes( const AccountIdentity& identity ) const;

        unordered_map<uint64, PendingOperation>   _mapRequestToPending;
        unordered_map<AccountID, AccountIdentity> _mapAccountToIdentity; ///< 이 프로세스에 접속한 계정이다
        vector<DeferredFound>                     _listDeferred;         ///< 캐시 없이 끝난 찾기이다. 맡긴 함수 안에서 부르지 않고 다음 `tick` 에 알린다
        OnlinePresenceConfig                      _settings;
        OnlineServiceHost*                        _pHost;
        Subscription                              _replyOwner; ///< 라우터 답의 주인 몫(`attach` 가 열고 `detach` 가 놓는다)
        uint64                                    _serverID;
        uint64                                    _nextLookupID;
        int64                                     _nextRefreshMs;
    };
} // namespace sw
