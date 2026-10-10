#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/UILayoutDump.h"
#include "Engine/UI/Render/UICanvasDump.h"
#include "Engine/UI/Screen/OptionsMenuScreen.h"
#include "Engine/UI/Screen/UINotificationService.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

#include <cstdlib>

// UIDeterminismTest — 같은 입력이면 같은 사각형 · 같은 그리기 목록: 견본 문서 넷(일시정지 · 옵션(시험 스키마) · HUD 견본 · 알림)을 해상도 셋 × UI 배율 둘로
// 레이아웃 덤프 · 캔버스 덤프해 골든(Test/EngineTest/UI/Golden)과 견주고, 같은 문서를 두 번 · 자식 생성 순서를 섞어 지어도 같은 덤프인지 본다.
// 가짜 래스터라이저(글자 0.5 em)라 글꼴 · FreeType 판에 기대지 않는다. 골든 갱신: SW_UPDATE_GOLDEN=1 로 돌리고 diff 를 보고 커밋한다. 디바이스 없음(nogpu).

namespace
{
    struct UIDeterminismTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;
        static constexpr uint32  kSettleFrames = 60; ///< 여는 애니메이션(0.2 초)이 끝날 만큼

        /** @brief 해상도 · UI 배율 한 조건입니다. */
        struct Condition
        {
            float32 _width;
            float32 _height;
            float32 _uiScale;
        };

        static constexpr Condition kArrCondition[] = {
            {1280.0f,  720.0f, 1.0f},
            {1280.0f,  720.0f, 1.5f},
            {1920.0f, 1080.0f, 1.0f},
            {1920.0f, 1080.0f, 1.5f},
            {3840.0f, 2160.0f, 1.0f},
            {3840.0f, 2160.0f, 1.5f},
        };

        /** @brief 옵션 메뉴의 시험 스키마 — 카테고리 둘, 형식마다 한 줄. */
        static constexpr const utf8* kSchemaXML = R"(
<UserSettingsSchema version="1">
    <Category id="audio" text="Audio"/>
    <Category id="video" text="Video"/>
    <Setting id="audio.master" category="audio" type="float" default="0.5" min="0" max="1" step="0.25" text="Master Volume"/>
    <Setting id="audio.voices" category="audio" type="int" default="8" min="1" max="16" step="1" text="Voices"/>
    <Setting id="audio.mute" category="audio" type="bool" default="false" text="Mute"/>
    <Setting id="video.mode" category="video" type="enum" default="windowed" text="Window Mode">
        <Option value="windowed"/>
        <Option value="fullscreen"/>
    </Setting>
</UserSettingsSchema>)";

        /** @brief HUD 견본 — 네 모서리 앵커 · 진행 막대 · 글 · 가운데 조준 상자(게임 팩에 기대지 않는 엔진 시험 문서). */
        static constexpr utf8 kHUDDocument[] = "<UIDocument _schemaVersion=\"1\">\n"
                                               "\t<UIScreenDesc _layer=\"HUD\" _bTakesFocus=\"false\" _bShowCursor=\"false\" />\n"
                                               "\t<SafeZonePanel _visibility=\"SelfHitTestInvisible\">\n"
                                               "\t\t<CanvasPanel _visibility=\"SelfHitTestInvisible\">\n"
                                               "\t\t\t<BorderPanel _name=\"Crosshair\">\n"
                                               "\t\t\t\t<_slot _anchorMin=\"0.5,0.5\" _anchorMax=\"0.5,0.5\" _offsetMin=\"-8,-8\" _offsetMax=\"8,8\" />\n"
                                               "\t\t\t\t<_background _color=\"1,1,1,0.9\" _cornerRadius=\"8,8,8,8\" />\n"
                                               "\t\t\t</BorderPanel>\n"
                                               "\t\t\t<TextWidget _name=\"Health\" _text=\"100\" _bLocalized=\"false\">\n"
                                               "\t\t\t\t<_slot _anchorMin=\"0,1\" _anchorMax=\"0,1\" _offsetMin=\"48,-120\" _offsetMax=\"368,-84\" />\n"
                                               "\t\t\t</TextWidget>\n"
                                               "\t\t\t<ProgressBarWidget _name=\"HealthBar\" _percent=\"0.75\" _fillColor=\"0.85,0.25,0.25,1\" _backgroundColor=\"0,0,0,0.55\">\n"
                                               "\t\t\t\t<_slot _anchorMin=\"0,1\" _anchorMax=\"0,1\" _offsetMin=\"48,-76\" _offsetMax=\"368,-56\" />\n"
                                               "\t\t\t</ProgressBarWidget>\n"
                                               "\t\t\t<TextWidget _name=\"Ammo\" _text=\"30 / 90\" _bLocalized=\"false\">\n"
                                               "\t\t\t\t<_slot _anchorMin=\"1,1\" _anchorMax=\"1,1\" _offsetMin=\"-248,-84\" _offsetMax=\"-48,-48\" />\n"
                                               "\t\t\t\t<_style _alignment=\"End\" />\n"
                                               "\t\t\t</TextWidget>\n"
                                               "\t\t\t<TextWidget _name=\"Objective\" _text=\"Reach the gate\" _bLocalized=\"false\">\n"
                                               "\t\t\t\t<_slot _anchorMin=\"1,0\" _anchorMax=\"1,0\" _offsetMin=\"-448,48\" _offsetMax=\"-48,84\" />\n"
                                               "\t\t\t</TextWidget>\n"
                                               "\t\t</CanvasPanel>\n"
                                               "\t</SafeZonePanel>\n"
                                               "</UIDocument>\n";

        /** @brief 골든 폴더(`Test/EngineTest/UI/Golden`)의 절대 경로 — 작업 폴더(`build/<프리셋>/Bin`)에서 위로 올라가며 찾는다. 못 찾으면 빈 글. */
        static sw::string findGoldenDirectory()
        {
            sw::string directory = sw::FileUtil::getCurrentPath();
            for ( uint32 depth = 0; depth < 8 && directory.empty() == false; ++depth )
            {
                const sw::string candidate = sw::FileUtil::joinPath( directory, "Test/EngineTest/UI/Golden" );
                if ( sw::FileUtil::isDirectory( candidate ) )
                    return candidate;
                const sw::string parent = sw::FileUtil::getDirectoryPart( directory );
                if ( parent == directory )
                    break;
                directory = parent;
            }
            return {};
        }

        static bool isUpdatingGolden()
        {
            const utf8* pUpdate = std::getenv( "SW_UPDATE_GOLDEN" );
            return pUpdate != nullptr && pUpdate[0] == '1';
        }

        /** @brief 줄끝을 LF 로(저장소 체크아웃이 CRLF 로 바꿔도 견준다). */
        static sw::string normalizeNewlines( const sw::string& text )
        {
            sw::string result;
            result.reserve( text.size() );
            for ( const utf8 character : text )
            {
                if ( character != '\r' )
                    result += character;
            }
            return result;
        }

        /** @brief 덤프 @p text 를 골든 @p fileName 과 견줍니다(갱신 모드면 씁니다). 다르면 첫 다른 줄을 실패 글에 적습니다. */
        static void expectGolden( const sw::string& fileName, const sw::string& text )
        {
            const sw::string directory = findGoldenDirectory();
            SW_ASSERT_TRUE_MSG( directory.empty() == false, "Test/EngineTest/UI/Golden not found above the working directory" );
            const sw::string path = sw::FileUtil::joinPath( directory, fileName );
            if ( isUpdatingGolden() )
            {
                SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( path, text ) );
                return;
            }
            sw::string golden;
            SW_ASSERT_TRUE_MSG( sw::FileUtil::readTextFile( path, golden ), ( "missing golden (record with SW_UPDATE_GOLDEN=1): " + path ).c_str() );
            golden = normalizeNewlines( golden );
            if ( golden == text )
                return;
            size_t lineStart  = 0;
            uint32 lineNumber = 1;
            for ( size_t index = 0; index < golden.size() && index < text.size() && golden[index] == text[index]; ++index )
            {
                if ( golden[index] == '\n' )
                {
                    lineStart = index + 1;
                    ++lineNumber;
                }
            }
            const size_t     goldenEnd  = golden.find( '\n', lineStart );
            const size_t     actualEnd  = text.find( '\n', lineStart );
            const sw::string goldenLine = golden.substr( lineStart, goldenEnd == sw::string::npos ? sw::string::npos : goldenEnd - lineStart );
            const sw::string actualLine = text.substr( lineStart, actualEnd == sw::string::npos ? sw::string::npos : actualEnd - lineStart );
            SW_EXPECT_TRUE_MSG( false, ( fileName + ":" + sw::to_string( lineNumber ) + " golden '" + goldenLine + "' actual '" + actualLine + "'" ).c_str() );
        }
    };

    /** @brief 문서 하나를 여는 방법입니다(문서 · 옵션 메뉴 · 알림). */
    enum class UIDeterminismSample : uint8
    {
        Pause,
        Options,
        HUD,
        Notifications,
    };

    /** @brief 입력 · 가짜 글꼴 · 설정 · UI 시스템 — 조건 하나의 뷰포트로 프레임을 돌립니다. 조건마다 새로 짓는다(앞 조건이 남긴 상태가 새지 않게). */
    struct UIDeterminismFixture
    {
        sw::UserSettingsManager         _settings;
        sw::InputManager                _input;
        sw::test::FakeFontSystemFixture _fonts;
        sw::UISystem                    _ui;
        sw::UIViewport                  _viewport;
        bool                            _bReady;

        explicit UIDeterminismFixture( const UIDeterminismTestUtil::Condition& condition )
            : _settings{}
            , _input{}
            , _fonts{}
            , _ui{}
            , _viewport{}
            , _bReady{ false }
        {
            sw::FontCatalogDesc catalog{};
            catalog._defaultFamily = "Latin";
            sw::test::FakeFontSystemFixture::addFamily( catalog, "Latin", "test/fonts/latin.ttf" );
            _settings.initialize( sw::UserSettingsTargets{} );
            _bReady = _settings.loadSchemaFromXMLText( UIDeterminismTestUtil::kSchemaXML, "test.settings.xml" ) && _input.initialize() &&
                      _fonts.initialize( catalog ) && _ui.initialize( _input, _fonts._fontSystem.get() );
            _settings.reapplyAll();
            _ui.setUserSettings( &_settings );
            _viewport._physicalSize = sw::float2{ condition._width, condition._height };
            _viewport._uiScale      = condition._uiScale;
            _viewport._size         = sw::float2{ condition._width / condition._uiScale, condition._height / condition._uiScale };
        }

        ~UIDeterminismFixture()
        {
            _ui.shutdown();
            _input.shutdown();
            _settings.shutdown();
        }

        UIDeterminismFixture( const UIDeterminismFixture& )            = delete;
        UIDeterminismFixture& operator=( const UIDeterminismFixture& ) = delete;

        void runFrame()
        {
            _input.beginFrame( UIDeterminismTestUtil::kFrameSeconds );
            _ui.processInput( UIDeterminismTestUtil::kFrameSeconds );
            _ui.update( UIDeterminismTestUtil::kFrameSeconds, _viewport );
            _input.endFrame();
        }

        void settle()
        {
            for ( uint32 frame = 0; frame < UIDeterminismTestUtil::kSettleFrames; ++frame )
            {
                runFrame();
            }
        }

        /** @brief 견본을 엽니다. 연 화면의 핸들입니다. */
        sw::UIScreenHandle open( UIDeterminismSample sample )
        {
            switch ( sample )
            {
                case UIDeterminismSample::Pause:
                {
                    return _ui.openScreen( "engine/ui/pause.ui.xml" );
                }
                case UIDeterminismSample::Options:
                {
                    return sw::OptionsMenuScreen::open( _ui );
                }
                case UIDeterminismSample::HUD:
                {
                    _ui.getDocumentCache().registerMemoryDocument( "test/hud.ui.xml", UIDeterminismTestUtil::kHUDDocument );
                    return _ui.openScreen( "test/hud.ui.xml" );
                }
                case UIDeterminismSample::Notifications:
                {
                    sw::UINotificationDesc desc{};
                    desc._durationSeconds = 30.0f;
                    desc._text            = "Game saved";
                    _ui.getNotifications().post( desc );
                    desc._kind = sw::UINotificationKind::Achievement;
                    desc._text = "Achievement unlocked: First steps";
                    _ui.getNotifications().post( desc );
                    _ui.getNotifications().post( desc ); // 같은 글 — "x2"
                    runFrame();
                    return _ui.getNotifications().getScreen();
                }
            }
            return sw::kInvalidUIScreenHandle;
        }

        /** @brief 화면 @p handle 의 레이아웃 덤프(물리 픽셀)입니다. */
        sw::string makeLayoutDump( sw::UIScreenHandle handle ) const
        {
            const sw::UIScreen* pScreen = _ui.findScreen( handle );
            return pScreen != nullptr ? sw::UILayoutDump::makeDump( pScreen->getTree(), _viewport._uiScale ) : sw::string( "(no screen)\n" );
        }
    };

    struct UIDeterminismSampleUtil
    {
        static const utf8* getName( UIDeterminismSample sample )
        {
            switch ( sample )
            {
                case UIDeterminismSample::Pause:
                    return "pause";
                case UIDeterminismSample::Options:
                    return "options";
                case UIDeterminismSample::HUD:
                    return "hud";
                case UIDeterminismSample::Notifications:
                    return "notifications";
            }
            return "unknown";
        }

        static sw::string makeConditionHeader( const UIDeterminismTestUtil::Condition& condition )
        {
            return "== " + sw::to_string( static_cast<uint32>( condition._width ) ) + "x" + sw::to_string( static_cast<uint32>( condition._height ) ) + " scale " +
                   sw::to_string( static_cast<uint32>( condition._uiScale * 100.0f ) ) + "%\n";
        }

        /** @brief 견본 하나를 여섯 조건으로 덤프해 골든 둘(`<이름>.layout.txt` · `<이름>.canvas.txt`)과 견줍니다. */
        static void expectSampleGolden( UIDeterminismSample sample )
        {
            sw::string layout;
            sw::string canvas;
            for ( const UIDeterminismTestUtil::Condition& condition : UIDeterminismTestUtil::kArrCondition )
            {
                UIDeterminismFixture fixture( condition );
                SW_ASSERT_TRUE( fixture._bReady );
                const sw::UIScreenHandle handle = fixture.open( sample );
                SW_ASSERT_TRUE( fixture._ui.findScreen( handle ) != nullptr );
                fixture.settle();
                const sw::string header = makeConditionHeader( condition );
                layout += header + fixture.makeLayoutDump( handle );
                canvas += header + sw::UICanvasDump::makeDump( fixture._ui.getCanvas() );
            }
            const sw::string name = getName( sample );
            UIDeterminismTestUtil::expectGolden( name + ".layout.txt", layout );
            UIDeterminismTestUtil::expectGolden( name + ".canvas.txt", canvas );
        }

        /** @brief 같은 견본을 새 시스템 둘로 지은 덤프가 같은지 봅니다(1920×1080 · 배율 1.5). */
        static void expectRebuildIsSame( UIDeterminismSample sample )
        {
            const UIDeterminismTestUtil::Condition& condition = UIDeterminismTestUtil::kArrCondition[3];
            sw::string                              arrDump[2];
            for ( sw::string& dump : arrDump )
            {
                UIDeterminismFixture fixture( condition );
                SW_ASSERT_TRUE( fixture._bReady );
                const sw::UIScreenHandle handle = fixture.open( sample );
                fixture.settle();
                dump = fixture.makeLayoutDump( handle ) + sw::UICanvasDump::makeDump( fixture._ui.getCanvas() );
            }
            SW_EXPECT_TRUE_MSG( arrDump[0] == arrDump[1], getName( sample ) );
        }
    };

    /**
     * @brief 코드로 짓는 견본 트리 — 칸마다 (이름 · 종류 · 앵커 자리) 하나. 위젯을 @p arrCreateOrder 순서로 **만들고** 칸 순서대로 붙인다(위젯 번호만 달라진다).
     */
    struct UIShuffledTreeBuilder
    {
        static constexpr uint32 kCellCount = 6;

        static sw::unique_ptr<sw::Widget> makeCell( uint32 cell )
        {
            const sw::string           name = "Cell" + sw::to_string( cell );
            sw::unique_ptr<sw::Widget> widget;
            switch ( cell % 3 )
            {
                case 0:
                {
                    sw::unique_ptr<sw::BorderPanel> border = sw::make_unique<sw::BorderPanel>();
                    border->setBackground( sw::UIBrush::makeSolid( sw::float4{ 0.1f * static_cast<float32>( cell ), 0.2f, 0.3f, 0.9f }, 6.0f ) );
                    widget = std::move( border );
                    break;
                }
                case 1:
                {
                    sw::unique_ptr<sw::TextWidget> text = sw::make_unique<sw::TextWidget>();
                    text->setText( "Cell text " + sw::to_string( cell ) );
                    widget = std::move( text );
                    break;
                }
                default:
                {
                    sw::unique_ptr<sw::ProgressBarWidget> progress = sw::make_unique<sw::ProgressBarWidget>();
                    progress->setPercent( 0.25f * static_cast<float32>( cell % 4 ) );
                    widget = std::move( progress );
                    break;
                }
            }
            widget->setName( sw::hashed_string( name ) );
            sw::WidgetLayoutSlot slot = widget->getLayoutSlot();
            const float32        x    = 0.1f + 0.15f * static_cast<float32>( cell );
            slot._anchorMin           = sw::float2{ x, 0.3f };
            slot._anchorMax           = sw::float2{ x, 0.3f };
            slot._offsetMin           = sw::float2{ 0.0f, 0.0f };
            slot._offsetMax           = sw::float2{ 120.0f, 40.0f + 10.0f * static_cast<float32>( cell ) };
            widget->setLayoutSlot( slot );
            return widget;
        }

        static sw::unique_ptr<sw::UIScreen> create( const uint32 ( &arrCreateOrder )[kCellCount] )
        {
            sw::unique_ptr<sw::Widget> arrCell[kCellCount];
            for ( const uint32 cell : arrCreateOrder )
            {
                arrCell[cell] = makeCell( cell );
            }
            sw::unique_ptr<sw::CanvasPanel> root = sw::make_unique<sw::CanvasPanel>();
            for ( sw::unique_ptr<sw::Widget>& cell : arrCell )
            {
                (void)root->addChild( std::move( cell ) );
            }
            sw::UIScreenDesc desc{};
            desc._layer = sw::UILayer::HUD;
            return sw::make_unique<sw::UIScreen>( desc, std::move( root ) );
        }
    };
} // namespace

/** @brief [UIDeterminismTest] 일시정지 문서(engine/ui/pause.ui.xml)의 레이아웃 · 캔버스 덤프가 해상도 셋 × 배율 둘에서 골든과 같다 */
SW_TEST_CASE( UIDeterminismTest, PauseMatchesGolden )
{
    UIDeterminismSampleUtil::expectSampleGolden( UIDeterminismSample::Pause );
}

/** @brief [UIDeterminismTest] 엔진 옵션 메뉴(시험 스키마 — 탭 둘 · 형식마다 행)의 덤프가 골든과 같다 */
SW_TEST_CASE( UIDeterminismTest, OptionsMatchesGolden )
{
    UIDeterminismSampleUtil::expectSampleGolden( UIDeterminismSample::Options );
}

/** @brief [UIDeterminismTest] HUD 견본(네 모서리 앵커 · 안전 영역)의 덤프가 골든과 같다 */
SW_TEST_CASE( UIDeterminismTest, HUDMatchesGolden )
{
    UIDeterminismSampleUtil::expectSampleGolden( UIDeterminismSample::HUD );
}

/** @brief [UIDeterminismTest] 알림 화면(항목 둘 · 합친 "x2")의 덤프가 골든과 같다 */
SW_TEST_CASE( UIDeterminismTest, NotificationsMatchGolden )
{
    UIDeterminismSampleUtil::expectSampleGolden( UIDeterminismSample::Notifications );
}

/** @brief [UIDeterminismTest] 같은 견본을 새 UI 시스템 둘로 지으면 덤프가 바이트까지 같다(전역 · 캐시 상태가 결과에 새지 않는다) */
SW_TEST_CASE( UIDeterminismTest, RebuildGivesSameDump )
{
    for ( const UIDeterminismSample sample :
          { UIDeterminismSample::Pause, UIDeterminismSample::Options, UIDeterminismSample::HUD, UIDeterminismSample::Notifications } )
    {
        UIDeterminismSampleUtil::expectRebuildIsSame( sample );
    }
}

/**
 * @brief [UIDeterminismTest] 위젯을 만드는 순서를 섞어도(위젯 번호가 달라도) 트리 순서가 같으면 덤프가 같다 — 번호 순 해시 맵 · 누적 순서가 결과에 새지 않는다
 * @details 같은 시스템에 두 화면을 차례로 올려 견준다(두 번째는 앞의 번호를 이어 받는다).
 */
SW_TEST_CASE( UIDeterminismTest, CreationOrderDoesNotLeak )
{
    constexpr uint32 kArrForward[UIShuffledTreeBuilder::kCellCount]  = { 0, 1, 2, 3, 4, 5 };
    constexpr uint32 kArrShuffled[UIShuffledTreeBuilder::kCellCount] = { 4, 1, 5, 0, 3, 2 };
    sw::string       arrDump[2];
    uint32           index = 0;
    for ( const uint32( *pOrder )[UIShuffledTreeBuilder::kCellCount] : { &kArrForward, &kArrShuffled } )
    {
        UIDeterminismFixture fixture( UIDeterminismTestUtil::kArrCondition[3] );
        SW_ASSERT_TRUE( fixture._bReady );
        if ( index == 1 )
        {
            // 번호를 앞당겨 쓰고 버린다 — 두 번째 트리의 위젯 번호가 첫 번째와 겹치지 않게
            const sw::UIScreenHandle warmup = fixture._ui.pushScreen( UIShuffledTreeBuilder::create( kArrForward ) );
            fixture._ui.closeScreen( warmup );
            fixture.runFrame();
        }
        const sw::UIScreenHandle handle = fixture._ui.pushScreen( UIShuffledTreeBuilder::create( *pOrder ) );
        fixture.settle();
        arrDump[index++] = fixture.makeLayoutDump( handle ) + sw::UICanvasDump::makeDump( fixture._ui.getCanvas() );
    }
    SW_EXPECT_TRUE( arrDump[0].find( "Cell5" ) != sw::string::npos );
    SW_EXPECT_TRUE( arrDump[0] == arrDump[1] );
}
