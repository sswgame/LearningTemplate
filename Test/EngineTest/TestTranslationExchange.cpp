#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/PortableObjectFile.h"
#include "Engine/Localization/TranslationMemory.h"
#include "Engine/LocalizationTools.h"

#include "EngineTest/LocalizationTestUtil.h"

#include "TestFramework/TestFramework.h"

using sw::test::LocalizationTestUtil;

namespace
{
    struct TranslationExchangeTestInternal
    {
        static const sw::PortableObjectEntry* findEntry( const sw::PortableObjectFile& file, sw::string_view key )
        {
            for ( const sw::PortableObjectEntry& entry : file._listEntry )
            {
                if ( entry._context == key )
                    return &entry;
            }
            return nullptr;
        }

        static sw::string hashOf( sw::string_view source ) { return sw::LocalizationTextUtil::formatSourceHash( sw::LocalizationTextUtil::computeSourceHash( source ) ); }

        /** @brief en 원문 셋(하나는 최대 길이 · 맥락) · ko 번역 둘(하나는 옛 원문의 번역) · 번역 메모리에 옛 쌍 하나인 프로젝트입니다. */
        static sw::string writeProject( const sw::string& folder, sw::string_view extraFields = {} )
        {
            const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko" ])", extraFields );
            LocalizationTestUtil::writeSourceTable( folder, "en", R"("menu.start": { "source": "Start Game", "context": "Main menu button", "maxLength": 12, "origins": [ "Menu.cpp" ] },
                                                                    "menu.quit": { "source": "Quit to desktop" },
                                                                    "menu.load": { "source": "Load Game" })" );
            LocalizationTestUtil::writeTranslation( folder, "ko",
                                                    "\"menu.start\": { \"text\": \"게임 시작\", \"sourceHash\": \"" + hashOf( "Start Game" ) +
                                                        "\", \"translatorComment\": \"버튼이 좁다\" }, \"menu.quit\": { \"text\": \"종료\", \"sourceHash\": \"" + hashOf( "Quit" ) +
                                                        "\" }, \"menu.removed\": { \"text\": \"사라진 메뉴\" }" );
            (void)sw::FileUtil::ensureDirectoryExists( sw::FileUtil::joinPath( folder, "tm" ) );
            (void)sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( folder, "tm/ko.tm.json" ),
                                               R"({ "culture": "ko", "entries": [ { "source": "Quit", "text": "종료" }, { "source": "Load game", "text": "불러오기" } ] })" );
            return projectPath;
        }
    };
} // namespace

/**
 * @brief [TranslationMemoryTest] 정확히 같은 원문 → 그대로, 정규화가 같거나 비슷한 원문 → 근사 일치, 너무 다르면 없음
 */
SW_TEST_CASE( TranslationMemoryTest, ExactNormalizedAndFuzzyMatches )
{
    sw::TranslationMemory memory;
    memory.setCulture( "ko" );
    SW_EXPECT_TRUE( memory.addPair( "Open the door", "문을 연다" ) );
    SW_EXPECT_FALSE( memory.addPair( "Open the door", "문을 연다" ) ); // 같은 쌍은 바뀐 것이 아니다
    (void)memory.addPair( "Save your progress?", "진행 상황을 저장할까요?" );

    sw::TranslationMemoryMatch match;
    SW_ASSERT_TRUE( memory.findBestMatch( "Open the door", match ) );
    SW_EXPECT_TRUE( match._bExact );

    SW_ASSERT_TRUE( memory.findBestMatch( "open  the door.", match ) ); // 대소문자 · 공백 · 끝 문장부호
    SW_EXPECT_FALSE( match._bExact );
    SW_EXPECT_TRUE( match._score > 0.95f );

    SW_ASSERT_TRUE( memory.findBestMatch( "Save your progress now?", match ) );
    SW_EXPECT_FALSE( match._bExact );
    SW_EXPECT_STREQ( "진행 상황을 저장할까요?", match._text.c_str() );

    SW_EXPECT_FALSE( memory.findBestMatch( "Completely different words", match ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::TranslationMemory::computeSimilarity( "abc", "xyz" ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, sw::TranslationMemory::computeSimilarity( "가나다라", "가나다마" ), 0.001f ); // 글자 단위(바이트가 아니다)
    SW_EXPECT_STREQ( "Open the door", memory.findSourceOfText( "문을 연다" ).c_str() );

    sw::TranslationMemory reloaded;
    SW_ASSERT_TRUE( reloaded.loadFromJsonText( memory.toJsonText(), "tm" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), reloaded.getEntryCount() );
}

/**
 * @brief [PortableObjectTest] PO 왕복 — 머리 · 번역가 메모 · 추출 설명 · 자리 · fuzzy · 옛 원문 · 이스케이프 · 여러 줄 글을 지킨다
 */
SW_TEST_CASE( PortableObjectTest, RoundTripKeepsCommentsAndFlags )
{
    sw::PortableObjectFile file;
    file._language                 = "ko";
    file._projectName              = "test";
    sw::PortableObjectEntry& entry = file._listEntry.emplace_back();
    entry._context                 = "menu.start";
    entry._source                  = "Say \"hi\"\nto {name}";
    entry._translation             = "{name}에게\n\"안녕\" 하고 말하기";
    entry._translatorComment       = "줄 1\n줄 2";
    entry._listExtractedComment    = { "Context: button", "Max length: 12" };
    entry._listReference           = { "Menu.cpp" };
    entry._previousSource          = "Say hi";
    entry._bFuzzy                  = true;

    const sw::string       text = file.toText();
    sw::PortableObjectFile parsed;
    sw::string             error;
    SW_ASSERT_TRUE_MSG( parsed.parse( text, &error ), error.c_str() );
    SW_EXPECT_STREQ( "ko", parsed._language.c_str() );
    SW_EXPECT_STREQ( "test", parsed._projectName.c_str() );
    SW_ASSERT_EQUAL( size_t( 1 ), parsed._listEntry.size() );
    const sw::PortableObjectEntry& back = parsed._listEntry[0];
    SW_EXPECT_STREQ( entry._source.c_str(), back._source.c_str() );
    SW_EXPECT_STREQ( entry._translation.c_str(), back._translation.c_str() );
    SW_EXPECT_STREQ( entry._translatorComment.c_str(), back._translatorComment.c_str() );
    SW_EXPECT_STREQ( "Say hi", back._previousSource.c_str() );
    SW_EXPECT_TRUE( back._bFuzzy );
    SW_EXPECT_EQUAL( size_t( 2 ), back._listExtractedComment.size() );

    // 도구가 쓰는 여러 줄 msgid(빈 첫 줄 + 이어지는 줄)도 읽는다.
    SW_ASSERT_TRUE( parsed.parse( "msgid \"\"\nmsgstr \"Language: ja\\n\"\n\nmsgctxt \"a\"\nmsgid \"\"\n\"Hello \"\n\"world\"\nmsgstr \"\"\n\"こんにちは\"\n", &error ) );
    SW_EXPECT_STREQ( "Hello world", parsed._listEntry[0]._source.c_str() );
    SW_EXPECT_STREQ( "こんにちは", parsed._listEntry[0]._translation.c_str() );
    SW_EXPECT_FALSE( parsed.parse( "msgid \"a\"\nmsgid_plural \"b\"\nmsgstr[0] \"x\"\n", &error ) );
}

/**
 * @brief [TranslationExchangeTest] 수집이 번역 메모리로 채운다 — 같은 원문은 그대로(지금 번역), 비슷한 원문은 검토 표시, 낡은 번역은 같은 원문만 바꾼다
 * @details "Quit to desktop" 은 메모리의 "Quit" 과 길이가 많이 달라 채우지 않고(낡은 번역 그대로), "Load Game" 은 "Load game" 의 근사 일치로 검토 표시가 된다.
 */
SW_TEST_CASE( TranslationExchangeTest, GatherPrefillsFromTranslationMemory )
{
    const sw::string folder      = test::makeTempDirectory( "loc_tm_prefill" );
    const sw::string projectPath = TranslationExchangeTestInternal::writeProject( folder, R"("codeRoots": [ "Code" ])" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( sw::FileUtil::joinPath( folder, "Code" ) ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( folder, "Code/Menu.cpp" ), R"(auto a = SW_LOCTEXT( "menu", "start", "Start Game" );)" ) );

    sw::LocalizationGatherResult result;
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, folder, true, result ) );
    SW_ASSERT_EQUAL( size_t( 1 ), result._listCulture.size() );
    const sw::LocalizationCultureReport& korean = result._listCulture[0];
    SW_EXPECT_EQUAL( uint32( 1 ), korean._prefilledFuzzy );
    SW_EXPECT_EQUAL( uint32( 1 ), korean._currentCount );
    SW_EXPECT_EQUAL( uint32( 1 ), korean._staleCount );
    SW_EXPECT_EQUAL( uint32( 1 ), korean._reviewCount );
    SW_EXPECT_EQUAL( uint32( 1 ), korean._orphanCount ); // 원문 표에 없는 키의 번역은 뺐다

    sw::TranslationTable translation;
    SW_ASSERT_TRUE( translation.loadFromFile( sw::FileUtil::joinPath( folder, "ko.translation.json" ) ) );
    SW_EXPECT_NULL( translation.findEntry( "menu.removed" ) );
    const sw::TranslationEntry* pLoad = translation.findEntry( "menu.load" );
    SW_ASSERT_NOT_NULL( pLoad );
    SW_EXPECT_STREQ( "불러오기", pLoad->_text.c_str() );
    SW_EXPECT_TRUE( pLoad->_bReview );

    // 지금 번역은 메모리에 쌓였다.
    sw::TranslationMemory memory;
    SW_ASSERT_TRUE( memory.loadFromFile( sw::FileUtil::joinPath( folder, "tm/ko.tm.json" ) ) );
    sw::TranslationMemoryMatch match;
    SW_ASSERT_TRUE( memory.findBestMatch( "Start Game", match ) );
    SW_EXPECT_TRUE( match._bExact );
}

/**
 * @brief [TranslationExchangeTest] PO 내보내기 → 번역가가 고침 → 가져오기: 맥락 · 메모가 왕복하고, fuzzy 는 검토 표시, 옛 원문의 번역은 낡은 것으로 들어온다
 */
SW_TEST_CASE( TranslationExchangeTest, ExportEditImportRoundTrip )
{
    const sw::string folder      = test::makeTempDirectory( "loc_po_roundtrip" );
    const sw::string projectPath = TranslationExchangeTestInternal::writeProject( folder );

    sw::vector<sw::LocalizationExchangeResult> listResult;
    SW_ASSERT_TRUE( sw::LocalizationTools::exportProjectPo( projectPath, listResult ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listResult.size() );
    SW_EXPECT_EQUAL( uint32( 3 ), listResult[0]._entryCount );
    SW_EXPECT_EQUAL( uint32( 1 ), listResult[0]._staleCount );

    sw::string text;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( listResult[0]._path, text ) );
    sw::PortableObjectFile exported;
    SW_ASSERT_TRUE( exported.parse( text ) );
    const sw::PortableObjectEntry* pStart = TranslationExchangeTestInternal::findEntry( exported, "menu.start" );
    SW_ASSERT_NOT_NULL( pStart );
    SW_EXPECT_STREQ( "버튼이 좁다", pStart->_translatorComment.c_str() );
    SW_EXPECT_STREQ( "Menu.cpp", pStart->_listReference.front().c_str() );
    SW_EXPECT_TRUE( text.find( "Max length: 12" ) != sw::string::npos );
    const sw::PortableObjectEntry* pQuit = TranslationExchangeTestInternal::findEntry( exported, "menu.quit" );
    SW_ASSERT_NOT_NULL( pQuit );
    SW_EXPECT_TRUE( pQuit->_bFuzzy );
    SW_EXPECT_STREQ( "Quit", pQuit->_previousSource.c_str() ); // 번역 메모리가 옛 원문을 안다

    // 번역가: quit 을 새 원문으로 다시 번역하고 fuzzy 를 지운다, load 를 번역하되 fuzzy 로 남긴다, 모르는 키 하나.
    for ( sw::PortableObjectEntry& entry : exported._listEntry )
    {
        if ( entry._context == "menu.quit" )
        {
            entry._translation = "바탕 화면으로 나가기";
            entry._bFuzzy      = false;
            entry._previousSource.clear();
        }
        if ( entry._context == "menu.load" )
        {
            entry._translation = "게임 불러오기";
            entry._bFuzzy      = true;
        }
    }
    sw::PortableObjectEntry& unknown = exported._listEntry.emplace_back();
    unknown._context                 = "menu.removed";
    unknown._source                  = "Gone";
    unknown._translation             = "사라짐";
    const sw::string editedPath      = sw::FileUtil::joinPath( folder, "edited.po" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( editedPath, exported.toText() ) );

    sw::LocalizationExchangeResult imported;
    {
        test::ScopedLogCollector logs; // 모르는 키는 경고
        SW_ASSERT_TRUE( sw::LocalizationTools::importPo( projectPath, editedPath, imported ) );
    }
    SW_EXPECT_EQUAL( uint32( 1 ), imported._unknownKeyCount );
    SW_EXPECT_EQUAL( uint32( 1 ), imported._fuzzyCount );

    sw::TranslationTable  translation;
    sw::SourceStringTable source;
    SW_ASSERT_TRUE( translation.loadFromFile( sw::FileUtil::joinPath( folder, "ko.translation.json" ) ) );
    SW_ASSERT_TRUE( source.loadFromFile( sw::FileUtil::joinPath( folder, "test.strings.json" ) ) );
    SW_EXPECT_TRUE( translation.computeState( "menu.quit", source.findEntry( "menu.quit" ) ) == sw::TranslationState::Current );
    SW_EXPECT_TRUE( translation.computeState( "menu.load", source.findEntry( "menu.load" ) ) == sw::TranslationState::Review );
    SW_EXPECT_TRUE( translation.computeState( "menu.start", source.findEntry( "menu.start" ) ) == sw::TranslationState::Current );
    SW_EXPECT_STREQ( "버튼이 좁다", translation.findEntry( "menu.start" )->_translatorComment.c_str() );
    SW_ASSERT_NOT_NULL( translation.findEntry( "menu.removed" ) ); // 원문 표에 없는 키는 가져오지 않는다 — 있던 줄도 바꾸지 않는다
    SW_EXPECT_STREQ( "사라진 메뉴", translation.findEntry( "menu.removed" )->_text.c_str() );

    // 원문이 그 사이 바뀐 PO 를 가져오면 번역은 그 옛 원문의 해시로 들어와 낡은 것이 된다.
    sw::PortableObjectFile oldPo;
    oldPo._language                   = "ko";
    sw::PortableObjectEntry& oldEntry = oldPo._listEntry.emplace_back();
    oldEntry._context                 = "menu.start";
    oldEntry._source                  = "Start";
    oldEntry._translation             = "시작";
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( editedPath, oldPo.toText() ) );
    SW_ASSERT_TRUE( sw::LocalizationTools::importPo( projectPath, editedPath, imported ) );
    SW_EXPECT_EQUAL( uint32( 1 ), imported._staleCount );
    SW_ASSERT_TRUE( translation.loadFromFile( sw::FileUtil::joinPath( folder, "ko.translation.json" ) ) );
    SW_EXPECT_TRUE( translation.computeState( "menu.start", source.findEntry( "menu.start" ) ) == sw::TranslationState::Stale );
}
