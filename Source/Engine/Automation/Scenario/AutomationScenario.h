/**
 * @file AutomationScenario.h
 * @brief 자동화 시나리오 한 편을 정의합니다. 시작 조건, 시간, 프레임별 단계 목록(읽은 값)을 가지며 형식은 `Engine/Automation/README.md` 에 있습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Automation/Scenario/AutomationScratch.h"
#include "Engine/Automation/Scenario/AutomationTimeLimits.h"
#include "Engine/FileSystem/AssetID.h"
#include "Engine/Input/Virtual/IVirtualInputSource.h"

namespace sw
{
    // 시나리오를 띄우는 쪽(`AppScenarioTest`, 스크립트 `py -3 -m Scripts scenario`)과 실행기가 나누는 글의 정본이다. 스크립트(`Scripts/common/ScenarioArguments.py`)가
    // 이 아래 `inline constexpr const utf8* kXxx = "…";` 줄을 읽으므로 이름이나 줄 모양을 바꾸면 스크립트가 멈춘다. 명령줄 철자(`-scenario` · `-unattended` …)는
    // `Core/Predefined/ArgumentList.xxx` 가 정본이다(`getCommandLineArgumentSpelling`).
    /** @brief 자동화 산출물(시나리오 보고 · 로그 · 사진 · 녹화)을 쓰는 폴더 — 작업 폴더 기준입니다(손 실행과 CTest 모두 `Bin`). 시나리오의 산출물은 그 아래 `<시나리오 name>/<프로세스>/` 에 쌓인다 — 병렬 실행이 겹치지 않는다. */
    inline constexpr const utf8* kAutomationOutputFolder = "Saved/Automation";
    /** @brief `kAutomationOutputFolder` 아래에서 에디터 상태 폴더들을 모으는 폴더 이름 — 사용자의 `Saved/Editor` 대신 시나리오마다 비운 폴더를 쓴다. */
    inline constexpr const utf8* kAutomationEditorStateFolder = "EditorState";
    /** @brief 에디터 상태 폴더를 주는 전역 변수 이름(에디터 모듈이 선언한다 — `EditorPaths.cpp`). 명령줄에는 `-<이름>=<폴더>` 로 준다. */
    inline constexpr const utf8* kAutomationEditorStateVariable = "gv_editorStateDir";

    /**
     * @brief 시나리오 경로의 이름 줄기(`game/empty/automation/fog.scenario.xml` → `fog`)입니다 — 보고 · 로그 · 에디터 상태 폴더 이름의 앞머리(`<줄기>_<백엔드>`)입니다.
     * @details 마지막 `/` 뒤 파일 이름의 첫 `.` 앞입니다. 스크립트(`ScenarioDeclaration.stem`)가 같은 규칙을 쓴다.
     */
    inline string getAutomationScenarioStem( string_view path )
    {
        string_view fileName;
        FileUtil::getFileNamePart( path, fileName );
        return string( fileName.substr( 0, fileName.find( '.' ) ) );
    }

    /** @brief 시나리오의 프레임 0 이 언제인가입니다. */
    enum class AutomationStartCondition : uint8
    {
        Immediately = 0, ///< 시나리오를 시작한 뒤 첫 프레임입니다. 씬이 플레이 중일 필요는 없지만 시작 차단 조건(로딩 화면 · 감시 울타리)이 풀려야 한다 — 차단된 채 시작하지 않는다.
        ScenePlaying,    ///< 활성 씬의 오브젝트 매니저가 플레이를 시작했고 로딩 화면(UI Loading 층)이 걷힌 첫 프레임(기본)
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationAttribute @brief 단계 요소의 속성 하나(이름과 텍스트)입니다. 단계 처리기가 해석합니다. */
    struct AutomationAttribute
    {
        string _name{};
        string _value{};
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationStep @brief 한 프레임에 할 일 하나입니다(엘리먼트 이름 + 속성). */
    struct SW_API AutomationStep
    {
        vector<AutomationAttribute> _listAttribute{};
        string                      _kind{}; ///< 요소 이름입니다(`Tap`, `Expect` 등).
        uint32                      _frameIndex{ 0 };
        uint32                      _orderInFile{ 0 }; ///< 파일에서 몇 번째 단계인지입니다(0 부터이며, 오류와 실패 보고에 적습니다).

        /** @brief 속성 글입니다. 없으면 nullptr 입니다. */
        const string* findAttribute( string_view name ) const;
        /** @brief 보고에 적는 위치(`frame 40 <Expect> #7`)입니다. */
        string describe() const;
        /** @brief 속성 이름이 모두 @p listAllowed 안에 있는지 봅니다. 아니면 false 와 이유(@p outError). */
        bool hasOnlyAttributes( std::initializer_list<string_view> listAllowed, string& outError ) const;
        /** @brief `hasOnlyAttributes` 에 더해 @p listRequired 의 속성이 모두 있는지 봅니다. 없으면 false 와 이유. */
        bool hasRequiredAttributes( std::initializer_list<string_view> listAllowed, std::initializer_list<string_view> listRequired, string& outError ) const;
        /** @brief 꼭 있어야 하는 속성입니다. 없으면 nullptr 와 이유(@p outError). */
        const string* requireAttribute( string_view name, string& outError ) const;
    };
} // namespace sw

namespace sw
{
    class XMLNode;

    /**
     * @struct AutomationLoad
     * @brief 부하 시나리오의 부하 선언(루트 아래 `<Load>` 한 줄)입니다. 부하 검증(`py -3 -m Scripts load`)이 같은 줄로 App · 온라인 봇 · 루프백 클러스터를 돌린다.
     * @details 엔진이 쓰는 값은 `_durationSeconds` 뿐이다 — 시나리오가 시작한 프레임에 그 시간만큼 프레임 측정 창(`gv_profileSeconds`)을 연다. 나머지는 실행기
     *          (`Scripts/qa/Load.py`)가 명령줄로 옮긴다(메시 수 → `-gv_benchMeshes`, 회선 → `-gv_netEmu*`). 형식을 한 곳에서 검사하려고 엔진도 읽는다.
     */
    struct AutomationLoad
    {
        string  _network{};               ///< 회선 프리셋 이름(`NetEmulationPresetTable` 의 `_name`), 비면 흉내 없음
        float32 _durationSeconds{ 0.0f }; ///< 측정 창(초, 워밍업 60 프레임 뒤). 0 보다 커야 한다
        float32 _rampUpSeconds{ 0.0f };   ///< 클라이언트를 이 시간에 걸쳐 늘린다(봇 · 클러스터)
        uint32  _clientCount{ 0 };        ///< 동시 클라이언트 수(봇 · 클러스터). App 하나는 0
        uint32  _meshCount{ 0 };          ///< 벤치 메시 수(`-gv_benchMeshes`), 0 이면 건드리지 않는다
        uint32  _moverCount{ 0 };         ///< 메시마다 틱에서 움직이는 컴포넌트 수(`-gv_benchTickMovers`) — 틱 오브젝트 수 = 메시 × 이 값
        bool    _bDeclared{ false };      ///< 시나리오에 `<Load>` 가 있으면 true 입니다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AutomationTestOptions
     * @brief 시나리오 파일이 실행기 · 테스트에게 하는 선언 묶음입니다. 단계 형식(시작 조건 · 시간 · 단계)과 달리 시나리오를 돌리는 쪽만 읽습니다.
     * @details Scratch · Launch · Load · isolated · editor · requireForeground 가 여기 든다. 이 묶음이 테스트 Automation 폴더로 가는 경계다 — 형식(`AutomationScenario`)은
     *          이 구조를 통째로 들고만 있으므로 폴더를 가를 때 이 구조만 옮기면 된다(`AutomationSplit` 3절).
     */
    struct SW_API AutomationTestOptions
    {
        vector<AutomationScratchFolder> _listScratchFolder{};  ///< 루트 아래 `<Scratch from as>`(여럿 가능). 실행기가 시작할 때 사본을 새로 만든다(`AutomationScratch::prepare`)
        vector<AutomationAttribute>     _listAllowedLog{};     ///< 루트 아래 `<AllowLog id reason>`(여럿 가능) — 이 시나리오에서 나와도 되는 로그 id(`_name`)와 사유(`_value`). 사유는 필수다
        vector<AutomationAttribute>     _listLaunchVariable{}; ///< 루트 아래 `<Launch variable value>`(여럿 가능). 첫 씬 · 렌더 파이프라인처럼 시작한 뒤에는 바꿀 수 없는 기동 때 값
        AutomationLoad                  _load{};               ///< 부하 선언(`<Load>`). 부하 시나리오는 `<Pass/>` 를 두지 않고 측정 창이 끝내며 `timeoutFrames` 를 보지 않는다
        /** @brief 새 프로세스에서만 도는 시나리오입니다(루트 `isolated="true"`). 시작 상태를 전제하거나 환경(창 크기 · 테마 · 세션)을 크게 바꿔 체인(`-scenario=a,b,c`)에 넣지 않는다. 테스트가 고른다. */
        bool _bIsolated{ false };
        /** @brief 에디터를 켠 실행(`-EnableEditor`)에서 도는 시나리오입니다(루트 `editor="true"`). 테스트(`AppScenarioTest`)가 인자를 고른다. `engine/automation/editor/` 의 시나리오는 폴더가 이미 에디터 실행이라 적지 않는다. */
        bool _bRequiresEditor{ false };
        /**
         * @brief 창이 전경이어야 도는 시나리오입니다(루트 `requireForeground="true"` — 실제 커서 클립 · 화면 복사를 본다. 데스크톱 전역 상태라 `input="mixed"` 시나리오에서만 쓸 수 있다).
         * @details 실행기가 시작 조건을 기다리는 프레임부터 창을 전경으로 가져온다. 시작하는 프레임까지 전경을 얻지 못하면 건너뜀(13)으로 끝낸다.
         *          단계가 아니라 루트 속성인 이유: 시작 뒤의 단계는 게임의 첫 틱보다 늦다.
         */
        bool _bRequiresForeground{ false };

        /** @brief `_listLaunchVariable` 을 명령줄 인자(`-gv_a=1 -gv_b=x`)로 씁니다. 선언이 없으면 빈 글입니다. 띄우는 쪽(`AppScenarioTest`, 손 실행)이 명령줄에 붙이고, 실행기는 시작할 때 값이 같은지 보고 다르면 읽기 오류로 멈춘다. */
        string makeLaunchArguments() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationScenario
     * @brief 시나리오 파일 하나를 읽은 값입니다. 단계는 프레임 순이며 같은 프레임은 파일 순입니다.
     * @details 읽기는 형식만 본다(루트, 적은 순서의 단계와 `<Wait>`, 속성 텍스트). 단계 종류와 속성이 맞는지는 실행기가 시작할 때 본다(`AutomationRunner`).
     *          게임과 에디터 모듈이 등록하는 단계 종류는 그때 채워져 있다.
     */
    class SW_API AutomationScenario
    {
    public:
        /**
         * @brief 시나리오 한 편의 벽시계 상한(초)의 기본값입니다 — `wallTimeoutSeconds` 를 적지 않은 시나리오가 받는 값입니다(다른 시한의 기본값과 같은 자리).
         * @details 프레임 시한(`timeoutFrames`)은 시계를 붙잡은 프레임(`WaitUntil` · 시계 잡기 조건)을 세지 않아 붙잡힌 채로는 영영 끝나지 않는다. 이 값은 그 틈을 막는 실시간 총량이고,
         *          값과 다른 시한과의 대소 관계는 `AutomationTimeLimits` 한 곳에 있다.
         */
        static constexpr float32 kDefaultWallTimeoutSeconds = AutomationTimeLimits::kDefaultWallTimeoutSeconds;

        AutomationScenario();

        /** @brief 리소스 경로 또는 절대 경로에서 읽습니다. 형식 오류면 false 이고 @p outError 에 `경로: 이유` 를 씁니다. */
        [[nodiscard]] bool loadFromPath( const AssetID& assetID, string& outError );
        /** @brief XML 글에서 읽습니다. */
        [[nodiscard]] bool parse( string_view xmlText, string_view sourceName, string& outError );

        const string& getName() const { return _name; }
        const string& getSourcePath() const { return _sourcePath; }
        float32       getFixedDelta() const { return _fixedDelta; }
        uint32        getTimeoutFrames() const { return _timeoutFrames; }
        uint32        getStartTimeoutFrames() const { return _startTimeoutFrames; }
        /** @brief 시나리오 시작(읽기)부터 끝까지 지나도 되는 벽시계 시간(초)입니다. 넘으면 실행기가 시간 초과(12)로 끝내고 보고를 씁니다. 부하 시나리오(`<Load>`)는 자기 그물이 있어 보지 않는다. */
        float32                       getWallTimeoutSeconds() const { return _wallTimeoutSeconds; }
        AutomationStartCondition      getStartCondition() const { return _startCondition; }
        VirtualInputMode              getInputMode() const { return _inputMode; }
        const vector<AutomationStep>& getSteps() const { return _listStep; }
        /** @brief @p step 과 같은 프레임에 있는 다른 단계 하나를 반환합니다. 없으면 nullptr 입니다(`WaitUntil` 은 그 프레임에 혼자 있어야 한다 — 실행기 준비와 실행기 없는 검사가 함께 쓴다). */
        const AutomationStep* findOtherStepAtSameFrame( const AutomationStep& step ) const;
        /** @brief 실행기와 테스트만 읽는 선언 묶음입니다(`AutomationTestOptions`). 시나리오의 단계 형식과 섞이지 않게 따로 둔다. */
        const AutomationTestOptions& getTestOptions() const { return _testOptions; }
        /**
         * @brief `<Scratch as>` 의 사본 id 를 이 프로세스의 이름(`AutomationScratch::makeInstanceID` — `scratch/assetmanage_p<pid>`)으로 바꾸고,
         *        단계 속성 글 안의 옛 사본 id 도 같은 규칙(`AutomationScratch::rebaseText`)으로 바꿉니다.
         * @details 실행기가 사본을 만들기 전에 한 번 부른다(`AutomationRunner::prepareScratchFolders`). 읽기(`parse`)는 선언한 id 를 그대로 둔다 — 시나리오를 읽기만 하는
         *          쪽(테스트 · 도구)은 프로세스가 달라 꼬리가 맞지 않는다. `<Launch>` 값은 바꾸지 않는다 — 띄우는 쪽이 명령줄에 준 값과 같은지 실행기가 보므로
         *          사본 id 를 기동 값에 쓰지 않는다.
         */
        void bindScratchInstances();

    private:
        /** @brief `<Load>` 요소를 읽습니다. 모르는 속성 · 틀린 값이면 false 와 이유입니다. */
        [[nodiscard]] bool parseLoad( const XMLNode& node, string& outReason );
        /** @brief `<Scenario>` 의 속성 하나를 읽습니다. 모르는 속성 · 틀린 값이면 false 와 이유입니다(오타가 기본값으로 조용히 바뀌지 않게 한다). */
        [[nodiscard]] bool parseRootAttribute( string_view name, const utf8* pText, string& outReason );
        /** @brief `<Scenario>` 의 자식(`<Load>` `<Scratch>` `<Launch>` `<Wait>` 와 단계)을 차례로 읽습니다. 틀리면 false 와 이유(원본 이름 포함)입니다. */
        [[nodiscard]] bool parseChildren( const XMLNode& root, string& outError );
        /** @brief 단계 하나를 지금 프레임(앞의 `<Wait>` 들이 흘린 합)으로 `_listStep` 에 더합니다. 단계 번호를 이어 갱신합니다. */
        [[nodiscard]] bool parseStepNode( const XMLNode& stepNode, uint32 frameIndex, uint32& inoutOrderInFile, string& outError );

        vector<AutomationStep>   _listStep;
        AutomationTestOptions    _testOptions;
        string                   _name;
        string                   _sourcePath;
        float32                  _fixedDelta;
        float32                  _wallTimeoutSeconds;
        uint32                   _timeoutFrames;
        uint32                   _startTimeoutFrames;
        AutomationStartCondition _startCondition;
        VirtualInputMode         _inputMode;
    };
} // namespace sw
