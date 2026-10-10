#include "pch.h"

#include "Core/Common/Macros.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Binding/UiBindingExpression.h"
#include "Engine/UI/Binding/UiBindingSet.h"
#include "Engine/UI/Binding/UiViewModel.h"
#include "Engine/UI/Document/UiDocumentCache.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "TestFramework/TestFramework.h"

// UiBindingTest — 데이터 바인딩(6-1): 뷰모델 필드 통지 · 단방향/양방향 · 메시지 패턴(format) · 변환기 · 타입 검사 · 칸에 맞는 무효화 · 폴링.
// 시험 뷰모델은 리플렉션 표를 손으로 짓는다(시험 타깃에 코드젠이 없다). 디바이스 없음(nogpu).

namespace
{
    /** @brief 손으로 지은 리플렉션 표를 가진 시험 뷰모델입니다. */
    class TestHUDViewModel : public sw::UiViewModel
    {
    public:
        TestHUDViewModel()
            : sw::UiViewModel{}
            , _name{}
            , _tint{ 1.0f, 1.0f, 1.0f, 1.0f }
            , _health{ 0.0f }
            , _ammo{ 0 }
            , _bAlive{ true }
        {
        }

        const sw::TypeInfo* getTypeInfo() const override { return &getStaticTypeInternal(); }

        void setAmmo( int32 ammo ) { setField( _ammo, ammo, "_ammo" ); }
        void setName( const sw::string& name ) { setField( _name, name, "_name" ); }

        sw::string _name;
        sw::float4 _tint;
        float32    _health;
        int32      _ammo;
        bool       _bAlive;

    private:
        static const sw::TypeInfo& getStaticTypeInternal()
        {
            static sw::TypeInfo s_info = makeTypeInternal();
            return s_info;
        }

        static sw::TypeInfo makeTypeInternal()
        {
            sw::TypeInfo info{};
            info._name               = sw::hashed_string( "TestHUDViewModel" );
            info._fullyQualifiedName = sw::hashed_string( "TestHUDViewModel" );
            info._listProperty.push_back( { sw::hashed_string( "_name" ), sw::hashed_string( "string" ), SW_OFFSET_OF( TestHUDViewModel, _name ) } );
            info._listProperty.push_back( { sw::hashed_string( "_tint" ), sw::hashed_string( "float4" ), SW_OFFSET_OF( TestHUDViewModel, _tint ) } );
            info._listProperty.push_back( { sw::hashed_string( "_health" ), sw::hashed_string( "float32" ), SW_OFFSET_OF( TestHUDViewModel, _health ) } );
            info._listProperty.push_back( { sw::hashed_string( "_ammo" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestHUDViewModel, _ammo ) } );
            info._listProperty.push_back( { sw::hashed_string( "_bAlive" ), sw::hashed_string( "bool" ), SW_OFFSET_OF( TestHUDViewModel, _bAlive ) } );
            return info;
        }
    };

    struct UiBindingTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        /** @brief 입력 한 프레임 → UI 입력 → UI 갱신(바인딩 · 레이아웃 · 그리기)입니다. */
        static void runFrame( sw::InputManager& input, sw::UiSystem& ui )
        {
            input.beginFrame( kFrameSeconds );
            ui.processInput( kFrameSeconds );
            ui.update( kFrameSeconds, sw::UiViewport{
                                          sw::float2{ 1280.0f, 720.0f }
            } );
            input.endFrame();
        }

        /** @brief 글 위젯 하나를 루트로 둔 코드 화면과 바인딩 하나입니다. */
        static sw::unique_ptr<sw::UiScreen> makeTextScreen( const utf8* pPropertyPath, const utf8* pExpression, sw::TextWidget*& pOutText )
        {
            sw::unique_ptr<sw::TextWidget> text = sw::make_unique<sw::TextWidget>();
            pOutText                            = text.get();
            sw::unique_ptr<sw::UiScreen> screen = sw::make_unique<sw::UiScreen>( sw::UiScreenDesc{}, std::move( text ) );
            screen->addBinding( sw::UiBindingDesc{ pPropertyPath, pExpression, pOutText->getID(), 1 } );
            return screen;
        }

        /** @brief 트리의 그리기 더러움 목록에 위젯이 있는가입니다. */
        static bool isPaintDirty( const sw::WidgetTree& tree, sw::WidgetID id )
        {
            for ( const sw::WidgetID dirty : tree.getPaintDirtyWidgets() )
            {
                if ( dirty == id )
                    return true;
            }
            return false;
        }
    };

    /** @brief 입력 관리자와 그것을 읽는 UI 시스템입니다(글꼴 없음 — 글 크기는 0). */
    struct UiBindingFixture
    {
        sw::InputManager        _input;
        sw::UiSystem            _ui;
        sw::LocalizationManager _localization;

        UiBindingFixture()
            : _input{}
            , _ui{}
            , _localization{}
        {
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
            _ui.setLocalization( &_localization );
        }

        ~UiBindingFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UiBindingFixture( const UiBindingFixture& )            = delete;
        UiBindingFixture& operator=( const UiBindingFixture& ) = delete;
    };
} // namespace

/** @brief [UiBindingTest] 식 풀기 — 종류 · 경로 · 옵션, 틀린 꼴은 이유와 함께 진다 */
SW_TEST_CASE( UiBindingTest, ExpressionParses )
{
    sw::UiBindingExpression expression{};
    sw::string              error;
    SW_ASSERT_TRUE( sw::UiBindingExpression::parse( "{bind:_health, mode=TwoWay, converter=Percent}", expression, error ) );
    SW_EXPECT_TRUE( expression._source == sw::UiBindingSource::ViewModel );
    SW_EXPECT_TRUE( expression._mode == sw::UiBindingMode::TwoWay );
    SW_EXPECT_STREQ( "_health", expression._path.c_str() );
    SW_EXPECT_STREQ( "Percent", expression._converter.c_str() );
    SW_ASSERT_TRUE( sw::UiBindingExpression::parse( "{ setting : audio.master }", expression, error ) );
    SW_EXPECT_TRUE( expression._source == sw::UiBindingSource::Setting );
    SW_EXPECT_TRUE( expression._mode == sw::UiBindingMode::TwoWay ); // 설정 바인딩은 양방향이 기본
    SW_EXPECT_STREQ( "audio.master", expression._path.c_str() );

    SW_EXPECT_FALSE( sw::UiBindingExpression::parse( "{bound:_health}", expression, error ) );
    SW_EXPECT_TRUE_MSG( error.find( "unknown binding kind" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_FALSE( sw::UiBindingExpression::parse( "{bind:_health, speed=2}", expression, error ) );
    SW_EXPECT_TRUE_MSG( error.find( "unknown option 'speed'" ) != sw::string::npos, error.c_str() );
    SW_EXPECT_FALSE( sw::UiBindingExpression::parse( "{bind: }", expression, error ) );
}

/** @brief [UiBindingTest] 단방향 — 뷰모델이 알릴 때만 칸이 바뀐다(알리지 않고 바꾼 값은 다음 알림까지 그대로) */
SW_TEST_CASE( UiBindingTest, OneWayUpdatesOnlyOnNotify )
{
    UiBindingFixture fixture;
    TestHUDViewModel viewModel;
    sw::TextWidget*  pText    = nullptr;
    viewModel._name           = "Alice";
    sw::UiScreenHandle handle = fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_text", "{bind:_name}", pText ) );
    fixture._ui.findScreen( handle )->setViewModel( &viewModel );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "Alice", pText->getText().c_str() ); // 걸 때 모든 칸을 처음부터 쓴다

    viewModel._name = "Bob"; // 알리지 않은 변경
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "Alice", pText->getText().c_str() );

    viewModel.setName( "Carol" );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "Carol", pText->getText().c_str() );
}

/** @brief [UiBindingTest] 같은 값을 넣으면 알리지 않는다 — 알림 번호가 오르지 않고 칸 쓰기도 없다 */
SW_TEST_CASE( UiBindingTest, SameValueDoesNotNotify )
{
    TestHUDViewModel viewModel;
    viewModel.setAmmo( 5 );
    const uint64 serial = viewModel.getChangeSerial();
    SW_EXPECT_FALSE( viewModel.setField( viewModel._ammo, 5, "_ammo" ) );
    SW_EXPECT_EQUAL( serial, viewModel.getChangeSerial() );
    SW_EXPECT_TRUE( viewModel.setField( viewModel._ammo, 6, "_ammo" ) );
    SW_EXPECT_EQUAL( serial + 1, viewModel.getChangeSerial() );
}

/** @brief [UiBindingTest] 양방향 — 문서의 슬라이더를 사용자가 움직이면 뷰모델에 되쓰고 알린다. 그 알림으로 같은 칸에 다시 쓰지 않는다 */
SW_TEST_CASE( UiBindingTest, TwoWaySliderWritesBack )
{
    UiBindingFixture fixture;
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/binding/slider.ui.xml",
                                                           "<UiDocument _schemaVersion=\"1\">\n"
                                                           "\t<BoxPanel>\n"
                                                           "\t\t<SliderWidget _name=\"Health\" _minValue=\"0\" _maxValue=\"100\" _step=\"10\" _value=\"{bind:_health, mode=TwoWay}\" />\n"
                                                           "\t\t<TextWidget _name=\"Label\" _text=\"{bind:_health}\" />\n"
                                                           "\t</BoxPanel>\n"
                                                           "</UiDocument>\n" );
    TestHUDViewModel viewModel;
    viewModel._health                = 40.0f;
    const sw::UiScreenHandle handle  = fixture._ui.openScreen( "test/binding/slider.ui.xml" );
    sw::UiScreen*            pScreen = fixture._ui.findScreen( handle );
    SW_ASSERT_NOT_NULL( pScreen );
    pScreen->setViewModel( &viewModel );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    sw::SliderWidget* pSlider = pScreen->getTree().findWidget<sw::SliderWidget>( "Health" );
    sw::TextWidget*   pLabel  = pScreen->getTree().findWidget<sw::TextWidget>( "Label" );
    SW_ASSERT_NOT_NULL( pSlider );
    SW_ASSERT_NOT_NULL( pLabel );
    SW_EXPECT_EQUAL( 2u, pScreen->getBindingSet().getBindingCount() );
    SW_EXPECT_NEAR_EQUAL( 40.0f, pSlider->getValue(), 0.001f );

    sw::UiActionEvent event{};
    event._action = sw::hashed_string( sw::UiActionName::kNavigateRight );
    (void)pSlider->onActionEvent( event, sw::UiRoutePhase::Bubble );
    SW_EXPECT_NEAR_EQUAL( 50.0f, viewModel._health, 0.001f ); // 되쓰기는 입력 그 자리에서
    const uint32 writesBefore = pScreen->getBindingSet().getWriteCount();
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "50", pLabel->getText().c_str() );                            // 같은 필드의 다른 바인딩은 알림을 받는다
    SW_EXPECT_EQUAL( writesBefore + 1, pScreen->getBindingSet().getWriteCount() ); // 슬라이더 자신에게는 다시 쓰지 않는다
}

/** @brief [UiBindingTest] format= 은 현지화 키의 메시지 패턴에 값을 {value} 로 넣는다 — 표에 없으면 값 그대로 */
SW_TEST_CASE( UiBindingTest, FormatUsesLocalizedPattern )
{
    UiBindingFixture fixture;
    fixture._localization.setString( "qa", sw::hashed_string( "HUD.Ammo" ), "Ammo: {value}" );
    SW_ASSERT_TRUE( fixture._localization.setCurrentLanguage( "qa" ) );
    TestHUDViewModel viewModel;
    viewModel._ammo        = 12;
    sw::TextWidget* pText  = nullptr;
    sw::TextWidget* pPlain = nullptr;
    sw::UiScreen*   pScreen =
        fixture._ui.findScreen( fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_text", "{bind:_ammo, format=HUD.Ammo}", pText ) ) );
    pScreen->setViewModel( &viewModel );
    sw::UiScreen* pMissing =
        fixture._ui.findScreen( fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_text", "{bind:_ammo, format=HUD.Missing}", pPlain ) ) );
    pMissing->setViewModel( &viewModel );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "Ammo: 12", pText->getText().c_str() );
    SW_EXPECT_STREQ( "12", pPlain->getText().c_str() );
}

/** @brief [UiBindingTest] 변환기는 이름으로 찾는다 — Percent · Invert · Seconds, 모르는 이름은 걸 때 오류 */
SW_TEST_CASE( UiBindingTest, ConverterByName )
{
    UiBindingFixture fixture;
    TestHUDViewModel viewModel;
    viewModel._health       = 0.75f;
    viewModel._ammo         = 125;
    sw::TextWidget* pText   = nullptr;
    sw::UiScreen*   pScreen = fixture._ui.findScreen( fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_text", "{bind:_health, converter=Percent}", pText ) ) );
    pScreen->setViewModel( &viewModel );
    sw::TextWidget* pClock   = nullptr;
    sw::UiScreen*   pScreen2 = fixture._ui.findScreen( fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_text", "{bind:_ammo, converter=Seconds}", pClock ) ) );
    pScreen2->setViewModel( &viewModel );
    sw::TextWidget* pHidden  = nullptr;
    sw::UiScreen*   pScreen3 = fixture._ui.findScreen( fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_bEnabled", "{bind:_bAlive, converter=Invert}", pHidden ) ) );
    pScreen3->setViewModel( &viewModel );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "75%", pText->getText().c_str() );
    SW_EXPECT_STREQ( "2:05", pClock->getText().c_str() );
    SW_EXPECT_FALSE( pHidden->isEnabled() );

    sw::TextWidget* pBad     = nullptr;
    sw::UiScreen*   pScreen4 = fixture._ui.findScreen( fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_text", "{bind:_ammo, converter=Nope}", pBad ) ) );
    pScreen4->setViewModel( &viewModel );
    {
        SW_TEST_DEFENSIVE_SCOPE( "an unknown converter is a bind error" );
        UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    }
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pScreen4->getBindingSet().getErrors().size() ) );
    SW_EXPECT_TRUE_MSG( pScreen4->getBindingSet().getErrors()[0].find( "unknown converter 'Nope'" ) != sw::string::npos,
                        pScreen4->getBindingSet().getErrors()[0].c_str() );
}

/** @brief [UiBindingTest] 갈래가 맞지 않고 변환기도 없으면 걸 때 오류다 — 조용히 0 이 되지 않는다(글 → 숫자, 색 → 숫자) */
SW_TEST_CASE( UiBindingTest, TypeMismatchWithoutConverterIsError )
{
    UiBindingFixture fixture;
    TestHUDViewModel viewModel;
    viewModel._name                           = "abc";
    sw::unique_ptr<sw::SliderWidget> slider   = sw::make_unique<sw::SliderWidget>();
    const sw::WidgetID               sliderID = slider->getID();
    slider->setRange( 0.0f, 10.0f, 1.0f );
    slider->setValue( 3.0f );
    sw::unique_ptr<sw::UiScreen> screen = sw::make_unique<sw::UiScreen>( sw::UiScreenDesc{}, std::move( slider ) );
    screen->addBinding( sw::UiBindingDesc{ "_value", "{bind:_name}", sliderID, 7 } );
    screen->addBinding( sw::UiBindingDesc{ "_maxValue", "{bind:_tint}", sliderID, 8 } );
    screen->addBinding( sw::UiBindingDesc{ "_minValue", "{bind:_missing}", sliderID, 9 } );
    screen->setViewModel( &viewModel );
    sw::UiScreen* pScreen = fixture._ui.findScreen( fixture._ui.pushScreen( std::move( screen ) ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "mismatched bindings are bind errors" );
        UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    }
    const sw::vector<sw::string>& listError = pScreen->getBindingSet().getErrors();
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listError.size() ) );
    SW_EXPECT_TRUE_MSG( listError[0].find( "(code):7: binding '{bind:_name}' on _value: cannot bind text" ) != sw::string::npos, listError[0].c_str() );
    SW_EXPECT_TRUE_MSG( listError[1].find( "cannot bind struct 'float4'" ) != sw::string::npos, listError[1].c_str() );
    SW_EXPECT_TRUE_MSG( listError[2].find( "has no property '_missing'" ) != sw::string::npos, listError[2].c_str() );
    SW_EXPECT_EQUAL( 0u, pScreen->getBindingSet().getBindingCount() );
    const sw::SliderWidget* pSlider = static_cast<const sw::SliderWidget*>( pScreen->getTree().findWidgetByID( sliderID ) );
    SW_ASSERT_NOT_NULL( pSlider );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pSlider->getValue(), 0.001f ); // 걸리지 않은 칸은 그대로
}

/** @brief [UiBindingTest] 바인딩이 쓴 칸에 맞는 무효화 — 글은 레이아웃, 색은 그리기만 */
SW_TEST_CASE( UiBindingTest, LayoutFieldMakesLayoutDirty )
{
    TestHUDViewModel               viewModel;
    sw::unique_ptr<sw::TextWidget> text  = sw::make_unique<sw::TextWidget>();
    sw::TextWidget*                pText = text.get();
    sw::UiScreen                   screen( sw::UiScreenDesc{}, std::move( text ) );
    screen.addBinding( sw::UiBindingDesc{ "_text", "{bind:_name}", pText->getID(), 1 } );
    screen.addBinding( sw::UiBindingDesc{ "_color", "{bind:_tint}", pText->getID(), 2 } );
    screen.setViewModel( &viewModel );
    const sw::UiBindingContext context{};
    screen.getBindingSet().update( screen.getBindings(), context );
    SW_ASSERT_EQUAL( 2u, screen.getBindingSet().getBindingCount() );
    screen.getTree().clearAllDirty();

    viewModel.setField( viewModel._tint, sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f }, "_tint" );
    screen.getBindingSet().update( screen.getBindings(), context );
    SW_EXPECT_TRUE( pText->getColor() == sw::float4( 1.0f, 0.0f, 0.0f, 1.0f ) );
    SW_EXPECT_TRUE( screen.getTree().getLayoutDirtyRoots().empty() ); // 색만 — 레이아웃은 그대로
    SW_EXPECT_TRUE( UiBindingTestUtil::isPaintDirty( screen.getTree(), pText->getID() ) );
    screen.getTree().clearAllDirty();

    viewModel.setName( "Longer name" );
    screen.getBindingSet().update( screen.getBindings(), context );
    SW_EXPECT_FALSE( screen.getTree().getLayoutDirtyRoots().empty() ); // 글 — 원하는 크기가 바뀐다
}

/** @brief [UiBindingTest] 한 프레임에 같은 필드를 여러 번 알려도 칸은 한 번 쓴다 */
SW_TEST_CASE( UiBindingTest, ManyNotifiesInOneFrameUpdateOnce )
{
    UiBindingFixture fixture;
    TestHUDViewModel viewModel;
    sw::TextWidget*  pText   = nullptr;
    sw::UiScreen*    pScreen = fixture._ui.findScreen( fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_text", "{bind:_ammo}", pText ) ) );
    pScreen->setViewModel( &viewModel );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    const uint32 writesBefore = pScreen->getBindingSet().getWriteCount();
    for ( int32 ammo = 1; ammo <= 10; ++ammo )
    {
        viewModel.setAmmo( ammo );
    }
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "10", pText->getText().c_str() );
    SW_EXPECT_EQUAL( writesBefore + 1, pScreen->getBindingSet().getWriteCount() );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui ); // 알림 없는 프레임은 쓰지 않는다
    SW_EXPECT_EQUAL( writesBefore + 1, pScreen->getBindingSet().getWriteCount() );
}

/** @brief [UiBindingTest] 폴링 바인딩은 알림 없이 바뀐 값을 매 프레임 견줘 잡는다(개발 편의 — 견준 수를 센다) */
SW_TEST_CASE( UiBindingTest, PollBindingDetectsChange )
{
    UiBindingFixture fixture;
    TestHUDViewModel viewModel;
    viewModel._ammo         = 3;
    sw::TextWidget* pText   = nullptr;
    sw::UiScreen*   pScreen = fixture._ui.findScreen( fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_text", "{poll:_ammo}", pText ) ) );
    pScreen->setViewModel( &viewModel );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "3", pText->getText().c_str() );
    const uint32 writesBefore = pScreen->getBindingSet().getWriteCount();
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_EQUAL( 1u, pScreen->getBindingSet().getPolledCount() );
    SW_EXPECT_EQUAL( writesBefore, pScreen->getBindingSet().getWriteCount() ); // 그대로면 쓰지 않는다

    viewModel._ammo = 4; // 알리지 않는다
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "4", pText->getText().c_str() );
}

/** @brief [UiBindingTest] 뷰모델이 화면보다 먼저 지워지면 바인딩이 놓는다 — 칸은 마지막 값으로 남고 다음 프레임이 죽지 않는다 */
SW_TEST_CASE( UiBindingTest, DestroyedViewModelIsReleased )
{
    UiBindingFixture fixture;
    sw::TextWidget*  pText = nullptr;
    sw::UiScreen*    pScreen =
        fixture._ui.findScreen( fixture._ui.pushScreen( UiBindingTestUtil::makeTextScreen( "_text", "{bind:_name}", pText ) ) );
    {
        TestHUDViewModel viewModel;
        viewModel._name = "Gone";
        pScreen->setViewModel( &viewModel );
        UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    }
    SW_EXPECT_TRUE( pScreen->getViewModel() == nullptr );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "Gone", pText->getText().c_str() );
}

/** @brief [UiBindingTest] 문서를 다시 읽어 트리를 새로 지어도 뷰모델은 화면에 남고, 새 위젯에 바인딩이 다시 걸린다 */
SW_TEST_CASE( UiBindingTest, DocumentReloadRebindsViewModel )
{
    UiBindingFixture     fixture;
    sw::UiDocumentCache& cache = fixture._ui.getDocumentCache();
    cache.registerMemoryDocument( "test/binding/reload.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                "\t<BoxPanel>\n"
                                                                "\t\t<TextWidget _name=\"Label\" _text=\"{bind:_name}\" />\n"
                                                                "\t</BoxPanel>\n"
                                                                "</UiDocument>\n" );
    TestHUDViewModel viewModel;
    viewModel._name       = "Before";
    sw::UiScreen* pScreen = fixture._ui.findScreen( fixture._ui.openScreen( "test/binding/reload.ui.xml" ) );
    SW_ASSERT_NOT_NULL( pScreen );
    pScreen->setViewModel( &viewModel );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "Before", pScreen->getTree().findWidget<sw::TextWidget>( "Label" )->getText().c_str() );

    cache.registerMemoryDocument( "test/binding/reload.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                "\t<BoxPanel>\n"
                                                                "\t\t<TextWidget _name=\"Title\" _text=\"Title\" />\n"
                                                                "\t\t<TextWidget _name=\"Label\" _text=\"{bind:_name}\" />\n"
                                                                "\t</BoxPanel>\n"
                                                                "</UiDocument>\n" );
    cache.reload( "test/binding/reload.ui.xml", nullptr );
    SW_EXPECT_TRUE( pScreen->getViewModel() == &viewModel );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    const sw::TextWidget* pLabel = pScreen->getTree().findWidget<sw::TextWidget>( "Label" );
    SW_ASSERT_NOT_NULL( pLabel );
    SW_EXPECT_STREQ( "Before", pLabel->getText().c_str() ); // 새 위젯에 다시 걸어 모든 칸을 썼다
    viewModel.setName( "After" );
    UiBindingTestUtil::runFrame( fixture._input, fixture._ui );
    SW_EXPECT_STREQ( "After", pLabel->getText().c_str() );
}
