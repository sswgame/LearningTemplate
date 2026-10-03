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
     * @details 이것이 `ActionRoom.h` 와 `PlayerLocomotion.h` **양쪽에 똑같이** 적혀 있었습니다.
     *          둘 다 `namespace sw` 라서, 두 킷을 같이 쓰려고 두 헤더를 한 번역 단위에 넣으면
     *          `error: redefinition of 'FacingDir'` 로 **컴파일이 안 됐습니다.** 액션 전투와
     *          오버월드를 한 게임에서 같이 쓸 수 없었다는 뜻입니다. 각 킷이 따로 빌드되는 동안
     *          아무도 부딪히지 않아서 드러나지 않았습니다.
     * @note 값과 순서는 두 사본이 똑같았습니다. 여기로 옮기면서 바뀐 것은 없습니다.
     */
    enum class FacingDir : uint8
    {
        Down = 0,
        Left,
        Right,
        Up
    };
} // namespace sw
