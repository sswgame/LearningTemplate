/**
 * @file ReloadOutcome.h
 * @brief 바뀐 파일 하나를 다시 읽은 결과 세 값입니다(핫 리로드). "맡은 파일이 아님"이 정상 결과로 따로 있어 `(void)` 로 버릴 이유가 없습니다.
 * @details 있을 수도 없을 수도 있는 파일을 처음 읽는 결과는 `ConfigReadResult`(Loaded · Missing · Invalid)다(docs/plans/DataDriven.md 7.3).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @enum ReloadOutcome
     * @brief 바뀐 파일 하나를 다시 읽은 결과입니다. `Failed` 는 다시 읽는 함수가 오류를 한 번 남긴 뒤 돌려주고, 부르는 쪽은 `switch` 로 멈출지 이어 갈지를 고릅니다.
     */
    enum class ReloadOutcome : uint8
    {
        Reloaded,   ///< 다시 읽어 입혔다
        NotTracked, ///< 이 리로더가 맡은 파일이 아니다 — 정상이다
        Failed,     ///< 맡은 파일인데 읽지 못했다. 오류는 이미 남겼다
    };
} // namespace sw
