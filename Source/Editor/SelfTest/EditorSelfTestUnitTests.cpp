/**
 * @file EditorSelfTestUnitTests.cpp
 * @brief 에디터 단위 테스트 가운데 에디터 화면(활성 씬 · 프레임)이 필요한 케이스(태그 `editorApp`)를 에디터 안에서 돌리는 자체 테스트입니다.
 * @details 나머지 에디터 케이스는 창 · GPU 없이 `App --test-editor`(ctest `UnitTests.Editor`)가 한 번 돈다 — 화면을 보지 않는 CPU 케이스를 백엔드마다 되풀이하지 않는다.
 */
#include "pch.h"

#include "Core/Diagnostics/CrashHandler.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestRunner.h"

#include "Editor/Common/Workspace/EditorSubsystems.h"
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
             * @brief 에디터 · 편집기 플러그인 · 키트 에디터 확장(`Kits` 아래 `Editor` 폴더)의 `[editorApp]` 케이스를 모두 돌린다. 하나라도 지면 실패다.
             * @details 실패 사유를 이 실패 메시지에 싣는다 — 시나리오 자식은 표준 출력을 남기지 않아, 실행기의 `[ FAIL ]` 줄만으로는 로그에 사유가 없었다.
             */
            static EditorSelfTestStep runEditorUnitTests( EditorSelfTestContext& context )
            {
                test::TestRunOptions options;
                options._filter        = "[editorApp]";
                options._sourceFolder  = "/Editor/";                            // 에디터 모듈은 모두 `Editor/` 폴더다 - 플러그인을 하나씩 적으면 키트의 에디터 확장 케이스가 어디서도 돌지 않았다
                options._pCrashHandler = editor::findSubsystem<CrashHandler>(); // 크래시 보고에 도는 케이스가 남는다
                vector<string> listFailure;
                if ( test::TestRunner::run( options, &listFailure ) )
                    return EditorSelfTestStep::Done;
                StringBuilder<constant::buffer::kMax4096> message;
                message.append( "an editor unit test failed:" );
                for ( size_t index = 0; index < listFailure.size() && index < kMaxReportedFailure; ++index )
                {
                    message.appendFormat( "\n  %#", listFailure[index] );
                }
                if ( listFailure.size() > kMaxReportedFailure )
                {
                    StringBuilder<constant::buffer::kMax256> messagePart;
                    messagePart.appendFormat( "\n  ... and %# more", listFailure.size() - kMaxReportedFailure );
                    message.append( messagePart.view() );
                }
                (void)context.expect( false, message.c_str() );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( EditorUnitTests, "editor.unitTests", 50, &EditorSelfTestUnitTestsInternal::runEditorUnitTests );
} // namespace sw::editor
