#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/UI/Document/UiDocument.h"
#include "Engine/UI/Document/UiDocumentCache.h"
#include "Engine/UI/Document/UiDocumentLoader.h"
#include "Engine/UI/Document/UiDocumentWriter.h"
#include "Engine/UI/Layout/SafeZonePanel.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UI/Widgets/UserWidget.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "TestFramework/TestFramework.h"

// UiDocumentTest — UI 문서(*.ui.xml): 위젯 원소 = 리플렉션 타입 · 속성 = PROPERTY · 조각(UserWidget)의 이름 범위 · 바인딩 식 떼기 · 명령 · 캐시 · 다시 쓰기.
// 문서는 메모리 문서(UiDocumentCache::registerMemoryDocument)로 넣고, 엔진 견본(engine/ui/pause.ui.xml)만 리소스에서 읽는다. 디바이스 없음(nogpu).

namespace
{
    struct UiDocumentTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        /** @brief 입력 한 프레임 → UI 입력 → UI 갱신(레이아웃 · 그리기)입니다. */
        static void runFrame( sw::InputManager& input, sw::UiSystem& ui )
        {
            input.beginFrame( kFrameSeconds );
            ui.processInput( kFrameSeconds );
            ui.update( kFrameSeconds, sw::UiViewport{
                                          sw::float2{ 1920.0f, 1080.0f }
            } );
            input.endFrame();
        }

        /** @brief 문서 @p path 를 캐시에서 읽어 짓고, 실패 문구를 @p outError 에 둡니다. */
        static sw::unique_ptr<sw::Widget> build( sw::UiDocumentCache& cache, const utf8* pPath, sw::vector<sw::UiBindingDesc>& outListBinding, sw::string& outError )
        {
            const sw::shared_ptr<const sw::UiDocumentAsset> document = cache.findOrLoad( pPath, outError );
            if ( document == nullptr )
                return {};
            return sw::UiDocumentLoader::instantiate( *document, cache, outListBinding, outError );
        }
    };

    /** @brief 입력 관리자와 그것을 읽는 UI 시스템입니다. */
    struct UiDocumentFixture
    {
        sw::InputManager _input;
        sw::UiSystem     _ui;

        UiDocumentFixture()
            : _input{}
            , _ui{}
        {
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
        }

        ~UiDocumentFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UiDocumentFixture( const UiDocumentFixture& )            = delete;
        UiDocumentFixture& operator=( const UiDocumentFixture& ) = delete;
    };

    /** @brief 명령을 덮어쓴 C++ 화면 — 받은 명령을 센다. */
    class CountingScreen : public sw::UiScreen
    {
    public:
        CountingScreen( const sw::UiScreenDesc& desc, sw::unique_ptr<sw::Widget> root )
            : sw::UiScreen{ desc, std::move( root ) }
            , _lastCommand{}
            , _commandCount{ 0 }
        {
        }

        bool onCommand( const sw::hashed_string& command, sw::Widget& source ) override
        {
            (void)source;
            _lastCommand = command;
            ++_commandCount;
            return true;
        }

        sw::hashed_string _lastCommand;
        uint32            _commandCount;
    };
} // namespace

/** @brief [UiDocumentTest] 엔진 견본(일시정지 메뉴): 루트 SafeZonePanel · 이름으로 찾는 버튼 · 화면 서술 · 조각 안의 이름 */
SW_TEST_CASE( UiDocumentTest, LoadsPauseMenuDocument )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    UiDocumentFixture        fixture;
    const sw::UiScreenHandle handle  = fixture._ui.openScreen( "engine/ui/pause.ui.xml" );
    sw::UiScreen*            pScreen = fixture._ui.findScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );

    SW_EXPECT_TRUE( sw::castTo<sw::SafeZonePanel>( pScreen->getTree().getRoot() ) != nullptr );
    const sw::ButtonWidget* pResume = pScreen->getTree().findWidget<sw::ButtonWidget>( "Resume" );
    SW_ASSERT_NOT_NULL( pResume );
    SW_EXPECT_STREQ( "Resume", pResume->getCommand().c_str() );
    SW_EXPECT_STREQ( "Resume", pScreen->getDesc()._defaultFocus.c_str() );
    SW_EXPECT_TRUE( pScreen->getDesc()._bPausesGame );
    SW_EXPECT_STREQ( "engine/ui/pause.ui.xml", pScreen->getDocumentPath().c_str() );
    SW_EXPECT_TRUE( pScreen->getTree().findWidget<sw::TextWidget>( "Hint.Label" ) != nullptr ); // 조각(inputhint) 안의 이름은 조각 위젯 이름으로 감싼다
    SW_EXPECT_TRUE( pScreen->getTree().findWidget<sw::UserWidget>( "Hint" ) != nullptr );
}

/** @brief [UiDocumentTest] 위젯도 칸도 아닌 원소는 로드 오류다 — 문구에 파일 · 줄 */
SW_TEST_CASE( UiDocumentTest, UnknownElementIsLoadError )
{
    sw::UiDocumentCache cache;
    cache.registerMemoryDocument( "test/unknownelement.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                "\t<BoxPanel>\n"
                                                                "\t\t<Bogus />\n"
                                                                "\t</BoxPanel>\n"
                                                                "</UiDocument>\n" );
    sw::string error;
    SW_EXPECT_TRUE( cache.findOrLoad( "test/unknownelement.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/unknownelement.ui.xml:3:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "unknown element <Bogus>" ) != sw::string::npos, error.c_str() );

    cache.registerMemoryDocument( "test/unknownroot.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                             "\t<NotAWidget />\n"
                                                             "</UiDocument>\n" );
    SW_EXPECT_TRUE( cache.findOrLoad( "test/unknownroot.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/unknownroot.ui.xml:2:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( cache.getCachedCount() ) ); // 실패는 캐시에 남지 않는다
}

/** @brief [UiDocumentTest] 모르는 속성은 파싱 오류, 모르는 열거자 · 읽지 못한 값은 짓기 오류다 — 조용히 기본값이 되지 않는다 */
SW_TEST_CASE( UiDocumentTest, UnknownAttributeIsLoadError )
{
    sw::UiDocumentCache cache;
    cache.registerMemoryDocument( "test/unknownattribute.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                  "\t<BoxPanel>\n"
                                                                  "\t\t<TextWidget _txet=\"Hello\" />\n"
                                                                  "\t</BoxPanel>\n"
                                                                  "</UiDocument>\n" );
    sw::string error;
    SW_EXPECT_TRUE( cache.findOrLoad( "test/unknownattribute.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/unknownattribute.ui.xml:3:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "unknown attribute '_txet'" ) != sw::string::npos, error.c_str() );

    cache.registerMemoryDocument( "test/unknownenumerator.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                   "\t<BoxPanel _orientation=\"Diagonal\" />\n"
                                                                   "</UiDocument>\n" );
    sw::vector<sw::UiBindingDesc> listBinding;
    test::ScopedLogCollector      logs; // 열거자 경고는 문구로 본다
    SW_EXPECT_TRUE( UiDocumentTestUtil::build( cache, "test/unknownenumerator.ui.xml", listBinding, error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/unknownenumerator.ui.xml:2:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "Diagonal" ) != sw::string::npos, error.c_str() );

    cache.registerMemoryDocument( "test/version.ui.xml", "<UiDocument _schemaVersion=\"0\">\n"
                                                         "\t<BoxPanel />\n"
                                                         "</UiDocument>\n" );
    SW_EXPECT_TRUE( cache.findOrLoad( "test/version.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "_schemaVersion" ) != sw::string::npos, error.c_str() );
}

/** @brief [UiDocumentTest] 패널이 아닌 위젯에 자식 위젯을 적으면 로드 오류다 */
SW_TEST_CASE( UiDocumentTest, NonPanelWithChildrenIsError )
{
    sw::UiDocumentCache cache;
    cache.registerMemoryDocument( "test/textchild.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                           "\t<TextWidget>\n"
                                                           "\t\t<TextWidget />\n"
                                                           "\t</TextWidget>\n"
                                                           "</UiDocument>\n" );
    sw::string error;
    SW_EXPECT_TRUE( cache.findOrLoad( "test/textchild.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/textchild.ui.xml:3:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "cannot have children" ) != sw::string::npos, error.c_str() );
}

/** @brief [UiDocumentTest] 조각이 돌고 돌아 자기를 다시 부르면(a → b → a) 짓기 오류다 — 무한히 짓지 않는다 */
SW_TEST_CASE( UiDocumentTest, RecursiveUserWidgetIsError )
{
    sw::UiDocumentCache cache;
    cache.registerMemoryDocument( "test/a.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                   "\t<UserWidget _name=\"B\" _document=\"test/b.ui.xml\" />\n"
                                                   "</UiDocument>\n" );
    cache.registerMemoryDocument( "test/b.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                   "\t<BoxPanel>\n"
                                                   "\t\t<UserWidget _name=\"A\" _document=\"test/a.ui.xml\" />\n"
                                                   "\t</BoxPanel>\n"
                                                   "</UiDocument>\n" );
    sw::vector<sw::UiBindingDesc> listBinding;
    sw::string                    error;
    SW_EXPECT_TRUE( UiDocumentTestUtil::build( cache, "test/a.ui.xml", listBinding, error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "fragment includes itself" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "test/b.ui.xml:3:" ) != sw::string::npos, error.c_str() );

    cache.registerMemoryDocument( "test/self.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                      "\t<UserWidget _document=\"test/self.ui.xml\" />\n"
                                                      "</UiDocument>\n" );
    SW_EXPECT_TRUE( UiDocumentTestUtil::build( cache, "test/self.ui.xml", listBinding, error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "fragment includes itself" ) != sw::string::npos, error.c_str() );
}

/** @brief [UiDocumentTest] 같은 조각을 두 번 쓰면 안쪽 이름이 "A.Label" · "B.Label" 로 갈린다 — 조각 안의 조각은 "A.Inner.Label" */
SW_TEST_CASE( UiDocumentTest, UserWidgetNamesAreScoped )
{
    UiDocumentFixture fixture;
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/part.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                               "\t<BoxPanel>\n"
                                                                               "\t\t<TextWidget _name=\"Label\" _text=\"Part\" />\n"
                                                                               "\t</BoxPanel>\n"
                                                                               "</UiDocument>\n" );
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/outer.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                                "\t<UserWidget _name=\"Inner\" _document=\"test/part.ui.xml\" />\n"
                                                                                "</UiDocument>\n" );
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/twice.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                                "\t<BoxPanel>\n"
                                                                                "\t\t<UserWidget _name=\"A\" _document=\"test/part.ui.xml\" />\n"
                                                                                "\t\t<UserWidget _name=\"B\" _document=\"test/part.ui.xml\" />\n"
                                                                                "\t\t<UserWidget _name=\"C\" _document=\"test/outer.ui.xml\" />\n"
                                                                                "\t</BoxPanel>\n"
                                                                                "</UiDocument>\n" );
    sw::UiScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( "test/twice.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::WidgetTree& tree = pScreen->getTree();
    const sw::TextWidget* pA   = tree.findWidget<sw::TextWidget>( "A.Label" );
    const sw::TextWidget* pB   = tree.findWidget<sw::TextWidget>( "B.Label" );
    SW_ASSERT_NOT_NULL( pA );
    SW_ASSERT_NOT_NULL( pB );
    SW_EXPECT_TRUE( pA != pB );
    SW_EXPECT_TRUE( tree.findWidgetByName( "Label" ) == nullptr );
    SW_EXPECT_TRUE( tree.findWidget<sw::TextWidget>( "C.Inner.Label" ) != nullptr );
    SW_EXPECT_STREQ( "Part", pA->getText().c_str() );
}

/** @brief [UiDocumentTest] 버튼 클릭(누르고 같은 버튼 위에서 떼기) · UI.Accept 는 화면 명령으로 간다 — 밖에서 떼면 클릭이 아니다 */
SW_TEST_CASE( UiDocumentTest, CommandRoutesToScreen )
{
    using Util = UiDocumentTestUtil;
    UiDocumentFixture fixture;
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/command.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                                  "\t<CanvasPanel>\n"
                                                                                  "\t\t<ButtonWidget _name=\"Go\" _command=\"Go\">\n"
                                                                                  "\t\t\t<_slot _offsetMin=\"100,100\" _offsetMax=\"300,160\" />\n"
                                                                                  "\t\t</ButtonWidget>\n"
                                                                                  "\t</CanvasPanel>\n"
                                                                                  "</UiDocument>\n" );
    // 기본 화면 — 등록한 함수로
    uint32                   goCount = 0;
    const sw::UiScreenHandle handle  = fixture._ui.openScreen( "test/command.ui.xml" );
    sw::UiScreen*            pScreen = fixture._ui.findScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );
    pScreen->registerCommand( "Go", SW_DELEGATE_LAMBDA( sw::UiCommandDelegate, [&goCount]( const sw::hashed_string&, sw::Widget& )
    { ++goCount; } ) );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseMove( 200, 130 ) ) );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 200, 130 ) ) );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE( pScreen->getTree().findWidget<sw::ButtonWidget>( "Go" )->isPressed() );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Left, 200, 130 ) ) );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_EQUAL( 1u, goCount );

    // 누르고 밖으로 끌어 떼면 클릭이 아니다(잡은 포인터라 뗌은 버튼이 받는다)
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 200, 130 ) ) );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseMove( 600, 600 ) ) );
    SW_EXPECT_TRUE( fixture._input.postRawEvent( sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Left, 600, 600 ) ) );
    Util::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_EQUAL( 1u, goCount );
    SW_EXPECT_FALSE( pScreen->getTree().findWidget<sw::ButtonWidget>( "Go" )->isPressed() );
    fixture._ui.closeScreen( handle );
    Util::runFrame( fixture._input, fixture._ui );

    // C++ 화면 클래스 — onCommand 덮어쓰기, 포커스를 쥔 버튼의 확인
    const sw::UiScreenHandle countingHandle = fixture._ui.openScreen<CountingScreen>( "test/command.ui.xml" );
    auto*                    pCounting      = static_cast<CountingScreen*>( fixture._ui.findScreen( countingHandle ) );
    SW_ASSERT_NOT_NULL( pCounting );
    sw::ButtonWidget* pGo = pCounting->getTree().findWidget<sw::ButtonWidget>( "Go" );
    SW_ASSERT_NOT_NULL( pGo );
    SW_ASSERT_TRUE( fixture._ui.getFocusManager().setFocus( pCounting->getTree(), pGo->getId() ) );
    sw::UiActionEvent accept{};
    accept._action = sw::hashed_string( sw::UiActionName::kAccept );
    SW_EXPECT_TRUE( pGo->onActionEvent( accept, sw::UiRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 1u, pCounting->_commandCount );
    SW_EXPECT_STREQ( "Go", pCounting->_lastCommand.c_str() );
}

/** @brief [UiDocumentTest] 같은 문서로 화면을 두 번 열어도 문서(와 조각)는 한 번만 읽어 파싱한다 */
SW_TEST_CASE( UiDocumentTest, CacheParsesOnce )
{
    UiDocumentFixture    fixture;
    sw::UiDocumentCache& cache = fixture._ui.getDocumentCache();
    cache.registerMemoryDocument( "test/part.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                      "\t<TextWidget _name=\"Label\" />\n"
                                                      "</UiDocument>\n" );
    cache.registerMemoryDocument( "test/menu.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                      "\t<BoxPanel>\n"
                                                      "\t\t<UserWidget _name=\"A\" _document=\"test/part.ui.xml\" />\n"
                                                      "\t\t<UserWidget _name=\"B\" _document=\"test/part.ui.xml\" />\n"
                                                      "\t</BoxPanel>\n"
                                                      "</UiDocument>\n" );
    const sw::UiScreenHandle first  = fixture._ui.openScreen( "test/menu.ui.xml" );
    const sw::UiScreenHandle second = fixture._ui.openScreen( "test/menu.ui.xml" );
    SW_EXPECT_TRUE( fixture._ui.findScreen( first ) != nullptr );
    SW_EXPECT_TRUE( fixture._ui.findScreen( second ) != nullptr );
    SW_EXPECT_EQUAL( 2u, cache.getParseCount() ); // 문서 하나 + 조각 하나
    SW_EXPECT_TRUE( cache.isCached( "test/menu.ui.xml" ) );
    SW_EXPECT_TRUE( cache.isCached( "test/part.ui.xml" ) );
}

/** @brief [UiDocumentTest] `{` 로 시작하는 속성 값은 값으로 읽지 않고 바인딩 식으로 뗀다 — 구조체 칸 안의 것도 경로와 함께 */
SW_TEST_CASE( UiDocumentTest, BindingAttributesAreExtracted )
{
    UiDocumentFixture fixture;
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/binding.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                                  "\t<BoxPanel>\n"
                                                                                  "\t\t<TextWidget _name=\"Health\" _text=\"{bind:_health}\" />\n"
                                                                                  "\t\t<TextWidget _name=\"Bar\">\n"
                                                                                  "\t\t\t<_slot _widthOverride=\"{bind:_barWidth, mode=OneWay}\" />\n"
                                                                                  "\t\t</TextWidget>\n"
                                                                                  "\t</BoxPanel>\n"
                                                                                  "</UiDocument>\n" );
    sw::UiScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( "test/binding.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::TextWidget* pHealth = pScreen->getTree().findWidget<sw::TextWidget>( "Health" );
    const sw::TextWidget* pBar    = pScreen->getTree().findWidget<sw::TextWidget>( "Bar" );
    SW_ASSERT_NOT_NULL( pHealth );
    SW_ASSERT_NOT_NULL( pBar );
    SW_EXPECT_TRUE( pHealth->getText().empty() ); // 식은 칸에 들어가지 않는다

    const sw::vector<sw::UiBindingDesc>& listBinding = pScreen->getBindings();
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listBinding.size() ) );
    SW_EXPECT_EQUAL( pHealth->getId(), listBinding[0]._widget );
    SW_EXPECT_STREQ( "_text", listBinding[0]._propertyPath.c_str() );
    SW_EXPECT_STREQ( "{bind:_health}", listBinding[0]._expression.c_str() );
    SW_EXPECT_EQUAL( 3u, listBinding[0]._sourceLine );
    SW_EXPECT_EQUAL( pBar->getId(), listBinding[1]._widget );
    SW_EXPECT_STREQ( "_slot._widthOverride", listBinding[1]._propertyPath.c_str() );
    SW_EXPECT_STREQ( "{bind:_barWidth, mode=OneWay}", listBinding[1]._expression.c_str() );
    SW_EXPECT_EQUAL( 0.0f, pBar->getLayoutSlot()._widthOverride );
}

/** @brief [UiDocumentTest] 읽고 다시 쓰면 같은 문서다 — 기본값과 같은 칸은 쓰지 않고, 조각은 원소만, 바인딩 식은 식 그대로 */
SW_TEST_CASE( UiDocumentTest, RoundTripSaveMatchesSource )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    UiDocumentFixture fixture;
    const utf8*       kPausePath = "engine/ui/pause.ui.xml";
    sw::string        sourceText;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( kPausePath, sourceText ) );
    sw::XmlDocument source;
    SW_ASSERT_TRUE( source.parse( sourceText ) );

    sw::UiScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( kPausePath ) );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::string written = sw::UiDocumentWriter::write( pScreen->getDesc(), {}, *pScreen->getTree().getRoot(), pScreen->getBindings() );
    SW_EXPECT_STREQ( source.saveToString().c_str(), written.c_str() );

    // 바인딩 식이 든 문서도 식 그대로 돌아온다
    const utf8* kBindingText = "<UiDocument _schemaVersion=\"1\">\n"
                               "\t<BoxPanel _spacing=\"4\">\n"
                               "\t\t<TextWidget _name=\"Health\" _text=\"{bind:_health}\" />\n"
                               "\t</BoxPanel>\n"
                               "</UiDocument>\n";
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/roundtrip.ui.xml", kBindingText );
    sw::UiScreen* pBound = fixture._ui.findScreen( fixture._ui.openScreen( "test/roundtrip.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pBound );
    sw::XmlDocument boundSource;
    SW_ASSERT_TRUE( boundSource.parse( kBindingText ) );
    SW_EXPECT_STREQ( boundSource.saveToString().c_str(),
                     sw::UiDocumentWriter::write( pBound->getDesc(), {}, *pBound->getTree().getRoot(), pBound->getBindings() ).c_str() );
}
