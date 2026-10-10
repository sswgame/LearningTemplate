#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/UI/Animation/UIAnimation.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/Document/UIDocumentWriter.h"
#include "Engine/UI/Layout/SafeZonePanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Style/UIStyleSheetCache.h"
#include "Engine/UI/Style/WidgetStyle.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UI/Widgets/UserWidget.h"

#include "TestFramework/TestFramework.h"

// UIDocumentTest — UI 문서(*.ui.xml): 위젯 원소 = 리플렉션 타입 · 속성 = PROPERTY · 조각(UserWidget)의 이름 범위 · 바인딩 식 떼기 · 명령 · 캐시 · 다시 쓰기 ·
// 핫 리로드(문서 · 스타일 · 조각, 실패는 옛 트리 유지).
// 문서는 메모리 문서(UIDocumentCache::registerMemoryDocument)로 넣고, 엔진 견본(engine/ui/pause.ui.xml)만 리소스에서 읽는다. 디바이스 없음(nogpu).

namespace
{
    struct UIDocumentTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        /** @brief 입력 한 프레임 → UI 입력 → UI 갱신(레이아웃 · 그리기)입니다. */
        static void runFrame( sw::InputManager& input, sw::UISystem& ui )
        {
            input.beginFrame( kFrameSeconds );
            ui.processInput( kFrameSeconds );
            ui.update( kFrameSeconds, sw::UIViewport{
                                          sw::float2{ 1920.0f, 1080.0f }
            } );
            input.endFrame();
        }

        /** @brief 문서 @p path 를 캐시에서 읽어 짓고, 실패 문구를 @p outError 에 둡니다. */
        static sw::unique_ptr<sw::Widget> build( sw::UIDocumentCache& cache, const utf8* pPath, sw::vector<sw::UIBindingDesc>& outListBinding, sw::string& outError )
        {
            const sw::shared_ptr<const sw::UIDocumentAsset> document = cache.findOrLoad( pPath, outError );
            if ( document == nullptr )
                return {};
            return sw::UIDocumentLoader::instantiate( *document, cache, outListBinding, outError );
        }
    };

    /** @brief 입력 관리자와 그것을 읽는 UI 시스템입니다. */
    struct UIDocumentFixture
    {
        sw::InputManager _input;
        sw::UISystem     _ui;

        UIDocumentFixture()
            : _input{}
            , _ui{}
        {
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
        }

        ~UIDocumentFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UIDocumentFixture( const UIDocumentFixture& )            = delete;
        UIDocumentFixture& operator=( const UIDocumentFixture& ) = delete;
    };

    /** @brief 명령을 덮어쓴 C++ 화면 — 받은 명령을 센다. */
    class CountingScreen : public sw::UIScreen
    {
    public:
        CountingScreen( const sw::UIScreenDesc& desc, sw::unique_ptr<sw::Widget> root )
            : sw::UIScreen{ desc, std::move( root ) }
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

    struct UIReloadTestUtil
    {
        /** @brief 버튼 셋(A · B · 넣으면 C)과 긴 스크롤 목록이 든 문서 글입니다. */
        static sw::string makeReloadDocument( bool bWithC, const utf8* pBCommand )
        {
            sw::string text = "<UIDocument _schemaVersion=\"1\">\n"
                              "\t<BoxPanel _orientation=\"Vertical\">\n"
                              "\t\t<ButtonWidget _name=\"A\" />\n";
            text += sw::string( "\t\t<ButtonWidget _name=\"B\" _command=\"" ) + pBCommand + "\" />\n";
            if ( bWithC )
                text += "\t\t<ButtonWidget _name=\"C\" />\n";
            text += "\t\t<ScrollPanel _name=\"Scroll\">\n"
                    "\t\t\t<_slot _heightOverride=\"100\" />\n"
                    "\t\t\t<BorderPanel>\n"
                    "\t\t\t\t<_slot _heightOverride=\"1000\" />\n"
                    "\t\t\t</BorderPanel>\n"
                    "\t\t</ScrollPanel>\n"
                    "\t</BoxPanel>\n"
                    "</UIDocument>\n";
            return text;
        }
    };
} // namespace

/** @brief [UIDocumentTest] 엔진 견본(일시정지 메뉴): 루트 SafeZonePanel · 이름으로 찾는 버튼 · 화면 서술 · 조각 안의 이름 */
SW_TEST_CASE( UIDocumentTest, LoadsPauseMenuDocument )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    UIDocumentFixture        fixture;
    const sw::UIScreenHandle handle  = fixture._ui.openScreen( "engine/ui/pause.ui.xml" );
    sw::UIScreen*            pScreen = fixture._ui.findScreen( handle );
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

/** @brief [UIDocumentTest] 위젯도 칸도 아닌 원소는 로드 오류다 — 문구에 파일 · 줄 */
SW_TEST_CASE( UIDocumentTest, UnknownElementIsLoadError )
{
    sw::UIDocumentCache cache;
    cache.registerMemoryDocument( "test/unknownelement.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                "\t<BoxPanel>\n"
                                                                "\t\t<Bogus />\n"
                                                                "\t</BoxPanel>\n"
                                                                "</UIDocument>\n" );
    sw::string error;
    SW_EXPECT_TRUE( cache.findOrLoad( "test/unknownelement.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/unknownelement.ui.xml:3:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "unknown element <Bogus>" ) != sw::string::npos, error.c_str() );

    cache.registerMemoryDocument( "test/unknownroot.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                             "\t<NotAWidget />\n"
                                                             "</UIDocument>\n" );
    SW_EXPECT_TRUE( cache.findOrLoad( "test/unknownroot.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/unknownroot.ui.xml:2:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( cache.getCachedCount() ) ); // 실패는 캐시에 남지 않는다
}

/** @brief [UIDocumentTest] 모르는 속성은 파싱 오류, 모르는 열거자 · 읽지 못한 값은 짓기 오류다 — 조용히 기본값이 되지 않는다 */
SW_TEST_CASE( UIDocumentTest, UnknownAttributeIsLoadError )
{
    sw::UIDocumentCache cache;
    cache.registerMemoryDocument( "test/unknownattribute.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                  "\t<BoxPanel>\n"
                                                                  "\t\t<TextWidget _txet=\"Hello\" />\n"
                                                                  "\t</BoxPanel>\n"
                                                                  "</UIDocument>\n" );
    sw::string error;
    SW_EXPECT_TRUE( cache.findOrLoad( "test/unknownattribute.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/unknownattribute.ui.xml:3:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "unknown attribute '_txet'" ) != sw::string::npos, error.c_str() );

    cache.registerMemoryDocument( "test/unknownenumerator.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                   "\t<BoxPanel _orientation=\"Diagonal\" />\n"
                                                                   "</UIDocument>\n" );
    sw::vector<sw::UIBindingDesc> listBinding;
    test::ScopedLogCollector      logs; // 열거자 경고는 문구로 본다
    SW_EXPECT_TRUE( UIDocumentTestUtil::build( cache, "test/unknownenumerator.ui.xml", listBinding, error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/unknownenumerator.ui.xml:2:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "Diagonal" ) != sw::string::npos, error.c_str() );

    cache.registerMemoryDocument( "test/version.ui.xml", "<UIDocument _schemaVersion=\"0\">\n"
                                                         "\t<BoxPanel />\n"
                                                         "</UIDocument>\n" );
    SW_EXPECT_TRUE( cache.findOrLoad( "test/version.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "_schemaVersion" ) != sw::string::npos, error.c_str() );
}

/** @brief [UIDocumentTest] 패널이 아닌 위젯에 자식 위젯을 적으면 로드 오류다 */
SW_TEST_CASE( UIDocumentTest, NonPanelWithChildrenIsError )
{
    sw::UIDocumentCache cache;
    cache.registerMemoryDocument( "test/textchild.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                           "\t<TextWidget>\n"
                                                           "\t\t<TextWidget />\n"
                                                           "\t</TextWidget>\n"
                                                           "</UIDocument>\n" );
    sw::string error;
    SW_EXPECT_TRUE( cache.findOrLoad( "test/textchild.ui.xml", error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "test/textchild.ui.xml:3:" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "cannot have children" ) != sw::string::npos, error.c_str() );
}

/** @brief [UIDocumentTest] 조각이 돌고 돌아 자기를 다시 부르면(a → b → a) 짓기 오류다 — 무한히 짓지 않는다 */
SW_TEST_CASE( UIDocumentTest, RecursiveUserWidgetIsError )
{
    sw::UIDocumentCache cache;
    cache.registerMemoryDocument( "test/a.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                   "\t<UserWidget _name=\"B\" _document=\"test/b.ui.xml\" />\n"
                                                   "</UIDocument>\n" );
    cache.registerMemoryDocument( "test/b.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                   "\t<BoxPanel>\n"
                                                   "\t\t<UserWidget _name=\"A\" _document=\"test/a.ui.xml\" />\n"
                                                   "\t</BoxPanel>\n"
                                                   "</UIDocument>\n" );
    sw::vector<sw::UIBindingDesc> listBinding;
    sw::string                    error;
    SW_EXPECT_TRUE( UIDocumentTestUtil::build( cache, "test/a.ui.xml", listBinding, error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "fragment includes itself" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_TRUE_MSG( error.find( "test/b.ui.xml:3:" ) != sw::string::npos, error.c_str() );

    cache.registerMemoryDocument( "test/self.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                      "\t<UserWidget _document=\"test/self.ui.xml\" />\n"
                                                      "</UIDocument>\n" );
    SW_EXPECT_TRUE( UIDocumentTestUtil::build( cache, "test/self.ui.xml", listBinding, error ) == nullptr );
    SW_EXPECT_TRUE_MSG( error.find( "fragment includes itself" ) != sw::string::npos, error.c_str() );
}

/** @brief [UIDocumentTest] 같은 조각을 두 번 쓰면 안쪽 이름이 "A.Label" · "B.Label" 로 갈린다 — 조각 안의 조각은 "A.Inner.Label" */
SW_TEST_CASE( UIDocumentTest, UserWidgetNamesAreScoped )
{
    UIDocumentFixture fixture;
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/part.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                               "\t<BoxPanel>\n"
                                                                               "\t\t<TextWidget _name=\"Label\" _text=\"Part\" />\n"
                                                                               "\t</BoxPanel>\n"
                                                                               "</UIDocument>\n" );
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/outer.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                                "\t<UserWidget _name=\"Inner\" _document=\"test/part.ui.xml\" />\n"
                                                                                "</UIDocument>\n" );
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/twice.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                                "\t<BoxPanel>\n"
                                                                                "\t\t<UserWidget _name=\"A\" _document=\"test/part.ui.xml\" />\n"
                                                                                "\t\t<UserWidget _name=\"B\" _document=\"test/part.ui.xml\" />\n"
                                                                                "\t\t<UserWidget _name=\"C\" _document=\"test/outer.ui.xml\" />\n"
                                                                                "\t</BoxPanel>\n"
                                                                                "</UIDocument>\n" );
    sw::UIScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( "test/twice.ui.xml" ) );
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

/** @brief [UIDocumentTest] 버튼 클릭(누르고 같은 버튼 위에서 떼기) · UI.Accept 는 화면 명령으로 간다 — 밖에서 떼면 클릭이 아니다 */
SW_TEST_CASE( UIDocumentTest, CommandRoutesToScreen )
{
    using Util = UIDocumentTestUtil;
    UIDocumentFixture fixture;
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/command.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                                  "\t<CanvasPanel>\n"
                                                                                  "\t\t<ButtonWidget _name=\"Go\" _command=\"Go\">\n"
                                                                                  "\t\t\t<_slot _offsetMin=\"100,100\" _offsetMax=\"300,160\" />\n"
                                                                                  "\t\t</ButtonWidget>\n"
                                                                                  "\t</CanvasPanel>\n"
                                                                                  "</UIDocument>\n" );
    // 기본 화면 — 등록한 함수로
    uint32                   goCount = 0;
    const sw::UIScreenHandle handle  = fixture._ui.openScreen( "test/command.ui.xml" );
    sw::UIScreen*            pScreen = fixture._ui.findScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );
    pScreen->registerCommand( "Go", SW_DELEGATE_LAMBDA( sw::UICommandDelegate, [&goCount]( const sw::hashed_string&, sw::Widget& )
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
    const sw::UIScreenHandle countingHandle = fixture._ui.openScreen<CountingScreen>( "test/command.ui.xml" );
    auto*                    pCounting      = static_cast<CountingScreen*>( fixture._ui.findScreen( countingHandle ) );
    SW_ASSERT_NOT_NULL( pCounting );
    sw::ButtonWidget* pGo = pCounting->getTree().findWidget<sw::ButtonWidget>( "Go" );
    SW_ASSERT_NOT_NULL( pGo );
    SW_ASSERT_TRUE( fixture._ui.getFocusManager().setFocus( pCounting->getTree(), pGo->getID() ) );
    sw::UIActionEvent accept{};
    accept._action = sw::hashed_string( sw::UIActionName::kAccept );
    SW_EXPECT_TRUE( pGo->onActionEvent( accept, sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 1u, pCounting->_commandCount );
    SW_EXPECT_STREQ( "Go", pCounting->_lastCommand.c_str() );
}

/** @brief [UIDocumentTest] 같은 문서로 화면을 두 번 열어도 문서(와 조각)는 한 번만 읽어 파싱한다 */
SW_TEST_CASE( UIDocumentTest, CacheParsesOnce )
{
    UIDocumentFixture    fixture;
    sw::UIDocumentCache& cache = fixture._ui.getDocumentCache();
    cache.registerMemoryDocument( "test/part.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                      "\t<TextWidget _name=\"Label\" />\n"
                                                      "</UIDocument>\n" );
    cache.registerMemoryDocument( "test/menu.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                      "\t<BoxPanel>\n"
                                                      "\t\t<UserWidget _name=\"A\" _document=\"test/part.ui.xml\" />\n"
                                                      "\t\t<UserWidget _name=\"B\" _document=\"test/part.ui.xml\" />\n"
                                                      "\t</BoxPanel>\n"
                                                      "</UIDocument>\n" );
    const sw::UIScreenHandle first  = fixture._ui.openScreen( "test/menu.ui.xml" );
    const sw::UIScreenHandle second = fixture._ui.openScreen( "test/menu.ui.xml" );
    SW_EXPECT_TRUE( fixture._ui.findScreen( first ) != nullptr );
    SW_EXPECT_TRUE( fixture._ui.findScreen( second ) != nullptr );
    SW_EXPECT_EQUAL( 2u, cache.getParseCount() ); // 문서 하나 + 조각 하나
    SW_EXPECT_TRUE( cache.isCached( "test/menu.ui.xml" ) );
    SW_EXPECT_TRUE( cache.isCached( "test/part.ui.xml" ) );
}

/** @brief [UIDocumentTest] `{` 로 시작하는 속성 값은 값으로 읽지 않고 바인딩 식으로 뗀다 — 구조체 칸 안의 것도 경로와 함께 */
SW_TEST_CASE( UIDocumentTest, BindingAttributesAreExtracted )
{
    UIDocumentFixture fixture;
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/binding.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                                  "\t<BoxPanel>\n"
                                                                                  "\t\t<TextWidget _name=\"Health\" _text=\"{bind:_health}\" />\n"
                                                                                  "\t\t<TextWidget _name=\"Bar\">\n"
                                                                                  "\t\t\t<_slot _widthOverride=\"{bind:_barWidth, mode=OneWay}\" />\n"
                                                                                  "\t\t</TextWidget>\n"
                                                                                  "\t</BoxPanel>\n"
                                                                                  "</UIDocument>\n" );
    sw::UIScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( "test/binding.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::TextWidget* pHealth = pScreen->getTree().findWidget<sw::TextWidget>( "Health" );
    const sw::TextWidget* pBar    = pScreen->getTree().findWidget<sw::TextWidget>( "Bar" );
    SW_ASSERT_NOT_NULL( pHealth );
    SW_ASSERT_NOT_NULL( pBar );
    SW_EXPECT_TRUE( pHealth->getText().empty() ); // 식은 칸에 들어가지 않는다

    const sw::vector<sw::UIBindingDesc>& listBinding = pScreen->getBindings();
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listBinding.size() ) );
    SW_EXPECT_EQUAL( pHealth->getID(), listBinding[0]._widget );
    SW_EXPECT_STREQ( "_text", listBinding[0]._propertyPath.c_str() );
    SW_EXPECT_STREQ( "{bind:_health}", listBinding[0]._expression.c_str() );
    SW_EXPECT_EQUAL( 3u, listBinding[0]._sourceLine );
    SW_EXPECT_EQUAL( pBar->getID(), listBinding[1]._widget );
    SW_EXPECT_STREQ( "_slot._widthOverride", listBinding[1]._propertyPath.c_str() );
    SW_EXPECT_STREQ( "{bind:_barWidth, mode=OneWay}", listBinding[1]._expression.c_str() );
    SW_EXPECT_EQUAL( 0.0f, pBar->getLayoutSlot()._widthOverride );
}

/** @brief [UIDocumentTest] 읽고 다시 쓰면 같은 문서다 — 기본값과 같은 칸은 쓰지 않고, 조각은 원소만, 바인딩 식은 식 그대로 */
SW_TEST_CASE( UIDocumentTest, RoundTripSaveMatchesSource )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    UIDocumentFixture fixture;
    const utf8*       kPausePath = "engine/ui/pause.ui.xml";
    sw::string        sourceText;
    SW_ASSERT_TRUE( sw::ResourceUtil::readTextResource( kPausePath, sourceText ) );
    sw::XMLDocument source;
    SW_ASSERT_TRUE( source.parse( sourceText ) );

    sw::UIScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( kPausePath ) );
    SW_ASSERT_NOT_NULL( pScreen );
    pScreen->getAnimationPlayer().tick( 1.0f ); // 여는 애니메이션(Open)이 끝난 값 — 문서에 적은 값과 같다
    const sw::string written = sw::UIDocumentWriter::write( pScreen->getDesc(), {}, *pScreen->getTree().getRoot(), pScreen->getBindings(),
                                                            pScreen->getAnimationPlayer().getAnimations() );
    SW_EXPECT_STREQ( source.saveToString().c_str(), written.c_str() );

    // 바인딩 식이 든 문서도 식 그대로 돌아온다
    const utf8* kBindingText = "<UIDocument _schemaVersion=\"1\">\n"
                               "\t<BoxPanel _spacing=\"4\">\n"
                               "\t\t<TextWidget _name=\"Health\" _text=\"{bind:_health}\" />\n"
                               "\t</BoxPanel>\n"
                               "</UIDocument>\n";
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/roundtrip.ui.xml", kBindingText );
    sw::UIScreen* pBound = fixture._ui.findScreen( fixture._ui.openScreen( "test/roundtrip.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pBound );
    sw::XMLDocument boundSource;
    SW_ASSERT_TRUE( boundSource.parse( kBindingText ) );
    SW_EXPECT_STREQ( boundSource.saveToString().c_str(),
                     sw::UIDocumentWriter::write( pBound->getDesc(), {}, *pBound->getTree().getRoot(), pBound->getBindings(), {} ).c_str() );
}

/** @brief [UIDocumentTest] 문서 파일이 바뀌어 다시 읽으면 화면을 새로 짓고 포커스 · 스크롤을 이름으로 이어 준다 */
SW_TEST_CASE( UIDocumentTest, ReloadRebuildsAndKeepsFocusByName )
{
    using Util = UIDocumentTestUtil;
    UIDocumentFixture fixture;
    const sw::string  path = test::makeTempPath( "reload.ui.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, UIReloadTestUtil::makeReloadDocument( false, "Old" ) ) );
    const sw::UIScreenHandle handle  = fixture._ui.openScreen( path );
    sw::UIScreen*            pScreen = fixture._ui.findScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );
    Util::runFrame( fixture._input, fixture._ui );
    sw::ButtonWidget* pOldB = pScreen->getTree().findWidget<sw::ButtonWidget>( "B" );
    SW_ASSERT_NOT_NULL( pOldB );
    const sw::WidgetID oldID = pOldB->getID();
    SW_ASSERT_TRUE( fixture._ui.getFocusManager().setFocus( pScreen->getTree(), oldID ) );
    sw::ScrollPanel* pScroll = pScreen->getTree().findWidget<sw::ScrollPanel>( "Scroll" );
    SW_ASSERT_NOT_NULL( pScroll );
    pScroll->setScrollOffset( sw::float2{ 0.0f, 300.0f } );
    SW_EXPECT_EQUAL( 300.0f, pScroll->getScrollOffset()._y );

    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, UIReloadTestUtil::makeReloadDocument( true, "New" ) ) );
    fixture._ui.getDocumentCache().reload( path, nullptr );
    SW_ASSERT_TRUE( fixture._ui.findScreen( handle ) == pScreen ); // 같은 화면 · 새 트리
    const sw::ButtonWidget* pNewB = pScreen->getTree().findWidget<sw::ButtonWidget>( "B" );
    SW_ASSERT_NOT_NULL( pNewB );
    SW_EXPECT_TRUE( pNewB->getID() != oldID );
    SW_EXPECT_STREQ( "New", pNewB->getCommand().c_str() );
    SW_EXPECT_TRUE( pScreen->getTree().findWidgetByName( "C" ) != nullptr );
    SW_EXPECT_EQUAL( pNewB->getID(), fixture._ui.getFocusManager().getFocusedWidget() );
    SW_EXPECT_EQUAL( 300.0f, pScreen->getTree().findWidget<sw::ScrollPanel>( "Scroll" )->getScrollOffset()._y );
    Util::runFrame( fixture._input, fixture._ui );
}

/** @brief [UIDocumentTest] 다시 읽은 문서가 깨졌으면 옛 트리를 그대로 둔다 — 실패가 화면을 지우지 않는다 */
SW_TEST_CASE( UIDocumentTest, FailedReloadKeepsOldTree )
{
    UIDocumentFixture    fixture;
    sw::UIDocumentCache& cache = fixture._ui.getDocumentCache();
    cache.registerMemoryDocument( "test/fail.ui.xml", UIReloadTestUtil::makeReloadDocument( false, "Old" ) );
    sw::UIScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( "test/fail.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::WidgetID oldID = pScreen->getTree().findWidgetByName( "B" )->getID();

    cache.registerMemoryDocument( "test/fail.ui.xml", "<UIDocument _schemaVersion=\"1\">\n\t<BoxPanel _bogus=\"1\" />\n</UIDocument>\n" );
    {
        test::ScopedLogCollector logs; // 다시 읽기 실패 오류는 기대한 것이다
        cache.reload( "test/fail.ui.xml", nullptr );
        SW_EXPECT_TRUE_MSG( logs.joined().find( "keeping the old one" ) != sw::string::npos, logs.joined().c_str() );
    }
    SW_EXPECT_EQUAL( oldID, pScreen->getTree().findWidgetByName( "B" )->getID() );
}

/** @brief [UIDocumentTest] 스타일 시트를 다시 읽으면 위젯만 다시 맞춘다 — 트리(위젯 번호)는 그대로 */
SW_TEST_CASE( UIDocumentTest, StyleReloadRestylesOnly )
{
    using Util = UIDocumentTestUtil;
    UIDocumentFixture fixture;
    fixture._ui.getStyleSheetCache().registerMemorySheet( "test/reload.uistyle.xml", "<UiStyleSheet _schemaVersion=\"1\">\n"
                                                                                     "\t<Rule _selector=\"ButtonWidget\" _backgroundColor=\"1,0,0,1\" />\n"
                                                                                     "</UiStyleSheet>\n" );
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/styled.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                                 "\t<_listStyleSheet><item>test/reload.uistyle.xml</item></_listStyleSheet>\n"
                                                                                 "\t<BoxPanel>\n"
                                                                                 "\t\t<ButtonWidget _name=\"Go\" />\n"
                                                                                 "\t</BoxPanel>\n"
                                                                                 "</UIDocument>\n" );
    sw::UIScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( "test/styled.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    Util::runFrame( fixture._input, fixture._ui );
    const sw::Widget* pGo = pScreen->getTree().findWidgetByName( "Go" );
    SW_ASSERT_NOT_NULL( pGo );
    const sw::WidgetID goID = pGo->getID();
    SW_EXPECT_TRUE( pGo->getComputedStyle()->_value._backgroundColor == ( sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } ) );

    fixture._ui.getStyleSheetCache().registerMemorySheet( "test/reload.uistyle.xml", "<UiStyleSheet _schemaVersion=\"1\">\n"
                                                                                     "\t<Rule _selector=\"ButtonWidget\" _backgroundColor=\"0,1,0,1\" />\n"
                                                                                     "</UiStyleSheet>\n" );
    fixture._ui.getStyleSheetCache().reload( "test/reload.uistyle.xml", nullptr );
    Util::runFrame( fixture._input, fixture._ui );
    const sw::Widget* pSame = pScreen->getTree().findWidgetByName( "Go" );
    SW_ASSERT_NOT_NULL( pSame );
    SW_EXPECT_EQUAL( goID, pSame->getID() );
    SW_EXPECT_TRUE( pSame->getComputedStyle()->_value._backgroundColor == ( sw::float4{ 0.0f, 1.0f, 0.0f, 1.0f } ) );
}

/** @brief [UIDocumentTest] 조각 문서가 바뀌면 그것을 끼운 문서로 연 화면도 새로 짓는다(안 쓰는 화면은 그대로) */
SW_TEST_CASE( UIDocumentTest, UserWidgetChangeReloadsParents )
{
    UIDocumentFixture    fixture;
    sw::UIDocumentCache& cache = fixture._ui.getDocumentCache();
    cache.registerMemoryDocument( "test/part.ui.xml", "<UIDocument _schemaVersion=\"1\">\n\t<TextWidget _name=\"Label\" _text=\"Old\" />\n</UIDocument>\n" );
    cache.registerMemoryDocument( "test/parent.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                        "\t<BoxPanel>\n"
                                                        "\t\t<UserWidget _name=\"Part\" _document=\"test/part.ui.xml\" />\n"
                                                        "\t</BoxPanel>\n"
                                                        "</UIDocument>\n" );
    cache.registerMemoryDocument( "test/other.ui.xml", "<UIDocument _schemaVersion=\"1\">\n\t<TextWidget _name=\"Other\" />\n</UIDocument>\n" );
    sw::UIScreen* pParent = fixture._ui.findScreen( fixture._ui.openScreen( "test/parent.ui.xml" ) );
    sw::UIScreen* pOther  = fixture._ui.findScreen( fixture._ui.openScreen( "test/other.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pParent );
    SW_ASSERT_NOT_NULL( pOther );
    const sw::WidgetID otherID = pOther->getTree().findWidgetByName( "Other" )->getID();

    cache.registerMemoryDocument( "test/part.ui.xml", "<UIDocument _schemaVersion=\"1\">\n\t<TextWidget _name=\"Label\" _text=\"New\" />\n</UIDocument>\n" );
    cache.reload( "test/part.ui.xml", nullptr );
    const sw::TextWidget* pLabel = pParent->getTree().findWidget<sw::TextWidget>( "Part.Label" );
    SW_ASSERT_NOT_NULL( pLabel );
    SW_EXPECT_STREQ( "New", pLabel->getText().c_str() );
    SW_EXPECT_EQUAL( otherID, pOther->getTree().findWidgetByName( "Other" )->getID() );
}

/**
 * @brief [UIDocumentTest] 오프스크린 화면(에디터 미리보기)은 스택 밖 — 활성 화면 · 화면 수와 무관하게 자기 렌더 텍스처 대상(뷰포트 물리 크기)으로 그려지고,
 *        문서를 다시 읽으면 같이 다시 짓는다
 * @details 변이: `UISystem::onDocumentReloaded` 의 오프스크린 줄을 빼면 다시 읽은 뒤에도 옛 위젯 번호가 남아 진다.
 */
SW_TEST_CASE( UIDocumentTest, OffscreenScreenDrawsToItsTargetAndReloads )
{
    UIDocumentFixture    fixture;
    sw::UIDocumentCache& cache = fixture._ui.getDocumentCache();
    cache.registerMemoryDocument( "test/offscreen.ui.xml", UIReloadTestUtil::makeReloadDocument( false, "Old" ) );
    const sw::UIScreenHandle handle  = fixture._ui.openOffscreenScreen( "test/offscreen.ui.xml", "rendertarget/test_preview" );
    sw::UIScreen*            pScreen = fixture._ui.findOffscreenScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_EQUAL( 0u, fixture._ui.getScreenCount() );
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() == nullptr );
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ 640.0f, 360.0f };
    viewport._physicalSize = sw::float2{ 1280.0f, 720.0f };
    viewport._uiScale      = 2.0f;
    fixture._ui.setOffscreenView( handle, viewport, 1.0f, sw::hashed_string{} );
    fixture._ui.update( 0.016f, sw::UIViewport{
                                    sw::float2{ 1920.0f, 1080.0f }
    } );
    sw::vector<sw::CanvasTargetDrawList> listTarget;
    fixture._ui.collectWorldCanvases( listTarget );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listTarget.size() ) );
    SW_EXPECT_STREQ( "rendertarget/test_preview", listTarget[0]._targetPath.c_str() );
    SW_EXPECT_NEAR_EQUAL( 1280.0f, listTarget[0]._list._targetSize._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 640.0f, pScreen->getTree().getRoot()->getGeometry()._size._x, 1e-3f ); // 루트 = 미리보기 뷰포트(UI 단위)

    const sw::WidgetID oldID = pScreen->getTree().findWidgetByName( "B" )->getID();
    cache.registerMemoryDocument( "test/offscreen.ui.xml", UIReloadTestUtil::makeReloadDocument( true, "New" ) );
    cache.reload( "test/offscreen.ui.xml", nullptr );
    SW_EXPECT_TRUE( pScreen->getTree().findWidgetByName( "B" )->getID() != oldID );
    SW_EXPECT_TRUE( pScreen->getTree().findWidgetByName( "C" ) != nullptr );
    fixture._ui.closeOffscreenScreen( handle );
    SW_EXPECT_TRUE( fixture._ui.findOffscreenScreen( handle ) == nullptr );
}

/**
 * @brief [UIDocumentTest] 오프스크린 화면(에디터 미리보기)은 문서의 Open 애니메이션을 틀지 않는다 — 문서에 적힌 값(불투명도 1)을 보이고, 스택에 올린 같은 문서는 첫 키(0)부터 시작한다
 * @details 미리보기가 Open 을 틀면 첫 프레임 값(투명)이 미리보기 · 저장 값이 된다. 변이: `openOffscreenScreen` 에서 Open 을 틀면 오프스크린 쪽 단언이 진다.
 */
SW_TEST_CASE( UIDocumentTest, OffscreenScreenDoesNotPlayOpenAnimation )
{
    UIDocumentFixture        fixture;
    const sw::UIScreenHandle offscreen = fixture._ui.openOffscreenScreen( "engine/ui/pause.ui.xml", "rendertarget/test_preview" );
    sw::UIScreen*            pPreview  = fixture._ui.findOffscreenScreen( offscreen );
    SW_ASSERT_NOT_NULL( pPreview );
    const sw::UIScreenHandle stacked  = fixture._ui.openScreen( "engine/ui/pause.ui.xml" );
    sw::UIScreen*            pStacked = fixture._ui.findScreen( stacked );
    SW_ASSERT_NOT_NULL( pStacked );
    SW_ASSERT_TRUE( pStacked->getAnimationPlayer().isPlaying( sw::hashed_string( sw::UIAnimation::kOpenName ) ) ); // 견본 문서에 Open 이 있다

    SW_EXPECT_NEAR_EQUAL( 0.0f, pStacked->getTree().findWidgetByName( "Window" )->getOpacity(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pPreview->getTree().findWidgetByName( "Window" )->getOpacity(), 1e-4f );
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ 1280.0f, 720.0f };
    viewport._physicalSize = viewport._size;
    fixture._ui.setOffscreenView( offscreen, viewport, 1.0f, sw::hashed_string{} );
    fixture._ui.update( 0.016f, viewport );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pPreview->getTree().findWidgetByName( "Window" )->getOpacity(), 1e-4f );
    fixture._ui.closeOffscreenScreen( offscreen );
}
