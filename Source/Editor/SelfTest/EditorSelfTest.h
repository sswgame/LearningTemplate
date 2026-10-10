/**
 * @file EditorSelfTest.h
 * @brief 에디터 안에서 도는 시험(에디터 자체 시험)의 등록 · 실행기입니다 — UE Automation(`IMPLEMENT_SIMPLE_AUTOMATION_TEST`) · Unity EditMode 시험의 자리.
 *
 * @details 패널 · 위젯 · 도킹처럼 에디터 컨텍스트와 ImGui 프레임이 모두 서 있어야 재현되는 동작을 시험합니다. 시험은 자기 .cpp 에서
 *          `SW_EDITOR_SELF_TEST` 한 줄로 등록하고, `-gv_editorSelfTest=<패턴>` 으로 켜면 에디터가 뜬 뒤 프레임마다 지금 시험의 한 단계를
 *          (패널을 그린 뒤 · 프레임을 닫기 전에) 부릅니다. 결과는 한 시험에 한 줄(`EditorSelfTest|PASS|<id>` · `EditorSelfTest|FAIL|<id>|<이유>`)을
 *          로그와 `-gv_editorSelfTestReport=<파일>` 에 남기고, 끝 줄(`EditorSelfTest|DONE|<통과>|<실패>`) 뒤에 앱을 닫습니다.
 *          `AppTest`(hostgpu)가 실제 App 을 이렇게 띄워 보고서를 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/Workspace/EditorRegistry.h"

namespace sw::editor
{
    /** @brief 시험 한 단계의 결과입니다. */
    enum class EditorSelfTestStep : uint8
    {
        Continue = 0, ///< 다음 프레임에 다시 부른다(패널이 한 번 그려지기를 기다릴 때)
        Done          ///< 끝났다
    };

    /**
     * @class EditorSelfTestContext
     * @brief 시험 하나가 실행되는 동안의 상태입니다 — 몇 번째 프레임인지, 실패 이유.
     */
    class EditorSelfTestContext
    {
    public:
        EditorSelfTestContext();

        /** @brief 이 시험의 몇 번째 단계인지입니다(0 부터). 단계 하나 = 에디터 프레임 하나입니다. */
        uint32 getStepIndex() const { return _stepIndex; }
        /** @brief 지금까지 실패가 없으면 true 입니다. */
        bool hasPassed() const { return _failure.empty(); }
        /** @brief 첫 실패의 이유입니다. 통과면 비어 있습니다. */
        const string& getFailure() const { return _failure; }

        /** @brief @p bCondition 이 거짓이면 실패로 적습니다(첫 실패만 남긴다). 조건을 그대로 돌려줍니다. */
        bool expect( bool bCondition, const utf8* pWhat );

        /** @brief 실행기가 단계를 하나 넘깁니다. */
        void advanceStep() { ++_stepIndex; }

    private:
        string _failure;
        uint32 _stepIndex;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 시험 본문입니다. 프레임마다 한 번 불리고, `Done` 을 돌려주면 끝납니다. */
    using EditorSelfTestFunc = EditorSelfTestStep ( * )( EditorSelfTestContext& context );

    /**
     * @struct EditorSelfTestRegistration
     * @brief 에디터 자체 시험 하나의 등록 줄입니다. `_pID` 는 `-gv_editorSelfTest` 패턴이 보는 이름(`영역.무엇`)이고 `_order` 가 실행 순서입니다.
     */
    struct EditorSelfTestRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "selftest";

        EditorSelfTestFunc _pfnRun;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorSelfTestRunner
     * @brief `-gv_editorSelfTest` 로 켜는 실행기입니다. `ImGuiEditor::updateUi` 가 패널을 그린 뒤 프레임마다 `runFrame` 을 부릅니다.
     */
    struct EditorSelfTestRunner
    {
        /** @brief `-gv_editorSelfTest` 가 주어졌으면 true 입니다. 이때 에디터는 저장된 레이아웃을 읽지도 쓰지도 않습니다(`EditorDockLayout`). */
        static bool isRequested();
        /** @brief 시험이 켜져 있으면 지금 시험의 한 단계를 돌립니다. 모두 끝나면 보고서를 쓰고 앱을 닫습니다. ImGui 프레임 안에서 부릅니다. */
        static void runFrame();
        /** @brief @p id 가 패턴 @p pattern 에 맞는지 봅니다. `*` 는 아무 글자열, 쉼표로 여러 패턴을 잇습니다. 빈 패턴은 아무것도 맞지 않습니다. */
        static bool matchesPattern( string_view id, string_view pattern );
    };
} // namespace sw::editor

/**
 * @brief 에디터 자체 시험 하나를 이 파일의 정적 등록자로 둡니다.
 * @param name 파일 안에서 유일한 식별자 조각(보통 함수 이름)
 * @param id `영역.무엇` 꼴의 시험 이름(리터럴)
 * @param order 실행 순서(작을수록 먼저)
 * @param func `EditorSelfTestFunc` 함수
 */
#define SW_EDITOR_SELF_TEST( name, id, order, func ) SW_EDITOR_REGISTER( ::sw::editor::EditorSelfTestRegistration, SelfTest_##name, { id, order }, func )
