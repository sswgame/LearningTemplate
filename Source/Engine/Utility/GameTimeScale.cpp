#include "pch.h"

#include "Engine/Utility/GameTimeScale.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Console/DevCommandRegistry.h"

namespace sw
{
    namespace
    {
        /** @brief 지금 걸린 정지 요청 수입니다(게임을 멈추는 UI 화면). 하나라도 있으면 게임 시간이 멈춥니다. */
        atomic<uint32> s_pauseRequestCount{ 0 };
        /** @brief 호스트가 적은 이번 프레임의 실제 경과(초) — 음수면 적은 적이 없다. 게임 스레드가 쓰고 읽는다. */
        float32 s_unscaledDeltaSeconds{ -1.0f };
    } // namespace

    /** @brief 게임 시간 배율입니다(1 = 실시간). 에디터 툴바 · 콘솔 `timescale` 도 이 값을 바꿉니다. */
    SW_GLOBAL_VARIABLE( float32, gv_timeScale, 1.0f, "게임 시간 배율 (1=실시간, 0.25=슬로 모션, 0=멈춤)" );

    float32 GameTimeScale::get()
    {
        if ( s_pauseRequestCount.load( std::memory_order_relaxed ) > 0 )
            return 0.0f;
        return MathUtil::clamp( gv_timeScale, kMinScale, kMaxScale );
    }

    void GameTimeScale::set( float32 scale )
    {
        gv_timeScale = MathUtil::clamp( scale, kMinScale, kMaxScale );
    }

    void GameTimeScale::addPauseRequest()
    {
        s_pauseRequestCount.fetch_add( 1, std::memory_order_relaxed );
    }

    void GameTimeScale::removePauseRequest()
    {
        SW_ASSERT( s_pauseRequestCount.load( std::memory_order_relaxed ) > 0 );
        s_pauseRequestCount.fetch_sub( 1, std::memory_order_relaxed );
    }

    uint32 GameTimeScale::getPauseRequestCount()
    {
        return s_pauseRequestCount.load( std::memory_order_relaxed );
    }

    void GameTimeScale::setUnscaledDeltaTime( float32 deltaSeconds )
    {
        s_unscaledDeltaSeconds = MathUtil::max( deltaSeconds, 0.0f );
    }

    float32 GameTimeScale::getUnscaledDeltaTime( float32 fallbackDeltaSeconds )
    {
        return s_unscaledDeltaSeconds >= 0.0f ? s_unscaledDeltaSeconds : fallbackDeltaSeconds;
    }
} // namespace sw

#if SW_DEV_COMMANDS_ENABLED
namespace sw
{
    namespace
    {
        struct GameTimeScaleDevCommandsInternal
        {
            static bool runTimeScale( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    float32 scale = 1.0f;
                    if ( StringUtil::parseFloat( listArgument[0], scale ) == false )
                        return false;
                    GameTimeScale::set( scale );
                }
                outReply = "time scale = " + to_string( GameTimeScale::get() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( TimeScale, "timescale", "timescale [scale]", "Show or set the game time scale (gv_timeScale, 0 = frozen)",
                    &GameTimeScaleDevCommandsInternal::runTimeScale );
} // namespace sw
#endif
