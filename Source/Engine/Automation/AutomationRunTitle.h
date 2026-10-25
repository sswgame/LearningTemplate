/**
 * @file AutomationRunTitle.h
 * @brief 자동화 시나리오 · 벤치 실행 정보 한 줄(창 제목과 로그가 같이 쓰는 글)을 만듭니다.
 * @details 창과 로그를 모르는 순수 함수라 `Test/EngineTest` 가 글자만 테스트합니다. 사용자가 캡처한 화면에서 어느 시나리오 · 백엔드 · 단계 · 프레임인지 읽으려는 것이 목적입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @struct AutomationRunTitleInput @brief 실행 정보 한 줄을 만들 재료입니다. */
    struct AutomationRunTitleInput
    {
        string_view _baseTitle{};    ///< 평소 창 제목(팩 설정의 창 제목). 비면 제목 앞부분 없이 정보만 쓴다
        string_view _scenarioName{}; ///< 시나리오 이름. 비면 시나리오가 아니라 벤치 실행이다
        string_view _backendName{};  ///< dx12 · dx11 · vulkan · gl 같은 백엔드 이름. 비면 적지 않는다(헤드리스)
        string_view _stepKind{};     ///< 지금 단계 이름(`Expect` 등). 비면 단계 칸을 적지 않는다
        string_view _benchSummary{}; ///< 벤치 인자 요약(`gv_benchMeshes=1000 …`). 시나리오가 없을 때만 쓴다
        uint32      _stepNumber{ 0 };
        uint32      _frame{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationRunTitleUtil @brief 실행 정보 글 만들기입니다. */
    struct SW_API AutomationRunTitleUtil
    {
        /** @brief 창 제목 글 길이의 위쪽 한계(바이트)입니다. 넘으면 `…` 로 줄입니다(`IWindow::kTitleCapacity` 보다 작다). */
        static constexpr uint32 kMaxTitleBytes = 200;
        /** @brief 창 제목을 몇 프레임마다 다시 만들지 정합니다(단계가 바뀔 때는 바로 만든다). */
        static constexpr uint32 kTitleRefreshFrames = 30;

        /** @brief `<시나리오> · <백엔드> · step <번호> <단계> · f<프레임>` (벤치면 `bench <인자> · <백엔드> · f<프레임>`). 창 제목 · 로그가 같이 씁니다. */
        [[nodiscard]] static string makeSummary( const AutomationRunTitleInput& input );
        /** @brief `<기본 제목> — <요약>` 을 `kMaxTitleBytes` 안으로 줄여 씁니다(UTF-8 글자 중간을 자르지 않는다). */
        [[nodiscard]] static string makeWindowTitle( const AutomationRunTitleInput& input );
        /** @brief 단계 전환 로그 한 줄 `[Scenario] <이름> step <번호> <단계> frame <F>` 을 씁니다. */
        [[nodiscard]] static string makeStepLogLine( string_view scenarioName, uint32 stepNumber, string_view stepKind, uint32 frame );
        /** @brief `gv_bench` 로 시작하는 `이름=값` 글만 골라 이름순으로 공백으로 이어 한 줄로 씁니다. */
        [[nodiscard]] static string makeBenchSummary( vector<string> listPair );
        /** @brief 글이 @p maxBytes 를 넘으면 UTF-8 글자 경계에서 자르고 `…` 를 붙입니다. */
        [[nodiscard]] static string truncateUtf8( string_view text, uint32 maxBytes );
    };
} // namespace sw

namespace sw
{
    class CommandLineManager;
    class IEngineFrameObserver;
    class IWindow;

    /**
     * @class AutomationRunTitleTracker
     * @brief 엔진이 프레임 끝마다 부르면, 시나리오 단계가 바뀔 때와 `kTitleRefreshFrames` 프레임마다만 창 제목을 바꿉니다(바뀔 때만 시스템 호출).
     * @details 시나리오가 없고 벤치 인자(`-gv_bench*`)만 있으면 벤치 요약을 적습니다. 처음 부를 때 벤치 인자가 있으면 `[Bench]` 로그 한 줄을 남깁니다.
     *          제목의 주인이 따로 있으면(에디터가 프레임마다 제목을 건다) 건드리지 않습니다 — 지금 제목이 처음 제목도 내가 건 제목도 아니면 물러납니다.
     *          창이 없는 호스트(헤드리스 · 전용 서버)는 창 포인터가 nullptr 이라 제목 갱신만 건너뛰고 로그는 남깁니다. 게임 스레드에서만 씁니다.
     */
    class SW_API AutomationRunTitleTracker
    {
    public:
        AutomationRunTitleTracker();
        ~AutomationRunTitleTracker();

        AutomationRunTitleTracker( const AutomationRunTitleTracker& )            = delete;
        AutomationRunTitleTracker& operator=( const AutomationRunTitleTracker& ) = delete;

        /**
         * @param pWindow 메인 창(없으면 nullptr)
         * @param pCommandLine 명령줄(벤치 인자를 읽는다. 없으면 nullptr)
         * @param backendName 지금 RHI 백엔드 이름(없으면 빈 글)
         * @param listObserver 프레임 관찰자(시나리오 세션이 실행 정보를 낸다)
         */
        void onFrameEnd( IWindow* pWindow, const CommandLineManager* pCommandLine, string_view backendName, const vector<IEngineFrameObserver*>& listObserver );

    private:
        string _baseTitle;
        string _lastTitle;
        string _benchSummary;
        string _lastStepKind;
        uint32 _frame;
        uint32 _lastStepNumber;
        bool   _bInitialized;
    };
} // namespace sw
