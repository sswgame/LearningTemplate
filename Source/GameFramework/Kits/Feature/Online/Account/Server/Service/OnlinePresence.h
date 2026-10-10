/**
 * @file OnlinePresence.h
 * @brief 서버 여럿의 접속 상태 — 이 프로세스에 붙은 계정을 캐시에 적고(`presence:<계정>` = 서버, `presence.name:<이름 해시>` = 계정 · 서버 · 이름),
 *        계정이 붙은 서버를 계정 id · 표시 이름으로 찾고, 다른 서버의 계정에게 알림을 버스(`push.<서버>`)로 넘깁니다. `IAccountPresence` 구현입니다.
 * @details - 캐시는 정본이 아니다 — 시한(기본 60 초)을 걸고 주기(기본 30 초)로 다시 적는다. 캐시가 비면(재시작) 다음 주기에 돌아온다. 정본은 저장소의 세션 레코드다.
 *          - 다시 적기는 "지금 값이 내 것일 때만"(CompareAndSet) — 다른 서버로 옮겨 간 계정을 덮지 않는다. 비교가 어긋나면(키가 없거나 남의 값 — 캐시는 둘을 가르지 않는다) "없을 때만" 적는다.
 *          - 떠날 때는 "내 것일 때만" 지운다(CompareAndErase) — 같은 계정이 이미 다른 서버에 새로 붙었으면 그 표시를 남긴다.
 *          - 알림은 최대 한 번이다(버스) — 놓치면 클라이언트가 다시 읽는다(거래 스냅숏은 저장소에 있다).
 *          - 캐시 · 버스가 없는 호스트(서버 한 대)면 찾기는 다음 `tick` 에 "없음", 원격 알림은 false 다. 찾기 결과는 언제나 맡긴 델리게이트로 한 번이다.
 *          계정 키트의 `AccountServer` 가 소유하고 호스트 틱 스레드에서 쓴다. Nakama 의 status registry · PlayFab 의 presence 와 같은 자리다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ServerBusMessage;

    class OnlineServiceHost;

    /** @brief 접속 상태 설정입니다. */
    struct OnlinePresenceSettings
    {
        int64 _ttlMs{ 60000 };             ///< 캐시 키 시한
        int64 _refreshIntervalMs{ 30000 }; ///< 다시 적는 간격(시한의 절반)
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlinePresence
     * @brief 접속 상태 창구입니다(호스트 틱 스레드).
     */
    class SW_GF_API OnlinePresence final : public IAccountPresence
    {
    public:
        OnlinePresence();
        ~OnlinePresence() override;

        void setSettings( const OnlinePresenceSettings& settings ) { _settings = settings; }
        /** @brief 호스트의 캐시 라우터 · 버스를 씁니다(호스트 `initialize` 뒤). 둘 다 있을 때만 켜진다(알림 주제 `makePushTopic` 구독은 부르는 쪽 — 버스 소비자는 서비스). */
        void attach( OnlineServiceHost* pHost );
        /** @brief 라우터에 맡긴 요청을 취소하고 내립니다(호스트가 살아 있을 때). */
        void shutdown();
        /** @brief 호스트가 내려간다 — 호스트를 놓고 기다리던 일을 버립니다(캐시 답은 호스트가 이미 거뒀다). 붙은 계정 목록은 남는다(다시 `attach` 하면 다시 적는다). */
        void detach();
        bool isEnabled() const { return _pHost != nullptr; }

        /** @brief 계정이 이 프로세스에 붙었다(또는 표시 이름이 바뀌었다). */
        void noteOnline( const AccountIdentity& identity );
        /** @brief 계정이 이 프로세스에서 떠났다. */
        void noteOffline( AccountID accountID );
        /** @brief 다시 적기 주기입니다. */
        void tick( int64 nowMs );
        /** @brief 이 서버 알림 주제(`push.<서버>`)의 메시지 — 붙어 있는 계정에게 넘긴다. 이 주제가 아니면 false. */
        bool handlePushMessage( const ServerBusMessage& message );

        /** @brief 서버 @p serverID 의 알림 주제입니다. */
        static string makePushTopic( uint64 serverID );
        static string makeAccountKey( AccountID accountID );
        static string makeNameKey( string_view displayName );

        // IAccountPresence
        uint64 submitFindByDisplayName( string_view displayName, const AccountPresenceDelegate& onFound ) override;
        uint64 submitFindByAccount( AccountID accountID, const AccountPresenceDelegate& onFound ) override;
        bool   sendRemotePush( AccountID accountID, uint16 kind, const BitWriter& body ) override;
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
            vector<uint8>           _bodyBytes{}; ///< Push — 알림 몸
            string                  _nameKey{};   ///< FindName — 찾는 이름(소문자 비교) · RefreshName — 키
            AccountPresenceDelegate _onFound{};   ///< Find* — 부른 쪽
            AccountID               _accountID{ kInvalidAccountID };
            uint64                  _lookupID{ 0 }; ///< Find* — 부른 쪽에 준 id
            uint16                  _kind{ 0 };     ///< Push — 알림 종류
            PendingKind             _pendingKind{ PendingKind::FindName };
        };

        struct DeferredFound
        {
            AccountPresenceResult   _result{};
            AccountPresenceDelegate _onFound{};
        };

        void          onCacheReply( const EphemeralReply& reply );
        void          submitPending( const EphemeralRequest& request, PendingOperation&& pending );
        void          writeAccountEntries( const AccountIdentity& identity, bool bRefresh );
        void          finishFindName( const PendingOperation& pending, const EphemeralReply& reply );
        void          finishFindAccount( const PendingOperation& pending, const EphemeralReply& reply );
        void          deliverDeferred();
        void          finishPush( const PendingOperation& pending, const EphemeralReply& reply );
        void          finishRefresh( const PendingOperation& pending, const EphemeralReply& reply );
        vector<uint8> makeServerBytes() const;
        vector<uint8> makeNameBytes( const AccountIdentity& identity ) const;

        unordered_map<uint64, PendingOperation>   _mapRequestToPending;
        unordered_map<AccountID, AccountIdentity> _mapAccountToIdentity; ///< 이 프로세스에 붙은 계정
        vector<DeferredFound>                     _listDeferred;         ///< 캐시 없이 끝난 찾기 — 다음 `tick` 에 알린다(맡긴 함수 안에서 부르지 않는다)
        OnlinePresenceSettings                    _settings;
        OnlineServiceHost*                        _pHost;
        uint64                                    _serverID;
        uint64                                    _nextLookupID;
        int64                                     _nextRefreshMs;
    };
} // namespace sw
