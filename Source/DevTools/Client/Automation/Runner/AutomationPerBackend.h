/**
 * @file AutomationPerBackend.h
 * @brief 시나리오 하나를 RHI 백엔드마다 자식 App 프로세스로 동시에 돌리고 결과를 하나로 모읍니다(`-scenario=<경로> -scenario-backends=all|dx11,vk`).
 * @details 언리얼 Gauntlet 의 컨트롤러 자리다 — 이 프로세스는 창 · GPU 를 세우지 않는 헤드리스 작업으로 서고, 자식마다 `-<백엔드>` 를 붙여 띄운 뒤 기다린다.
 *          시나리오 상태는 프로세스로 끊긴다(백엔드 사이에 씬 · 전역 변수 · 장치가 이어지지 않는다). 골든이 백엔드별이라 자식마다 제 골든과 견준다.
 *          자식의 로그는 `-log-file` 로 따로 쓴다 — 부모가 `-log-file=X.log` 를 받았으면 `X.<백엔드>.log`, 아니면 `Saved/Automation/<시나리오>/matrix-<프로세스>/<백엔드>.log`.
 *          보고(`-scenario-report=R.json`)를 받았으면 자식은 `R.<백엔드>.json`, 부모는 `R.json` 에 백엔드별 결과를 쓴다. 받지 않아도 자식은 기본 폴더의 `<백엔드>.json` 에 쓴다 —
 *          부모가 그 보고의 `records[]`(`<Record>`)를 시나리오마다 백엔드 사이에 견주고(`findRecordMismatches`), 하나라도 다르면 실행이 실패(10)다. 언리얼 자동화의 결정성 대조 자리다.
 *          자식 창은 2 열 바둑판으로 놓는다(`-WinTile` · `-WinTileColumns` — 칸 간격은 자식 창이 제 틀 두께로 정한다, 넘겨받은 인자에 `-WinX` · `-WinTile` 이 있으면 두지 않는다) — 백엔드마다 화면이 겹치지 않고 한 번에 보인다.
 *          자식마다 실행 뿌리(`<이 실행 뿌리>/<백엔드>` — `ProcessRoots::_run`, 환경 `SW_RUN_ROOT`)를 비워 준다 — 자식의 상태(사용자 데이터 · 에디터 상태 · 시나리오 사본)가 모두 그 아래다.
 *          자식은 로그가 정지 시한(`AutomationTimeLimits::kProcessStallSeconds`) 넘게 자라지 않거나 프로세스 시한(`kProcessKillSeconds` × 체인의 단계 수)을 넘기면 죽이고 시간 초과(12)로 센다.
 *          종료 코드: 하나라도 실패면 표 순서로 첫 실패 백엔드의 코드, 아니면 하나라도 통과면 0, 모두 건너뜀이면 13. 이 기계에서 못 도는 백엔드(77)는 건너뜀으로 센다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /**
     * @enum AutomationBackendSplit @brief 체인의 시나리오를 백엔드에 어떻게 나눠 줄지입니다(`-scenario-backend-split=independent|all`).
     * @details Independent 는 판정이 백엔드에 달린 시나리오만 모두에게 주고, All 은 모든 시나리오를 돌아가며 하나에게 준다 — 그림 · 기록 대조는 모든 백엔드를 도는
     *          구성(Release)이 맡고, 검사 빌드(Debug)는 단언 · 검사를 시나리오마다 한 번 지나는 것이 목적이라 All 이다(언리얼 CI 가 Debug 를 RHI 하나로 도는 것과 같다).
     */
    enum class AutomationBackendSplit : uint8
    {
        None,        ///< 모든 시나리오를 모든 백엔드에서
        Independent, ///< 백엔드와 무관한 시나리오만 하나씩
        All,         ///< 모든 시나리오를 하나씩
    };

    /** @struct AutomationPerBackendOptions @brief 백엔드마다 실행 하나의 입력입니다. */
    struct AutomationPerBackendOptions
    {
        AbsolutePath           _executablePath{};                      ///< 자식으로 띄울 App(이 프로세스의 실행 파일)
        string                 _scenarioPath{};                        ///< `-scenario` 의 값 그대로
        vector<string>         _listBackend{};                         ///< 명령줄 이름(`dx11` · `vk` …) — 쿠킹 표 순서
        vector<string>         _listPassThroughArgument{};             ///< 자식에 그대로 넘길 원본 인자(`-unattended` · `-game=` …) — 백엔드 목록 · 로그 · 보고 · 백엔드 인자는 뺐다
        AbsolutePath           _logFilePath{};                         ///< 부모의 `-log-file`(비면 기본 폴더)
        AbsolutePath           _reportPath{};                          ///< 부모의 `-scenario-report`(비면 쓰지 않는다)
        AbsolutePath           _shaderRequestLogPath{};                ///< 부모의 `-shader-request-log`(비면 기록하지 않는다) — 자식마다 `.<백엔드>` 를 붙여 넘긴다(자식이 한 파일을 덮지 않게)
        int32                  _splitOffset{ -1 };                     ///< `-scenario-backend-split-offset` — 나눌 때 첫 시나리오를 맡을 백엔드. -1 이면 `run` 이 UTC 날짜로 정한다
        AutomationBackendSplit _split{ AutomationBackendSplit::None }; ///< `-scenario-backend-split` — 백엔드 하나에만 줄 시나리오(`assignScenarios`)
        int32                  _slotCount{ 0 };                        ///< `-scenario-backend-slots` — 동시에 도는 자식 수의 상한. 0 이면 모두 함께 띄운다
        string                 _userDataRoot{};                        ///< 부모의 사용자 데이터 뿌리(`AutomationUserDataRoot::resolve`) — 비지 않으면 자식마다 제 실행 뿌리의 `userdata` 를 건다. 비면(사람이 OS 폴더를 고름) 환경을 그대로 물려준다
    };

    /** @struct AutomationBackendRecords @brief 한 시나리오를 한 백엔드에서 돌린 기록 줄(자식 보고의 `records[]`, 적은 순서)입니다. */
    struct AutomationBackendRecords
    {
        string         _backend{};
        vector<string> _listRecordLine{};
    };

    /** @struct AutomationStageFailure @brief 한 백엔드에서 진 단계 하나의 판정(자식 단계 보고의 `name` · `failure`)입니다 — 부모 요약(`foldFailures`)의 재료입니다. */
    struct AutomationStageFailure
    {
        string _stage{};   ///< 시나리오 이름(보고의 `name`)
        string _backend{}; ///< 명령줄 백엔드 이름(`dx12`)
        string _kind{};    ///< 실패 종류(`AutomationFailureKind` 이름 — 보고가 없으면 종료 코드 이름)
        string _reason{};  ///< 까닭 한 줄
    };

    /** @class AutomationPerBackend @brief 백엔드마다 실행입니다. 게임 스레드가 아닌 헤드리스 작업에서 부릅니다. */
    class SW_DEVTOOLS_API AutomationPerBackend
    {
    public:
        /**
         * @brief `all` 이나 `dx11,vk` 를 쿠킹 표 순서의 명령줄 이름 목록으로 바꿉니다. 모르는 이름 · 빈 목록 · 겹친 이름이면 그 이름이 대상인 오류입니다.
         */
        [[nodiscard]] static expected<vector<string>> parseBackendList( string_view text );

        /** @brief `-scenario-backend-split` 의 값(`independent` · `all`)을 읽습니다. 다른 값(빈 값 포함)은 그 값이 대상인 오류입니다. */
        [[nodiscard]] static expected<AutomationBackendSplit> parseBackendSplit( string_view text );

        /**
         * @brief 백엔드마다(@p options 의 `_listBackend` 순서) 돌릴 `-scenario` 목록입니다. 나누지 않으면 모두 같은 목록입니다.
         * @details Independent 로 나누면 판정이 백엔드에 달린 단계(`<ExpectImage>` · 마젠타 문턱이 있는 `<CaptureEditorUI>` · `<EditorSelfTest>` · 백엔드 사이에 견주는 `<Record>` · 렌더러 감시 값 `RHI.*` · `GPU.*` · `Render.*` …)가 있는 시나리오는 모두에게,
         *          나머지는 순서대로 돌아가며 하나에게 준다. All 이면 모든 시나리오를 돌아가며 하나에게 준다. 체인의 순서는 그대로다.
         *          읽지 못하는 시나리오도 모두에게 준다 — 자식마다 읽기 오류를 낸다.
         */
        static vector<string> assignScenarios( const AutomationPerBackendOptions& options );

        /**
         * @brief 자식이 맡은 목록(@p scenarioList)의 첫 시나리오가 적은 `<Launch>` 값의 인자(`-gv_이름=값`)입니다. 게임이 기동하며 읽는 값이라 명령줄로 주고, 다음 단계의 값은 세션이 건다.
         * @details 자식마다 첫 시나리오가 다르다(나누기). 예전에는 ctest 가 체인의 첫 시나리오 값을 적어 모든 자식이 물려받아, 그 시나리오를 맡지 않은 자식이 남의 미니게임으로 섰다.
         *          @p listGivenArgument 에 이미 있는 이름(사람이 준 값)은 내지 않는다.
         */
        static vector<string> makeLaunchArguments( string_view scenarioList, const vector<string>& listGivenArgument );

        /** @brief 원본 인자에서 부모가 자식마다 다시 정하는 인자(백엔드 목록 · 로그 파일 · 보고 · 백엔드 선택)를 뺀 목록입니다. */
        static vector<string> filterPassThroughArguments( const vector<string>& listOriginalArgument );

        /**
         * @brief 같은 시나리오의 백엔드별 기록(@p listRecords)을 순번마다 견주어, 다른 순번마다 한 줄을 돌려줍니다(모두 같거나 백엔드가 하나면 빈 목록).
         * @details 줄 모양: `record #2 'frame 300 Nile.Money': dx11=120 dx12=120 vk=118 gl=120`. 이름(` = ` 앞)이 다른 백엔드는 줄 전체를, 그 순번이 없는 백엔드는 `(none)` 을 적는다.
         */
        static vector<string> findRecordMismatches( const vector<AutomationBackendRecords>& listRecords );

        /**
         * @brief 진 단계들을 (단계 · 종류 · 까닭)이 같은 것끼리 백엔드로 접은 요약 줄입니다(처음 나온 순서). 줄 모양: `vfxsort: GoldenMismatch dx11,dx12,vk,gl - <까닭>`.
         * @details 모든 백엔드가 같은 까닭으로 진 것(시나리오의 전제)과 한 백엔드만 진 것(백엔드 결함)이 한눈에 갈린다. `preflight.sh` 가 단계 끝마다 이 줄을 찍는다.
         */
        static vector<string> foldFailures( const vector<AutomationStageFailure>& listStageFailure );

        /**
         * @brief 부모의 끝 줄입니다 — `SCENARIO_RESULT=<PASSED|FAILED|SKIPPED|TIMEOUT|LOAD_ERROR> stages=… failed=… notRun=…`(`WINDOWS_OK=` 와 같은 꼴, 스크립트가 읽는다).
         * @param exitCode 부모가 낼 종료 코드(`run` 의 값) — 결과 이름은 이것이 정한다
         */
        static string makeResultLine( int32 exitCode, uint32 stageCount, uint32 failedCount, uint32 notRunCount );

        /** @brief 자식 경로 @p path 의 확장자 앞에 `.<백엔드>` 를 끼웁니다(`a/b.log` → `a/b.dx12.log`, 확장자가 없으면 끝에 붙인다). */
        static AbsolutePath insertBackendSuffix( const AbsolutePath& path, string_view backend );

        /**
         * @brief 백엔드마다 자식을 동시에 띄우고 모두 끝나기를 기다린 뒤 모은 종료 코드를 돌려줍니다(파일 머리 주석의 규칙).
         * @details 끝에 진 단계를 백엔드로 접은 요약(`foldFailures`)을 로그에 남기고, 표준 출력의 마지막 줄로 `makeResultLine` 을 쓴다.
         */
        [[nodiscard]] static int32 run( const AutomationPerBackendOptions& options );
    };
} // namespace sw
