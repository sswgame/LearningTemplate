/**
 * @file AutomationScenario.h
 * @brief 자동화 시나리오 한 편을 정의합니다. 시작 조건, 시간, 프레임별 단계 목록(읽은 값)을 가지며 형식은 `Engine/Automation/README.md` 에 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Automation/AutomationScratch.h"
#include "Engine/Input/Virtual/IVirtualInputSource.h"

namespace sw
{
    /** @brief 시나리오의 프레임 0 이 언제인가입니다. */
    enum class AutomationStartCondition : uint8
    {
        Immediately = 0, ///< 시나리오를 시작한 뒤 첫 프레임입니다.
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
     * @class AutomationScenario
     * @brief 시나리오 파일 하나를 읽은 값입니다. 단계는 프레임 순이며 같은 프레임은 파일 순입니다.
     * @details 읽기는 형식만 본다(루트, `<At frame>`, 속성 텍스트). 단계 종류와 속성이 맞는지는 실행기가 시작할 때 본다(`AutomationRunner`).
     *          게임과 에디터 모듈이 등록하는 단계 종류는 그때 채워져 있다.
     */
    class SW_API AutomationScenario
    {
    public:
        AutomationScenario();

        /** @brief 리소스 경로 또는 절대 경로에서 읽습니다. 형식 오류면 false 이고 @p outError 에 `경로: 이유` 를 씁니다. */
        [[nodiscard]] bool loadFromPath( string_view path, string& outError );
        /** @brief XML 글에서 읽습니다. */
        [[nodiscard]] bool parse( string_view xmlText, string_view sourceName, string& outError );

        const string&            getName() const { return _name; }
        const string&            getSourcePath() const { return _sourcePath; }
        float32                  getFixedDelta() const { return _fixedDelta; }
        uint32                   getTimeoutFrames() const { return _timeoutFrames; }
        uint32                   getStartTimeoutFrames() const { return _startTimeoutFrames; }
        AutomationStartCondition getStartCondition() const { return _startCondition; }
        VirtualInputMode         getInputMode() const { return _inputMode; }
        /** @brief 새 프로세스에서만 도는 시나리오입니다(루트 `isolated="true"`). 시작 상태를 전제하거나 환경(창 크기 · 테마 · 세션)을 크게 바꿔 체인(`-scenario=a,b,c`)에 넣지 않는다. 실행기는 이 값을 쓰지 않고 테스트가 고른다. */
        bool isIsolated() const { return _bIsolated; }
        /**
         * @brief 에디터를 켠 실행(`-EnableEditor`)에서 도는 시나리오입니다(루트 `editor="true"`). 게임 팩 시나리오가 그 게임의 에디터 창(일정 디버그 창 등)을 확인할 때 적는다.
         * @details 실행기는 이 값을 쓰지 않고 테스트(`AppScenarioTest`)가 인자를 고른다. `engine/automation/editor/` 의 시나리오는 폴더가 이미 에디터 실행이라 적지 않는다.
         */
        bool                          requiresEditor() const { return _bRequiresEditor; }
        const vector<AutomationStep>& getSteps() const { return _listStep; }
        /** @brief 부하 선언입니다(`<Load>` 가 없으면 `_bDeclared` 가 false). 부하 시나리오는 `<Pass/>` 를 두지 않고 측정 창이 끝내며, `timeoutFrames` 를 보지 않는다. */
        const AutomationLoad& getLoad() const { return _load; }
        /** @brief 이 시나리오가 쓰는 폴더의 사본 선언입니다(루트 아래 `<Scratch from as>`, 여럿 가능). 실행기가 시작할 때 사본을 새로 만든다(`AutomationScratch::prepare`). */
        const vector<AutomationScratchFolder>& getScratchFolders() const { return _listScratchFolder; }
        /**
         * @brief 기동 때 정해지는 전역 변수 선언입니다(루트 아래 `<Launch variable value>`, 여럿 가능 — 첫 씬 · 렌더 파이프라인처럼 시작한 뒤에는 바꿀 수 없는 값).
         * @details 띄우는 쪽(`AppScenarioTest`, 손 실행)이 `makeLaunchArguments` 를 명령줄에 붙이고, 실행기는 시작할 때 값이 같은지 보고 다르면 읽기 오류로 멈춘다.
         *          언리얼 Gauntlet 이 테스트마다 명령줄을 정하는 자리다.
         */
        const vector<AutomationAttribute>& getLaunchVariables() const { return _listLaunchVariable; }
        /** @brief `getLaunchVariables` 를 명령줄 인자(`-gv_a=1 -gv_b=x`)로 씁니다. 선언이 없으면 빈 글입니다. */
        string makeLaunchArguments() const;

    private:
        /** @brief `<Load>` 요소를 읽습니다. 모르는 속성 · 틀린 값이면 false 와 이유입니다. */
        [[nodiscard]] bool parseLoad( const XMLNode& node, string& outReason );

        vector<AutomationStep>          _listStep;
        vector<AutomationScratchFolder> _listScratchFolder;
        vector<AutomationAttribute>     _listLaunchVariable; ///< `<Launch>` 의 변수 이름(`_name`)과 값(`_value`)
        AutomationLoad                  _load;
        string                          _name;
        string                          _sourcePath;
        float32                         _fixedDelta;
        uint32                          _timeoutFrames;
        uint32                          _startTimeoutFrames;
        AutomationStartCondition        _startCondition;
        VirtualInputMode                _inputMode;
        bool                            _bIsolated;
        bool                            _bRequiresEditor;
    };
} // namespace sw
