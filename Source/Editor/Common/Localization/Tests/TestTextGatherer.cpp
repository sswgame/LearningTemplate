// TextGathererTest — 번역 글 수집: 코드 매크로(`SW_LOCTEXT` · `SW_LOCFORMAT` · `SW_LOCKEY`) 읽기, 같은 키의 다른 원문 · 틀린 메시지 구문, 원문 표에 합치기(nogpu).
// 리플렉션 XML 수집 케이스는 손으로 만든 타입을 전역 TypeRegistry 에 영구히 올려야 해 아직 옮기지 않았다(반사 테스트 타입 자리로 옮길 일).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Localization/TextGatherer.h"

#include "Engine/Localization/LocalizationDocuments.h"

namespace
{
    struct TextGathererTestInternal
    {
        static const sw::GatheredText* findText( const sw::TextGatherer& gatherer, sw::string_view key )
        {
            for ( const sw::GatheredText& text : gatherer.getTexts() )
            {
                if ( text._key == key )
                    return &text;
            }
            return nullptr;
        }

        static bool hasIssueContaining( const sw::vector<sw::TextGatherIssue>& listIssue, sw::string_view fragment, bool bError )
        {
            for ( const sw::TextGatherIssue& issue : listIssue )
            {
                const bool bMatches = issue._bError == bError && ( issue._message.find( sw::string( fragment ) ) != sw::string::npos || issue._location.find( sw::string( fragment ) ) != sw::string::npos );
                if ( bMatches )
                    return true;
            }
            return false;
        }

        static bool contains( const sw::vector<sw::string>& listValue, sw::string_view value )
        {
            for ( const sw::string& existing : listValue )
            {
                if ( existing == value )
                    return true;
            }
            return false;
        }
    };
} // namespace

/**
 * @brief [TextGathererTest] 코드의 `SW_LOCTEXT` · `SW_LOCFORMAT` 를 모은다 — 이어 붙인 리터럴 · 이스케이프 · 날 문자열, 주석 · 문자열 · `#define` 안의 것은 건너뛴다
 */
SW_TEST_CASE( TextGathererTest, CodeScannerReadsLiteralMacroCalls )
{
    const sw::string_view kSource = R"code(
        #define SW_LOCTEXT( NamespaceLiteral, KeyLiteral, SourceLiteral ) lookup( NamespaceLiteral "." KeyLiteral, SourceLiteral )
        // SW_LOCTEXT( "Comment", "Line", "not gathered" )
        /* SW_LOCTEXT( "Comment", "Block", "not gathered" ) */
        const utf8* pDoc = "SW_LOCTEXT( \"String\", \"Literal\", \"not gathered\" )";
        label = SW_LOCTEXT( "Menu", "Start", "Start " "Game" );
        title = SW_LOCTEXT( "Menu", "Quote", "Say \"hi\"\tnow" );
        raw   = SW_LOCTEXT( "Menu", "Raw", R"(Use {count} keys)" );
        line  = SW_LOCFORMAT( "HUD", "Ammo", "{n, plural, one {# round} other {# rounds}}", args );
        bad   = SW_LOCTEXT( "Menu", kKey, "dynamic key" );
    )code";

    sw::TextGatherer gatherer;
    gatherer.gatherCodeText( kSource, "Source/Test/Probe.cpp" );

    SW_EXPECT_EQUAL( size_t( 4 ), gatherer.getTexts().size() );
    const sw::GatheredText* pStart = TextGathererTestInternal::findText( gatherer, "Menu.Start" );
    SW_REQUIRE_NOT_NULL( pStart );
    SW_EXPECT_EQUAL( sw::string( "Start Game" ), pStart->_source );
    SW_REQUIRE_TRUE( pStart->_listOrigin.empty() == false );
    SW_EXPECT_EQUAL( sw::string( "Source/Test/Probe.cpp" ), pStart->_listOrigin.front() );
    const sw::GatheredText* pQuote = TextGathererTestInternal::findText( gatherer, "Menu.Quote" );
    const sw::GatheredText* pRaw   = TextGathererTestInternal::findText( gatherer, "Menu.Raw" );
    SW_REQUIRE_TRUE( pQuote != nullptr && pRaw != nullptr );
    SW_EXPECT_EQUAL( sw::string( "Say \"hi\"\tnow" ), pQuote->_source );
    SW_EXPECT_EQUAL( sw::string( "Use {count} keys" ), pRaw->_source );
    SW_EXPECT_NOT_NULL( TextGathererTestInternal::findText( gatherer, "HUD.Ammo" ) );
    SW_EXPECT_NULL( TextGathererTestInternal::findText( gatherer, "Comment.Line" ) );
    SW_EXPECT_NULL( TextGathererTestInternal::findText( gatherer, "String.Literal" ) );

    // 리터럴이 아닌 키는 오류로 알린다(줄 번호 포함) — 수집할 수 없는 글이 조용히 빠지지 않게.
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( gatherer.getIssues(), "Probe.cpp:10", true ) );
}

/**
 * @brief [TextGathererTest] 같은 키에 원문이 둘이면 오류 · 원문이 메시지 구문으로 틀리면 오류
 */
SW_TEST_CASE( TextGathererTest, ConflictingOrBrokenSourceIsAnError )
{
    sw::TextGatherer gatherer;
    gatherer.gatherCodeText( R"(a = SW_LOCTEXT( "UI", "Ok", "OK" ); b = SW_LOCTEXT( "UI", "Ok", "Okay" ); c = SW_LOCTEXT( "UI", "Bad", "{n, plural, one {x}}" );)",
                             "Probe.cpp" );
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( gatherer.getIssues(), "two different source texts", true ) );
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( gatherer.getIssues(), "not a valid message pattern", true ) );
    const sw::GatheredText* pOk = TextGathererTestInternal::findText( gatherer, "UI.Ok" );
    SW_REQUIRE_NOT_NULL( pOk );
    SW_EXPECT_EQUAL( sw::string( "OK" ), pOk->_source );
}

/**
 * @brief [TextGathererTest] 합치기 — 더해짐 · 바뀜 · 지워짐을 보고하고, 손으로 넣은 줄 · 맥락 · 설명 · 최대 길이는 지킨다
 * @details 키 참조가 어디에도 없으면 오류다(원문을 지어낼 수 없다). 데이터의 글은 다른 표에 그 키가 있으면 참조, 없으면 글 자체가 키가 된다.
 */
SW_TEST_CASE( TextGathererTest, MergeReportsAddedChangedRemovedAndKeepsManualRows )
{
    sw::SourceStringTable gatherTable;
    sw::string            error;
    SW_REQUIRE_TRUE( gatherTable.loadFromXMLText( R"(<SourceStringTable culture="en">
	<entries>
		<entry key="Menu.Start" source="Start" comment="Main menu" maxLength="12">
			<origins>
				<item>Old.cpp</item>
			</origins>
		</entry>
		<entry key="Menu.Gone" source="Gone">
			<origins>
				<item>Old.cpp</item>
			</origins>
		</entry>
		<entry key="Manual.Only" source="Hand written" />
	</entries>
</SourceStringTable>)",
                                                  "gather", &error ) );
    sw::SourceStringTable otherTable;
    SW_REQUIRE_TRUE( otherTable.loadFromXMLText( R"(<SourceStringTable culture="en"><entries><entry key="settings.title" source="Settings" /></entries></SourceStringTable>)", "other", &error ) );

    sw::TextGatherer gatherer;
    gatherer.addKeyedText( "Menu.Start", "Start Game", {}, "Menu.cpp" );
    gatherer.addKeyedText( "Menu.Quit", "Quit", {}, "Menu.cpp" );
    gatherer.addKeyReference( "settings.title", "engine/settings/usersettings.schema.xml" );
    gatherer.addKeyReference( "settings.unknown", "engine/settings/usersettings.schema.xml" );
    gatherer.addTextOrKey( "Welcome, traveler!", "Dialogue line", "game/x/intro.dialogue.xml" );
    gatherer.addTextOrKey( "Manual.Only", "Sign.text", "game/x/maps/a.scene.xml" );

    const sw::TextGatherReport report = gatherer.mergeInto( gatherTable, { &otherTable } );
    SW_EXPECT_TRUE( TextGathererTestInternal::contains( report._listAdded, "Menu.Quit" ) );
    SW_EXPECT_TRUE( TextGathererTestInternal::contains( report._listAdded, "Welcome, traveler!" ) );
    SW_EXPECT_TRUE( TextGathererTestInternal::contains( report._listChanged, "Menu.Start" ) );
    SW_EXPECT_TRUE( TextGathererTestInternal::contains( report._listRemoved, "Menu.Gone" ) );
    SW_EXPECT_TRUE( report.hasErrors() ); // settings.unknown
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( report._listIssue, "settings.unknown", true ) );

    const sw::SourceTextEntry* pStart = gatherTable.findEntry( "Menu.Start" );
    SW_REQUIRE_NOT_NULL( pStart );
    SW_EXPECT_EQUAL( sw::string( "Start Game" ), pStart->_source );
    SW_EXPECT_EQUAL( sw::string( "Main menu" ), pStart->_comment );
    SW_EXPECT_EQUAL( uint32( 12 ), pStart->_maxLength );
    SW_REQUIRE_TRUE( pStart->_listOrigin.empty() == false );
    SW_EXPECT_EQUAL( sw::string( "Menu.cpp" ), pStart->_listOrigin.front() );

    const sw::SourceTextEntry* pManual = gatherTable.findEntry( "Manual.Only" );
    SW_REQUIRE_NOT_NULL( pManual ); // 손으로 넣은 줄은 남고, 참조되면 자리를 단다
    SW_EXPECT_EQUAL( sw::string( "Hand written" ), pManual->_source );
    SW_EXPECT_EQUAL( size_t( 1 ), pManual->_listOrigin.size() );

    const sw::SourceTextEntry* pWelcome = gatherTable.findEntry( "Welcome, traveler!" );
    SW_REQUIRE_NOT_NULL( pWelcome );
    SW_EXPECT_EQUAL( sw::string( "Dialogue line" ), pWelcome->_context );
    SW_EXPECT_NULL( gatherTable.findEntry( "settings.title" ) ); // 다른 표의 키는 이 표에 더하지 않는다
}

/**
 * @brief [TextGathererTest] 코드의 `SW_LOCKEY` · `SW_LOCKEYFORMAT` 은 키만 모은다 — 원문은 표에서 오고, 표에 없는 키는 오류, 표의 원문은 바꾸지 않는다
 */
SW_TEST_CASE( TextGathererTest, KeyOnlyMacrosGatherReferencesAndNeverWriteSource )
{
    const sw::string_view kSource = R"code(
        tip   = SW_LOCKEY( "EditorTip", "Known" );
        line  = SW_LOCKEYFORMAT( "EditorTip", "Counted", args );
        gone  = SW_LOCKEY( "EditorTip", "Missing" );
        bad   = SW_LOCKEY( "EditorTip", kKey );
    )code";

    sw::TextGatherer gatherer;
    gatherer.gatherCodeText( kSource, "Source/Test/Probe.cpp" );
    SW_EXPECT_EQUAL( size_t( 3 ), gatherer.getTexts().size() );
    const sw::GatheredText* pKnown = TextGathererTestInternal::findText( gatherer, "EditorTip.Known" );
    SW_REQUIRE_NOT_NULL( pKnown );
    SW_EXPECT_TRUE( pKnown->_kind == sw::GatheredTextKind::KeyReference );
    SW_EXPECT_TRUE( TextGathererTestInternal::findText( gatherer, "EditorTip.Counted" ) != nullptr );

    sw::SourceStringTable table;
    sw::string            error;
    SW_REQUIRE_TRUE( table.loadFromXMLText( R"(<SourceStringTable culture="en">
	<entries>
		<entry key="EditorTip.Known" source="Hand written text" />
		<entry key="EditorTip.Counted" source="{n} items" />
	</entries>
</SourceStringTable>)",
                                            "gather", &error ) );
    const sw::TextGatherReport report = gatherer.mergeInto( table, {} );
    SW_EXPECT_TRUE( report._listAdded.empty() );
    SW_EXPECT_TRUE( report._listChanged.empty() );
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( report._listIssue, "EditorTip.Missing", true ) );       // 표가 정의하지 않은 키
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( report._listIssue, "must be string literals", true ) ); // 동적 키
    const sw::SourceTextEntry* pEntry = table.findEntry( "EditorTip.Known" );
    SW_REQUIRE_NOT_NULL( pEntry );
    SW_EXPECT_EQUAL( sw::string( "Hand written text" ), pEntry->_source ); // 코드가 원문을 덮지 않는다
    SW_REQUIRE_TRUE( pEntry->_listOrigin.empty() == false );
    SW_EXPECT_EQUAL( sw::string( "Source/Test/Probe.cpp" ), pEntry->_listOrigin.front() );
}
