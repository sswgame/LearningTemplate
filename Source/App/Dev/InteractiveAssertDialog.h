/**
 * @file App/Dev/InteractiveAssertDialog.h
 * @brief App 이 개발 도구의 단언 대화상자(`DevTools/Client/Diagnostics/AssertDialog`)를 거는 입구입니다.
 * @details 개발 도구 헤더는 배포 타깃이 include 하는 것부터 막혀 있다(`DevToolsAPI.h`) — `if constexpr` 로 버려지는 갈래라도 선언은 보여야 하므로 App 은 이 헤더만 본다.
 *          정의(`InteractiveAssertDialog.cpp`)는 개발 구성에만 있고(`App/Dev/` — 배포는 짓지 않는다) 배포에서는 버려지는 갈래라 링크에 정의가 없어도 된다(`RHIBackendSwitcher` 와 같은 자리).
 *          서버는 대화상자가 없다 — 클라이언트 개발 도구라 호스트 공용 개발 계층(`IHostDevLayer`)에 두지 않는다.
 */
#pragma once

namespace sw
{
    class CommandLineManager;

    /**
     * @struct InteractiveAssertDialog
     * @brief 사람이 지켜보는 에디터 실행에서만 단언을 대화상자로 묻게 합니다. 파일 머리말 참고.
     */
    struct InteractiveAssertDialog
    {
        /** @brief `AssertDialog::installForInteractiveRun` 을 부릅니다. `build::kShipping` 이 아닌 갈래에서만 부른다. */
        static void install( const CommandLineManager& commandLine );
    };
} // namespace sw
