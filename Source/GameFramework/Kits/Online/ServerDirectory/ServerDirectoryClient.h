/**
 * @file ServerDirectoryClient.h
 * @brief 서버 디렉터리 클라이언트 — 상태 · 배정 · 목록 요청을 보내고, 상태 알림을 받아 마지막 상태를 듭니다. `OnlineServiceClient` 에 올리는 `IOnlineClientService`.
 * @details - 요청마다 결과 델리게이트(오류 코드 — `OnlineError::kOk` · 공통 · 키트 코드, 전송 실패는 `kUnavailable` — 와 몸)를 한 번 부른다(`tick` 스레드).
 *          - 상태는 로그인 전에도 묻는다(서버 고르기 · 점검 안내 화면). 배정은 로그인 뒤.
 *          언리얼 Online Services 의 비동기 호출 + 완료 델리게이트와 같은 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/ServerDirectory/ServerDirectoryTypes.h"

namespace sw
{
    /**
     * @class ServerDirectoryClient
     * @brief 연결 하나의 서버 디렉터리 창구입니다. 부하 시험 봇은 봇마다 하나 둔다(가볍다).
     */
    class SW_GF_API ServerDirectoryClient final : public IOnlineClientService
    {
    public:
        using StatusDelegate     = Delegate<void( uint16, const ServerDirectoryStatus& )>;
        using AssignmentDelegate = Delegate<void( uint16, const ServerAssignment& )>;
        using ServerListDelegate = Delegate<void( uint16, const vector<ServerListEntry>& )>;

        ServerDirectoryClient();

        /** @brief @p pClient 는 빌려 쓴다 — `registerClientService( this )` 는 부르는 쪽이(초기화 전에). */
        void initialize( OnlineServiceClient* pClient );

        uint64 requestStatus( const StatusDelegate& onStatus );
        uint64 requestAssignment( const ServerAssignmentRequest& request, const AssignmentDelegate& onAssignment );
        uint64 requestServerList( string_view kind, const ServerListDelegate& onServerList );

        /** @brief 마지막으로 받은 상태(응답 · 알림)입니다. */
        const ServerDirectoryStatus& getLastStatus() const { return _lastStatus; }
        /** @brief 상태를 받을 때마다 오르는 수입니다(화면이 다시 그릴지 본다). */
        uint64 getStatusRevision() const { return _statusRevision; }

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kServerDirectory; }
        uint32 getProtocolVersion() const override;
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        void onStatusResponse( const OnlineResponse& response );
        void onAssignmentResponse( const OnlineResponse& response );
        void onServerListResponse( const OnlineResponse& response );

        ServiceClientCallTable<StatusDelegate>     _statusCallTable;
        ServiceClientCallTable<AssignmentDelegate> _assignmentCallTable;
        ServiceClientCallTable<ServerListDelegate> _serverListCallTable;
        ServerDirectoryStatus                      _lastStatus;
        OnlineServiceClient*                       _pClient;
        uint64                                     _statusRevision;
    };
} // namespace sw
