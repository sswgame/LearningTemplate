/**
 * @file AppHeadlessTasks.h
 * @brief App 이 맡는 헤드리스 작업입니다. 엔진의 `Headless` 단계가 하지 못하는 일(크래시 보고, 엔진이 선 뒤의 에디터 모듈 작업 · 초상화 굽기)만 여기에 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class CommandLineManager;
    class Engine;
    class IHostDevLayer;

    /**
     * @struct AppHeadlessTasks
     * @brief 창과 RHI 없이 시작한 App 이 인자에 따라 실행하는 작업들입니다.
     * @details 에디터 모듈 작업(로컬라이제이션, 임포트, 참조 검사)은 개발 계층의 표가 든다(`DevTools/Host/EditorModuleHeadlessTasks` — 인자 · 호스트 · 실행이 한 줄). 여기서는 부르기만 한다.
     *          실패한 작업이 하나라도 있으면 `App::initialize` 가 false 를 반환한다(종료 코드가 0 이 아님).
     */
    struct AppHeadlessTasks
    {
        /** @brief 호스트 서비스 뒤 작업 하나의 결과입니다. */
        enum class TaskResult : uint8
        {
            NotRequested, ///< 인자가 없어 실행하지 않았다.
            Failed,       ///< 실행했고 실패했다.
            Finished,     ///< 이 작업만 하고 끝낸다(성공).
        };

        /**
         * @brief 헤드리스로 선 App 의 작업을 실행합니다 — 크래시 보고(`-crash-reporter`)면 그것만 하고, 아니면 개발 계층 @p pDevLayer 의 에디터 모듈 작업 표를 돌립니다.
         * @param pDevLayer 개발 계층입니다(배포는 nullptr — 에디터 모듈 작업의 인자도 배포에는 없다).
         * @return 실패한 작업이 없으면 true 입니다(명령줄이 없으면 아무것도 실행하지 않고 true).
         */
        [[nodiscard]] static bool runAll( const CommandLineManager* pCommandLine, IHostDevLayer* pDevLayer );

        /**
         * @brief 호스트 서비스(게임 · 에디터 인스턴스)가 선 뒤 한 번 하고 끝나는 작업(초상화 굽기 `--render-portraits`)을 실행합니다. 게임 타입 · 렌더 스레드가 있어 엔진을 받습니다.
         * @details 창과 RHI 없이 시작하는 위 작업과 달리 이 작업들은 일반 기동(창 · 렌더 스레드 · 모듈)을 모두 거친 뒤에 돈다 — `--render-portraits` 는 게임 컴포넌트 타입과 디바이스가 있어야 그린다.
         * @return 실행한 작업이 없으면 `NotRequested` 입니다(기동을 이어 간다). 그 밖이면 App 은 메인 루프에 들어가지 않고 끝낸다.
         */
        [[nodiscard]] static TaskResult runAfterHostServices( const CommandLineManager& commandLine, Engine& hostEngine );
    };
} // namespace sw
