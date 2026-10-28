/**
 * @file AdminCommandProvider.h
 * @brief GM 도구 패널이 명령을 보내는 창구입니다. 실제 `AdminClient` 를 감싼 제공자와, 서버 없이 도는 메모리 제공자를 같은 인터페이스로 둡니다.
 * @details 패널은 어느 쪽인지 모른다. 에디터에는 온라인 연결(`OnlineServiceClient`)이 없으므로 기본은 메모리 제공자이고,
 *          운영 도구나 게임이 연결을 만들면 `AdminToolSession::useLiveClient` 로 실제 클라이언트를 건다.
 *          메모리 제공자는 서버 저장 로직(`AdminStoreLogic`, 서버 키트)을 링크하지 않고 같은 규칙의 일부(등급 문지기, 메모와 멱등 키 필수, 회수 잔액 검사, 환불 회수의 빚,
 *          감사 기록)만 흉내 낸다. 서버 쪽 규칙의 기준은 `AdminStoreLogic` 이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Kits/Feature/Online/Admin/Runtime/AdminClient.h"

namespace sw::editor
{
    /**
     * @class IAdminCommandProvider
     * @brief GM 명령 하나를 보내고 응답을 콜백으로 돌려주는 창구입니다.
     */
    class IAdminCommandProvider
    {
    public:
        virtual ~IAdminCommandProvider() = default;

        /** @brief 패널 상단에 보이는 제공자 이름입니다. */
        virtual const utf8* getProviderName() const = 0;
        /** @brief 실제 서버로 가는 제공자이면 true 입니다. */
        virtual bool isLive() const = 0;
        /** @brief 명령 하나를 보냅니다. 응답은 @p onReply 로 정확히 한 번 옵니다(메모리 제공자는 호출 안에서 바로 부릅니다). */
        virtual void sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, AdminClient::ReplyDelegate onReply ) = 0;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class AdminClientProvider
     * @brief 실제 `AdminClient` 로 보내는 제공자입니다. 클라이언트는 빌려 쓰며, 없으면 `Unavailable` 응답을 돌려줍니다(`AdminClient` 와 같습니다).
     */
    class AdminClientProvider final : public IAdminCommandProvider
    {
    public:
        AdminClientProvider();

        /** @brief 빌려 쓸 클라이언트를 정합니다. nullptr 이면 연결 없음입니다. */
        void bind( AdminClient* pClient );

        const utf8* getProviderName() const override { return "Live (AdminClient)"; }
        bool        isLive() const override { return true; }
        void        sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, AdminClient::ReplyDelegate onReply ) override;

    private:
        AdminClient* _pClient;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 메모리 제공자가 가진 계정 하나입니다. */
    struct FakeAdminAccount
    {
        string                _displayName{};
        vector<LedgerBalance> _listBalance{};
        ServiceSanctionState  _sanction{};
        AccountID             _accountID{ kInvalidAccountID };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class FakeAdminProvider
     * @brief 서버 없이 도는 메모리 제공자입니다. 조회, 지급과 회수, 제재, 감사 열람만 처리하고 나머지 명령은 `Unavailable` 입니다.
     */
    class FakeAdminProvider final : public IAdminCommandProvider
    {
    public:
        /** @brief 처음 들어 있는 계정입니다(`kSeedPlayerID` 의 "Player One" 은 cur.gold 100). */
        static constexpr AccountID kSeedPlayerID = 0x100;
        static constexpr AccountID kSeedOtherID  = 0x101;
        /** @brief 한 번에 지급하거나 회수할 수 있는 양의 상한입니다. 넘으면 `CapExceeded` 입니다. */
        static constexpr int64 kMaxAdjustAmount = 1000000000ll;

        /** @param nowMs 처음 시각(유닉스 밀리초)입니다. 시계를 읽지 않으므로 그 뒤로는 `setNowMs` 로만 흐릅니다. */
        explicit FakeAdminProvider( int64 nowMs );

        /** @brief 명령을 보내는 GM 의 등급입니다. 기본은 `Operator` 입니다. */
        void      setActingRole( AdminRole role ) { _actingRole = role; }
        AdminRole getActingRole() const { return _actingRole; }
        /** @brief 감사 기록과 제재 시각에 쓰는 시각(유닉스 밀리초)입니다. 세션이 명령을 보내기 전에 넣고, 테스트는 고정 시각을 넣습니다. */
        void setNowMs( int64 nowMs ) { _nowMs = nowMs; }

        const FakeAdminAccount*          findAccount( AccountID accountID ) const;
        const vector<ServiceAuditEntry>& getAuditLog() const { return _listAudit; }
        /** @brief 멱등 키로 이미 처리한 응답의 수입니다. */
        int32 getRecordedKeyCount() const { return static_cast<int32>( _listRecorded.size() ); }

        const utf8* getProviderName() const override { return "Fake (in-memory)"; }
        bool        isLive() const override { return false; }
        void        sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, AdminClient::ReplyDelegate onReply ) override;

    private:
        /** @brief 처리한 상태 변경 명령의 키와 응답입니다. 같은 키로 다시 오면 이 응답을 돌려줍니다. */
        struct RecordedReply
        {
            AdminReply        _reply{};
            NetIdempotencyKey _key{};
        };

        AdminResult       execute( uint16 method, const AdminRequest& request, AdminReply& outReply );
        AdminResult       executeLookup( const AdminRequest& request, AdminReply& outReply ) const;
        AdminResult       executeAdjust( const AdminRequest& request, AdminReply& outReply );
        AdminResult       executeSanction( const AdminRequest& request, AdminReply& outReply );
        AdminResult       executeListAudit( const AdminRequest& request, AdminReply& outReply ) const;
        void              appendAudit( const utf8* pAction, const AdminRequest& request, string_view before, string_view after );
        int64             getNowMs() const { return _nowMs; }
        FakeAdminAccount* findMutableAccount( AccountID accountID );

        vector<FakeAdminAccount>  _listAccount;
        vector<ServiceAuditEntry> _listAudit;
        vector<RecordedReply>     _listRecorded;
        int64                     _nowMs;
        AdminRole                 _actingRole;
    };
} // namespace sw::editor
