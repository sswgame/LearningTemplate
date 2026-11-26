#include "pch.h"

#include "Engine/Config/GameTimeScale.h"

#include "Core/Concurrency/atomic.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{

    /** @brief 게임 시간 배율입니다(1 = 실시간). 에디터 툴바 · 콘솔 `timescale` 도 이 값을 바꿉니다. */
    SW_GLOBAL_VARIABLE( float32, gv_timeScale, 1.0f, "게임 시간 배율 (1=실시간, 0.25=슬로 모션, 0=멈춤)" );

    GameTimeScale::GameTimeScale()
        : _arrPauseRequestCount{}
        , _worldTimeShareCount{ 0 }
        , _unscaledDeltaSeconds{ -1.0f }
        , _pauseSubscriptions{ SW_DELEGATE_METHOD( SubscriptionList::ReleaseFunction, &GameTimeScale::releasePauseRequest, this ) }
        , _worldTimeShareSubscriptions{ SW_DELEGATE_METHOD( SubscriptionList::ReleaseFunction, &GameTimeScale::releaseWorldTimeShare, this ) }
    {
    }

    float32 GameTimeScale::get() const
    {
        return isPaused() ? 0.0f : getSetting();
    }

    float32 GameTimeScale::getSetting() const
    {
        return MathUtil::clamp( gv_timeScale, kMinScale, kMaxScale );
    }

    bool GameTimeScale::isPaused() const
    {
        if ( _arrPauseRequestCount[static_cast<uint32>( GamePauseReason::Host )].load( std::memory_order_relaxed ) > 0 )
            return true;
        const bool bPlayerPause = _arrPauseRequestCount[static_cast<uint32>( GamePauseReason::Player )].load( std::memory_order_relaxed ) > 0;
        return bPlayerPause && _worldTimeShareCount.load( std::memory_order_relaxed ) == 0;
    }

    void GameTimeScale::set( float32 scale )
    {
        gv_timeScale = MathUtil::clamp( scale, kMinScale, kMaxScale );
    }

    Subscription GameTimeScale::addPauseRequest( GamePauseReason reason )
    {
        _arrPauseRequestCount[static_cast<uint32>( reason )].fetch_add( 1, std::memory_order_relaxed );
        return _pauseSubscriptions.issue( static_cast<uint64>( reason ) );
    }

    void GameTimeScale::releasePauseRequest( uint64 reason )
    {
        _arrPauseRequestCount[reason].fetch_sub( 1, std::memory_order_relaxed );
    }

    Subscription GameTimeScale::shareWorldTime()
    {
        _worldTimeShareCount.fetch_add( 1, std::memory_order_relaxed );
        return _worldTimeShareSubscriptions.issue( 0 );
    }

    void GameTimeScale::releaseWorldTimeShare( [[maybe_unused]] uint64 unusedID )
    {
        _worldTimeShareCount.fetch_sub( 1, std::memory_order_relaxed );
    }

    uint32 GameTimeScale::getPauseRequestCount() const
    {
        uint32 count = 0;
        for ( const atomic<uint32>& reasonCount : _arrPauseRequestCount )
        {
            count += reasonCount.load( std::memory_order_relaxed );
        }
        return count;
    }

    void GameTimeScale::setUnscaledDeltaTime( RealSeconds deltaTime )
    {
        _unscaledDeltaSeconds = MathUtil::max( deltaTime.count(), 0.0f );
    }

    RealSeconds GameTimeScale::getUnscaledDeltaTime() const
    {
        // 적은 적이 없는 값을 다른 시간(벽시계 · 배율 걸린 경과)으로 대신하지 않는다 — 대신하면 녹화와 재생이 다른 시간을 읽는다.
        SW_ASSERT( _unscaledDeltaSeconds >= 0.0f );
#if !defined( SW_DO_CHECKS )
        // 단언이 없는 구성에서도 적지 않은 값(-1)을 경과로 내보내지 않는다 — 음수 경과는 움직임을 조용히 멈추거나 거꾸로 돌린다.
        if ( _unscaledDeltaSeconds < 0.0f )
            reportFatalError( "GameTimeScale::getUnscaledDeltaTime before setUnscaledDeltaTime (Engine::beginFrame or the test must set it)" );
#endif
        return RealSeconds{ _unscaledDeltaSeconds };
    }
} // namespace sw

namespace sw
{
    // 정지 요청 · 이번 프레임의 실제 경과.
    SW_ENGINE_SUBSYSTEM_WITH_SLOT( GameTimeScale, GameVisible, Process, Shared, (), SW_ENGINE_DEFAULT_FACTORY( GameTimeScale ) );
} // namespace sw
