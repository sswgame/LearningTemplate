/**
 * @file AutomationScenario.h
 * @brief 자동화 시나리오 한 편 — 시작 조건 · 시간 · 프레임별 단계 목록(읽은 값)입니다. 형식은 `Engine/Automation/README.md`.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Input/IVirtualInputSource.h"

namespace sw
{
    /** @brief 시나리오의 프레임 0 이 언제인가입니다. */
    enum class AutomationStartCondition : uint8
    {
        Immediately = 0, ///< 시나리오를 시작한 뒤 첫 프레임
        ScenePlaying,    ///< 활성 씬의 오브젝트 매니저가 플레이를 시작한 첫 프레임(기본)
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationAttribute @brief 단계 엘리먼트의 속성 하나(이름 · 글)입니다. 단계 처리기가 해석합니다. */
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
        string                      _kind{}; ///< 엘리먼트 이름(`Tap` · `Expect` …)
        uint32                      _frameIndex{ 0 };
        uint32                      _orderInFile{ 0 }; ///< 파일에서 몇 번째 단계인가(0 부터) — 오류 · 실패 보고에 적는다

        /** @brief 속성 글입니다. 없으면 nullptr 입니다. */
        const string* findAttribute( string_view name ) const;
        /** @brief 보고에 적는 위치(`frame 40 <Expect> #7`)입니다. */
        string describe() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationScenario
     * @brief 시나리오 파일 하나를 읽은 값입니다. 단계는 프레임 순(같은 프레임은 파일 순)입니다.
     * @details 읽기는 형식만 봅니다(루트 · `<At frame>` · 속성 글). 단계 종류 · 속성이 맞는가는 실행기가 시작할 때 본다(`AutomationRunner`) —
     *          게임 · 에디터 모듈이 등록하는 단계 종류는 그때 차 있다.
     */
    class SW_API AutomationScenario
    {
    public:
        AutomationScenario();

        /** @brief 리소스 경로 또는 절대 경로에서 읽습니다. 형식 오류면 false 이고 @p outError 에 `경로: 이유` 를 씁니다. */
        [[nodiscard]] bool loadFromPath( string_view path, string& outError );
        /** @brief XML 글에서 읽습니다. */
        [[nodiscard]] bool parse( string_view xmlText, string_view sourceName, string& outError );

        const string&                 getName() const { return _name; }
        const string&                 getSourcePath() const { return _sourcePath; }
        float32                       getFixedDelta() const { return _fixedDelta; }
        uint32                        getTimeoutFrames() const { return _timeoutFrames; }
        uint32                        getStartTimeoutFrames() const { return _startTimeoutFrames; }
        AutomationStartCondition      getStartCondition() const { return _startCondition; }
        VirtualInputMode              getInputMode() const { return _inputMode; }
        const vector<AutomationStep>& getSteps() const { return _listStep; }

    private:
        vector<AutomationStep>   _listStep;
        string                   _name;
        string                   _sourcePath;
        float32                  _fixedDelta;
        uint32                   _timeoutFrames;
        uint32                   _startTimeoutFrames;
        AutomationStartCondition _startCondition;
        VirtualInputMode         _inputMode;
    };
} // namespace sw
