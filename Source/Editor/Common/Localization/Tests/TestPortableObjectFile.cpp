#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Localization/PortableObjectFile.h"

// PortableObjectFileTest — 번역가와 주고받는 PO 파일(외부 도구가 고친 글을 다시 읽는다): 쓰고 읽은 항목이 같고(이스케이프 · 여러 줄 · 주석 · fuzzy),
// 외부 도구가 쓰는 이어진 따옴표 줄을 잇고, 틀린 줄은 줄 번호와 함께 거절한다(nogpu).

/**
 * @brief [PortableObjectFileTest] 쓴 글을 다시 읽으면 항목이 같다 — 따옴표 · 역슬래시 · 줄바꿈 · 탭, 주석, 참조, fuzzy, 이전 원문
 */
SW_TEST_CASE( PortableObjectFileTest, RoundTripsEntries )
{
    sw::PortableObjectFile original;
    original._language    = "ko";
    original._projectName = "editor";
    sw::PortableObjectEntry entry;
    entry._context              = "EdCommon.Save";
    entry._source               = "Save \"all\"\nC:\\path\tend";
    entry._translation          = "모두 \"저장\"\n끝";
    entry._previousSource       = "Save all";
    entry._translatorComment    = "short label";
    entry._listExtractedComment = { "toolbar button" };
    entry._listReference        = { "Source/Editor/A.cpp", "Source/Editor/B.cpp" };
    entry._bFuzzy               = true;
    original._listEntry.push_back( entry );
    sw::PortableObjectEntry untranslated;
    untranslated._context = "EdCommon.Open";
    untranslated._source  = "Open";
    original._listEntry.push_back( untranslated );

    sw::PortableObjectFile reread;
    sw::string             error;
    SW_REQUIRE_TRUE( reread.parse( original.toText(), &error ) );
    SW_EXPECT_TRUE( error.empty() );
    SW_REQUIRE_EQUAL( size_t( 2 ), reread._listEntry.size() );
    const sw::PortableObjectEntry& read = reread._listEntry[0];
    SW_EXPECT_EQUAL( entry._context, read._context );
    SW_EXPECT_EQUAL( entry._source, read._source );
    SW_EXPECT_EQUAL( entry._translation, read._translation );
    SW_EXPECT_EQUAL( entry._previousSource, read._previousSource );
    SW_EXPECT_EQUAL( entry._translatorComment, read._translatorComment );
    SW_EXPECT_TRUE( read._listReference == entry._listReference );
    SW_EXPECT_TRUE( read._bFuzzy );
    SW_EXPECT_TRUE( reread._listEntry[1]._translation.empty() );
    SW_EXPECT_EQUAL( sw::string( "ko" ), reread._language );
}

/**
 * @brief [PortableObjectFileTest] 외부 도구가 나눠 쓴 이어진 따옴표 줄(msgstr "" 다음 줄들)을 이어 읽고, 틀린 줄은 줄 번호와 함께 거절한다
 */
SW_TEST_CASE( PortableObjectFileTest, ReadsContinuationsAndRejectsBrokenLines )
{
    const utf8*            pWrapped = "msgid \"\"\nmsgstr \"\"\n\"Language: ko\\n\"\n\nmsgctxt \"Key.A\"\nmsgid \"\"\n\"first \"\n\"second\"\nmsgstr \"\"\n\"하나 \"\n\"둘\"\n";
    sw::PortableObjectFile wrapped;
    sw::string             error;
    SW_REQUIRE_TRUE( wrapped.parse( pWrapped, &error ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), wrapped._listEntry.size() );
    SW_EXPECT_EQUAL( sw::string( "first second" ), wrapped._listEntry[0]._source );
    SW_EXPECT_EQUAL( sw::string( "하나 둘" ), wrapped._listEntry[0]._translation );

    sw::PortableObjectFile broken;
    SW_EXPECT_FALSE( broken.parse( "msgctxt \"Key.A\"\nmsgid \"unterminated\nmsgstr \"x\"\n", &error ) );
    SW_EXPECT_FALSE( error.empty() );
    sw::PortableObjectFile unknown;
    SW_EXPECT_FALSE( unknown.parse( "msgctxt \"Key.A\"\nmsgid \"a\"\nbogus \"b\"\n", &error ) );
}
