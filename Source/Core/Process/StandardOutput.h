/**
 * @file StandardOutput.h
 * @brief 명령줄 도구의 결과 글(도움말 · 목록)을 표준 출력에 그대로 씁니다.
 * @details 로그(`SW_LOG_*`)는 시각 · 범주 머리를 붙이고 로그 수준에 따라 사라지므로, 사람이나 스크립트가 그대로 읽는 출력(`-help` 의 인자 목록)은 이것으로 씁니다.
 *          실패 알림은 이것이 아니라 로그이고, 로거가 서기 전이면 `SW_LOW_LEVEL_ERROR` 입니다. 표준 출력을 직접 부르는 자리는 이 파일의 구현 하나입니다(`CheckFacilityBypass`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"

namespace sw
{
    /** @brief 표준 출력 쓰기입니다(전부 static). */
    struct SW_API StandardOutput
    {
        /** @brief @p text 를 그대로 쓰고 바로 비웁니다(줄바꿈을 붙이지 않습니다). 다 썼으면 true 입니다. */
        [[nodiscard]] static bool write( string_view text );
    };
} // namespace sw
