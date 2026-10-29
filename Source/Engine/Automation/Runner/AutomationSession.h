/**
 * @file AutomationSession.h
 * @brief `-scenario=a,b,c` 한 번의 실행을 소유합니다. 시나리오 순서(`AutomationScenarioChain`)와 지금 단계의 실행기(`AutomationRunner`)를 들고, 단계가 끝나면 다음 단계를 시작하고, 끝나면 종료 코드를 냅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Automation/AutomationRunTitle.h"
#include "Engine/Automation/Runner/AutomationOutcome.h"
#include "Engine/Automation/Scenario/ScenarioPlayback.h"
#include "Engine/Automation/Scenario/ScenarioRecorder.h"
#include "Engine/EngineFrameObserver.h"

namespace sw
{
    class AutomationRunner;
    class AutomationScenarioChain;

    /**
     * @class AutomationSession
     * @brief 엔진 루프에 프레임 관찰자(`IEngineFrameObserver`)로 붙는 시나리오 구동기입니다. 루프는 관찰자 목록만 알고, 세션 · 실행기 · 체인 · 단계 전환은 모릅니다.
     * @details 호스트 범위의 서브시스템입니다(App 이 엔진을 세우기 전에 걸고, 전용 서버는 시나리오를 쓰지 않아 걸지 않는다). 서브시스템으로서의 초기화는 하는 일이 없고,
     *          `-scenario` 를 읽은 런처가 엔진이 선 뒤에 `begin` 으로 시작해 `Engine::registerFrameObserver` 로 붙입니다. 실행기는 엔진 서비스(로그 리스너 · 입력)를 빌려 쓰므로
     *          엔진이 내려가기 전에 `shutdownSubsystem` 이 비웁니다 — 호스트 범위는 엔진보다 오래 살기 때문에 런처가 엔진 종료 앞에서 이 서브시스템만 먼저 내립니다
     *          (모듈 라이브러리 창구의 뒤에 서는 서브시스템이라 `shutdownDependentsOf` 로 닿는다). 한 단계가 끝나면(통과해야 다음으로 간다 — `AutomationScenarioChain`) 같은 프로세스에서 다음 시나리오의 실행기를 새로 만든다. 그 단계가 시작할 때
     *          - 프로세스의 **작업 폴더**가 처음과 같은지 보고(파일 대화상자 같은 것이 옮긴다) 다르면 경고하고 되돌린다,
     *          - `gv_fixedFrameDelta` 를 그 시나리오의 `fixedDelta` 로 맞추고,
     *          - 렌더 스레드가 지금까지 쓴 시나리오 스크린샷 수를 기준선으로 잡는다(실행기는 자기 단계가 요청한 것만 기다린다 — 렌더 스레드의 수는 프로세스 전체로 누적된다),
     *          - 시작할 때 모은 재현 명령 재료(게임 · 백엔드 · 에디터 · `-scenario` 목록)를 단계 실행기에 넘긴다(보고의 `reproduce`).
     *          **재생**(`IScenarioPlayer` — `ScenarioPlayback::play`)도 같은 실행기로 한다: 판정이 끝나도 프로세스를 끝내지 않고, 행 감시 · 보고 · 고정 프레임 시간(고르지 않으면)이 없다.
     *          끝까지 돌면 반복하거나(`_bLoop`) 끝을 알린다. 사람이 키보드 · 마우스를 건드리면 멈출 수 있다(`_bStopOnHumanInput` — 데모 · 어트랙트).
     *          게임 스레드에서만 쓴다.
     */
    class SW_API AutomationSession : public IEngineFrameObserver, public ISubsystem, public IScenarioPlayer
    {
    public:
        /** @brief 서브시스템 이름입니다(같은 범위의 다른 서브시스템이 의존 칸에 적는다). */
        static constexpr const utf8* kSubsystemName = "Automation";

        AutomationSession();
        ~AutomationSession() override;
        AutomationSession( const AutomationSession& )            = delete;
        AutomationSession& operator=( const AutomationSession& ) = delete;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        /** @brief 지금 단계의 실행기와 체인을 비웁니다(엔진 서비스를 빌려 쓰는 것이 엔진보다 먼저 사라지게). 시작하지 않았으면 아무것도 하지 않습니다. */
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /**
         * @brief 모듈 라이브러리 창구(`HostSubsystemNames::kModuleLoader`)보다 뒤에 섭니다 — 시나리오는 모듈이 올라온 뒤에 시작하고,
         *        `shutdownDependentsOf( 창구 )` 가 구동기만 엔진 종료 앞에서 내립니다(창구는 엔진 범위가 종료하는 동안 살아 있어야 한다). 창구는 구동기를 모릅니다.
         */
        span<const utf8* const> getSubsystemDependencies() const override;

        /**
         * @brief 시나리오 목록(`a,b,c` 또는 하나)으로 시작하고 첫 단계를 엽니다.
         * @param scenarioArgument `-scenario=` 값(쉼표로 가른다)
         * @param reportPath `-scenario-report=` 값(비면 보고 파일을 쓰지 않는다. 단계가 여럿이면 단계마다 번호가 붙는다)
         * @param bKeepGoing `-scenario-keep-going` — 실패한 단계 뒤에도 이어 돌린다
         * @param completedScreenshotCount 지금까지 렌더 스레드가 쓴 시나리오 스크린샷 수
         * @return 돌릴 시나리오가 하나라도 있으면 true 입니다(없으면 아무것도 하지 않고 체인도 남기지 않는다).
         */
        [[nodiscard]] bool begin( string_view scenarioArgument, string_view reportPath, bool bKeepGoing, uint32 completedScreenshotCount );
        /**
         * @brief 플레이를 시나리오로 녹화합니다(`-scenario-record=<Saved/Automation/...scenario.xml>`). 활성 씬이 플레이를 시작한 첫 프레임부터 입력을 적고,
         *        시나리오가 끝나거나 창이 닫힐 때 파일을 씁니다.
         * @param watchList `-scenario-record-watches=` 값 — 쉼표로 가른 검사점(`감시 값`, `감시 값@대상`, `감시 값@대상@키`). 끝 프레임의 값이 `<Expect>` 로 실려 녹화가 스스로를 검증한다.
         * @details 시나리오와 함께 돌리면(`-scenario`) 시나리오가 낸 가상 입력도 적는다 — 시나리오 → 녹화 → 재생의 왕복으로 결정적 재생을 확인한다. 시작 조건은 시나리오의 `ScenePlaying` 과 같은 프레임이다.
         * @return 경로가 `Saved/Automation/` 아래의 `.scenario.xml` 이 아니면 false 입니다.
         */
        [[nodiscard]] bool beginRecording( string_view outputPath, string_view watchList );
        // IScenarioPlayer — 게임 · 콘솔의 재생
        [[nodiscard]] bool play( const AssetID& scenarioPath, const ScenarioPlayOptions& options ) override;
        void               stopPlayback() override;
        bool               isPlaying() const override { return _bPlayback; }

        /** @brief 시나리오나 녹화가 돌고 있으면 true 입니다(프레임 관찰자로 붙일지 런처가 본다). */
        bool isBusy() const { return _pRunner != nullptr || _recordPath.empty() == false; }

        /** @brief 프레임 시작(입력 `beginFrame` 전)입니다. 지금 단계의 시작 조건 · 가상 입력 붙이기 · 입력 전 단계를 돌립니다. 입력이 없으면 아무것도 하지 않습니다. */
        void onFrameBegin( const EngineFrameContext& context ) override;
        /**
         * @brief 프레임 끝(씬 틱과 렌더 제출 뒤, 입력 프레임을 닫기 전)입니다. 문맥의 스크린샷 수(프로세스 전체)에서 단계 기준선을 빼 실행기에 넘깁니다.
         * @return 체인이 끝났으면 프로세스 종료 코드, 아니면 `kRunning` 입니다(입력이 없어도 `kRunning`).
         */
        [[nodiscard]] int32 onFrameEnd( const EngineFrameContext& context ) override;
        /** @brief 돌고 있는 시나리오 단계의 이름 · 단계 · 프레임입니다(창 제목). 돌고 있지 않으면 false 입니다. */
        [[nodiscard]] bool tryGetRunStatus( string& outScenarioName, string& outStepKind, uint32& outStepNumber, uint32& outFrame ) const override;
        /**
         * @brief 창이 닫혀 루프가 끝났습니다. 지금 단계를 그 결과로 끝맺고, 돌릴 단계가 남았으면 실패로 봅니다(`CloseWindow` 는 마지막 단계에만 둔다).
         * @return 프로세스 종료 코드, 시작한 단계가 없으면 `kRunning` 입니다.
         */
        [[nodiscard]] int32 onWindowClosed( const EngineFrameContext& context ) override;
        /** @brief `<Screenshot>` 이 요청한 경로를 하나 꺼내 이번 패킷에 싣습니다(앞 관찰자가 이미 채웠으면 다음 패킷으로 미룬다). */
        void onBuildRenderPacket( EngineRenderPacketContext& inoutContext ) override;

    private:
        /** @brief 체인의 지금 단계 시나리오를 새 실행기로 시작합니다. */
        void startStage( uint32 completedScreenshotCount );
        /** @brief 단계 하나가 끝났을 때 부릅니다(`AutomationOutcome`). 통과했고 다음 단계가 있으면 그것을 시작하고 `kRunning`, 아니면 체인의 종료 코드(`getAutomationExitCode`)입니다. */
        [[nodiscard]] int32 onStageEnded( AutomationOutcome result, uint32 completedScreenshotCount );
        /** @brief 재생 실행기를 새로 세웁니다(처음 · 반복). */
        void startPlayback();
        /** @brief 재생의 프레임 끝입니다 — 사람 입력 · 단계 결과를 보고 반복 · 끝을 정한다. 늘 `kRunning`(재생은 프로세스를 끝내지 않는다). */
        int32 updatePlayback( const EngineFrameContext& context );
        /** @brief 재생을 끝맺고(돌고 있으면 멈추고) 고정 프레임 시간을 되돌린 뒤 알린다. */
        void endPlayback( ScenarioPlayEnd end, InputManager* pInput );
        /** @brief 녹화를 시작할 프레임인지 보고(활성 씬이 플레이 중이고 시작 차단이 없다) 시작합니다. */
        void startRecordingWhenReady();
        /** @brief 녹화를 멈추고 검사점을 읽어 파일을 씁니다. 녹화 중이 아니면 아무것도 하지 않습니다. */
        void finishRecording();

        unique_ptr<AutomationRunner>        _pRunner;
        unique_ptr<AutomationScenarioChain> _pChain;
        ScenarioRecorder                    _recorder;
        vector<string>                      _listRecordWatch;           ///< `-scenario-record-watches` 의 검사점
        string                              _recordPath;                ///< `-scenario-record` 경로(비면 녹화하지 않는다)
        string                              _reportPath;                ///< `-scenario-report` 원본(단계마다 `AutomationScenarioChain::makeStageReportPath`)
        string                              _workingFolder;             ///< 시작할 때의 작업 폴더(단계마다 이 폴더에서 시작한다)
        AutomationReproduceInput            _reproduceInput;            ///< 재현 명령 재료(게임 · 시작 때 백엔드 · 에디터 · 시나리오 목록) — 단계마다 실행기에 넘긴다
        uint32                              _screenshotBaseline;        ///< 단계가 시작할 때 렌더 스레드가 이미 쓴 스크린샷 수
        ScenarioPlayOptions                 _playOptions{};             ///< 재생 선택(재생 중일 때만 뜻이 있다)
        AssetID                             _playPath{};                ///< 재생하는 시나리오
        string                              _playPreviousFixedDelta{};  ///< 재생이 고정 프레임 시간을 걸기 전 값(끝나면 되돌린다)
        uint64                              _playHumanInputSerial{ 0 }; ///< 재생을 시작할 때의 사람 입력 수
        bool                                _bPlayback{ false };        ///< 재생 중(테스트 실행이 아니다)
    };
} // namespace sw
