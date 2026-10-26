/**
 * @file TaskHandleTracker.h
 * @brief 자기가 낸 태스크의 핸들만 쥐고 그것만 기다리는 추적기입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    class TaskManager;

    /**
     * @class TaskHandleTracker
     * @brief 한 소유자가 낸 태스크의 핸들만 쥐고, 종료 · 취소가 그 태스크들만 기다리게 합니다.
     * @details 엔진 전체 대기(`TaskManager::waitAll`)는 소유자가 내지 않은 태스크(오디오 같은 영구 태스크 포함)까지 기다려, 그런 태스크가 활성 수에
     *          들면 종료가 영영 멈춘다. 소유자는 태스크를 내는 자리에서 `track` 으로 올리고, 기다릴 때 `waitIdle` 을 쓴다. 에디터의 백그라운드 잡도 이것을 쓴다
     *          (에디터 상태 저장소에 하나).
     *          락을 잡아 어느 스레드든 안전하다. `waitIdle` 은 폴링이라 워커 일을 돕지 않지만, **메인 스레드가 기다리면 메인 전용
     *          큐(`TaskThreadAffinity::MainThread`)를 비워 준다**(그렇지 않으면 메인에서만 도는 태스크를 메인이 기다려 교착한다). 메인이 아닌 스레드가
     *          기다리는 `MainThread` 태스크는 메인이 다른 곳에서 틱을 돌아야 끝나며, 시간 제한이 안전망이다.
     */
    class SW_API TaskHandleTracker
    {
    public:
        /** @brief 낸 태스크를 올립니다. 끝난 핸들은 올린 수가 문턱을 넘을 때 묶어서 정리합니다(올릴 때마다 훑지 않는다). */
        void track( const TaskHandle& handle );

        /** @brief 아직 끝나지 않은 태스크의 수입니다. */
        [[nodiscard]] uint32 getPendingCount() const;

        /**
         * @brief 올라온 태스크가 모두 끝나기를 기다립니다.
         * @param timeoutMs 최대 대기 시간(밀리초)입니다.
         * @param owner 오류 로그에 적을 소유자 이름입니다.
         * @return 모두 끝났으면 true, 시간이 다 되면 false 입니다. false 일 때 아직 도는 태스크의 이름을 담아 오류 로그를 남깁니다.
         */
        [[nodiscard]] bool waitIdle( uint32 timeoutMs, string_view owner );

        /** @brief 아직 끝나지 않은 태스크 하나의 이름입니다(없으면 빈 문자열, Shipping 은 이름이 없다). 진단 로그용입니다. */
        [[nodiscard]] string describeFirstPending() const;

    private:
        /** @brief 아직 끝나지 않은 태스크를 낸 매니저입니다(없거나 매니저가 사라졌으면 nullptr). 메인 스레드가 기다릴 때 메인 큐를 돕는 데 씁니다. */
        [[nodiscard]] TaskManager* findPendingOwner() const;

        mutable mutex                  _mutex;
        vector<TaskHandle> _listHandle SW_GUARDED_BY( _mutex );
        size_t _pruneThreshold         SW_GUARDED_BY( _mutex ){ 64 };
    };
} // namespace sw
