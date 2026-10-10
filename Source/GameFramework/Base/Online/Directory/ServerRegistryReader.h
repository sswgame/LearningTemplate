/**
 * @file ServerRegistryReader.h
 * @brief 종류 하나의 서버 목록을 캐시에서 주기적으로 읽어 스냅숏으로 듭니다 — 색인을 읽고, 기록을 하나씩 읽고, 다 오면 갈아 끼웁니다. 기록이 없는(죽은) 멤버는 색인에서 지웁니다.
 * @details - 서비스 스레드 하나에서 쓴다(라우터와 같은 스레드). 스냅숏은 읽기가 끝날 때까지 옛것이다 — 고르기는 옛 스냅숏 + 이 프로세스가 고른 몫(`pickServer` 가 얹는다)으로 한다
 *            (다음 읽기까지 같은 서버에 몰리지 않게).
 *          - 라우터보다 먼저 내려갈 수 있다 — `shutdown` 이 기다리던 캐시 요청을 취소한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Directory/ServerSelection.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;

    /**
     * @class ServerRegistryReader
     * @brief 종류 하나의 서버 목록 읽기 캐시입니다.
     */
    class SW_GF_API ServerRegistryReader
    {
    public:
        static constexpr int32 kMaxServerCount = 1024;

        ServerRegistryReader();
        ~ServerRegistryReader();

        ServerRegistryReader( const ServerRegistryReader& )            = delete;
        ServerRegistryReader& operator=( const ServerRegistryReader& ) = delete;

        /** @brief @p pRouter 는 빌려 쓴다. 첫 읽기는 다음 `tick` 에. */
        void initialize( EphemeralStoreRouter* pRouter, string_view kind, int64 refreshPeriodMs );
        /** @brief 기다리던 캐시 요청을 취소하고 스냅숏을 비웁니다. 두 번 불러도 됩니다. */
        void shutdown();

        /** @brief 주기가 됐거나(또는 `requestRefresh`) 읽는 중이 아니면 새로 읽기 시작합니다. */
        void tick( int64 nowMs );
        /** @brief 다음 `tick` 에 주기와 상관없이 읽습니다(읽는 중이면 끝난 뒤의 `tick` 에). */
        void requestRefresh();

        /** @brief 고릅니다 — 고르면 그 서버에 @p query `_seatCount` 를 얹습니다(다음 스냅숏까지). */
        [[nodiscard]] bool pickServer( const ServerSelectionQuery& query, int64 nowMs, ServerStatus& outStatus );

        const vector<ServerStatus>& getSnapshot() const { return _listSnapshot; }
        const string&               getKind() const { return _kind; }
        uint64                      getRefreshCount() const { return _refreshCount; }
        bool                        isRefreshing() const { return _outstandingCount > 0; }

    private:
        void startRefresh( int64 nowMs );
        void onIndexReply( const EphemeralReply& reply );
        void onRecordReply( const EphemeralReply& reply );
        void finishRefresh();

        vector<ServerStatus>          _listSnapshot;
        vector<ServerStatus>          _listBuilding;
        unordered_map<uint64, uint64> _mapRequestToServer; ///< 기록 읽기 요청 id → 서버 id
        string                        _kind;
        EphemeralStoreRouter*         _pRouter;
        uint64                        _indexRequestID; ///< 기다리는 색인 읽기(0 = 없음)
        int64                         _refreshPeriodMs;
        int64                         _lastRefreshStartMs;
        uint64                        _refreshCount;
        int32                         _outstandingCount;
        uint8                         _bRefreshRequested;
    };
} // namespace sw
