/**
 * @file GameSound.h
 * @brief 게임 코드가 소리를 내는 한 줄 창구입니다. 오디오 서비스가 없는 구성(헤드리스 · 시험)에서는 조용히 false 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct GameSound
     * @brief `game::getService<IAudioSystem>()` 를 찾고 널을 거르는 일을 게임마다 다시 쓰지 않게 모은 것입니다.
     */
    struct SW_GF_API GameSound
    {
        /** @brief 효과음 하나를 비동기로 냅니다(리소스 경로 — `game/<게임>/sounds/x.ogg`). */
        static bool play( string_view path );
        /** @brief 배경 음악을 바꿉니다. */
        static bool playMusic( string_view path );
    };
} // namespace sw
