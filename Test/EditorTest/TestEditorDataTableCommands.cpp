#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorDataTableCommandsTest] 읽지 못한 언어 파일은 저장이 덮지 않는다 — 그 언어의 번역이 지워지지 않는다
 * @details 깨진 언어 파일(끝의 쉼표 · 병합 표식)을 조용히 건너뛰면 그 언어의 칸이 모두 비고, 영어 하나를 고쳐 저장할 때 빈 칸으로 그 파일을
 *          다시 써 **그 언어의 번역이 모두 지워진다.** 그래서 읽기는 실패를 알리고, 저장은 읽지 못한 파일을 덮지 않고 실패로 끝나며 고친 표시를
 *          남긴다(저장 확인이 다시 묻는다).
 */
SW_TEST_CASE( EditorDataTableCommandsTest, SaveDoesNotOverwriteALanguageFileItCouldNotRead )
{
    const string folder        = test::makeTempDirectory( "localization" );
    const string enPath        = FileUtil::joinPath( folder, "en_US.json" );
    const string koPath        = FileUtil::joinPath( folder, "ko_KR.json" );
    const string kBrokenKorean = "{ \"greeting\": \"안녕하세요\", }\n";
    SW_ASSERT_TRUE( FileUtil::writeTextFile( enPath, "{ \"greeting\": \"Hello\" }\n" ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( koPath, kBrokenKorean ) );

    vector<LocalizationRecord> listRecord;
    {
        test::ScopedDefensiveTestLog expected( "a language file with a trailing comma" );
        SW_EXPECT_FALSE( EditorDataTableCommands::loadLocalizationFrom( folder, listRecord ) );
    }
    SW_ASSERT_EQUAL( size_t( 1 ), listRecord.size() );
    SW_EXPECT_STREQ( "Hello", listRecord[0]._enUS.c_str() );
    SW_EXPECT_TRUE( listRecord[0]._koKR.empty() );

    listRecord[0]._enUS      = "Hi";
    listRecord[0]._bModified = true;
    {
        test::ScopedDefensiveTestLog expected( "the unreadable Korean file is not overwritten" );
        SW_EXPECT_FALSE( EditorDataTableCommands::saveLocalizationTo( folder, listRecord ) );
    }

    string koAfter;
    SW_ASSERT_TRUE( FileUtil::readTextFile( koPath, koAfter ) );
    SW_EXPECT_STREQ( kBrokenKorean.c_str(), koAfter.c_str() ); // 그대로다
    SW_EXPECT_TRUE( listRecord[0]._bModified );                // 저장되지 않은 것이 남아 있다

    // 고친 뒤에는 모두 저장되고 표시가 풀린다.
    SW_ASSERT_TRUE( FileUtil::writeTextFile( koPath, "{ \"greeting\": \"안녕하세요\" }\n" ) );
    SW_EXPECT_TRUE( EditorDataTableCommands::saveLocalizationTo( folder, listRecord ) );
    SW_EXPECT_FALSE( listRecord[0]._bModified );
}
