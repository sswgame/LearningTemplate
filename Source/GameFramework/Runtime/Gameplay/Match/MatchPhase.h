/**
 * @file MatchPhase.h
 * @brief 한 판의 단계입니다. 장르 공통 어휘이며 점수판(`MatchState`)과 경기 규칙(카트 경기)이 같은 값을 씁니다.
 * @details 단계를 바꾸는 시계는 규칙마다 다르다(점수판은 시간 제한, 카트는 결승과 결승 시한). 그래서 값만 공유한다.
 *          언리얼 `AGameMode` 의 MatchState(WaitingToStart → InProgress → WaitingPostMatch)와 같은 자리이다.
 *          값이 상태(세이브 · 넷 스냅샷)에 번호로 저장되므로 뒤에만 추가한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 판의 단계입니다. */
    enum class MatchPhase : uint8
    {
        Waiting = 0, ///< `start` 전이다. 참가자(카트는 차)를 이때 더한다
        Warmup,      ///< 시작 전 대기이다(카운트다운). 카트는 차가 서 있다
        InProgress,
        Ended
    };
} // namespace sw
