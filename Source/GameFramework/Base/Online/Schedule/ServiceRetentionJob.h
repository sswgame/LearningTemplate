/**
 * @file ServiceRetentionJob.h
 * @brief 보존 기간이 지난 서비스 레코드를 지우는 예약 작업입니다.
 * @details 규칙(`ServiceRetentionRule`)을 받아 예약 작업 `retention.sweep` 을 매일 한 번 등록하고, 회차를 차지한 서버 하나가 저장소 스레드에서 규칙마다 쓴다.
 *          - 한 회차에 규칙마다 훑는 수와 지우는 수에 상한이 있다. 테이블이 크면 여러 회차에 걸쳐 커서를 따라 훑는다.
 *          - 회차가 끝나기 전에 서버가 종료하면 차지 레코드가 임대 시간 뒤에 풀려 다른 서버가 이어받는다. 지우기는 멱등이라 다시 돌아도 안전하다.
 *          - 지운 수는 INFO 로그로 남기고, 저장소 오류는 ERROR 로그를 남긴 뒤 회차를 실패로 끝낸다.
 *          종료하는 쪽(`shutdown`)은 맡긴 일이 끝날 때까지 저장소 완료를 회수한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Store/ServiceRetention.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IServiceStore;

    /** @brief 보존 기간 쓸기 예약 작업의 설정입니다. */
    struct ServiceRetentionJobConfig
    {
        int32 _minuteOfDay{ 5 * 60 };   ///< 실행 시각(UTC 분)
        int32 _maxScanPerRule{ 5000 };  ///< 규칙마다 한 회차에 훑는 레코드 상한
        int32 _maxDeletePerRule{ 500 }; ///< 규칙마다 한 회차에 지우는 레코드 상한
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServiceRetentionJob
     * @brief 보존 기간 쓸기를 예약 작업으로 돌립니다. 서비스 스레드 하나가 쓴다.
     */
    class SW_GF_API ServiceRetentionJob final : public IScheduledJobHandler
    {
    public:
        static constexpr const utf8* kJobID = "retention.sweep";

        ServiceRetentionJob();
        ~ServiceRetentionJob() override;

        /** @brief 작업을 등록합니다. 규칙이 없으면 등록하지 않고 true 입니다. 일정이 틀리면 false 입니다. */
        [[nodiscard]] bool initialize( IServiceStore* pStore, ServiceScheduler* pScheduler, vector<ServiceRetentionRule> listRule, const ServiceRetentionJobConfig& settings );
        /** @brief 맡긴 일이 끝나기를 기다린 뒤 비웁니다. */
        void shutdown();

        void onScheduledRun( const ScheduledRun& run ) override;

        /** @brief 마지막으로 끝난 회차가 지운 레코드 수입니다(테스트 · 진단). */
        int64 getLastDeletedCount() const { return _lastDeletedCount; }
        bool  isRunning() const { return _bPending == SW_TRUE; }

    private:
        class SweepWork;

        void onSweepCompleted( vector<ServiceRetentionRule> listRule, ServiceStoreResult result, int64 deletedCount );

        vector<ServiceRetentionRule> _listRule;
        ServiceRetentionJobConfig    _settings;
        ScheduledRun                 _run;
        IServiceStore*               _pStore;
        ServiceScheduler*            _pScheduler;
        int64                        _lastDeletedCount;
        uint8                        _bPending;
        uint8                        _bRunHeld;
    };
} // namespace sw
