#include "pch.h"

#include "Engine/Config/GameTimeScale.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineServices.h"

namespace sw
{

    /** @brief 게임 시간 배율입니다(1 = 실시간). 에디터 툴바 · 콘솔 `timescale` 도 이 값을 바꿉니다. */
    SW_GLOBAL_VARIABLE( float32, gv_timeScale, 1.0f, "게임 시간 배율 (1=실시간, 0.25=슬로 모션, 0=멈춤)" );

    GameTimeScale::GameTimeScale()
        : _pauseRequestCount{ 0 }
        , _unscaledDeltaSeconds{ -1.0f }
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
        return _pauseRequestCount.load( std::memory_order_relaxed ) > 0;
    }

    void GameTimeScale::set( float32 scale )
    {
        gv_timeScale = MathUtil::clamp( scale, kMinScale, kMaxScale );
    }

    void GameTimeScale::addPauseRequest()
    {
        _pauseRequestCount.fetch_add( 1, std::memory_order_relaxed );
    }

    void GameTimeScale::removePauseRequest()
    {
        SW_ASSERT( _pauseRequestCount.load( std::memory_order_relaxed ) > 0 );
        _pauseRequestCount.fetch_sub( 1, std::memory_order_relaxed );
    }

    uint32 GameTimeScale::getPauseRequestCount() const
    {
        return _pauseRequestCount.load( std::memory_order_relaxed );
    }

    void GameTimeScale::setUnscaledDeltaTime( float32 deltaSeconds )
    {
        _unscaledDeltaSeconds = MathUtil::max( deltaSeconds, 0.0f );
    }

    float32 GameTimeScale::getUnscaledDeltaTime() const
    {
        // 적은 적이 없는 값을 다른 시간(벽시계 · 배율 걸린 경과)으로 대신하지 않는다 — 대신하면 녹화와 재생이 다른 시간을 읽는다.
        SW_ASSERT( _unscaledDeltaSeconds >= 0.0f );
#if !defined( SW_DEBUG )
        // 단언이 없는 구성에서도 적지 않은 값(-1)을 경과로 내보내지 않는다 — 음수 경과는 움직임을 조용히 멈추거나 거꾸로 돌린다.
        if ( _unscaledDeltaSeconds < 0.0f )
            SW_FATAL( "GameTimeScale::getUnscaledDeltaTime before setUnscaledDeltaTime (Engine::beginFrame or the test must set it)" );
#endif
        return _unscaledDeltaSeconds;
    }

    namespace
    {
        bool runTimeScale( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.size() > 1 )
                return false;
            if ( listArgument.size() == 1 )
            {
                float32 scale = 1.0f;
                if ( StringUtil::parseFloat( listArgument[0], scale ) == false )
                    return false;
                engine::getSubsystem<GameTimeScale>().set( scale );
            }
            // 정한 배율을 답한다 — 정지 요청(정지 메뉴 · 로딩 화면)이 걸려 있으면 실효 배율은 0 이라 그것을 덧붙인다.
            const GameTimeScale& timeScale = engine::getSubsystem<GameTimeScale>();
            outReply                       = formatToString( "time scale = %#%#", timeScale.getSetting(), timeScale.isPaused() ? " (paused)" : "" );
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( TimeScale, "timescale", "timescale [scale]", "Show or set the game time scale (gv_timeScale, 0 = frozen)",
                    &runTimeScale );
} // namespace sw
