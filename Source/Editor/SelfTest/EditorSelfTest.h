/**
 * @file EditorSelfTest.h
 * @brief 에디터 안에서 도는 테스트(에디터 자체 테스트)를 등록하고 실행합니다. 언리얼의 Automation(`IMPLEMENT_SIMPLE_AUTOMATION_TEST`), 유니티의 EditMode 테스트에 해당합니다.
 *
 * @details 패널, 위젯, 도킹처럼 에디터 컨텍스트와 ImGui 프레임이 모두 있어야 재현되는 동작을 테스트한다. 테스트는 자기 .cpp 에서
 *          `SW_EDITOR_SELF_TEST` 한 줄로 등록한다. `-gv_editorSelfTest=<패턴>` 으로 켜면 에디터가 뜬 뒤 프레임마다 현재 테스트의 한 단계를
 *          (패널을 그린 뒤, 프레임을 닫기 전에) 호출한다. 결과는 테스트마다 한 줄(`EditorSelfTest|PASS|<id>`, `EditorSelfTest|FAIL|<id>|<이유>`)로
 *          로그와 `-gv_editorSelfTestReport=<파일>` 에 남기고, 마지막 줄(`EditorSelfTest|DONE|<통과>|<실패>`) 뒤에 앱을 닫는다.
 *          `AppTest`(hostgpu)가 실제 App 을 이렇게 실행해 보고서를 읽는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw::editor
{
    /** @brief 테스트 한 단계의 결과입니다. */
    enum class EditorSelfTestStep : uint8
    {
        Continue = 0, ///< 다음 프레임에 다시 호출한다. 패널이 한 번 그려지기를 기다릴 때 쓴다
        Done          ///< 끝났다
    };

    /**
     * @class EditorSelfTestContext
     * @brief 테스트 하나가 실행되는 동안의 상태입니다. 몇 번째 프레임인지와 실패 이유를 담습니다.
     */
    class SW_EDITOR_API EditorSelfTestContext
    {
    public:
        EditorSelfTestContext();

        /** @brief 이 테스트의 몇 번째 단계인지 반환합니다. 0 부터 시작하며 단계 하나가 에디터 프레임 하나입니다. */
        uint32 getStepIndex() const { return _stepIndex; }
        /** @brief 지금까지 실패가 없으면 true 입니다. */
        bool hasPassed() const { return _failure.empty(); }
        /** @brief 첫 실패의 이유입니다. 통과했으면 비어 있습니다. */
        const string& getFailure() const { return _failure; }

        /** @brief @p bCondition 이 거짓이면 실패로 기록합니다. 첫 실패만 남깁니다. 조건을 그대로 반환합니다. */
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
    /** @brief 테스트 본문입니다. 프레임마다 한 번 호출하며, `Done` 을 반환하면 끝납니다. */
    using EditorSelfTestFunc = EditorSelfTestStep ( * )( EditorSelfTestContext& context );

    /**
     * @struct EditorSelfTestRegistration
     * @brief 에디터 자체 테스트 하나의 등록 항목입니다. `_pID` 는 `-gv_editorSelfTest` 패턴이 매칭하는 이름(`영역.무엇`)이고, `_order` 가 실행 순서입니다.
     */
    struct EditorSelfTestRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "selftest";

        EditorSelfTestFunc _pfnRun;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 에디터 자체 테스트 하나의 결과입니다. Test Runner 창과 프로브가 읽습니다. */
    struct EditorSelfTestResult
    {
        string _id;
        string _reason;          ///< 실패 이유. 통과했으면 빈 문자열이다
        uint32 _frameCount{ 0 }; ///< 걸린 에디터 프레임 수
        bool   _bPassed{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorSelfTestRunner
     * @brief 에디터 자체 테스트 실행기입니다. `ImGuiEditor::updateUI` 가 패널을 그린 뒤 프레임마다 `runFrame` 을 호출합니다.
     * @details 실행 경로는 두 가지다. 명령줄 `-gv_editorSelfTest=<패턴>` 은 끝나면 보고서를 쓰고 앱을 닫는다. Test Runner 창의 `requestRun` 은 그 자리에서 실행하고 앱은 그대로 둔다.
     *          한 번에 한 실행만 돈다. 실행하는 동안 위젯 레이블 기록을 켜고, 끝나면 시작 전 상태로 되돌린다. 시나리오가 켜 둔 기록을 끄지 않기 위해서다.
     */
    struct SW_EDITOR_API EditorSelfTestRunner
    {
        /** @brief `-gv_editorSelfTest` 가 주어졌으면 true 입니다. 이때 에디터는 저장된 레이아웃을 읽지도 쓰지도 않습니다(`EditorDockLayout`). */
        static bool isRequested();
        /** @brief 실행이 있으면 현재 테스트의 한 단계를 돌립니다. 테스트가 끝나면 결과를 남기고, 닫으라는 실행이면 보고서를 쓰고 앱을 닫습니다. ImGui 프레임 안에서 호출합니다. */
        static void runFrame();
        /**
         * @brief 패턴 @p pattern 에 맞는 테스트를 다음 프레임부터 돌립니다. 이미 도는 실행이 있으면 false 입니다.
         * @param bQuitWhenDone true 이면 끝난 뒤 `-gv_editorSelfTestReport` 에 보고서를 쓰고 앱을 닫습니다. 명령줄 실행이 이 경우입니다.
         */
        [[nodiscard]] static bool requestRun( string_view pattern, bool bQuitWhenDone );
        /** @brief 실행이 진행 중이면 true 입니다. */
        static bool isRunning();
        /** @brief 마지막(또는 현재) 실행의 결과입니다. 끝난 테스트만 담깁니다. */
        static const vector<EditorSelfTestResult>& getResults();
        /** @brief @p id 가 패턴 @p pattern 에 맞는지 검사합니다. `*` 는 임의의 문자열이고, 쉼표로 여러 패턴을 연결합니다. 빈 패턴은 아무것도 맞지 않습니다. */
        static bool matchesPattern( string_view id, string_view pattern );
    };
} // namespace sw::editor

/**
 * @brief 에디터 자체 테스트 하나를 이 파일의 정적 등록자로 둡니다.
 * @param name 파일 안에서 유일한 식별자 조각입니다. 보통 함수 이름입니다.
 * @param id `영역.무엇` 형태의 테스트 이름입니다. 리터럴이어야 합니다.
 * @param order 실행 순서입니다. 작을수록 먼저 실행합니다.
 * @param func `EditorSelfTestFunc` 함수입니다.
 */
#define SW_EDITOR_SELF_TEST( name, id, order, func ) SW_EDITOR_REGISTER( ::sw::editor::EditorSelfTestRegistration, SelfTest_##name, { id, order }, func )
