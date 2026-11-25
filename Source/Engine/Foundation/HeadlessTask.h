/**
 * @file Engine/Foundation/HeadlessTask.h
 * @brief 헤드리스 작업(쿠킹 · 테스트 · 문서 · 베이크 …)의 등록 줄과 그 결과 상태입니다 — 작업은 자기 자리에서 `SW_HEADLESS_TASK` 한 줄로 오르고, 기동 단계 `Headless` 가 차례로 고릅니다.
 * @details 예전에는 `HeadlessSubsystem` 이 작업 열둘을 `||` 사슬로 손에 들고(쿠커 여섯을 직접 include), 서버가 거절할 작업을 따로 표로 적었다 — 작업을 더할 때마다 두 곳을 고쳤다.
 *          줄 하나가 이름 · 우선순위(작을수록 먼저, 주석이던 "순서가 곧 우선순위" 를 값으로) · 도는 호스트 · 요청 판정 · 본문을 함께 든다.
 *          - 요청된 작업 가운데 이 호스트에서 돌지 않는 것이 하나라도 있으면 아무것도 돌리지 않고 실패한다(조용히 넘기지 않는다 — 옛 `rejectClientOnlyTask`).
 *          - 그다음 요청된 작업 중 우선순위가 가장 앞선 하나만 돈다. 들어가며 `HeadlessTaskState::enterHeadless` 가 누수 기준선을 잡는다.
 *          배포에서 빠지는 작업(단위 테스트)은 그 줄을 `#if !defined( SW_SHIPPING )` 안에 둔다(CppStyle.md "Build configuration switches"). 언리얼 커맨드릿(`-run=`)이 클래스 등록으로 찾히는 것과 같은 자리다.
 *          바닥 층(0)이라 어느 층의 기능 폴더든 자기 작업을 올릴 수 있다(쿠커를 DevTools/Cook 으로 옮길 때 줄도 함께 옮긴다).
 */
#pragma once
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/span.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    /** @brief 헤드리스 작업의 결과입니다(호스트 소유). */
    struct HeadlessTaskState
    {
        /** @brief 작업이 종료 코드를 정하지 않았다는 값입니다. */
        static constexpr int32 kNoExitCode = -1;

        uint8 _bFailed : 1;     ///< 그 작업이 실패했다 — 호스트가 종료 코드로 내보낸다
        int32 _failureExitCode; ///< 실패했을 때의 종료 코드(작업이 정하지 않으면 `kNoExitCode`) — 백엔드마다 실행은 시나리오 종료 코드(10 · 11 · 12 · 13 · 15)를 그대로 낸다

        HeadlessTaskState();

        /**
         * @brief 헤드리스 작업으로 들어갑니다 — 누수 기준선을 잡고 헤드리스로 표시합니다. 헤드리스로 표시하는 길은 이것뿐입니다.
         * @details 헤드리스 작업은 보통 실행이 기준선을 잡는 자리(`Engine::initialize` 끝)에 닿지 않는다. 기준선이 없으면 종료 때 살아 있는
         *          기동 블록을 모두 누수로 찍으므로, 표시와 기준선을 한 호출로 묶어 분기마다 따로 기억하지 않게 한다.
         */
        void enterHeadless()
        {
            // 들어가기는 한 번이다 — 고르는 길(`selectAndRun`)이 이미 들어간 뒤 작업 본문이 다시 불러도 열린 누수 창을 한 번 더 열지 않는다.
            if ( _bHeadless == SW_TRUE )
                return;
            MemoryProfiler::captureMemoryLeakBaseline();
            markHeadless();
        }

        /**
         * @brief 누수 기준선을 잡지 않고 헤드리스로만 표시합니다 — 기준선은 프로세스 전역(`MemoryProfiler`)이라, 이미 헤드리스 작업 안에서 도는 실행(단위 테스트가 고르는 규칙을 볼 때)이
         *        다시 잡으면 열린 누수 창을 한 번 더 연다. 실제 기동 길은 `enterHeadless` 다.
         */
        void markHeadless() { _bHeadless = SW_TRUE; }

        bool isHeadless() const { return _bHeadless; }

    private:
        uint8 _bHeadless : 1; ///< 헤드리스 작업을 했다(뒤 단계를 세우지 않았다)
    };

    /** @brief 작업이 도는 호스트입니다(비트). */
    enum class HeadlessTaskHost : uint8
    {
        Client          = 1u << 0, ///< App
        DedicatedServer = 1u << 1, ///< Server
        Any             = Client | DedicatedServer,
    };

    /** @brief 엔진 작업의 우선순위입니다(작을수록 먼저). 순서는 한 곳에서 읽히게 여기 둔다 — 새 작업은 사이 값을 고른다. */
    namespace HeadlessTaskPriority
    {
        inline constexpr int32 kHelp               = 100; ///< 도움말은 무엇과 함께 주어도 먼저다
        inline constexpr int32 kUnitTest           = 200;
        inline constexpr int32 kScenarioPerBackend = 300;
        inline constexpr int32 kCookShaders        = 400;
        inline constexpr int32 kReflectionDocs     = 500;
        inline constexpr int32 kBakeRetarget       = 600;
        inline constexpr int32 kVerifyShaders      = 700;
        inline constexpr int32 kMakePatch          = 800;
        inline constexpr int32 kCook               = 900;
        inline constexpr int32 kImportLipSync      = 1000;
        inline constexpr int32 kEditorTools        = 1100; ///< 에디터 모듈이 하는 도구 — 엔진은 헤드리스로 세우기만 하고 호스트가 엔진이 선 뒤에 돌린다(`DevTools/Host/EditorModuleHeadlessTasks`)
    } // namespace HeadlessTaskPriority

    /** @brief 작업 본문이 받는 문맥입니다. */
    struct HeadlessTaskContext
    {
        CommandLineManager& _commandLine;
        HeadlessTaskState&  _state;            ///< 본문이 실패 종료 코드(`_failureExitCode`)를 정할 때 쓴다. `_bFailed` 는 본문의 반환값이 정한다
        bool                _bDedicatedServer; ///< 전용 서버가 굽는 팩은 서버가 읽지 않는 종류를 뺀다.
    };

    /** @brief 이 명령줄이 작업을 청했으면 true 입니다. 읽기만 합니다. */
    using HeadlessTaskRequestFunction = bool ( * )( const CommandLineManager& commandLine );
    /** @brief 작업 본문입니다. 성공이면 true 입니다(헤드리스 표시 · 누수 기준선은 부르기 전에 잡혔다). */
    using HeadlessTaskRunFunction = bool ( * )( HeadlessTaskContext& context );

    /** @brief 헤드리스로 들어갈 때 누수 기준선을 잡을지입니다 — 기준선은 프로세스 전역이라 실제 기동 길(`runRequested`)만 잡는다. */
    enum class HeadlessLeakBaseline : uint8
    {
        Capture, ///< 기동 길 — 기준선을 잡는다(`HeadlessTaskState::enterHeadless`)
        Keep     ///< 이미 잡힌 기준선 안에서 고르는 규칙만 본다(테스트) — 표시만 한다(`markHeadless`)
    };

    /** @brief `HeadlessTaskRegistration::selectAndRun` 의 결과입니다. */
    enum class HeadlessTaskResult : uint8
    {
        NotRequested, ///< 청한 작업이 없다 — 보통 기동을 이어 간다
        Ran,          ///< 작업 하나를 돌렸다(성패는 상태의 `_bFailed`)
        Rejected,     ///< 이 호스트에서 돌지 않는 작업이 청해졌다 — 실패로 끝냈다
    };

    /**
     * @struct HeadlessTaskRegistration
     * @brief 등록 줄 하나와 그 레지스트리입니다(파일 머리말). 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다.
     *        정적 초기화와 기동 단계(게임 스레드)에서만 씁니다. 모듈을 내리면 그 모듈의 줄도 빠집니다.
     */
    struct SW_API HeadlessTaskRegistration
    {
        const utf8*                 _pName;    ///< 로그 · 중복 검사 이름
        int32                       _priority; ///< 여럿이 청해졌을 때 작을수록 먼저 고른다(`HeadlessTaskPriority`)
        HeadlessTaskHost            _host;     ///< 도는 호스트 — 그 밖의 호스트에 청해지면 기동이 실패한다
        HeadlessTaskRequestFunction _pIsRequested;
        HeadlessTaskRunFunction     _pRun;

        /** @brief 올립니다. 이름이 비었거나 같은 이름이 있으면 false 이고 올리지 않습니다. */
        [[nodiscard]] static bool registerTask( const HeadlessTaskRegistration* pRegistration );
        static void               unregisterTask( const HeadlessTaskRegistration* pRegistration );

        /**
         * @brief @p listRegistration 가운데 청해진 작업을 고릅니다 — 이 호스트에서 돌지 않는 것이 청해졌으면 거절, 아니면 우선순위가 가장 앞선 하나를 돌립니다.
         * @details 등록 목록과 떼어 둔 것은 테스트가 자기 줄로 고르는 규칙을 보게 하려는 것이다. 목록은 우선순위 순이어야 한다(`runRequested` 가 그 순서로 넘긴다).
         */
        static HeadlessTaskResult selectAndRun( span<const HeadlessTaskRegistration* const> listRegistration, HeadlessTaskContext& context, HeadlessLeakBaseline leakBaseline );
        /** @brief 등록된 작업으로 `selectAndRun` 합니다. */
        static HeadlessTaskResult runRequested( HeadlessTaskContext& context );
        /**
         * @brief 이 명령줄이 등록된 작업을 하나라도 청했으면 true 입니다 — `Headless` 단계보다 먼저 서는 것(오디오 장치 · 파일 감시 · 모듈 로드)이 헤드리스 실행인지 미리 본다.
         * @details 작업은 엔진 · 개발 도구 이미지에 있어 기동 단계를 채울 때 모두 올라와 있다(모듈이 올리는 헤드리스 작업은 없다 — 모듈 로드 뒤라 여기서 보이지 않는다).
         */
        [[nodiscard]] static bool isAnyRequested( const CommandLineManager& commandLine );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_HEADLESS_TASK` 가 두는 정적 등록자입니다(공통 틀 `StaticRegistrar`). */
    SW_DEFINE_STATIC_REGISTRAR( HeadlessTaskRegistrar, const HeadlessTaskRegistration*, &HeadlessTaskRegistration::registerTask, &HeadlessTaskRegistration::unregisterTask );
} // namespace sw

/**
 * @brief 헤드리스 작업 하나를 올립니다(.cpp 의 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다.
 * @param priority 작을수록 먼저 고른다(`HeadlessTaskPriority`)
 * @param host `HeadlessTaskHost` 의 값 이름(`Client` · `DedicatedServer` · `Any`)
 */
#define SW_HEADLESS_TASK( id, pName, priority, host, pIsRequested, pRun ) \
    SW_STATIC_REGISTRATION( headlessTask, ::sw::HeadlessTaskRegistration, ::sw::HeadlessTaskRegistrar, id, pName, priority, ::sw::HeadlessTaskHost::host, pIsRequested, pRun )
