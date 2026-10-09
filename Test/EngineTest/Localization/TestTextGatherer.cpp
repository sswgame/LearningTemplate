#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/DevTools/LocalizationTools.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/TextGatherer.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "EngineTest/LocalizationTestUtil.h"

#include "TestFramework/TestFramework.h"

using sw::test::LocalizationTestUtil;

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

#if !defined( SW_SHIPPING )
        /** @brief `Localizable` · `NotLocalizable` · 표시 없는 문자열 프로퍼티를 가진 시험용 타입을 리플렉션 표에 올립니다(코드젠 없이 손으로). */
        static void registerProbeType()
        {
            static sw::TypeInfo s_info{};
            static bool         s_bRegistered{ false };
            if ( s_bRegistered )
                return;
            s_bRegistered              = true;
            s_info._name               = sw::hashed_string( "LocGatherProbe" );
            s_info._fullyQualifiedName = sw::hashed_string( "sw::LocGatherProbe" );
            s_info._typeId             = static_cast<uint32>( s_info._name.getHash() );

            sw::PropertyInfo label{};
            label._name                                                        = sw::hashed_string( "_label" );
            label._typeName                                                    = sw::hashed_string( "string" );
            label._metadata._mapCustomMeta[sw::hashed_string( "Localizable" )] = "1";
            label._metadata._mapCustomMeta[sw::hashed_string( "MaxLength" )]   = "16";
            s_info._listProperty.push_back( label );

            sw::PropertyInfo internalName{};
            internalName._name                                                           = sw::hashed_string( "_debugName" );
            internalName._typeName                                                       = sw::hashed_string( "string" );
            internalName._metadata._mapCustomMeta[sw::hashed_string( "NotLocalizable" )] = "1";
            s_info._listProperty.push_back( internalName );

            sw::PropertyInfo tooltip{};
            tooltip._name     = sw::hashed_string( "_tooltip" );
            tooltip._typeName = sw::hashed_string( "string" );
            s_info._listProperty.push_back( tooltip );

            sw::engine::getTypeRegistry().registerClass( s_info );
        }
#endif
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
        line  = SW_LOCFORMAT( "Hud", "Ammo", "{n, plural, one {# round} other {# rounds}}", args );
        bad   = SW_LOCTEXT( "Menu", kKey, "dynamic key" );
    )code";

    sw::TextGatherer gatherer;
    gatherer.gatherCodeText( kSource, "Source/Test/Probe.cpp" );

    SW_EXPECT_EQUAL( size_t( 4 ), gatherer.getTexts().size() );
    const sw::GatheredText* pStart = TextGathererTestInternal::findText( gatherer, "Menu.Start" );
    SW_ASSERT_NOT_NULL( pStart );
    SW_EXPECT_STREQ( "Start Game", pStart->_source.c_str() );
    SW_EXPECT_STREQ( "Source/Test/Probe.cpp", pStart->_listOrigin.front().c_str() );
    SW_EXPECT_STREQ( "Say \"hi\"\tnow", TextGathererTestInternal::findText( gatherer, "Menu.Quote" )->_source.c_str() );
    SW_EXPECT_STREQ( "Use {count} keys", TextGathererTestInternal::findText( gatherer, "Menu.Raw" )->_source.c_str() );
    SW_EXPECT_NOT_NULL( TextGathererTestInternal::findText( gatherer, "Hud.Ammo" ) );
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
    gatherer.gatherCodeText( R"(a = SW_LOCTEXT( "Ui", "Ok", "OK" ); b = SW_LOCTEXT( "Ui", "Ok", "Okay" ); c = SW_LOCTEXT( "Ui", "Bad", "{n, plural, one {x}}" );)",
                             "Probe.cpp" );
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( gatherer.getIssues(), "two different source texts", true ) );
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( gatherer.getIssues(), "not a valid message pattern", true ) );
    SW_EXPECT_STREQ( "OK", TextGathererTestInternal::findText( gatherer, "Ui.Ok" )->_source.c_str() );
}

/**
 * @brief [TextGathererTest] 합치기 — 더해짐 · 바뀜 · 지워짐을 보고하고, 손으로 넣은 줄 · 맥락 · 설명 · 최대 길이는 지킨다
 * @details 키 참조가 어디에도 없으면 오류다(원문을 지어낼 수 없다). 데이터의 글은 다른 표에 그 키가 있으면 참조, 없으면 글 자체가 키가 된다.
 */
SW_TEST_CASE( TextGathererTest, MergeReportsAddedChangedRemovedAndKeepsManualRows )
{
    sw::SourceStringTable gatherTable;
    sw::string            error;
    SW_ASSERT_TRUE( gatherTable.loadFromJsonText( R"({ "culture": "en", "entries": {
        "Menu.Start": { "source": "Start", "comment": "Main menu", "maxLength": 12, "origins": [ "Old.cpp" ] },
        "Menu.Gone": { "source": "Gone", "origins": [ "Old.cpp" ] },
        "Manual.Only": { "source": "Hand written" } } })",
                                                  "gather", &error ) );
    sw::SourceStringTable otherTable;
    SW_ASSERT_TRUE( otherTable.loadFromJsonText( R"({ "culture": "en", "entries": { "settings.title": { "source": "Settings" } } })", "other", &error ) );

    sw::TextGatherer gatherer;
    gatherer.addKeyedText( "Menu.Start", "Start Game", {}, "Menu.cpp" );
    gatherer.addKeyedText( "Menu.Quit", "Quit", {}, "Menu.cpp" );
    gatherer.addKeyReference( "settings.title", "engine/settings/engine.settings.xml" );
    gatherer.addKeyReference( "settings.unknown", "engine/settings/engine.settings.xml" );
    gatherer.addTextOrKey( "Welcome, traveler!", "Dialogue line", "game/x/intro.dialogue.json" );
    gatherer.addTextOrKey( "Manual.Only", "Sign.text", "game/x/maps/a.scene.xml" );

    const sw::TextGatherReport report = gatherer.mergeInto( gatherTable, { &otherTable } );
    SW_EXPECT_TRUE( TextGathererTestInternal::contains( report._listAdded, "Menu.Quit" ) );
    SW_EXPECT_TRUE( TextGathererTestInternal::contains( report._listAdded, "Welcome, traveler!" ) );
    SW_EXPECT_TRUE( TextGathererTestInternal::contains( report._listChanged, "Menu.Start" ) );
    SW_EXPECT_TRUE( TextGathererTestInternal::contains( report._listRemoved, "Menu.Gone" ) );
    SW_EXPECT_TRUE( report.hasErrors() ); // settings.unknown
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( report._listIssue, "settings.unknown", true ) );

    const sw::SourceTextEntry* pStart = gatherTable.findEntry( "Menu.Start" );
    SW_ASSERT_NOT_NULL( pStart );
    SW_EXPECT_STREQ( "Start Game", pStart->_source.c_str() );
    SW_EXPECT_STREQ( "Main menu", pStart->_comment.c_str() );
    SW_EXPECT_EQUAL( uint32( 12 ), pStart->_maxLength );
    SW_EXPECT_STREQ( "Menu.cpp", pStart->_listOrigin.front().c_str() );

    const sw::SourceTextEntry* pManual = gatherTable.findEntry( "Manual.Only" );
    SW_ASSERT_NOT_NULL( pManual ); // 손으로 넣은 줄은 남고, 참조되면 자리를 단다
    SW_EXPECT_STREQ( "Hand written", pManual->_source.c_str() );
    SW_EXPECT_EQUAL( size_t( 1 ), pManual->_listOrigin.size() );

    const sw::SourceTextEntry* pWelcome = gatherTable.findEntry( "Welcome, traveler!" );
    SW_ASSERT_NOT_NULL( pWelcome );
    SW_EXPECT_STREQ( "Dialogue line", pWelcome->_context.c_str() );
    SW_EXPECT_NULL( gatherTable.findEntry( "settings.title" ) ); // 다른 표의 키는 이 표에 더하지 않는다
}

/**
 * @brief [TextGathererTest] 리플렉션 XML — `Localizable` 프로퍼티의 값을 모으고(최대 길이 메타 포함), 표시 없는 프로퍼티의 문장은 하드코딩 의심으로 경고한다
 */
SW_TEST_CASE( TextGathererTest, ReflectedXmlGathersLocalizablePropertiesAndFlagsHardcodedText )
{
#if !defined( SW_SHIPPING )
    TextGathererTestInternal::registerProbeType();
    const sw::string_view kScene = R"(<Scene>
        <GameObject _name="Sign">
            <_listComponent>
                <LocGatherProbe _label="Welcome to the farm" _debugName="Sign A - north gate" _tooltip="Press E to read the sign" />
                <LocGatherProbe _label="ui.sign.title" _tooltip="icons/sign_icon" />
            </_listComponent>
        </GameObject>
    </Scene>)";
    sw::TextGatherer      gatherer;
    gatherer.gatherReflectedXml( kScene, "game/test/maps/farm.scene.xml" );

    const sw::GatheredText* pWelcome = TextGathererTestInternal::findText( gatherer, "Welcome to the farm" );
    SW_ASSERT_NOT_NULL( pWelcome );
    SW_EXPECT_TRUE( pWelcome->_kind == sw::GatheredTextKind::TextOrKey );
    SW_EXPECT_STREQ( "LocGatherProbe._label", pWelcome->_context.c_str() );
    SW_EXPECT_EQUAL( uint32( 16 ), pWelcome->_maxLength );
    SW_EXPECT_NOT_NULL( TextGathererTestInternal::findText( gatherer, "ui.sign.title" ) );

    // 표시 없는 문장 프로퍼티는 경고, NotLocalizable · 경로는 조용하다.
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( gatherer.getIssues(), "Press E to read the sign", false ) );
    SW_EXPECT_FALSE( TextGathererTestInternal::hasIssueContaining( gatherer.getIssues(), "north gate", false ) );
    SW_EXPECT_FALSE( TextGathererTestInternal::hasIssueContaining( gatherer.getIssues(), "sign_icon", false ) );
#endif
}

/**
 * @brief [TextGathererTest] 대화 에셋과 프로젝트 규칙(손으로 읽는 XML 카탈로그 · 키 참조)도 같은 수집기로 들어간다
 */
SW_TEST_CASE( TextGathererTest, DialogueAndAssetRulesAreGathered )
{
    sw::LocalizationProject project;
    sw::string              error;
    SW_ASSERT_TRUE( project.loadFromJsonText( R"({ "name": "p", "sourceCulture": "en", "stringTables": [ "p.strings.json" ], "assetRules": [
        { "files": "items.xml", "elements": [ "Item" ], "attribute": "name", "kind": "text", "context": "Item name" },
        { "files": ".settings.xml", "elements": [ "Setting" ], "attribute": "text", "kind": "key" } ] })",
                                              "p", &error ) );
    SW_EXPECT_FALSE( project.loadFromJsonText( R"({ "name": "p", "sourceCulture": "en", "stringTables": [ "a" ], "assetRules": [ { "files": "x.xml", "elements": [ "A" ], "attribute": "b", "kind": "maybe" } ] })",
                                               "p", &error ) );

    sw::TextGatherer gatherer;
    sw::LocalizationTools::gatherAssetFile( project, gatherer, R"(<ItemCatalog><Item id="a" name="Combat Helmet"/><Item id="b" name="Recon Vest"/></ItemCatalog>)",
                                            "game/x/data/items.xml" );
    sw::LocalizationTools::gatherAssetFile( project, gatherer, R"(<UserSettingsSchema><Setting id="a" text="settings.a"/></UserSettingsSchema>)",
                                            "game/x/data/x.settings.xml" );
    sw::LocalizationTools::gatherAssetFile(
        project, gatherer, R"({ "nodes": [ { "id": 1, "type": "Dialogue", "speaker": "npc.elder", "text": "Welcome back.", "choices": [ "Yes", "No" ] } ], "links": [] })",
        "game/x/dialogue/intro.dialogue.json" );

    const sw::GatheredText* pHelmet = TextGathererTestInternal::findText( gatherer, "Combat Helmet" );
    SW_ASSERT_NOT_NULL( pHelmet );
    SW_EXPECT_STREQ( "Item name", pHelmet->_context.c_str() );
    const sw::GatheredText* pSettingKey = TextGathererTestInternal::findText( gatherer, "settings.a" );
    SW_ASSERT_NOT_NULL( pSettingKey );
    SW_EXPECT_TRUE( pSettingKey->_kind == sw::GatheredTextKind::KeyReference );
    SW_EXPECT_NOT_NULL( TextGathererTestInternal::findText( gatherer, "Welcome back." ) );
    SW_EXPECT_NOT_NULL( TextGathererTestInternal::findText( gatherer, "npc.elder" ) );
    SW_EXPECT_NOT_NULL( TextGathererTestInternal::findText( gatherer, "No" ) );
}

/**
 * @brief [TextGathererTest] 프로젝트 수집 — 코드 폴더 · 데이터 폴더를 훑어 원문 표를 쓰고, 해시 없는 번역에 해시를 찍으며, 확인 모드는 쓰지 않고 낡았다고만 한다
 * @details 수집기가 원문을 바꾸면 그 번역은 낡은 것이 된다(해시가 그대로이므로). 확인 모드(`--check-text`)는 CI 가 "표를 갱신하지 않은 커밋" 을 잡는 자리다.
 */
SW_TEST_CASE( TextGathererTest, GatherProjectWritesTableStampsHashesAndCheckModeDetectsDrift )
{
    const sw::string repositoryRoot = test::makeTempDirectory( "loc_gather_repo" );
    const sw::string codeFolder     = sw::FileUtil::joinPath( repositoryRoot, "Code" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( codeFolder ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( codeFolder, "Menu.cpp" ), R"(auto a = SW_LOCTEXT( "Menu", "Start", "Start" );)" ) );

    const sw::string locFolder   = sw::FileUtil::joinPath( repositoryRoot, "Loc" );
    const sw::string projectPath = LocalizationTestUtil::writeProject( locFolder, "en", R"([ "ko" ])", R"("codeRoots": [ "Code" ])" );
    LocalizationTestUtil::writeSourceTable( locFolder, "en", R"("Menu.Start": { "source": "Start" })" );
    LocalizationTestUtil::writeTranslation( locFolder, "ko", R"("Menu.Start": { "text": "시작" })" );

    sw::LocalizationGatherResult result;
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, true, result ) );
    SW_EXPECT_EQUAL( uint32( 1 ), result._fileCount );
    SW_EXPECT_FALSE( result._report.hasTextChanges() );
    SW_ASSERT_EQUAL( size_t( 1 ), result._listCulture.size() );
    SW_EXPECT_TRUE( result._listCulture[0]._bChanged ); // 해시를 찍었다
    SW_EXPECT_EQUAL( uint32( 1 ), result._listCulture[0]._currentCount );

    // 원문을 고친다 → 확인 모드는 쓰지 않고 낡았다고만 한다 → 수집하면 번역은 낡은 것이 된다.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( codeFolder, "Menu.cpp" ), R"(auto a = SW_LOCTEXT( "Menu", "Start", "Start Game" );)" ) );
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, false, result ) );
    SW_EXPECT_TRUE( result._bOutOfDate );
    SW_EXPECT_FALSE( result._bWritten );
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, true, result ) );
    SW_EXPECT_EQUAL( size_t( 1 ), result._report._listChanged.size() );
    SW_EXPECT_EQUAL( uint32( 1 ), result._listCulture[0]._staleCount );
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, false, result ) );
    SW_EXPECT_FALSE( result._bOutOfDate ); // 다시 확인하면 최신
}

/**
 * @brief [TextGathererTest] 저장소의 로컬라이제이션 프로젝트는 최신이다 — 코드 · 데이터를 고치고 `App --gather-text` 를 돌리지 않은 커밋을 잡는다
 */
SW_TEST_CASE( TextGathererTest, RepositoryProjectsAreUpToDate )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "the shipping build carries no property metadata (Localizable) - gathering runs in development builds" );
#endif
    sw::vector<sw::string> listProjectPath;
    sw::LocalizationTools::collectProjectPaths( listProjectPath, true );
    SW_ASSERT_TRUE( listProjectPath.size() >= 2u ); // 엔진 + Shooter3D
    const sw::string repositoryRoot = sw::LocalizationTools::findRepositoryRoot();
    for ( const sw::string& projectPath : listProjectPath )
    {
        sw::LocalizationGatherResult result;
        SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, false, result ) );
        SW_EXPECT_TRUE_MSG( result._bOutOfDate == false, ( projectPath + " is out of date - run App --gather-text" ).c_str() );
        SW_EXPECT_TRUE_MSG( result._report.hasErrors() == false, ( projectPath + " has gather errors" ).c_str() );
    }
}

/**
 * @brief [TextGathererTest] 번역의 리치 텍스트 태그 열이 원문과 다르면 보고한다 — "[b]Start[/b]" 의 번역 "시작" 은 굵게를 잃었다
 */
SW_TEST_CASE( TextGathererTest, TranslationTagMismatchIsReported )
{
    const sw::string repositoryRoot = test::makeTempDirectory( "loc_gather_tags" );
    const sw::string codeFolder     = sw::FileUtil::joinPath( repositoryRoot, "Code" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( codeFolder ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( codeFolder, "Menu.cpp" ),
                                                 R"(auto a = SW_LOCTEXT( "Menu", "Start", "[b]Start[/b]" ); auto b = SW_LOCTEXT( "Menu", "Quit", "[b]Quit[/b]" );)" ) );
    const sw::string locFolder   = sw::FileUtil::joinPath( repositoryRoot, "Loc" );
    const sw::string projectPath = LocalizationTestUtil::writeProject( locFolder, "en", R"([ "ko" ])", R"("codeRoots": [ "Code" ])" );
    LocalizationTestUtil::writeSourceTable( locFolder, "en", R"("Menu.Start": { "source": "[b]Start[/b]" }, "Menu.Quit": { "source": "[b]Quit[/b]" })" );
    LocalizationTestUtil::writeTranslation( locFolder, "ko", R"("Menu.Start": { "text": "시작" }, "Menu.Quit": { "text": "[b]종료[/b]" })" );

    sw::LocalizationGatherResult result;
    SW_ASSERT_TRUE( sw::LocalizationTools::gatherProject( projectPath, repositoryRoot, false, result ) );
    SW_EXPECT_TRUE( TextGathererTestInternal::hasIssueContaining( result._report._listIssue, "ko:Menu.Start", false ) );
    SW_EXPECT_FALSE( TextGathererTestInternal::hasIssueContaining( result._report._listIssue, "ko:Menu.Quit", false ) );
}
