#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"

#include "Engine/Localization/LocalizationDocuments.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorDataTableCommandsTestInternal
    {
        /** @brief 원문 표 하나 · 문화권 ko 를 가진 프로젝트를 임시 폴더에 만들고 프로젝트 경로를 돌려줍니다. */
        static string writeProject( const string& folder, const string& koreanJSON )
        {
            const string projectPath = FileUtil::joinPath( folder, "test.locproject.json" );
            // 시험 준비 — 실패는 writeTextFile 이 오류로 남기고 뒤 단언이 깨진다
            (void)FileUtil::writeTextFile( projectPath, R"({ "name": "test", "sourceCulture": "en", "cultures": [ "ko" ], "stringTables": [ "test.strings.json" ] })" );
            (void)FileUtil::writeTextFile( FileUtil::joinPath( folder, "test.strings.json" ), // 위와 같다
                                           R"({ "culture": "en", "entries": { "greeting": { "source": "Hello", "comment": "Title screen" } } })" );
            (void)FileUtil::writeTextFile( FileUtil::joinPath( folder, "ko.translation.json" ), koreanJSON ); // 위와 같다
            return projectPath;
        }
    };
} // namespace

/**
 * @brief [EditorDataTableCommandsTest] 읽지 못한 번역 표는 저장이 덮지 않는다 — 그 문화권의 번역이 지워지지 않는다
 * @details 깨진 번역 표(끝의 쉼표 · 병합 표식)를 조용히 건너뛰면 그 문화권의 칸이 모두 비고, 원문 하나를 고쳐 저장할 때 빈 칸으로 그 파일을
 *          다시 써 **그 문화권의 번역이 모두 지워진다.** 그래서 읽기는 실패를 알리고, 저장은 읽지 못한 파일을 덮지 않고 실패로 끝나며 고친 표시를
 *          남긴다(저장 확인이 다시 묻는다).
 */
SW_TEST_CASE( EditorDataTableCommandsTest, SaveDoesNotOverwriteATranslationFileItCouldNotRead )
{
    const string folder        = test::makeTempDirectory( "localization" );
    const string kBrokenKorean = "{ \"culture\": \"ko\", \"entries\": { \"greeting\": { \"text\": \"안녕하세요\" }, } }\n";
    const string projectPath   = EditorDataTableCommandsTestInternal::writeProject( folder, kBrokenKorean );
    const string koPath        = FileUtil::joinPath( folder, "ko.translation.json" );

    LocalizationSheet sheet;
    {
        test::ScopedDefensiveTestLog expected( "a translation file with a trailing comma" );
        SW_EXPECT_FALSE( EditorDataTableCommands::loadLocalizationProject( projectPath, sheet ) );
    }
    SW_ASSERT_EQUAL( size_t( 1 ), sheet._listRecord.size() );
    SW_EXPECT_STREQ( "Hello", sheet._listRecord[0]._source.c_str() );
    SW_ASSERT_EQUAL( size_t( 1 ), sheet._listRecord[0]._listTranslation.size() );
    SW_EXPECT_TRUE( sheet._listRecord[0]._listTranslation[0].empty() );

    sheet._listRecord[0]._source    = "Hi";
    sheet._listRecord[0]._bModified = true;
    {
        test::ScopedDefensiveTestLog expected( "the unreadable Korean file is not overwritten" );
        SW_EXPECT_FALSE( EditorDataTableCommands::saveLocalizationProject( sheet ) );
    }

    string koAfter;
    SW_ASSERT_TRUE( FileUtil::readTextFile( koPath, koAfter ) );
    SW_EXPECT_STREQ( kBrokenKorean.c_str(), koAfter.c_str() ); // 그대로다
    SW_EXPECT_TRUE( sheet._listRecord[0]._bModified );         // 저장되지 않은 것이 남아 있다

    // 고친 뒤에는 모두 저장되고 표시가 풀린다.
    SW_ASSERT_TRUE( FileUtil::writeTextFile( koPath, R"({ "culture": "ko", "entries": { "greeting": { "text": "안녕하세요" } } })" ) );
    SW_EXPECT_TRUE( EditorDataTableCommands::saveLocalizationProject( sheet ) );
    SW_EXPECT_FALSE( sheet._listRecord[0]._bModified );
}

/**
 * @brief [EditorDataTableCommandsTest] 번역을 고친 칸은 지금 원문의 해시를 받고, 원문만 고친 줄의 번역은 낡은 것이 된다 — 메모 · 설명은 지킨다
 * @details 저장이 번역 칸마다 해시를 다시 찍으면 원문을 고쳐도 낡음이 드러나지 않고(번역가가 모른다), 하나도 안 찍으면 손으로 고친 번역이
 *          영영 확인되지 않는다. 디스크의 지금 내용 위에 고친 칸만 얹어야 번역가 메모가 남는다.
 */
SW_TEST_CASE( EditorDataTableCommandsTest, EditedTranslationIsCurrentAndSourceEditMakesOthersStale )
{
    const string folder      = test::makeTempDirectory( "localization_states" );
    const string projectPath = EditorDataTableCommandsTestInternal::writeProject(
        folder, R"({ "culture": "ko", "entries": { "greeting": { "text": "안녕", "translatorComment": "반말로" } } })" );

    LocalizationSheet sheet;
    SW_ASSERT_TRUE( EditorDataTableCommands::loadLocalizationProject( projectPath, sheet ) );
    SW_ASSERT_EQUAL( size_t( 1 ), sheet._listRecord.size() );

    // 1) 번역을 고친다 → 지금 원문의 번역(Current), 메모는 남는다
    sheet._listRecord[0]._listTranslation[0] = "안녕하세요";
    sheet._listRecord[0]._bModified          = true;
    SW_ASSERT_TRUE( EditorDataTableCommands::saveLocalizationProject( sheet ) );
    TranslationTable  korean;
    SourceStringTable source;
    SW_ASSERT_TRUE( korean.loadFromFile( FileUtil::joinPath( folder, "ko.translation.json" ) ) );
    SW_ASSERT_TRUE( source.loadFromFile( FileUtil::joinPath( folder, "test.strings.json" ) ) );
    SW_EXPECT_TRUE( korean.computeState( "greeting", source.findEntry( "greeting" ) ) == TranslationState::Current );
    SW_EXPECT_STREQ( "반말로", korean.findEntry( "greeting" )->_translatorComment.c_str() );
    SW_EXPECT_STREQ( "Title screen", source.findEntry( "greeting" )->_comment.c_str() );

    // 2) 원문만 고친다 → 번역은 낡은 것
    SW_ASSERT_TRUE( EditorDataTableCommands::loadLocalizationProject( projectPath, sheet ) );
    sheet._listRecord[0]._source    = "Hello there";
    sheet._listRecord[0]._bModified = true;
    SW_ASSERT_TRUE( EditorDataTableCommands::saveLocalizationProject( sheet ) );
    SW_ASSERT_TRUE( EditorDataTableCommands::loadLocalizationProject( projectPath, sheet ) );
    SW_ASSERT_EQUAL( size_t( 1 ), sheet._listRecord[0]._listState.size() );
    SW_EXPECT_TRUE( sheet._listRecord[0]._listState[0] == TranslationState::Stale );

    // 3) 지운 키는 원문 표 · 번역 표에서 함께 사라진다
    sheet._listRemovedKey.push_back( sheet._listRecord[0]._key );
    sheet._listRecord.clear();
    SW_ASSERT_TRUE( EditorDataTableCommands::saveLocalizationProject( sheet ) );
    SW_ASSERT_TRUE( korean.loadFromFile( FileUtil::joinPath( folder, "ko.translation.json" ) ) );
    SW_EXPECT_NULL( korean.findEntry( "greeting" ) );
}
