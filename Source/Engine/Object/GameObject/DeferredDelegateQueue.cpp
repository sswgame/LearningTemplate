/**
 * @file DeferredDelegateQueue.cpp
 * @brief 지연 델리게이트 큐 구현입니다(넣기 · 맞바꿔 비우기 · 버리기).
 */
#include "pch.h"

#include "Engine/Object/GameObject/DeferredDelegateQueue.h"

namespace sw
{
    DeferredDelegateQueue::DeferredDelegateQueue()
        : _mutex{}
        , _listPending{}
        , _listProcessing{}
    {
        _listPending.reserve( 128 );
        _listProcessing.reserve( 128 );
    }

    void DeferredDelegateQueue::push( Callback callback )
    {
        if ( callback.isBound() == false )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        _listPending.push_back( std::move( callback ) );
    }

    void DeferredDelegateQueue::drain()
    {
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _listPending.empty() )
                return;
            _listProcessing.swap( _listPending );
        }
        for ( Callback& callback : _listProcessing )
        {
            if ( callback.isBound() )
                callback();
        }
        _listProcessing.clear();
    }

    void DeferredDelegateQueue::clear()
    {
        // 처리 목록은 건드리지 않는다. 비우기 밖에서는 늘 비어 있고, 비우는 중(콜백이 씬을 비우는 경우)이면 지금 도는 목록이다.
        std::scoped_lock<mutex> lock{ _mutex };
        _listPending.clear();
    }
} // namespace sw
