#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/UI/Binding/UiBindingSet.h"
#include "Engine/UI/Core/UiEvents.h"
#include "Engine/UI/Document/UiDocumentCache.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/CheckBoxWidget.h"
#include "Engine/UI/Widgets/ComboBoxWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "TestFramework/TestFramework.h"

// UiSettingsBindingTest — 사용자 설정 바인딩(6-3): {setting:id} 가 위젯 값 · 범위 · 선택지 · 사용 가능을 채우고, 사용자 입력은 보류 값으로,
// 다른 곳의 변경(되돌리기)은 변경 통보로 받는다. 리스너는 화면과 함께 뗀다. 설정 대상이 없는 시험 스키마(XML 글)로 매니저를 세운다. 디바이스 없음(nogpu).

namespace
{
    struct UiSettingsBindingTestUtil
    {
        static constexpr float32     kFrameSeconds = 1.0f / 60.0f;
        static constexpr const utf8* kSchemaXml    = R"(
<UserSettingsSchema version="1">
    <Category id="audio" text="t.audio"/>
    <Category id="video" text="t.video"/>
    <Setting id="audio.master" category="audio" type="float" default="0.5" min="0" max="1" step="0.25"/>
    <Setting id="video.mode" category="video" type="enum" default="windowed">
        <Option value="windowed" text="Settings.Windowed"/>
        <Option value="fullscreen" text="Settings.Fullscreen"/>
        <Option value="borderless"/>
    </Setting>
    <Setting id="video.vsync" category="video" type="bool" default="true" enabledWhen="video.mode!=windowed"/>
</UserSettingsSchema>)";
        static constexpr const utf8* kDocumentPath = "test/settingsbinding/options.ui.xml";
        static constexpr const utf8* kDocumentText = "<UiDocument _schemaVersion=\"1\">\n"
                                                     "\t<BoxPanel>\n"
                                                     "\t\t<SliderWidget _name=\"Master\" _value=\"{setting:audio.master}\" />\n"
                                                     "\t\t<ComboBoxWidget _name=\"Mode\" _selectedIndex=\"{setting:video.mode}\" />\n"
                                                     "\t\t<CheckBoxWidget _name=\"VSync\" _bChecked=\"{setting:video.vsync}\" />\n"
                                                     "\t</BoxPanel>\n"
                                                     "</UiDocument>\n";

        static void runFrame( sw::InputManager& input, sw::UiSystem& ui )
        {
            input.beginFrame( kFrameSeconds );
            ui.processInput( kFrameSeconds );
            ui.update( kFrameSeconds, sw::UiViewport{
                                          sw::float2{ 1280.0f, 720.0f }
            } );
            input.endFrame();
        }

        static float32 readFloat( const sw::UserSettingsManager& settings, const utf8* pId )
        {
            float32 value = -1.0f;
            (void)sw::StringUtil::parseFloat( settings.getValue( sw::hashed_string( pId ) ), value );
            return value;
        }
    };

    /** @brief 시험 스키마의 설정 매니저 · 입력 · 그것을 쓰는 UI 시스템, 그리고 옵션 문서 화면입니다. */
    struct UiSettingsBindingFixture
    {
        sw::UserSettingsManager _settings;
        sw::InputManager        _input;
        sw::UiSystem            _ui;
        sw::UiScreenHandle      _screen;

        UiSettingsBindingFixture()
            : _settings{}
            , _input{}
            , _ui{}
            , _screen{ sw::kInvalidUiScreenHandle }
        {
            _settings.initialize( sw::UserSettingsTargets{} );
            SW_EXPECT_TRUE( _settings.loadSchemaFromXmlText( UiSettingsBindingTestUtil::kSchemaXml, "test.settings.xml" ) );
            _settings.reapplyAll();
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
            _ui.setUserSettings( &_settings );
            _ui.getDocumentCache().registerMemoryDocument( UiSettingsBindingTestUtil::kDocumentPath, UiSettingsBindingTestUtil::kDocumentText );
            _screen = _ui.openScreen( UiSettingsBindingTestUtil::kDocumentPath );
            runFrame();
        }

        ~UiSettingsBindingFixture()
        {
            _ui.shutdown();
            _input.shutdown();
            _settings.shutdown();
        }

        UiSettingsBindingFixture( const UiSettingsBindingFixture& )            = delete;
        UiSettingsBindingFixture& operator=( const UiSettingsBindingFixture& ) = delete;

        void runFrame() { UiSettingsBindingTestUtil::runFrame( _input, _ui ); }

        template <typename WidgetType>
        WidgetType* find( const utf8* pName ) const
        {
            const sw::UiScreen* pScreen = _ui.findScreen( _screen );
            return pScreen != nullptr ? pScreen->getTree().findWidget<WidgetType>( sw::hashed_string( pName ) ) : nullptr;
        }
    };
} // namespace

/** @brief [UiSettingsBindingTest] 슬라이더가 설정 값 · 범위 · 눈금을 읽고, 사용자가 움직이면 보류 값으로 넣는다 */
SW_TEST_CASE( UiSettingsBindingTest, SliderReadsAndWritesPendingValue )
{
    UiSettingsBindingFixture fixture;
    SW_EXPECT_EQUAL( 3u, fixture._ui.findScreen( fixture._screen )->getBindingSet().getBindingCount() );
    sw::SliderWidget* pSlider = fixture.find<sw::SliderWidget>( "Master" );
    SW_ASSERT_NOT_NULL( pSlider );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSlider->getValue(), 0.0001f );

    sw::UiActionEvent event{};
    event._action = sw::hashed_string( sw::UiActionName::kNavigateRight );
    (void)pSlider->onActionEvent( event, sw::UiRoutePhase::Bubble ); // 한 칸 = 설정 정의의 눈금 0.25
    SW_EXPECT_NEAR_EQUAL( 0.75f, UiSettingsBindingTestUtil::readFloat( fixture._settings, "audio.master" ), 0.0001f );
    SW_EXPECT_TRUE( fixture._settings.isPending( "audio.master" ) );
    fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 0.75f, pSlider->getValue(), 0.0001f );
}

/** @brief [UiSettingsBindingTest] 설정이 눈금으로 고쳐 받은 값은 위젯에 되쓴다 — 끌어 놓은 자리(눈금 밖)가 아니라 설정 값이 보인다 */
SW_TEST_CASE( UiSettingsBindingTest, ClampedValueIsWrittenBack )
{
    UiSettingsBindingFixture fixture;
    sw::SliderWidget*        pSlider = fixture.find<sw::SliderWidget>( "Master" );
    SW_ASSERT_NOT_NULL( pSlider );
    const sw::WidgetGeometry& geometry = pSlider->getGeometry();
    SW_ASSERT_TRUE( geometry._size._x > 0.0f );
    sw::UiPointerEvent down{};
    down._kind     = sw::UiPointerEventKind::Down;
    down._button   = sw::MouseButton::Left;
    down._position = geometry._translation + sw::float2{ geometry._size._x * 0.3f, geometry._size._y * 0.5f };
    (void)pSlider->onPointerEvent( down, sw::UiRoutePhase::Bubble );
    const float32 dragged = pSlider->getValue();
    const float32 stored  = UiSettingsBindingTestUtil::readFloat( fixture._settings, "audio.master" );
    SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( dragged - stored ) > 0.001f, "the dragged value is off the 0.25 grid" );

    fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( stored, pSlider->getValue(), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::MathUtil::fmod( pSlider->getValue(), 0.25f ), 0.0001f );
}

/** @brief [UiSettingsBindingTest] 지금 바꿀 수 없는 설정(enabledWhen)은 위젯을 끈다 — 조건 설정이 바뀌면 다시 켠다 */
SW_TEST_CASE( UiSettingsBindingTest, DisabledSettingDisablesWidget )
{
    UiSettingsBindingFixture fixture;
    sw::CheckBoxWidget*      pVSync = fixture.find<sw::CheckBoxWidget>( "VSync" );
    SW_ASSERT_NOT_NULL( pVSync );
    SW_EXPECT_TRUE( pVSync->isChecked() );
    SW_EXPECT_FALSE( pVSync->isEnabled() ); // video.mode 가 windowed

    SW_EXPECT_TRUE( fixture._settings.setPendingValue( "video.mode", "fullscreen" ) == sw::UserSettingSetResult::Accepted );
    fixture.runFrame();
    SW_EXPECT_TRUE( pVSync->isEnabled() );
}

/** @brief [UiSettingsBindingTest] 보류 값을 되돌리면(다른 곳의 변경 — 변경 통보) 위젯도 돌아간다 */
SW_TEST_CASE( UiSettingsBindingTest, RevertUpdatesWidgets )
{
    UiSettingsBindingFixture fixture;
    sw::SliderWidget*        pSlider = fixture.find<sw::SliderWidget>( "Master" );
    SW_ASSERT_NOT_NULL( pSlider );
    sw::UiActionEvent event{};
    event._action = sw::hashed_string( sw::UiActionName::kNavigateRight );
    (void)pSlider->onActionEvent( event, sw::UiRoutePhase::Bubble );
    fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 0.75f, pSlider->getValue(), 0.0001f );

    fixture._settings.revertPending();
    fixture.runFrame();
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSlider->getValue(), 0.0001f );
}

/** @brief [UiSettingsBindingTest] 열거형 설정은 콤보의 선택지(글 키, 없으면 값)와 고른 자리를 채운다 — 고르면 그 선택지 값이 보류 값이 된다 */
SW_TEST_CASE( UiSettingsBindingTest, EnumOptionsFillComboBox )
{
    UiSettingsBindingFixture fixture;
    sw::ComboBoxWidget*      pMode = fixture.find<sw::ComboBoxWidget>( "Mode" );
    SW_ASSERT_NOT_NULL( pMode );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( pMode->getOptions().size() ) );
    SW_EXPECT_STREQ( "Settings.Windowed", pMode->getOptions()[0].c_str() );
    SW_EXPECT_STREQ( "Settings.Fullscreen", pMode->getOptions()[1].c_str() );
    SW_EXPECT_STREQ( "borderless", pMode->getOptions()[2].c_str() );
    SW_EXPECT_EQUAL( 0u, pMode->getSelectedIndex() );

    pMode->choosePopupOption( 2 );
    SW_EXPECT_STREQ( "borderless", sw::string( fixture._settings.getValue( "video.mode" ) ).c_str() );
    SW_EXPECT_TRUE( fixture._settings.setPendingValue( "video.mode", "fullscreen" ) == sw::UserSettingSetResult::Accepted );
    fixture.runFrame();
    SW_EXPECT_EQUAL( 1u, pMode->getSelectedIndex() );
}

/** @brief [UiSettingsBindingTest] 설정 변경 통보 리스너는 화면이 닫히면 떨어진다 */
SW_TEST_CASE( UiSettingsBindingTest, ListenerRemovedOnScreenClose )
{
    UiSettingsBindingFixture fixture;
    SW_EXPECT_TRUE( fixture._settings.hasEventListener() );
    fixture._ui.closeScreen( fixture._screen );
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._ui.findScreen( fixture._screen ) == nullptr );
    SW_EXPECT_FALSE( fixture._settings.hasEventListener() );
}

/** @brief [UiSettingsBindingTest] 없는 설정 id 는 걸 때 오류다 */
SW_TEST_CASE( UiSettingsBindingTest, UnknownSettingIsError )
{
    UiSettingsBindingFixture fixture;
    fixture._ui.getDocumentCache().registerMemoryDocument( "test/settingsbinding/unknown.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                                                  "\t<SliderWidget _value=\"{setting:audio.missing}\" />\n"
                                                                                                  "</UiDocument>\n" );
    const sw::UiScreenHandle handle = fixture._ui.openScreen( "test/settingsbinding/unknown.ui.xml" );
    {
        SW_TEST_DEFENSIVE_SCOPE( "an unknown setting id is a bind error" );
        fixture.runFrame();
    }
    const sw::vector<sw::string>& listError = fixture._ui.findScreen( handle )->getBindingSet().getErrors();
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listError.size() ) );
    SW_EXPECT_TRUE_MSG( listError[0].find( "test/settingsbinding/unknown.ui.xml:2: binding '{setting:audio.missing}' on _value: unknown setting 'audio.missing'" ) !=
                            sw::string::npos,
                        listError[0].c_str() );
}
