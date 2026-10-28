/**
 * @file MaterialPSOBuilder.h
 * @brief 머티리얼 변형 PSO 를 렌더 스레드 밖에서 만드는 전용 빌더 스레드와 그 작업 줄입니다(RHI 비동기 업로드 U12).
 * @details 요청은 렌더 스레드가 셋업 구간에 넣고(`submit`), 결과는 완료 목록에 쌓여 렌더 스레드가 셋업 구간에 가져갑니다(`takeResults`).
 *          그래서 PSO 저장소를 쓰는 스레드는 렌더 스레드 하나로 유지되어 "삽입은 기록 시작 전에만" 불변식이 섭니다.
 *          **스레드는 하나입니다.** PSO 병렬 생성은 드라이버가 직렬이라 이득 9 % 로 기각됐고, 이 빌더는 빨리 만들려는 것이 아니라 렌더 스레드의 임계 경로에서 빼려는 것입니다.
 *          스레드를 쓸 수 없는 백엔드(GL, `RHICapabilities::_bThreadSafeResourceCreation` 이 거짓)나 `-gv_psoAsync=0` 은 렌더 스레드가 같은 줄을 시간 예산 안에서 직접 만듭니다
 *          (`buildOnCaller`) — 폴백이 아니라 같은 `Building` 상태 규칙을 다른 방식으로 채웁니다.
 *          언리얼의 PSO 비동기 컴파일 스레드와 같은 모양입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class MaterialPSOBuilder
     * @brief 머티리얼 변형 PSO 의 생성 작업 줄과 전용 스레드입니다. 줄과 결과는 `_mutex` 로 지키고, 요청과 가져가기는 렌더 스레드만 부릅니다.
     */
    class MaterialPSOBuilder final
    {
    public:
        /** @brief 만들 PSO 하나입니다. `_key` 는 `RenderPSOCache::materialPSOKey` 입니다. */
        struct Job
        {
            uint64               _key{ 0 };
            RHIPipelineStateDesc _desc{};
        };

        /** @brief 만든 결과입니다. `_handle` 이 0 이면 실패입니다. 가져간 쪽이 소유합니다. */
        struct Result
        {
            uint64                 _key{ 0 };
            RHIPipelineStateDesc   _desc{};
            RHIPipelineStateHandle _handle{ 0 };
        };

        MaterialPSOBuilder() = default;
        /** @brief 스레드를 세우고 합류합니다(남은 결과의 PSO 는 버립니다 — 디바이스가 살아 있다면 먼저 `shutdown` 을 부릅니다). */
        ~MaterialPSOBuilder();

        MaterialPSOBuilder( const MaterialPSOBuilder& )            = delete;
        MaterialPSOBuilder& operator=( const MaterialPSOBuilder& ) = delete;

        /**
         * @brief 생성 작업을 줄에 넣습니다.
         * @param pDevice 만들 디바이스입니다. 줄이 비워질 때까지 살아 있어야 합니다(`shutdown` 이 그 전에 불립니다).
         * @param bUrgent 이번 프레임 배치가 필요한 키는 true, 미리 만들기(`warmPinnedMaterialPSOs`)는 false 입니다. 급한 줄이 먼저 나갑니다.
         * @param bThread true 이면 빌더 스레드가 만듭니다(처음 넣을 때 시작합니다). false 이면 `buildOnCaller` 가 만듭니다.
         */
        void submit( IRHIDevice* pDevice, Job&& job, bool bUrgent, bool bThread );
        /** @brief 완료된 결과를 @p outListResult 로 옮깁니다(앞의 내용은 지웁니다). */
        void takeResults( vector<Result>& outListResult );
        /**
         * @brief 줄의 작업을 부른 스레드에서 만듭니다. 급한 것부터 @p budgetMicroseconds 가 지날 때까지 하고, 줄이 있으면 최소 하나는 만듭니다(0 이면 전부).
         * @return 이번에 끝낸 작업 수입니다(결과는 `takeResults` 로 가져옵니다).
         */
        uint32 buildOnCaller( uint32 budgetMicroseconds );
        /** @brief 줄에 있거나 만드는 중인 작업 수입니다(결과 대기 제외). */
        [[nodiscard]] uint32 getPendingCount() const;
        /**
         * @brief 스레드를 합류하고 줄을 버리고 남은 결과의 PSO 를 파괴합니다. @p pDevice 가 null 이면 핸들만 잊습니다(디바이스가 이미 없을 때).
         * @details 이 뒤에 `submit` 하면 스레드를 다시 시작합니다.
         */
        void shutdown( IRHIDevice* pDevice );

    private:
        void run( ThreadStopToken stopToken );
        /** @brief 급한 줄 먼저 하나 꺼냅니다. 없으면 false 입니다. 꺼낸 작업은 만드는 중으로 셉니다(`_inFlight`). */
        bool popJob( Job& outJob, IRHIDevice*& outDevice );
        void pushResult( Job&& job, RHIPipelineStateHandle handle );

        mutable mutex  _mutex;
        vector<Job>    _listUrgent;
        vector<Job>    _listLow;
        vector<Result> _listResult;
        IRHIDevice*    _pDevice{ nullptr };
        uint32         _inFlight{ 0 };
        atomic<uint32> _wakeWord{ 0 };
        sw::thread     _thread;
    };
} // namespace sw
