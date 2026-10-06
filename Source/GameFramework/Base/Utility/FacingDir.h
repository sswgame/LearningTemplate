/**
 * @file FacingDir.h
 * @brief 2D 캐릭터가 바라보는 4방향입니다. **킷들이 나눠 쓰는 한 벌입니다.**
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief 2D 액션 캐릭터가 바라보는 4방향입니다.
     * @details `ActionRoom.h` 와 `PlayerLocomotion.h` 가 함께 씁니다. 킷마다 따로 선언하지 말 것 —
     *          둘 다 `namespace sw` 라서 두 킷의 헤더를 한 번역 단위에 넣으면 `redefinition of 'FacingDir'` 로
     *          컴파일이 안 되고, 각 킷이 따로 빌드되는 동안은 드러나지 않습니다.
     */
    enum class FacingDir : uint8
    {
        Down = 0,
        Left,
        Right,
        Up
    };
} // namespace sw
