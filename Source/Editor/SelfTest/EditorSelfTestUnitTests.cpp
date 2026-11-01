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
            /** @brief 실패 줄을 로그 메시지에 몇 개까지 싣는지입니다. 나머지는 개수만 적는다(보고 파일에 전부 있다). */
            static constexpr size_t kMaxReportedFailure = 8;

            /**
             * @brief 에디터 폴더에서 정의한 케이스를 모두 돌린다. 하나라도 지면 실패다.
             * @details 실패 사유를 이 실패 메시지에 싣는다 — 시나리오 자식은 표준 출력을 남기지 않아, 실행기의 `[ FAIL ]` 줄만으로는 로그에 사유가 없었다.
             */
            static EditorSelfTestStep runEditorUnitTests( EditorSelfTestContext& context )
            {
                test::TestRunOptions options;
                options._sourceFolder = "Source/Editor/";
                vector<string> listFailure;
                if ( test::TestRunner::run( options, &listFailure ) )
                    return EditorSelfTestStep::Done;
                string message = "an editor unit test failed:";
                for ( size_t index = 0; index < listFailure.size() && index < kMaxReportedFailure; ++index )
                {
                    message += "\n  " + listFailure[index];
                }
                if ( listFailure.size() > kMaxReportedFailure )
                    message += formatToString( "\n  ... and %# more", listFailure.size() - kMaxReportedFailure );
                (void)context.expect( false, message.c_str() );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( EditorUnitTests, "editor.unitTests", 50, &EditorSelfTestUnitTestsInternal::runEditorUnitTests );
} // namespace sw::editor
