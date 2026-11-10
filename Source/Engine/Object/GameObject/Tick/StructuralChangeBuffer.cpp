/**
 * @file StructuralChangeBuffer.cpp
 * @brief 틱 중 규칙 — 동결 · 지연 큐 · 비우는 순서 · 스레드별 틱 상태입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/StructuralChangeBuffer.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"

// DeferredDelegateQueue
namespace sw
{
    DeferredDelegateQueue::DeferredDelegateQueue()
        : _pending{}
        , _listProcessing{}
    {
        _pending.reserve( 128 );
        _listProcessing.reserve( 128 );
    }

    void DeferredDelegateQueue::push( Callback callback )
    {
        if ( callback.isBound() == false )
            return;
        _pending.push( std::move( callback ) );
    }

    void DeferredDelegateQueue::flush()
    {
        _flushThread.assumeHeld(); // drain 은 한 스레드(게임 스레드)만 부른다
        if ( _pending.takeAll( _listProcessing ) == false )
            return;
        for ( Callback& callback : _listProcessing )
        {
            if ( callback.isBound() )
                callback();
        }
        _listProcessing.clear();
    }

    bool DeferredDelegateQueue::isEmpty()
    {
        return _pending.isEmpty();
    }

    void DeferredDelegateQueue::clear()
    {
        // 처리 목록은 건드리지 않는다. 비우기 밖에서는 늘 비어 있고, 비우는 중(콜백이 씬을 비우는 경우)이면 지금 도는 목록이다.
        _pending.clear();
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /**
         * @brief 이 스레드의 틱 상태입니다(`StructuralChangeBuffer::getThreadState`). Engine 의 이 TU 하나에만 있습니다(파일 머리말).
         * @details 포인터뿐이라 초기화가 상수이고, TLS 접근에 감싸는 함수가 붙지 않습니다.
         */
        thread_local TickThreadState t_tickThreadState{};
    } // namespace

    StructuralChangeBuffer::StructuralChangeBuffer()
        : _bFrozen{ false }
        , _bDeferredHierarchyChange{ SW_FALSE }
        , _structuralQueue{}
        , _postTickQueue{}
    {
    }

    void StructuralChangeBuffer::freeze()
    {
        // 지난 단계의 구조 변경 큐는 `drain` 이 비웠다 — 이번 단계의 계층 변경 미룸을 새로 센다.
        _bDeferredHierarchyChange.store( SW_FALSE, std::memory_order_relaxed );
        _bFrozen.store( true, std::memory_order_release );
    }

    void StructuralChangeBuffer::thaw()
    {
        _bFrozen.store( false, std::memory_order_release );
    }

    void StructuralChangeBuffer::deferStructuralChange( Callback func )
    {
        _structuralQueue.push( std::move( func ) );
    }

    void StructuralChangeBuffer::deferHierarchyChange( Callback func )
    {
        // "하나라도" 플래그 — 이미 서 있으면 쓰지 않는다(워커가 공유 칸에 거듭 쓰지 않게).
        if ( _bDeferredHierarchyChange.load( std::memory_order_relaxed ) == SW_FALSE )
            _bDeferredHierarchyChange.store( SW_TRUE, std::memory_order_relaxed );
        _structuralQueue.push( std::move( func ) );
    }

    void StructuralChangeBuffer::deferPostTick( Callback func )
    {
        _postTickQueue.push( std::move( func ) );
    }

    void StructuralChangeBuffer::executeOrDeferPostTick( Callback func )
    {
        if ( func.isBound() == false )
            return;
        if ( isFrozen() )
            _postTickQueue.push( std::move( func ) );
        else
            func();
    }

    void StructuralChangeBuffer::flush( GameObjectManager& manager )
    {
        // 지연된 구조 변경(컴포넌트 추가 · attach · detach · 태그 · 활성)을 **부른 순서대로** 먼저 적용한다. 지연 큐 · 파괴보다 앞이다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.deferredTransforms" );
            _structuralQueue.flush();
        }

        // 틱 중의 세터가 쓴 것(대기 칸 · 쓰기 큐)을 적용한다. 구조 변경(위의 지연 attach · detach)이 끝난 뒤라 부모 사슬이 안정됐다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.queuedTransforms" );
            manager.getTransformHierarchy().applyTickWrites( manager, manager.getPrimitiveRegistry() );
        }

        // 병렬 onTick 이 미룬 스폰 · 데미지 · 태그, 그리고 그것이 만든 오브젝트의 병합.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.postTick" );
            _postTickQueue.flush();
            manager.mergePendingAdds();
            // 틱이 만든 것(스폰)은 같은 프레임 안에 시작한다.
            manager.dispatchPendingBeginPlay();
        }
    }

    void StructuralChangeBuffer::flushUntilEmpty( GameObjectManager& manager )
    {
        // 서로 다시 미루는 고리를 끊는 상한이다. 정상이면 한두 번에 빈다.
        constexpr uint32 kMaxFlushPass = 64;
        for ( uint32 pass = 0; pass < kMaxFlushPass; ++pass )
        {
            flush( manager );
            if ( _structuralQueue.isEmpty() && _postTickQueue.isEmpty() )
                return;
        }
        // 상한에 닿으면 남은 것을 호출하지 않고 버린다. SW_ASSERT 로 멈추면 언로드가 반쯤 끝난 채 남고, 남겨 두면 모듈이 내려간 뒤
        // 다음 프레임 drain 이 없는 코드를 부른다. 게임 쪽 일 몇 개를 잃는 편이 안전하다.
        SW_LOG_ERROR_ID( "StructuralChangeBuffer.DeferredWorkKeptRe", "Deferred work kept re-queueing after %# drain passes before module unload; discarding the rest", kMaxFlushPass );
        clear();
    }

    void StructuralChangeBuffer::clear()
    {
        _structuralQueue.clear();
        _postTickQueue.clear();
    }

    TickThreadState& StructuralChangeBuffer::getThreadState()
    {
        return t_tickThreadState;
    }

    const GameObject* StructuralChangeBuffer::getTickingObject()
    {
        return t_tickThreadState._pTickingObject;
    }
} // namespace sw
