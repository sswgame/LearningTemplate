/**
 * @file AutomationPerBackend.h
 * @brief 시나리오 하나를 RHI 백엔드마다 자식 App 프로세스로 동시에 돌리고 결과를 하나로 모읍니다(`-scenario=<경로> -scenario-backends=all|dx11,vk`).
 * @details 언리얼 Gauntlet 의 컨트롤러 자리다 — 이 프로세스는 창 · GPU 를 세우지 않는 헤드리스 작업으로 서고, 자식마다 `-<백엔드>` 를 붙여 띄운 뒤 기다린다.
 *          시나리오 상태는 프로세스로 끊긴다(백엔드 사이에 씬 · 전역 변수 · 장치가 이어지지 않는다). 골든이 백엔드별이라 자식마다 제 골든과 견준다.
 *          자식의 로그는 `-log-file` 로 따로 쓴다 — 부모가 `-log-file=X.log` 를 받았으면 `X.<백엔드>.log`, 아니면 `Saved/Automation/<시나리오>/matrix-<프로세스>/<백엔드>.log`.
 *          보고(`-scenario-report=R.json`)를 받았으면 자식은 `R.<백엔드>.json`, 부모는 `R.json` 에 백엔드별 결과를 쓴다. 받지 않아도 자식은 기본 폴더의 `<백엔드>.json` 에 쓴다 —
 *          부모가 그 보고의 `records[]`(`<Record>`)를 시나리오마다 백엔드 사이에 견주고(`findRecordMismatches`), 하나라도 다르면 실행이 실패(10)다. 언리얼 자동화의 결정성 대조 자리다.
 *          자식 창은 2 열 바둑판으로 놓는다(`-WinX` · `-WinY` — 넘겨받은 인자에 이미 있으면 두지 않는다) — 백엔드마다 화면이 겹치지 않고 한 번에 보인다.
 *          자식은 프로세스 시한(`AutomationTimeLimits::kProcessKillSeconds` × 체인의 단계 수) 안에 끝나야 한다 — 넘기면 죽이고 시간 초과(12)로 센다.
 *          종료 코드: 하나라도 실패면 표 순서로 첫 실패 백엔드의 코드, 아니면 하나라도 통과면 0, 모두 건너뜀이면 13. 이 기계에서 못 도는 백엔드(77)는 건너뜀으로 센다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @struct AutomationPerBackendOptions @brief 백엔드마다 실행 하나의 입력입니다. */
    struct AutomationPerBackendOptions
    {
        string         _executablePath{};          ///< 자식으로 띄울 App(이 프로세스의 실행 파일)
        string         _scenarioPath{};            ///< `-scenario` 의 값 그대로
        vector<string> _listBackend{};             ///< 명령줄 이름(`dx11` · `vk` …) — 쿠킹 표 순서
        vector<string> _listPassThroughArgument{}; ///< 자식에 그대로 넘길 원본 인자(`<Launch>` 값 · `-unattended` …) — 백엔드 목록 · 로그 · 보고 · 백엔드 인자는 뺐다
        string         _logFilePath{};             ///< 부모의 `-log-file`(비면 기본 폴더)
        string         _reportPath{};              ///< 부모의 `-scenario-report`(비면 쓰지 않는다)
        uint32         _windowWidth{ 0 };          ///< 자식 창의 클라이언트 크기(`EngineConfig._window`) — 자식 창을 바둑판으로 놓는 간격이다. 0 이면 놓지 않는다(플랫폼이 놓는다)
        uint32         _windowHeight{ 0 };
        int32          _splitOffset{ -1 };        ///< `-scenario-backend-split-offset` — 나눌 때 첫 시나리오를 맡을 백엔드. -1 이면 `run` 이 UTC 날짜로 정한다
        bool           _bSplitScenarios{ false }; ///< `-scenario-backend-split` — 그림을 견주지 않는 시나리오는 백엔드 하나에만 준다(`assignScenarios`)
    };

    /** @struct AutomationBackendRecords @brief 한 시나리오를 한 백엔드에서 돌린 기록 줄(자식 보고의 `records[]`, 적은 순서)입니다. */
    struct AutomationBackendRecords
    {
        string         _backend{};
        vector<string> _listRecordLine{};
    };

    /** @class AutomationPerBackend @brief 백엔드마다 실행입니다. 게임 스레드가 아닌 헤드리스 작업에서 부릅니다. */
    class SW_API AutomationPerBackend
    {
    public:
        /**
         * @brief `all` 이나 `dx11,vk` 를 쿠킹 표 순서의 명령줄 이름 목록으로 바꿉니다. 모르는 이름 · 빈 목록 · 겹친 이름이면 false 이고 @p outError 에 까닭.
         */
        [[nodiscard]] static bool parseBackendList( string_view text, vector<string>& outListBackend, string& outError );

        /**
         * @brief 백엔드마다(@p options 의 `_listBackend` 순서) 돌릴 `-scenario` 목록입니다. 나누지 않으면 모두 같은 목록입니다.
         * @details 나누면 판정이 백엔드에 달린 단계(`<ExpectImage>` · 마젠타 문턱이 있는 `<CaptureEditorUI>` · `<EditorSelfTest>` · 백엔드 사이에 견주는 `<Record>`)가 있는 시나리오는 모두에게, 나머지는 순서대로 돌아가며 하나에게 준다 — 체인의 순서는 그대로다.
         *          읽지 못하는 시나리오도 모두에게 준다 — 자식마다 읽기 오류를 낸다.
         */
        static vector<string> assignScenarios( const AutomationPerBackendOptions& options );

        /** @brief 원본 인자에서 부모가 자식마다 다시 정하는 인자(백엔드 목록 · 로그 파일 · 보고 · 백엔드 선택)를 뺀 목록입니다. */
        static vector<string> filterPassThroughArguments( const vector<string>& listOriginalArgument );

        /**
         * @brief 같은 시나리오의 백엔드별 기록(@p listRecords)을 순번마다 견주어, 다른 순번마다 한 줄을 돌려줍니다(모두 같거나 백엔드가 하나면 빈 목록).
         * @details 줄 모양: `record #2 'frame 300 Nile.Money': dx11=120 dx12=120 vk=118 gl=120`. 이름(` = ` 앞)이 다른 백엔드는 줄 전체를, 그 순번이 없는 백엔드는 `(none)` 을 적는다.
         */
        static vector<string> findRecordMismatches( const vector<AutomationBackendRecords>& listRecords );

        /** @brief 자식 경로 @p path 의 확장자 앞에 `.<백엔드>` 를 끼웁니다(`a/b.log` → `a/b.dx12.log`, 확장자가 없으면 끝에 붙인다). */
        static string insertBackendSuffix( string_view path, string_view backend );

        /** @brief 백엔드마다 자식을 동시에 띄우고 모두 끝나기를 기다린 뒤 모은 종료 코드를 돌려줍니다(파일 머리 주석의 규칙). */
        [[nodiscard]] static int32 run( const AutomationPerBackendOptions& options );
    };
} // namespace sw
