#include "pch.h"

#include "GameFramework/Base/Utility/TimerQueue.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct TimerQueueInternal
        {
            /** @brief 한 프레임에 한 반복 타이머를 부르는 최대 횟수입니다 — 디버거 정지 뒤 수천 번 불리지 않게. */
            static constexpr int32 kMaxCatchUpCount = 8;
        };
    } // namespace
} // namespace sw

namespace sw
{
    TimerQueue::TimerQueue()
        : _listTimer{}
        , _time{ 0.0 }
        , _nextHandle{ 1 }
        , _bAdvancing{ SW_FALSE }
    {
    }

    TimerHandle TimerQueue::schedule( float32 delay, const Callback& callback, bool bRepeat, float32 interval )
    {
        if ( callback.isBound() == false )
            return 0;
        Timer timer;
        timer._callback = callback;
        timer._dueTime  = _time + static_cast<float64>( MathUtil::max( 0.0f, delay ) );
        timer._interval = bRepeat ? MathUtil::max( 1.0e-3f, interval > 0.0f ? interval : delay ) : 0.0f;
        timer._bRepeat  = bRepeat ? SW_TRUE : SW_FALSE;
        timer._handle   = _nextHandle++;
        _listTimer.push_back( timer );
        return timer._handle;
    }

    bool TimerQueue::cancel( TimerHandle handle )
    {
        Timer* pTimer = findTimer( handle );
        if ( pTimer == nullptr )
            return false;
        pTimer->_bCancelled = SW_TRUE; // 지우기는 advance 끝에서 — 도는 중에 목록을 옮기지 않는다
        if ( _bAdvancing == SW_FALSE )
        {
            _listTimer.erase( std::remove_if( _listTimer.begin(), _listTimer.end(), []( const Timer& timer )
            { return timer._bCancelled != SW_FALSE; } ),
                              _listTimer.end() );
        }
        return true;
    }

    void TimerQueue::setPaused( TimerHandle handle, bool bPaused )
    {
        Timer* pTimer = findTimer( handle );
        if ( pTimer == nullptr || ( pTimer->_bPaused != SW_FALSE ) == bPaused )
            return;
        if ( bPaused )
            pTimer->_pausedRemaining = static_cast<float32>( pTimer->_dueTime - _time );
        else
            pTimer->_dueTime = _time + static_cast<float64>( pTimer->_pausedRemaining );
        pTimer->_bPaused = bPaused ? SW_TRUE : SW_FALSE;
    }

    void TimerQueue::clear()
    {
        if ( _bAdvancing != SW_FALSE )
        {
            for ( Timer& timer : _listTimer )
            {
                timer._bCancelled = SW_TRUE;
            }
            return;
        }
        _listTimer.clear();
    }

    int32 TimerQueue::advance( float32 deltaTime )
    {
        if ( deltaTime > 0.0f )
            _time += static_cast<float64>( deltaTime );
        _bAdvancing     = SW_TRUE;
        int32 callCount = 0;
        // 콜백이 새 타이머를 더하면 목록이 자란다 — 자리로 돌고 이번 프레임에 더한 것은 다음 프레임부터.
        const size_t timerCount = _listTimer.size();
        for ( size_t timerIndex = 0; timerIndex < timerCount; ++timerIndex )
        {
            for ( int32 catchUp = 0; catchUp < TimerQueueInternal::kMaxCatchUpCount; ++catchUp )
            {
                Timer& timer = _listTimer[timerIndex];
                if ( timer._bCancelled != SW_FALSE || timer._bPaused != SW_FALSE || timer._dueTime > _time )
                    break;
                const Callback callback = timer._callback; // 콜백이 목록을 늘려 자리가 옮겨져도 안전하게 복사해서 부른다
                if ( timer._bRepeat != SW_FALSE )
                    timer._dueTime += static_cast<float64>( timer._interval );
                else
                    timer._bCancelled = SW_TRUE;
                callback();
                ++callCount;
            }
            // 따라잡기 한도를 넘었으면 지금 시각으로 다시 맞춘다.
            Timer& timer = _listTimer[timerIndex];
            if ( timer._bRepeat != SW_FALSE && timer._bCancelled == SW_FALSE && timer._dueTime <= _time )
                timer._dueTime = _time + static_cast<float64>( timer._interval );
        }
        _bAdvancing = SW_FALSE;
        _listTimer.erase( std::remove_if( _listTimer.begin(), _listTimer.end(), []( const Timer& timer )
        { return timer._bCancelled != SW_FALSE; } ),
                          _listTimer.end() );
        return callCount;
    }

    bool TimerQueue::isActive( TimerHandle handle ) const
    {
        return findTimer( handle ) != nullptr;
    }

    float32 TimerQueue::getRemaining( TimerHandle handle ) const
    {
        const Timer* pTimer = findTimer( handle );
        if ( pTimer == nullptr )
            return -1.0f;
        return pTimer->_bPaused != SW_FALSE ? pTimer->_pausedRemaining : static_cast<float32>( MathUtil::max( 0.0, pTimer->_dueTime - _time ) );
    }

    TimerQueue::Timer* TimerQueue::findTimer( TimerHandle handle )
    {
        return const_cast<Timer*>( static_cast<const TimerQueue*>( this )->findTimer( handle ) );
    }

    const TimerQueue::Timer* TimerQueue::findTimer( TimerHandle handle ) const
    {
        for ( const Timer& timer : _listTimer )
        {
            if ( timer._handle == handle && timer._bCancelled == SW_FALSE )
                return &timer;
        }
        return nullptr;
    }
} // namespace sw
