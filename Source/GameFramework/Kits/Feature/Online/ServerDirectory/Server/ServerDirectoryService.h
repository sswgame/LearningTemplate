/**
 * @file ServerDirectoryService.h
 * @brief 서버 디렉터리 로직 — 배정 · 상태 · 목록은 메모리에서 바로 답하고, 점검 · 공지 바꾸기는 영속 레코드 + 감사 줄을 한 트랜잭션으로 씁니다. 전송을 모른다(서비스 스레드 하나).
 * @details - 점검 · 공지는 영속(`sd_maintenance` · `sd_notice`)이다. 주기(기본 30 초)로 다시 읽고, 다른 서버가 바꿨다는 버스 알림(`notifyChanged` — 바인딩이 부른다)이
 *            오면 바로 읽는다(버스는 최대 한 번이라 주기 읽기가 놓친 것을 메운다). 바꾼 서버는 쓰기가 끝나면 버스 `sd.changed` 를 낸다.
 *          - 보이는 내용(기간 안의 점검 · 공지)의 해시가 바뀌면(다시 읽기 · 기간 경계 — 시작 · 끝은 다시 읽지 않아도 보이는 것이 바뀐다) `takeStatusChange` 가 참이다 —
 *            바인딩이 이 서버의 모두에게 알린다.
 *          - 배정은 서버 종류마다 기반 `ServerRegistryReader` 하나(설정 `_listServerKind`)의 스냅숏으로 고른다(기반 `ServerSelection` 규칙). 점검이 걸린 종류는 허용 계정만,
 *            그들은 Maintenance 상태 서버도 후보다.
 *          - 바꾸기 API 는 GM 도구(GF_Admin)가 게임 조립에서 부른다 — 네트워크 메서드로 열지 않는다. 완료는 `drainCompletions` 로 꼬리표와 함께.
 *          PlayFab Title News(공지) · 타이틀 점검, 넥슨 · 엔씨 류 MMO 의 "서버 선택 + 점검 화이트리스트" 와 같은 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
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
        vector<string> _listServerKind{};                ///< 배정 · 목록을 받는 서버 종류("game" …)
        int64          _reloadPeriodMs{ 30000 };         ///< 점검 · 공지 다시 읽기 주기
        int64          _registryRefreshPeriodMs{ 2000 }; ///< 서버 목록 다시 읽기 주기
        int64          _staleMs{ 15000 };                ///< 하트비트가 이보다 오래된 서버는 고르지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 빌려 쓰는 것들입니다(로직보다 오래 산다). */
    struct ServerDirectoryDependencies
    {
        IServiceStore*        _pStore{ nullptr };  ///< 필수
        EphemeralStoreRouter* _pRouter{ nullptr }; ///< 필수(서버 목록)
        IServerBus*           _pBus{ nullptr };    ///< 서버 한 대면 null — 바꾼 뒤 알리기만(받기는 바인딩이 구독)
    };
} // namespace sw

namespace sw
{
    /** @brief 바꾸기 요청의 완료입니다. */
    struct ServerDirectoryCompletion
    {
        uint64                _requestTag{ 0 };
        ServerDirectoryResult _result{ ServerDirectoryResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 간 버스 주제입니다(내보내는 클래스 밖 — 다른 모듈이 값으로 읽는다). */
    struct ServerDirectoryBus
    {
        static constexpr const utf8* kChangedTopic = "sd.changed"; ///< 점검 · 공지를 바꿨다 — 몸 없음, 받는 서버는 다시 읽는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServerDirectoryService
     * @brief 서버 디렉터리 로직입니다.
     */
    class SW_GF_API ServerDirectoryService
    {
    public:
        ServerDirectoryService();
        ~ServerDirectoryService();

        ServerDirectoryService( const ServerDirectoryService& )            = delete;
        ServerDirectoryService& operator=( const ServerDirectoryService& ) = delete;

        void initialize( const ServerDirectoryDependencies& dependencies, const ServerDirectorySettings& settings );
        /** @brief 서버 목록 읽기를 멈춥니다(기다리던 캐시 요청 취소). 맡긴 저장소 일은 저장소의 `shutdown` · `pollCompletions` 가 거둔다 — 이 객체는 그때까지 살아 있어야 한다. */
        void shutdown();

        /** @brief 목록 읽기 · 주기 다시 읽기 · 기간 경계 판정. 라우터 · 저장소 완료는 호스트(또는 서버 루프)가 비운다. */
        void tick( int64 nowMs );
        /** @brief 다른 서버가 점검 · 공지를 바꿨다(버스 `ServerDirectoryBus::kChangedTopic`) — 다음 tick 에 다시 읽는다. */
        void notifyChanged();
        /** @brief 보이는 상태가 지난번 이후 바뀌었는가입니다(읽으면 지운다). */
        [[nodiscard]] bool takeStatusChange();

        // 클라이언트 요청(동기 — 메모리)
        ServerDirectoryStatus makeStatus( int64 nowMs ) const;
        ServerAssignment      assignServer( AccountID accountID, const ServerAssignmentRequest& request, int64 nowMs );
        ServerDirectoryResult listServers( string_view kind, int64 nowMs, vector<ServerListEntry>& outListEntry ) const;
        /** @brief 지금 @p kind 에 걸린 점검이 @p accountID 를 막는가입니다(계정 · 게임 서버가 조립에서 물을 수 있다). 막으면 그 창을 @p pOutWindow 에. */
        bool isBlockedByMaintenance( string_view kind, AccountID accountID, int64 nowMs, MaintenanceWindow* pOutWindow = nullptr ) const;

        // 바꾸기(비동기 — 완료는 drainCompletions). @p actorID 는 감사 줄의 GM 계정(0 = system).
        void setMaintenance( const MaintenanceWindow& window, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag );
        void clearMaintenance( string_view scope, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag );
        void postNotice( const ServiceNotice& notice, AccountID actorID, int64 nowMs, uint64 requestTag );
        void removeNotice( uint64 noticeID, AccountID actorID, int64 nowMs, uint64 requestTag );

        void   drainCompletions( vector<ServerDirectoryCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        uint64 getStatusRevision() const { return _statusRevision; }
        int32  getPendingCount() const { return _pendingCount; }

        /** @brief 저장소 일의 `complete` 가 부른다(키트 안). */
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
        vector<ServiceNotice>                    _listNotice; ///< 우선순위 내림차순
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
