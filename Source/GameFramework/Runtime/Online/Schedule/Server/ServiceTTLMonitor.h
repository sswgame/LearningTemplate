/**
 * @file ServiceTTLMonitor.h
 * @brief 보존 기간이 지난 서비스 레코드를 지우는 예약 작업입니다.
 * @details 규칙(`ServiceTTLRule`)을 받아 예약 작업 `ttl.sweep` 을 매일 한 번 등록하고, 회차를 차지한 서버 하나가 저장소 스레드에서 규칙마다 쓴다.
 *          - 한 회차에 규칙마다 훑는 수와 지우는 수에 상한이 있다(규칙의 `_maxScanPerSweep` · `_maxDeletePerSweep`, 값은 테이블 주인의 Config JSON).
 *            테이블이 크면 여러 회차에 걸쳐 이어서 훑는다. 상한이 규칙마다인 것은 채팅 기록과 닫힌 신고처럼 표 크기가 몇 자리씩 달라서다(MongoDB TTL monitor 는 한 번에
 *            지우는 수를 서버 매개변수로, DynamoDB 는 표마다 배경 처리량으로 정한다).
 *          - 회차가 끝나기 전에 서버가 종료하면 차지 레코드가 임대 시간 뒤에 풀려 다른 서버가 이어받는다. 지우기는 멱등이라 다시 돌아도 안전하다.
 *          - 지운 수는 INFO 로그로 남기고, 저장소 오류는 ERROR 로그를 남긴 뒤 회차를 실패로 끝낸다.
 *          종료하는 쪽(`shutdown`)은 맡긴 일이 끝날 때까지 저장소 완료를 회수한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServicePeriodicJob.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServiceScheduler.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStoreTracker.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"

namespace sw
{
    class IServiceStore;

    /**
     * @class ServiceTTLMonitor
     * @brief 보존 기간 쓸기를 예약 작업으로 돌립니다. 서비스 스레드 하나가 쓴다.
     */
    class SW_GF_API ServiceTTLMonitor final
    {
    public:
        static constexpr const utf8* kJobID = "ttl.sweep"; ///< 예약 회차 기록의 키다. 이름을 바꾸면 옛 회차 기록은 남은 채 새 회차부터 다시 센다(쓸기는 멱등이다)

        ServiceTTLMonitor();
        ~ServiceTTLMonitor();

        /** @brief 작업을 @p minuteOfDay(UTC 분)에 등록합니다. 규칙이 없으면 등록하지 않고 true 입니다. 예약 쓸기에 올릴 수 없는 규칙(`isSchedulable`)이 있거나 일정이 틀리면 false 입니다. */
        [[nodiscard]] bool initialize( IServiceStore* pStore, ServiceScheduler* pScheduler, vector<ServiceTTLRule> listRule, int32 minuteOfDay );
        /** @brief 맡긴 일이 끝나기를 기다린 뒤 비웁니다. */
        void shutdown();

        void onScheduledRun( const ScheduledRun& run );

        /** @brief 마지막으로 끝난 회차가 지운 레코드 수입니다(테스트 · 진단). */
        int64 getLastDeletedCount() const { return _lastDeletedCount; }
        bool  isRunning() const { return _sweepJob.isRunning(); }

    private:
        void onSweepCompleted( vector<ServiceTTLRule> listRule, ServiceStoreResult result, int64 deletedCount );

    private:
        vector<ServiceTTLRule> _listRule;
        ServiceScheduler*      _pScheduler;
        int64                  _lastDeletedCount;
        ServicePeriodicJob     _sweepJob; ///< 주기 없이 예약 회차로만 돈다
        ServiceStoreTracker    _store;    ///< 저장소에 맡긴 일의 앞이다
    };
} // namespace sw
