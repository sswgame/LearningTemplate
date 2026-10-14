/**
 * @file ServerDirectoryService.h
 * @brief 서버 디렉터리 로직입니다. 배정, 상태, 목록은 메모리에서 바로 응답하고, 점검과 공지의 변경은 영속 레코드와 감사 기록을 한 트랜잭션으로 씁니다. 전송은 모르며 서비스 스레드 하나에서 씁니다.
 * @details - 점검과 공지는 영속 테이블(`sd_maintenance`, `sd_notice`)에 있다. 주기(기본 30초)마다 다시 읽고, 다른 서버가 바꿨다는 버스 알림(`notifyChanged`, 바인딩이 부른다)이 오면 바로 읽는다.
 *            버스는 최대 한 번만 배달하므로 주기적인 읽기가 놓친 변경을 메운다. 바꾼 서버는 쓰기가 끝나면 버스 `sd.changed` 를 낸다.
 *          - 보이는 내용(기간 안의 점검과 공지)의 해시가 바뀌면 `takeStatusChange` 가 true 이다. 다시 읽기나 기간 경계에서 바뀌며, 시작과 끝은 다시 읽지 않아도 보이는 내용이 바뀐다. 바인딩이 이 서버의 모든 클라이언트에 알린다.
 *          - 배정은 서버 종류마다 기반 계층의 `ServerRegistryReader` 하나(설정 `_listServerKind`)의 스냅샷으로 고른다. 기반 계층의 `ServerSelection` 규칙을 따른다.
 *            점검이 걸린 종류는 허용 계정만 배정받으며, 그들은 Maintenance 상태의 서버도 후보가 된다.
 *          - 변경 API 는 GM 도구(GF_Admin)가 게임 조립 단계에서 부른다. 네트워크 메서드로는 열지 않는다. 완료는 요청 태그와 함께 `drainCompletions` 로 받는다.
 *
 *          PlayFab Title News(공지)와 타이틀 점검, 국내 MMO 의 "서버 선택과 점검 화이트리스트" 와 같은 구조이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Directory/ServerRegistryReader.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerDirectoryTypes.h"

namespace sw
{
    class EphemeralStoreRouter;
    class IServerBus;
    class IServiceStore;
    class ServerDirectoryWriteWork;

    /** @brief 서버 디렉터리 설정입니다. */
    struct ServerDirectorySettings
    {
        vector<string> _listServerKind{};                ///< 배정과 목록을 받는 서버 종류이다(예: "game")
        int64          _reloadPeriodMs{ 30000 };         ///< 점검과 공지를 다시 읽는 주기이다
        int64          _registryRefreshPeriodMs{ 2000 }; ///< 서버 목록을 다시 읽는 주기이다
        int64          _staleMs{ 15000 };                ///< 하트비트가 이보다 오래된 서버는 고르지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 빌려 쓰는 객체들입니다. 로직보다 오래 살아야 합니다. */
    struct ServerDirectoryDependencies
    {
        IServiceStore*        _pStore{ nullptr };  ///< 필수
        EphemeralStoreRouter* _pRouter{ nullptr }; ///< 필수이다. 서버 목록에 쓴다
        IServerBus*           _pBus{ nullptr };    ///< 서버가 한 대이면 null 이다. 바꾼 뒤 알리기만 하며 받기는 바인딩이 구독한다
    };
} // namespace sw

namespace sw
{
    /** @brief 변경 요청의 완료입니다. */
    struct ServerDirectoryCompletion
    {
        uint64                _requestTag{ 0 };
        ServerDirectoryResult _result{ ServerDirectoryResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 간 버스 주제입니다. 다른 모듈이 값으로 읽으므로 내보내는 클래스 밖에 둡니다. */
    struct ServerDirectoryBus
    {
        static constexpr const utf8* kChangedTopic = "sd.changed"; ///< 점검이나 공지를 바꿨다는 알림이다. 본문은 없고 받는 서버가 다시 읽는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServerDirectoryService
     * @brief 서버 디렉터리 로직 서비스입니다.
     */
    class SW_KIT_API ServerDirectoryService
    {
    public:
        ServerDirectoryService();
        ~ServerDirectoryService();

        ServerDirectoryService( const ServerDirectoryService& )            = delete;
        ServerDirectoryService& operator=( const ServerDirectoryService& ) = delete;

        void initialize( const ServerDirectoryDependencies& dependencies, const ServerDirectorySettings& settings );
        /** @brief 서버 목록 읽기를 멈추고 기다리던 캐시 요청을 취소합니다. 맡긴 저장소 작업은 저장소의 `shutdown` 과 `pollCompletions` 가 회수하므로 이 객체는 그때까지 살아 있어야 합니다. */
        void shutdown();

        /** @brief 목록 읽기, 주기적인 다시 읽기, 기간 경계 판정을 합니다. 라우터와 저장소의 완료는 호스트(또는 서버 루프)가 비웁니다. */
        void tick( int64 nowMs );
        /** @brief 다른 서버가 점검이나 공지를 바꿨음을 버스(`ServerDirectoryBus::kChangedTopic`)로 받았을 때 부릅니다. 다음 tick 에 다시 읽습니다. */
        void notifyChanged();
        /** @brief 보이는 상태가 지난번 이후 바뀌었으면 true 입니다. 읽으면 표시를 지웁니다. */
        [[nodiscard]] bool takeStatusChange();

        // 클라이언트 요청 함수들이며 동기이고 메모리에서 처리한다
        ServerDirectoryStatus makeStatus( int64 nowMs ) const;
        ServerAssignment      assignServer( AccountID accountID, const ServerAssignmentRequest& request, int64 nowMs );
        ServerDirectoryResult listServers( string_view kind, int64 nowMs, vector<ServerListEntry>& outListEntry ) const;
        /** @brief 지금 @p kind 에 걸린 점검이 @p accountID 를 막으면 true 입니다. 계정 서버와 게임 서버가 조립 단계에서 물을 수 있습니다. 막으면 그 점검 창을 @p pOutWindow 에 채웁니다. */
        bool isBlockedByMaintenance( string_view kind, AccountID accountID, int64 nowMs, MaintenanceWindow* pOutWindow = nullptr ) const;

        // 변경 함수들이며 비동기이다. 완료는 drainCompletions 로 받는다. @p actorID 는 감사 기록의 GM 계정이며 0이면 system 이다.
        void setMaintenance( const MaintenanceWindow& window, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag );
        void clearMaintenance( string_view scope, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag );
        void postNotice( const ServiceNotice& notice, AccountID actorID, int64 nowMs, uint64 requestTag );
        void removeNotice( uint64 noticeID, AccountID actorID, int64 nowMs, uint64 requestTag );

        void   drainCompletions( vector<ServerDirectoryCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        uint64 getStatusRevision() const { return _statusRevision; }
        int32  getPendingCount() const { return _pendingCount; }

        /** @brief 저장소 작업의 `complete` 가 호출합니다. 키트 안에서만 씁니다. */
        void applyReload( vector<MaintenanceWindow>&& listMaintenance, vector<ServiceNotice>&& listNotice, bool bReadOk, int64 nowMs );
        void applyWrite( uint64 requestTag, ServerDirectoryResult result );

    private:
        void                  startReload( int64 nowMs );
        void                  markStatusIfChanged( int64 nowMs );
        uint64                computeVisibleHash( int64 nowMs ) const;
        ServerRegistryReader* findReader( string_view kind ) const;
        void                  submitWrite( unique_ptr<ServerDirectoryWriteWork> work );

        vector<unique_ptr<ServerRegistryReader>> _listReader;
        vector<MaintenanceWindow>                _listMaintenance;
        vector<ServiceNotice>                    _listNotice; ///< 우선순위 내림차순이다
        EventBuffer<ServerDirectoryCompletion>   _completionBuffer;
        ServerDirectorySettings                  _settings;
        ServerDirectoryDependencies              _dependencies;
        int64                                    _lastReloadMs;
        uint64                                   _lastVisibleHash;
        uint64                                   _statusRevision;
        int32                                    _pendingCount;
        uint8                                    _bReloading;
        uint8                                    _bReloadRequested;
        uint8                                    _bStatusChanged;
    };
} // namespace sw
