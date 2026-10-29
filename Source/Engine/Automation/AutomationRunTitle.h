/**
 * @file AutomationRunTitle.h
 * @brief 자동화 시나리오 · 벤치 실행 정보 한 줄(창 제목과 로그가 같이 쓰는 글)을 만듭니다.
 * @details 창과 로그를 모르는 순수 함수라 단위 테스트(`SW_TEST_CASE`)로 글자만 확인할 수 있습니다. 사용자가 캡처한 화면에서 어느 시나리오 · 백엔드 · 단계 · 프레임인지 읽으려는 것이 목적입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @struct AutomationRunTitleInput @brief 실행 정보 한 줄을 만들 재료입니다. */
    struct AutomationRunTitleInput
    {
        string_view _baseTitle{};    ///< 평소 창 제목(팩 설정의 창 제목). 비면 제목 앞부분 없이 정보만 쓴다
        string_view _scenarioName{}; ///< 시나리오 이름. 비면 시나리오가 아니라 벤치 실행이다
        string_view _backendName{};  ///< dx12 · dx11 · vulkan · gl 같은 백엔드 이름. 비면 적지 않는다(헤드리스)
        string_view _stepKind{};     ///< 지금 단계 이름(`Expect` 등). 비면 단계 칸을 적지 않는다
        string_view _benchSummary{}; ///< 벤치 인자 요약(`gv_stressTestMeshes=1000 …`). 시나리오가 없을 때만 쓴다
        uint32      _stepNumber{ 0 };
        uint32      _frame{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AutomationReproduceInput
     * @brief 이 실행을 다시 띄우는 App 명령줄(재현 명령)의 재료입니다. 보고 JSON 의 `reproduce` 가 이것으로 만든 글입니다.
     * @details 원본 인자(`CommandLineManager::getOriginalArguments`)를 그대로 옮기고, 보고 · 녹화 경로처럼 결과를 바꾸지 않는 인자는 빼며, 이름에 token · secret · password 같은 조각이 든
     *          인자의 값은 `REDACTED` 로 가린다(규칙은 `AutomationRunTitle.cpp` 의 `AutomationReproduceRules` 한 곳). 원본에 없는 것만 실행기가 아는 값(게임 · 실제 백엔드 · 에디터 ·
     *          시나리오 · `<Launch>` 변수)으로 채운다 — 세션 없이 돈 실행기는 원본이 비어 이 값만으로 짓는다.
     */
    struct AutomationReproduceInput
    {
        vector<string> _listOriginalArgument{}; ///< 프로그램 이름을 뺀 원본 인자(앞의 `-` 까지 든 글)

        string         _game{};              ///< `-game=` 값. 비면 적지 않는다
        string         _backendName{};       ///< RHI 백엔드 이름(`DirectX12` · `Vulkan` …, `-gv_rhiBackend=` 값). 비면 적지 않는다(창 없는 호스트)
        string         _scenarioArgument{};  ///< `-scenario=` 값(쉼표 목록 그대로). 비면 재현 명령을 만들지 않는다
        vector<string> _listExtraArgument{}; ///< `-gv_name=value` 처럼 앞의 `-` 까지 든 인자
        bool           _bEditor{ false };
        bool           _bKeepGoing{ false };
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
        /** @brief `gv_stressTest` 로 시작하는 `이름=값` 글만 골라 이름순으로 공백으로 이어 한 줄로 씁니다. */
        [[nodiscard]] static string makeBenchSummary( vector<string> listPair );
        /**
         * @brief 재현 명령 `App.exe -game=<게임> -gv_rhiBackend=<백엔드> [-EnableEditor] "-scenario=<목록>" [-scenario-keep-going] <추가 인자>` 를 씁니다. 시나리오 인자가 없으면 빈 글입니다.
         * @details 공백이나 점이 든 인자는 큰따옴표로 감싼다(PowerShell 은 점이 든 인자를 쪼갠다).
         */
        [[nodiscard]] static string makeReproduceCommand( const AutomationReproduceInput& input );
        /** @brief 글이 @p maxBytes 를 넘으면 UTF-8 글자 경계에서 자르고 `…` 를 붙입니다. */
        [[nodiscard]] static string truncateUTF8( string_view text, uint32 maxBytes );
    };
} // namespace sw

namespace sw
{
    class CommandLineManager;
    class IEngineFrameObserver;
    class IWindowTitleTarget;

    /**
     * @class AutomationRunTitleTracker
     * @brief 엔진이 프레임 끝마다 부르면, 시나리오 단계가 바뀔 때와 `kTitleRefreshFrames` 프레임마다만 창 제목을 바꿉니다(바뀔 때만 시스템 호출).
     * @details 시나리오가 없고 벤치 인자(`-gv_stressTest*`)만 있으면 벤치 요약을 적습니다. 처음 부를 때 벤치 인자가 있으면 `[Bench]` 로그 한 줄을 남깁니다.
     *          창 제목을 거는 곳은 여기 하나입니다. 평소 제목은 `MainWindowSubsystem::setBaseTitle`(에디터가 프레임마다 씬 · 미저장 표시를 넣어 정한다)이고,
     *          정한 적이 없으면 창이 처음 가진 제목입니다. 실행 중이면 그 뒤에 `AutomationRunTitleUtil::makeWindowTitle` 로 실행 정보를 붙이고, 아니면 평소 제목 그대로 겁니다.
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
         * @param pTitleTarget 제목을 거는 이음매(`MainWindowSubsystem::getTitleTarget`, 없으면 nullptr — 창 없는 실행)
         * @param pCommandLine 명령줄(벤치 인자를 읽는다. 없으면 nullptr)
         * @param backendName 지금 RHI 백엔드 이름(없으면 빈 글)
         * @param listObserver 프레임 관찰자(시나리오 세션이 실행 정보를 낸다)
         */
        void onFrameEnd( IWindowTitleTarget* pTitleTarget, const CommandLineManager* pCommandLine, string_view backendName, const vector<IEngineFrameObserver*>& listObserver );
        /** @brief 처음 상태로 돌아가며 프레임마다 모은 제목 · 요약 글을 저장소까지 놓습니다. 엔진 종료가 종료 누수 창이 닫히기 전에 부릅니다(트래커는 엔진 멤버라 그보다 오래 산다). */
        void reset();

    private:
        string _initialTitle; ///< 창이 처음 가진 제목(호스트가 평소 제목을 정하지 않았을 때의 평소 제목)
        string _lastBase;     ///< 마지막으로 창에 건 때의 평소 제목(바뀌면 바로 다시 건다)
        string _benchSummary;
        string _lastStepKind;
        uint32 _frame;
        uint32 _lastStepNumber;
        bool   _bInitialized;
        bool   _bRunShown; ///< 지금 창 제목에 실행 정보가 붙어 있다
    };
} // namespace sw
