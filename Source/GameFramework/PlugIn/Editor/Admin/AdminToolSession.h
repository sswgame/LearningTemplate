/**
 * @file AdminToolSession.h
 * @brief GM 도구 패널의 상태입니다. 제공자에게 명령을 보내고 응답(조회 결과, 감사 줄, 마지막 결과)을 보관합니다. ImGui 를 모르므로 화면 없이 테스트합니다.
 * @details 상태를 바꾸는 명령은 멱등 키를 세션이 만들어 보내고(같은 키로 다시 보내면 서버가 처음 응답을 돌려준다), 성공하면 같은 계정을 다시 조회해 화면의 잔액과 제재를 새로 고친다.
 *          기본 제공자는 서버 없이 도는 메모리 제공자(`FakeAdminProvider`)이고, `useLiveClient` 로 실제 `AdminClient` 를 건다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorSubsystem.h"

#include "GameFramework/PlugIn/Editor/Admin/AdminCommandProvider.h"

namespace sw::editor
{
    /**
     * @class AdminToolSession
     * @brief GM 도구 패널 하나가 쓰는 명령 보내기와 결과 보관입니다.
     * @details 이 확장이 기여하는 에디터 서브시스템이다(`SW_EDITOR_SUBSYSTEM`) — 에디터 범위가 소유하고 패널은 `editor::findSubsystem<AdminToolSession>()` 로 찾는다.
     */
    class AdminToolSession final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "AdminToolSession";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief 감사 열람이 한 번에 가져오는 줄 수입니다. */
        static constexpr int32 kAuditRequestCount = 20;

        AdminToolSession();
        AdminToolSession( const AdminToolSession& )            = delete; // 활성 제공자가 자기 멤버를 가리킨다
        AdminToolSession& operator=( const AdminToolSession& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) 제공자
        // ------------------------------------------------------------------------------
        /** @brief 메모리 제공자로 되돌립니다. */
        void useFake();
        /** @brief 실제 클라이언트로 보냅니다. @p pClient 는 빌려 쓰며 nullptr 이면 연결 없음(`Unavailable`)입니다. */
        void                   useLiveClient( AdminClient* pClient );
        IAdminCommandProvider& getProvider() { return *_pActive; }
        FakeAdminProvider&     getFakeProvider() { return _fake; }

        // ------------------------------------------------------------------------------
        // 2) 명령. 응답은 제공자가 정한 때에 결과 칸에 들어온다
        // ------------------------------------------------------------------------------
        /** @brief 계정 id 로 조회합니다. 성공하면 `getLookup` 이 채워집니다. */
        void requestLookup( AccountID accountID );
        /** @brief 지급(양수)이나 회수(음수)를 보냅니다. @p bRefund 는 환불 회수이며 잔액이 모자라면 빚으로 남깁니다. */
        void requestAdjust( AccountID accountID, string_view assetID, int64 amount, bool bRefund, string_view memo );
        /** @brief 제재를 겁니다. @p durationSeconds 가 0 이면 해제입니다. 영구 정지(`Ban`)는 길이를 보지 않고 영구입니다. */
        void requestSanction( AccountID accountID, ServiceSanctionKind kind, int64 durationSeconds, string_view reasonCode, string_view memo );
        /** @brief 감사 기록을 열람합니다. @p subjectPrefix 가 비면 전부입니다. */
        void requestAudit( string_view subjectPrefix );
        /** @brief 열린 신고(@p bOpen) 또는 처리한 신고를 최근 것부터 읽습니다. 성공하면 `getReports` 가 채워집니다. */
        void requestReports( bool bOpen );
        /** @brief 열린 신고를 처리합니다(Resolved 나 Dismissed). 메모가 처리 메모입니다. 성공하면 열린 신고를 다시 읽습니다. */
        void requestResolveReport( string_view reportKey, ServiceReportStatus status, string_view memo );
        /** @brief 점검 창을 겁니다. @p durationSeconds 가 0 이면 해제할 때까지입니다. */
        void requestSetMaintenance( string_view scope, string_view messageKey, int64 durationSeconds, string_view memo );
        void requestClearMaintenance( string_view scope, string_view memo );
        /** @brief 공지를 올립니다(GM 이 쓴 문구). @p durationSeconds 가 0 이면 내릴 때까지입니다. */
        void requestPostNotice( uint64 noticeID, string_view text, int32 priority, int64 durationSeconds, string_view memo );
        void requestRemoveNotice( uint64 noticeID, string_view memo );
        /** @brief 라이브 이벤트 정의(JSON)를 읽습니다. 성공하면 `getLiveEvents` 가 채워집니다. */
        void requestLiveEvents();
        /** @brief 라이브 이벤트 정의(JSON) 하나를 올리거나 덮어씁니다. 성공하면 목록을 다시 읽습니다. */
        void requestPutLiveEvent( string_view definitionJSON, string_view memo );
        void requestRemoveLiveEvent( string_view eventID, string_view memo );

        // ------------------------------------------------------------------------------
        // 3) 결과
        // ------------------------------------------------------------------------------
        bool                              hasLookup() const { return _bHasLookup; }
        AccountID                         getLookupAccountID() const { return _lookupAccountID; }
        const AdminReply&                 getLookup() const { return _lookup; }
        const vector<ServiceAuditEntry>&  getAuditRows() const { return _lookupAudit._listAudit; }
        const vector<ServiceReport>&      getReports() const { return _reports._listReport; }
        const vector<AdminLiveEventView>& getLiveEvents() const { return _liveEvents._listLiveEvent; }
        AdminResult                       getLastResult() const { return _lastResult; }
        uint16                            getLastMethod() const { return _lastMethod; }
        bool                              wasLastReplayed() const { return _bLastReplayed; }
        const string&                     getStatusLine() const { return _statusLine; }
        /** @brief 응답을 받은 명령의 수입니다. */
        int32 getCompletedCount() const { return _completedCount; }
        /** @brief 보냈는데 아직 응답이 없는 명령의 수입니다. */
        int32 getPendingCount() const { return _pendingCount; }

    private:
        void send( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key );
        void onReply( const AdminClient::Reply& reply );

        FakeAdminProvider      _fake;
        AdminClientProvider    _live;
        IAdminCommandProvider* _pActive;
        AdminReply             _lookup;
        AdminReply             _lookupAudit;
        AdminReply             _reports;
        AdminReply             _liveEvents;
        string                 _statusLine;
        int32                  _completedCount;
        int32                  _pendingCount;
        AccountID              _lookupAccountID;
        AdminResult            _lastResult;
        uint16                 _lastMethod;
        bool                   _bHasLookup;
        bool                   _bLastReplayed;
    };
} // namespace sw::editor
