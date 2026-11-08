/**
 * @file ReflectionDump.h
 * @brief 실행 인자로 고른 타입 · enum 의 리플렉션 등록 내용을 첫 프레임에 로그로 남기는 개발 기능입니다(`-gv_dumpReflection=CameraComponent,CameraRole`).
 * @details 예전에는 엔진 프레임(`Engine::beginFrame`)이 테스트용 변수를 읽어 배포 바이너리에 분기가 남았다. 묻는 창구(`TypeRegistry::describeType`)는 엔진에 두고,
 *          언제 무엇을 찍을지는 개발 도구가 정한다(언리얼 `obj classes` 같은 콘솔 덤프 명령 자리).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @struct ReflectionDump @brief `-gv_dumpReflection` 의 이름들을 한 번 찍고 비웁니다. */
    struct ReflectionDump
    {
        /** @brief 요청이 있으면 그 이름들(쉼표로 여럿)의 등록 내용을 로그로 남기고 요청을 비웁니다. 개발 도구의 프레임 앞 훅(`DevToolsHost::onFrameBegin`)이 부릅니다. */
        static void runPending();
    };
} // namespace sw
