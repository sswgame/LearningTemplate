/**
 * @file ServerDirectoryClient.h
 * @brief 서버 디렉터리 클라이언트입니다. 상태, 배정, 목록 요청을 보내고 상태 알림을 받아 마지막 상태를 보관합니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 입니다.
 * @details - 요청마다 결과 델리게이트를 `tick` 스레드에서 한 번 부른다. 델리게이트는 오류 코드와 본문을 받는다. 오류 코드는 `OnlineError::kOk`, 공통 오류, 키트 오류 중 하나이고 전송 실패는 `kUnavailable` 이다.
 *          - 상태는 로그인 전에도 묻는다. 서버 고르기 화면과 점검 안내 화면이 쓴다. 배정은 로그인한 뒤에 한다.
 *
 *          언리얼 Online Services 의 비동기 호출과 완료 델리게이트에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerDirectoryTypes.h"

namespace sw
{
    /**
     * @class ServerDirectoryClient
     * @brief 연결 하나의 서버 디렉터리 클라이언트입니다. 가벼우므로 부하 테스트 봇은 봇마다 하나씩 둡니다.
     */
    class SW_KIT_API ServerDirectoryClient final : public IOnlineClientService
    {
    public:
        using StatusDelegate     = Delegate<void( uint16, const ServerDirectoryStatus& )>;
        using AssignmentDelegate = Delegate<void( uint16, const ServerAssignment& )>;
        using ServerListDelegate = Delegate<void( uint16, const vector<ServerListEntry>& )>;

        ServerDirectoryClient();

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 초기화하기 전에 부릅니다. */
        void initialize( OnlineServiceClient* pClient );

        uint64 requestStatus( const StatusDelegate& onStatus );
        /** @brief 요청에 범위 밖 값(`ServerAssignmentRequest` 의 `PROPERTY` 키)이 있으면 보내지 않고 바로 `kInvalidRequest` 로 콜백하며 0 을 반환합니다. */
        uint64 requestAssignment( const ServerAssignmentRequest& request, const AssignmentDelegate& onAssignment );
        /** @brief 종류가 상한보다 길면 보내지 않고 바로 `kInvalidRequest` 로 콜백하며 0 을 반환합니다. */
        uint64 requestServerList( string_view kind, const ServerListDelegate& onServerList );

        /** @brief 마지막으로 받은 상태입니다. 응답이나 알림으로 받은 것입니다. */
        const ServerDirectoryStatus& getLastStatus() const { return _lastStatus; }
        /** @brief 상태를 받을 때마다 올라가는 수입니다. 화면이 다시 그릴지 판단하는 데 씁니다. */
        uint64 getStatusRevision() const { return _statusRevision; }

        // IOnlineClientService
        const OnlineProtocolEntry& getProtocol() const override;
        void                       onServicePush( uint16 kind, BitReader& body ) override;

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
