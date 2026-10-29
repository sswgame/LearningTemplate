/**
 * @file FrameStall.h
 * @brief 다음 게임 스레드 프레임 하나를 정해진 시간만큼 붙잡는 개발용 지연입니다(개발 명령 `profile.stall <ms>`).
 * @details 프로파일러의 스파이크 정지 같은 기능을 시나리오가 확인하려면 임계값을 넘는 프레임이 필요합니다. 그 프레임을 에디터 프레임 비용에서
 *          기대하면 구성마다 결과가 달라집니다(Release 의 `GT.Frame` 은 0.1 ms 안팎입니다). 그래서 지연을 명시적으로 걸고,
 *          개발 도구가 씬 틱 뒤(`GT.Frame` 구간 안)에서 그 지연을 한 번 소비합니다.
 *          개발 도구 단위라 배포 빌드에는 형식째 없습니다 — 출시 빌드에 프레임을 붙잡는 길을 남기지 않습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /**
     * @struct FrameStall
     * @brief 한 번 걸고 한 번 소비하는 프레임 지연입니다. 걸린 지연이 없으면 `consume` 은 아무것도 하지 않습니다.
     */
    struct SW_DEVTOOLS_API FrameStall
    {
        /** @brief 다음 `consume` 이 @p milliseconds 동안 부른 스레드를 붙잡게 합니다. 아직 소비하지 않은 지연이 있으면 새 값으로 바꿉니다. */
        static void request( int32 milliseconds );
        /** @brief 걸린 지연이 있으면 그만큼 부른 스레드를 재우고 지웁니다. 개발 도구(`DevToolsHost::onAfterSceneTick`)가 `GT.Frame` 구간 안에서 부릅니다. */
        static void consume();
    };
} // namespace sw
