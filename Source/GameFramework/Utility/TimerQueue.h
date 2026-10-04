/**
 * @file TimerQueue.h
 * @brief 게임 시간 타이머 — "3 초 뒤에" · "0.5 초마다" 를 손잡이로 걸고 멈추고 지웁니다(언리얼 `FTimerManager`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 타이머 손잡이입니다. 0 은 없음입니다. */
    using TimerHandle = uint64;

    /**
     * @class TimerQueue
     * @brief 게임이 매 프레임 `advance` 로 시간을 넘기면 때가 된 타이머를 부릅니다. 같은 프레임에 여러 번 지난 반복 타이머는 지난 횟수만큼 부릅니다
     *        (프레임이 길어도 0.5 초마다 = 초당 두 번이 지켜진다). 콜백 안에서 타이머를 더하거나 지워도 됩니다.
     * @details 시간은 게임 시간입니다 — 일시정지 · 슬로모션을 반영한 deltaTime 을 넘기면 타이머도 따른다. 콜백은 델리게이트라 게임 모듈의 람다를 들고
     *          있으면 핫 리로드 전에 `clear` 합니다(정적 · 모듈 코드는 리로드에서 사라진다).
     */
    class SW_GF_API TimerQueue
    {
    public:
        using Callback = Delegate<void()>;

        TimerQueue();

        /** @brief @p delay 초 뒤에 한 번(반복이면 그 뒤로 @p interval 초마다) 부릅니다. */
        TimerHandle schedule( float32 delay, const Callback& callback, bool bRepeat = false, float32 interval = 0.0f );
        /** @brief 지웁니다. 없던 손잡이면 false 입니다. */
        bool cancel( TimerHandle handle );
        void setPaused( TimerHandle handle, bool bPaused );
        void clear();
        /** @brief 시간을 넘기고 때가 된 타이머를 부릅니다. 부른 횟수입니다. */
        int32 advance( float32 deltaTime );

        bool isActive( TimerHandle handle ) const;
        /** @brief 다음에 불리기까지 남은 시간입니다. 없으면 −1 입니다. */
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

        vector<Timer> _listTimer;
        float64       _time;
        TimerHandle   _nextHandle;
        uint8         _bAdvancing;
    };
} // namespace sw
