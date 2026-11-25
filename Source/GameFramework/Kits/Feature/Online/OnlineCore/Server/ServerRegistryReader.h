/**
 * @file ServerRegistryReader.h
 * @brief 종류 하나의 서버 목록을 캐시에서 주기적으로 읽어 스냅숏으로 보관합니다. 색인을 읽고, 기록을 하나씩 읽고, 다 오면 교체합니다.
 *        기록이 없는(죽은) 멤버는 색인에서 지웁니다.
 * @details - 서비스 스레드 하나에서 쓴다(라우터와 같은 스레드). 스냅숏은 읽기가 끝날 때까지 옛것이다. 고르기는 옛 스냅숏에 이 프로세스가 고른 인원(`pickServer` 가 더한다)을 더해 한다
 *            (다음 읽기까지 같은 서버에 몰리지 않게).
 *          - 좌석 예약은 서버 사이에 나눈다: 고르면 휘발 저장소의 예약 셈(`ServerRecord::makeSeatKey`, 창 TTL)에 더하고, 읽기는 기록의 부하에 그 셈을 더한다 —
 *            배정하는 서버가 여럿이면 각자 자기 몫만 보고 같은 서버를 넘치게 채우던 구멍(감사 O-18). 게임 서버가 확정(`ServerRegistration::confirmSeats`)하면 빠지고, 확정이 없으면 창이 끝나 사라진다.
 *          - 라우터보다 먼저 종료할 수 있다. `shutdown` 이 기다리던 캐시 요청을 취소한다.
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"

// 접속 분배입니다. 서버 목록에서 손님 하나(또는 파티나 경기 인원)를 받을 서버를 고르는 순수 함수입니다. 클라이언트 배정(서버 디렉터리)과 매칭의 전용 서버 배정이 같이 쓴다.
// 후보 조건은 Open(점검 허용이면 Maintenance 도), 같은 종류, 빌드 버전 일치(질의가 0 이면 무관), `_staleMs` 안의 하트비트, 부하 + 좌석 ≤ 정원이다.
// 같은 지역 후보가 있으면 그중에서, 없고 `_bAllowOtherRegion` 이면 전체에서. 찬 비율(부하 × 1024 / 정원)이 낮은 것, 같으면 서버 id 가 작은 것(결정적).
namespace sw
{
    /** @brief 고르기 질의입니다. */
    struct SW_KIT_SERVER_API ServerSelectionQuery
    {
        string _kind{};
        string _region{};
        int64  _staleMs{ 15000 }; ///< 하트비트가 이보다 오래되면 죽은 것으로 본다
        uint32 _buildVersion{ 0 };
        int32  _seatCount{ 1 }; ///< 한 번에 들어갈 좌석 수(파티나 경기 인원)
        uint8  _bAllowOtherRegion{ SW_TRUE };
        uint8  _bIncludeMaintenance{ SW_FALSE }; ///< 점검 중 허용 계정이다. Maintenance 상태 서버도 후보로 본다

        // 고르기입니다.
        /** @brief 고른 서버의 번호(@p listStatus 안)입니다. 후보가 없으면 false 이고 @p outIndex 는 -1. */
        [[nodiscard]] static bool pickServer( const vector<ServerStatus>& listStatus, const ServerSelectionQuery& query, int64 nowMs, int32& outIndex );
        /** @brief @p status 가 질의의 후보인가입니다(지역은 보지 않는다). */
        static bool isCandidate( const ServerStatus& status, const ServerSelectionQuery& query, int64 nowMs );
    };
} // namespace sw

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;

    /**
     * @class ServerRegistryReader
     * @brief 종류 하나의 서버 목록 읽기 캐시입니다.
     */
    class SW_KIT_SERVER_API ServerRegistryReader
    {
    public:
        static constexpr int32 kMaxServerCount = 1024;

        ServerRegistryReader();
        ~ServerRegistryReader();

        ServerRegistryReader( const ServerRegistryReader& )            = delete;
        ServerRegistryReader& operator=( const ServerRegistryReader& ) = delete;

        /** @brief @p pRouter 는 빌려 쓴다. 첫 읽기는 다음 `tick` 에. @p seatReservationTTLMs 는 좌석 예약 창이다(서버 설정 `_seatReservationSeconds`, 필수 — 1 이상). */
        void initialize( EphemeralStoreRouter* pRouter, string_view kind, int64 refreshPeriodMs, int64 seatReservationTTLMs );
        /** @brief 기다리던 캐시 요청을 취소하고 스냅숏을 비웁니다. 두 번 불러도 됩니다. */
        void shutdown();

        /** @brief 주기가 됐거나(또는 `requestRefresh`) 읽는 중이 아니면 새로 읽기 시작합니다. */
        void update( UnixMilliseconds nowMs );
        /** @brief 다음 `tick` 에 주기와 상관없이 읽습니다(읽는 중이면 끝난 뒤의 `tick` 에). */
        void requestRefresh();

        /** @brief 서버를 고릅니다. 고르면 그 서버에 @p query 의 `_seatCount` 를 더합니다(다음 스냅숏까지). */
        [[nodiscard]] bool pickServer( const ServerSelectionQuery& query, UnixMilliseconds nowMs, ServerStatus& outStatus );

        const vector<ServerStatus>& getSnapshot() const { return _listSnapshot; }
        const string&               getKind() const { return _kind; }
        const DevCounter&           getRefreshCount() const { return _refreshCount; }
        bool                        isRefreshing() const { return _outstandingCount > 0; }

    private:
        void startRefresh( UnixMilliseconds nowMs );
        void onIndexReply( const EphemeralReply& reply );
        void onRecordReply( const EphemeralReply& reply );
        void onSeatReply( const EphemeralReply& reply );
        void finishRefresh();

    private:
        vector<ServerStatus>          _listSnapshot;
        vector<ServerStatus>          _listBuilding;
        unordered_map<uint64, uint64> _mapRequestToServer;  ///< 기록 읽기 요청 id → 서버 id
        unordered_map<uint64, uint64> _mapRequestToSeat;    ///< 예약 셈 읽기 요청 id → 서버 id
        unordered_map<uint64, int32>  _mapServerToReserved; ///< 이번 읽기에서 본 서버마다의 예약 좌석 수
        string                        _kind;
        EphemeralStoreRouter*         _pRouter;
        Subscription                  _replyOwner;     ///< 라우터 답의 주인 몫(`openReplyOwner`). 놓으면 기다리던 답이 지워진다
        uint64                        _indexRequestID; ///< 기다리는 색인 읽기(0 = 없음)
        int64                         _refreshPeriodMs;
        int64                         _seatReservationTTLMs;
        UnixMilliseconds              _lastRefreshStartMs;
        DevCounter                    _refreshCount;
        int32                         _outstandingCount;
        uint8                         _bRefreshRequested;
    };
} // namespace sw
