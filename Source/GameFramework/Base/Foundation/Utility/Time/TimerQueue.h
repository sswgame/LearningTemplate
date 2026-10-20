/**
 * @file TimerQueue.h
 * @brief "3 초 뒤에", "0.5 초마다" 같은 게임 시간 타이머를 핸들로 등록하고 멈추고 지웁니다. 언리얼의 `FTimerManager` 에 해당합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 타이머 핸들입니다. 0 은 타이머가 없다는 뜻입니다. */
    using TimerHandle = uint64;

    /**
     * @class TimerQueue
     * @brief 게임이 매 프레임 `advance` 로 시간을 넘기면 때가 된 타이머를 부릅니다.
     * @details 한 프레임 동안 여러 번 지난 반복 타이머는 지난 횟수만큼 부릅니다. 그래서 프레임이 길어도 "0.5 초마다" 는 초당 두 번으로 지켜집니다.
     *          콜백 안에서 타이머를 더하거나 지워도 됩니다.
     *          시간은 게임 시간입니다. 일시정지와 슬로모션을 반영한 deltaTime 을 넘기면 타이머도 그에 따릅니다.
     *          콜백은 델리게이트입니다. 게임 모듈의 람다를 보관하고 있다면 핫 리로드 전에 `clear` 합니다. 리로드하면 모듈 코드가 사라집니다.
     */
    class SW_GF_API TimerQueue
    {
    public:
        using Callback = Delegate<void()>;

        TimerQueue();

        /** @brief @p delay 초 뒤에 한 번 부릅니다. 반복이면 그 뒤로 @p interval 초마다 부릅니다. */
        TimerHandle schedule( float32 delay, const Callback& callback, bool bRepeat = false, float32 interval = 0.0f );
        /** @brief 타이머를 지웁니다. 없는 핸들이면 false 입니다. */
        bool cancel( TimerHandle handle );
        void setPaused( TimerHandle handle, bool bPaused );
        void clear();
        /** @brief 시간을 넘기고 때가 된 타이머를 부릅니다. 부른 횟수를 돌려줍니다. */
        int32 advance( float32 deltaTime );

        bool isActive( TimerHandle handle ) const;
        /** @brief 다음에 불리기까지 남은 시간입니다. 타이머가 없으면 -1 입니다. */
        float32 getRemaining( TimerHandle handle ) const;
        size_t  getCount() const { return _listTimer.size(); }
        float64 getTime() const { return _time; }

    private:
        struct Timer
        {
            Callback    _callback{};
            float64     _dueTime{ 0.0 };
            float32     _interval{ 0.0f };
            float32     _pausedRemaining{ 0.0f };
            TimerHandle _handle{ 0 };
            uint8       _bRepeat{ SW_FALSE };
            uint8       _bPaused{ SW_FALSE };
            uint8       _bCancelled{ SW_FALSE };
        };

        Timer*       findTimer( TimerHandle handle );
        const Timer* findTimer( TimerHandle handle ) const;
        /** @brief 지운 표시가 된 타이머를 목록에서 뺍니다. `advance` 도중에는 부르지 않습니다. 도는 중인 목록이 옮겨집니다. */
        void removeCancelledTimers();

        vector<Timer> _listTimer;
        float64       _time;
        TimerHandle   _nextHandle;
        uint8         _bAdvancing;
    };
} // namespace sw
