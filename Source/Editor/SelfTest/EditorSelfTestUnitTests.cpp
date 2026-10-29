/**
 * @file EditorSelfTestUnitTests.cpp
 * @brief 에디터 이미지의 단위 테스트(`Source/Editor` 아래 `Tests/` 의 `SW_TEST_CASE`)를 에디터 안에서 돌리는 자체 테스트입니다.
 * @details 엔진의 `App --test` 는 기동 안(모듈 라이브러리 단계)에서 돌아 에디터 모듈이 올라오기 전이다 — 에디터 케이스가 등록되지 않는다.
 *          에디터 모듈이 선 뒤인 자체 테스트가 같은 실행기를 소스 폴더 거르개(`TestRunOptions::_sourceFolder`)로 돌린다.
 */
#include "pch.h"

#include "Core/Testing/TestRunner.h"

#include "Editor/SelfTest/EditorSelfTest.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSelfTestUnitTestsInternal
        {
            /** @brief 에디터 폴더에서 정의한 케이스를 모두 돌린다. 하나라도 지면 실패다(실패 줄은 실행기가 `[ FAIL ]` 로 남긴다). */
            static EditorSelfTestStep runEditorUnitTests( EditorSelfTestContext& context )
            {
                test::TestRunOptions options;
                options._sourceFolder = "Source/Editor/";
                (void)context.expect( test::TestRunner::run( options ), "an editor unit test failed (see the [ FAIL ] lines)" );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( EditorUnitTests, "editor.unitTests", 50, &EditorSelfTestUnitTestsInternal::runEditorUnitTests );
} // namespace sw::editor
