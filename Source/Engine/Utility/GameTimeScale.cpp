#include "pch.h"

#include "Engine/Utility/GameTimeScale.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /** @brief 게임 시간 배율입니다(1 = 실시간). 에디터 툴바 · 콘솔 `timescale` 도 이 값을 바꿉니다. */
    SW_GLOBAL_VARIABLE_FLOAT( gv_timeScale, 1.0f, "게임 시간 배율 (1=실시간, 0.25=슬로 모션, 0=멈춤)" );

    float32 GameTimeScale::get()
    {
        return MathUtil::clamp( gv_timeScale, kMinScale, kMaxScale );
    }

    void GameTimeScale::set( float32 scale )
    {
        gv_timeScale = MathUtil::clamp( scale, kMinScale, kMaxScale );
    }
} // namespace sw
