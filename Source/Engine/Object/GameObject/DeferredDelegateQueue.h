/**
 * @file DeferredDelegateQueue.h
 * @brief 아무 스레드나 넣고 한 스레드가 넣은 순서대로 비우는 델리게이트 큐입니다(틱이 미룬 일).
 *
 * [왜 별도 타입인가]
 * 매니저의 지연 큐 둘(계층 변경 · 틱 뒤 작업)이 뮤텍스 · 목록 · 예약 · 비우기 · `clear` 를 같은 모양으로 씁니다. 큐 하나는 멤버 하나와
 * 넣기 전달자 하나입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    /**
     * @class DeferredDelegateQueue
     * @brief 병렬 틱이 미룬 일을 담았다가 틱 뒤 게임 스레드에서 넣은 순서대로 실행합니다.
     * @details 넣기는 아무 스레드나 합니다(잠금 하나). 비우기는 한 스레드만 합니다. 비울 때 대기 목록을 처리 목록과 **맞바꾼 뒤**
     *          잠금 밖에서 실행하므로, 실행 중에 새로 넣은 일은 **다음** 비우기로 갑니다 — 한 번의 비우기가 끝없이 늘어나지 않습니다.
     *          두 목록은 용량을 들고 있어 프레임마다 할당하지 않습니다.
     */
    class SW_API DeferredDelegateQueue
    {
    public:
        using Callback = Delegate<void()>;

        /** @brief 두 목록의 용량을 조금 잡아 두고 만듭니다. */
        DeferredDelegateQueue();

        DeferredDelegateQueue( const DeferredDelegateQueue& )            = delete;
        DeferredDelegateQueue& operator=( const DeferredDelegateQueue& ) = delete;

        /** @brief 일을 넣습니다. 묶이지 않은 델리게이트는 버립니다. 아무 스레드에서나 부를 수 있습니다. */
        void push( Callback callback );

        /** @brief 지금까지 넣은 일을 넣은 순서대로 실행하고 비웁니다. 실행 중에 넣은 일은 다음 비우기로 갑니다. 한 스레드만 부릅니다. */
        void drain();

        /** @brief 실행하지 않고 전부 버립니다. */
        void clear();

    private:
        mutex            _mutex;
        vector<Callback> _listPending;    ///< 넣은 일(잠금 아래)
        vector<Callback> _listProcessing; ///< 비우는 중인 일(비우는 스레드만)
    };
} // namespace sw
