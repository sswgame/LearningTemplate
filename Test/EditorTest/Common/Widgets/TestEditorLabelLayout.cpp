#include "pch.h"

#include "Editor/Common/Widgets/EditorLabelLayout.h"

#include "TestFramework/TestFramework.h"

using sw::editor::EditorLabelLayoutUtil;

namespace
{
    struct TestEditorLabelLayoutInternal
    {
        /** @brief 바이트 하나 = 10 px 인 고정 폭 글꼴입니다. */
        static float32 measureFixed( sw::string_view text, void* /*pUserData*/ ) { return static_cast<float32>( text.size() ) * 10.0f; }
    };
} // namespace

/**
 * @brief [EditorLabelLayoutTest] 끊을 자리('_')가 폭 안에 있으면 글자 가운데가 아니라 그 뒤에서 끊는다
 */
SW_TEST_CASE( EditorLabelLayoutTest, BreaksAfterSeparatorBeforeMidWord )
{
    sw::vector<sw::string_view> listLine;
    EditorLabelLayoutUtil::breakLines( "harvest_valley", 100.0f, 2, &TestEditorLabelLayoutInternal::measureFixed, nullptr, listLine );
    SW_ASSERT_EQUAL( listLine.size(), size_t{ 2 } );
    SW_EXPECT_TRUE( listLine[0] == "harvest_" );
    SW_EXPECT_TRUE( listLine[1] == "valley" );
}

/**
 * @brief [EditorLabelLayoutTest] 끊을 자리가 없으면 글자 단위로 끊고, 마지막 줄은 남은 전부다(말줄임은 그리는 쪽)
 */
SW_TEST_CASE( EditorLabelLayoutTest, LongWordWithoutSeparatorKeepsWholeLastLine )
{
    sw::vector<sw::string_view> listLine;
    EditorLabelLayoutUtil::breakLines( "starskirmishfolder", 80.0f, 2, &TestEditorLabelLayoutInternal::measureFixed, nullptr, listLine );
    SW_ASSERT_EQUAL( listLine.size(), size_t{ 2 } );
    SW_EXPECT_TRUE( listLine[0] == "starskir" );
    SW_EXPECT_TRUE( listLine[1] == "mishfolder" );
}

/**
 * @brief [EditorLabelLayoutTest] 폭 안에 드는 이름은 한 줄이고, 빈 이름도 빈 줄 하나다
 */
SW_TEST_CASE( EditorLabelLayoutTest, ShortTextIsOneLine )
{
    sw::vector<sw::string_view> listLine;
    EditorLabelLayoutUtil::breakLines( "empty", 80.0f, 2, &TestEditorLabelLayoutInternal::measureFixed, nullptr, listLine );
    SW_ASSERT_EQUAL( listLine.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listLine[0] == "empty" );
    EditorLabelLayoutUtil::breakLines( "", 80.0f, 2, &TestEditorLabelLayoutInternal::measureFixed, nullptr, listLine );
    SW_ASSERT_EQUAL( listLine.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listLine[0].empty() );
}

/**
 * @brief [EditorLabelLayoutTest] 공백 뒤에서 끊으면 다음 줄 머리의 공백은 버리고, UTF-8 글자 가운데서는 끊지 않는다
 */
SW_TEST_CASE( EditorLabelLayoutTest, SkipsLeadingSpaceAndKeepsUtf8Whole )
{
    sw::vector<sw::string_view> listLine;
    EditorLabelLayoutUtil::breakLines( "big  tree", 50.0f, 2, &TestEditorLabelLayoutInternal::measureFixed, nullptr, listLine );
    SW_ASSERT_EQUAL( listLine.size(), size_t{ 2 } );
    SW_EXPECT_TRUE( listLine[0] == "big  " );
    SW_EXPECT_TRUE( listLine[1] == "tree" );

    // "가나다" 는 글자마다 3 바이트(30 px) — 폭 40 이면 첫 줄에 한 글자만 든다. 바이트 단위로 끊으면 4 바이트째에서 글자가 깨진다.
    EditorLabelLayoutUtil::breakLines( "\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4", 40.0f, 2, &TestEditorLabelLayoutInternal::measureFixed, nullptr, listLine );
    SW_ASSERT_EQUAL( listLine.size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( listLine[0].size(), size_t{ 3 } );
    SW_EXPECT_EQUAL( listLine[1].size(), size_t{ 6 } );
}
