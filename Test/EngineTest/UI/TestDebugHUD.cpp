#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Input/InputManager.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Debug/DebugHUD.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "TestFramework/TestFramework.h"

// DebugHUDTest — 디버그 HUD: 섹션 등록부(등록 · 순서 · 거절 · 모듈 언로드), 설정 글(켬 · 끔 낱말), 사용자 설정 왕복(파일까지),
// HUD 화면이 켜진 섹션만 그리고 모서리를 따르는지, 설정 창이 열린 동안만 게임 입력을 막는지. 디바이스 없음(nogpu).

namespace
{
    struct DebugHUDTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;
        /** @brief HUD 갱신 간격(0.25 초)을 넘기는 프레임 수입니다. */
        static constexpr uint32 kRefreshFrameCount = 20;

        static void writeTestSection( sw::DebugHUDSectionWriter& writer ) { writer.addLine( "value", "42" ); }

        static void runFrames( sw::InputManager& input, sw::UISystem& ui, sw::DebugHUD& hud, uint32 frameCount )
        {
            for ( uint32 index = 0; index < frameCount; ++index )
            {
                input.beginFrame( kFrameSeconds );
                ui.processInput( kFrameSeconds );
                hud.update( nullptr );
                ui.update( kFrameSeconds, sw::UIViewport{
                                              sw::float2{ 1280.0f, 720.0f }
                } );
                input.endFrame();
            }
        }

        static sw::GlobalVariableInfo* findVariable( const utf8* pName ) { return sw::engine::getGlobalVariableManager().findVariable( pName ); }

        static sw::string readVariable( const utf8* pName )
        {
            const sw::GlobalVariableInfo* pVariable = findVariable( pName );
            return pVariable != nullptr ? pVariable->getValueAsString() : sw::string{};
        }
    };

    /** @brief HUD 전역 변수 넷을 케이스 앞의 값으로 되돌립니다(테스트는 Engine.dll 의 전역 변수를 extern 으로 못 읽어 이름으로 다룬다). */
    struct DebugHUDVariableGuard
    {
        static constexpr uint32      kVariableCount           = 4;
        static constexpr const utf8* kArrName[kVariableCount] = { "gv_debugHUD", "gv_debugHUDSections", "gv_debugHUDCorner", "gv_debugHUDOpacity" };
        sw::string                   _arrSaved[kVariableCount];

        DebugHUDVariableGuard()
            : _arrSaved{}
        {
            for ( uint32 index = 0; index < kVariableCount; ++index )
            {
                _arrSaved[index] = DebugHUDTestUtil::readVariable( kArrName[index] );
            }
        }

        ~DebugHUDVariableGuard()
        {
            for ( uint32 index = 0; index < kVariableCount; ++index )
            {
                sw::GlobalVariableInfo* pVariable = DebugHUDTestUtil::findVariable( kArrName[index] );
                if ( pVariable != nullptr )
                    (void)pVariable->setValueFromString( _arrSaved[index] ); // 되돌리기 — 읽은 값이라 실패하지 않는다
            }
        }

        DebugHUDVariableGuard( const DebugHUDVariableGuard& )            = delete;
        DebugHUDVariableGuard& operator=( const DebugHUDVariableGuard& ) = delete;
    };

    /** @brief HUD 스키마를 읽은 설정 매니저 · 입력 · UI · HUD 입니다(설정 대상은 엔진 전역 변수). */
    struct DebugHUDFixture
    {
        DebugHUDVariableGuard   _guard;
        sw::UserSettingsManager _settings;
        sw::InputManager        _input;
        sw::UISystem            _ui;
        sw::DebugHUD            _hud;

        DebugHUDFixture()
            : _guard{}
            , _settings{}
            , _input{}
            , _ui{}
            , _hud{}
        {
            sw::UserSettingsTargets targets;
            targets._pGlobalVariableManager = &sw::engine::getGlobalVariableManager();
            _settings.initialize( targets );
            SW_EXPECT_TRUE( _settings.loadSchema( sw::DebugHUD::kSettingsSchemaPath ) );
            _settings.reapplyAll();
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr ) );
            _ui.setUserSettings( &_settings );
            _hud.initialize( _ui, &_settings );
        }

        ~DebugHUDFixture()
        {
            _hud.shutdown();
            _ui.shutdown();
            _input.shutdown();
            _settings.shutdown();
        }

        DebugHUDFixture( const DebugHUDFixture& )            = delete;
        DebugHUDFixture& operator=( const DebugHUDFixture& ) = delete;

        void runFrames( uint32 frameCount ) { DebugHUDTestUtil::runFrames( _input, _ui, _hud, frameCount ); }
    };
} // namespace

/** @brief [DebugHUDTest] 등록자가 살아 있는 동안만 섹션이 있다 — 모듈을 내리면(등록자가 사라지면) 빠지고 판 번호가 오른다 */
SW_TEST_CASE( DebugHUDTest, RegistrarScopeIsTheModuleLifetime )
{
    sw::DebugHUDRegistry& registry      = sw::DebugHUDRegistry::get();
    const uint32          countBefore   = registry.getCount();
    const uint64          revisionStart = registry.getRevision();
    {
        static const sw::DebugHUDSectionRegistration kRegistration{ "testmodule", "Test Module", &DebugHUDTestUtil::writeTestSection, 1000, sw::DebugHUDSectionFlag::kNone };
        const sw::DebugHUDSectionRegistrar           registrar{ &kRegistration };
        SW_EXPECT_TRUE( registrar._bRegistered );
        SW_EXPECT_EQUAL( countBefore + 1, registry.getCount() );
        SW_EXPECT_TRUE( registry.findSection( "TestModule" ) == &kRegistration ); // 대소문자 무시
        SW_EXPECT_TRUE( registry.getRevision() > revisionStart );
    }
    SW_EXPECT_EQUAL( countBefore, registry.getCount() );
    SW_EXPECT_TRUE( registry.findSection( "testmodule" ) == nullptr );
    SW_EXPECT_EQUAL( revisionStart + 2, registry.getRevision() );
}

/** @brief [DebugHUDTest] 순서 값 → 이름순이고, 같은 이름 · 명령 예약어 · 구분 글자가 든 이름은 거절한다 */
SW_TEST_CASE( DebugHUDTest, RegistryOrdersAndRejects )
{
    sw::DebugHUDRegistry&                        registry = sw::DebugHUDRegistry::get();
    static const sw::DebugHUDSectionRegistration kLate{ "testlate", "Late", &DebugHUDTestUtil::writeTestSection, 2000, sw::DebugHUDSectionFlag::kNone };
    static const sw::DebugHUDSectionRegistration kEarly{ "testearly", "Early", &DebugHUDTestUtil::writeTestSection, -2000, sw::DebugHUDSectionFlag::kNone };
    static const sw::DebugHUDSectionRegistration kDuplicate{ "TESTLATE", "Duplicate", &DebugHUDTestUtil::writeTestSection, 0, sw::DebugHUDSectionFlag::kNone };
    static const sw::DebugHUDSectionRegistration kReserved{ "window", "Reserved", &DebugHUDTestUtil::writeTestSection, 0, sw::DebugHUDSectionFlag::kNone };
    static const sw::DebugHUDSectionRegistration kSpaced{ "two words", "Spaced", &DebugHUDTestUtil::writeTestSection, 0, sw::DebugHUDSectionFlag::kNone };
    static const sw::DebugHUDSectionRegistration kDash{ "-dash", "Dash", &DebugHUDTestUtil::writeTestSection, 0, sw::DebugHUDSectionFlag::kNone };

    const sw::DebugHUDSectionRegistrar late{ &kLate };
    const sw::DebugHUDSectionRegistrar early{ &kEarly };
    SW_EXPECT_TRUE( late._bRegistered && early._bRegistered );
    SW_EXPECT_TRUE( registry.getSections().front() == &kEarly );
    SW_EXPECT_TRUE( registry.getSections().back() == &kLate );
    SW_EXPECT_FALSE( registry.registerSection( &kDuplicate ) );
    SW_EXPECT_FALSE( registry.registerSection( &kReserved ) );
    SW_EXPECT_FALSE( registry.registerSection( &kSpaced ) );
    SW_EXPECT_FALSE( registry.registerSection( &kDash ) );
    SW_EXPECT_TRUE( registry.findSection( "testlate" ) == &kLate );
}

/** @brief [DebugHUDTest] 설정 글 — 적히지 않은 섹션은 기본, `이름` 은 켬 · `-이름` 은 끔, 기본과 같으면 낱말을 지우고 남의 낱말은 남긴다 */
SW_TEST_CASE( DebugHUDTest, StateTextKeepsOnlyOverrides )
{
    static const sw::DebugHUDSectionRegistration kOnByDefault{ "alpha", "Alpha", &DebugHUDTestUtil::writeTestSection, 0, sw::DebugHUDSectionFlag::kDefaultOn };
    static const sw::DebugHUDSectionRegistration kOffByDefault{ "beta", "Beta", &DebugHUDTestUtil::writeTestSection, 0, sw::DebugHUDSectionFlag::kNone };

    SW_EXPECT_TRUE( sw::DebugHUDSectionState::isSectionShown( "", kOnByDefault ) );
    SW_EXPECT_FALSE( sw::DebugHUDSectionState::isSectionShown( "", kOffByDefault ) );
    SW_EXPECT_FALSE( sw::DebugHUDSectionState::isSectionShown( "-ALPHA", kOnByDefault ) );
    SW_EXPECT_TRUE( sw::DebugHUDSectionState::isSectionShown( "gone beta", kOffByDefault ) );
    SW_EXPECT_TRUE( sw::DebugHUDSectionState::isSectionShown( "-beta, beta", kOffByDefault ) ); // 뒤 낱말이 이긴다

    sw::string text = sw::DebugHUDSectionState::makeStateText( "gone", "alpha", false, true );
    SW_EXPECT_STREQ( "gone -alpha", text );
    text = sw::DebugHUDSectionState::makeStateText( text, "beta", true, false );
    SW_EXPECT_STREQ( "gone -alpha beta", text );
    text = sw::DebugHUDSectionState::makeStateText( text, "alpha", true, true );
    SW_EXPECT_STREQ( "gone beta", text );
    text = sw::DebugHUDSectionState::makeStateText( text, "beta", false, false );
    SW_EXPECT_STREQ( "gone", text ); // 내린 모듈(gone)의 낱말은 그대로 남는다
}

/** @brief [DebugHUDTest] 명령 · 창이 쓰는 값이 사용자 설정을 거쳐 파일에 남고, 새 매니저가 읽으면 같은 HUD 상태로 돌아온다 */
SW_TEST_CASE( DebugHUDTest, SettingsRoundTripThroughUserFile )
{
    const sw::string path = test::makeTempPath( "debughud_usersettings.json" );
    {
        DebugHUDFixture fixture;
        fixture._settings.setUserFilePath( path );
        fixture._hud.setShown( true );
        SW_EXPECT_TRUE( fixture._hud.setSectionShown( "fps", false ) );
        SW_EXPECT_TRUE( fixture._hud.setSectionShown( "physics", true ) );
        SW_EXPECT_FALSE( fixture._hud.setSectionShown( "nosuchsection", true ) );
        fixture._hud.setCorner( sw::DebugHUDCorner::BottomRight );
        fixture._hud.setOpacity( 0.5f );
        SW_EXPECT_TRUE( fixture._hud.isShown() );
        SW_EXPECT_STREQ( "-fps physics", fixture._settings.getAppliedValue( "debug.hudSections" ) );
        SW_EXPECT_STREQ( "bottomRight", fixture._settings.getAppliedValue( "debug.hudCorner" ) );
        SW_EXPECT_TRUE( sw::DebugHUD::getCorner() == sw::DebugHUDCorner::BottomRight );
        SW_EXPECT_NEAR_EQUAL( 0.5f, sw::DebugHUD::getOpacity(), 0.0001f );
    }
    // 앞 픽스처가 전역 변수를 되돌렸다 — 파일만 남은 상태에서 다시 읽는다.
    DebugHUDFixture fixture;
    SW_EXPECT_FALSE( fixture._hud.isShown() );
    SW_ASSERT_TRUE( fixture._settings.loadUserFile( path ) );
    fixture._settings.reapplyAll();
    SW_EXPECT_TRUE( fixture._hud.isShown() );
    SW_EXPECT_STREQ( "-fps physics", DebugHUDTestUtil::readVariable( "gv_debugHUDSections" ) );
    SW_EXPECT_TRUE( sw::DebugHUD::getCorner() == sw::DebugHUDCorner::BottomRight );
    SW_EXPECT_NEAR_EQUAL( 0.5f, sw::DebugHUD::getOpacity(), 0.0001f );
    const sw::DebugHUDSectionRegistration* pFps = sw::DebugHUDRegistry::get().findSection( "fps" );
    SW_ASSERT_NOT_NULL( pFps );
    SW_EXPECT_FALSE( sw::DebugHUD::isSectionShown( *pFps ) );
}

/** @brief [DebugHUDTest] HUD 화면은 켜진 섹션만 그리고, 모서리 설정대로 놓이며, 게임 입력을 막지 않는다 — 꺼지면 화면이 닫힌다 */
SW_TEST_CASE( DebugHUDTest, ScreenDrawsShownSectionsAtTheCorner )
{
    DebugHUDFixture fixture;
    fixture.runFrames( 2 );
    SW_EXPECT_FALSE( fixture._hud.isScreenOpen() );
    const uint32 screensWhenOff = fixture._ui.getScreenCount();

    fixture._hud.setShown( true );
    fixture.runFrames( DebugHUDTestUtil::kRefreshFrameCount );
    SW_ASSERT_TRUE( fixture._hud.isScreenOpen() );
    SW_EXPECT_EQUAL( screensWhenOff + 1, fixture._ui.getScreenCount() );
    SW_EXPECT_EQUAL( sw::DebugHUD::getShownSectionCount(), fixture._hud.getDrawnSectionCount() );
    SW_EXPECT_TRUE( fixture._hud.isSectionDrawn( "fps" ) );
    SW_EXPECT_FALSE( fixture._ui.isGameInputBlocked() );

    sw::DebugHUDCorner corner = sw::DebugHUDCorner::TopRight;
    SW_ASSERT_TRUE( fixture._hud.tryGetArrangedCorner( corner ) );
    SW_EXPECT_TRUE( corner == sw::DebugHUDCorner::TopLeft );
    fixture._hud.setCorner( sw::DebugHUDCorner::BottomRight );
    fixture.runFrames( 2 );
    SW_ASSERT_TRUE( fixture._hud.tryGetArrangedCorner( corner ) );
    SW_EXPECT_TRUE( corner == sw::DebugHUDCorner::BottomRight );

    SW_EXPECT_TRUE( fixture._hud.setSectionShown( "fps", false ) );
    fixture.runFrames( 2 );
    SW_EXPECT_FALSE( fixture._hud.isSectionDrawn( "fps" ) );
    SW_EXPECT_EQUAL( sw::DebugHUD::getShownSectionCount(), fixture._hud.getDrawnSectionCount() );

    fixture._hud.setShown( false );
    fixture.runFrames( 2 );
    SW_EXPECT_FALSE( fixture._hud.isScreenOpen() );
    SW_EXPECT_EQUAL( screensWhenOff, fixture._ui.getScreenCount() );
}

/** @brief [DebugHUDTest] 그리는 중인 섹션의 모듈이 내려가면 다음 갱신에서 빠진다 — 사라진 본문을 부르지 않는다 */
SW_TEST_CASE( DebugHUDTest, UnloadedSectionLeavesTheScreen )
{
    DebugHUDFixture fixture;
    fixture._hud.setShown( true );
    {
        static const sw::DebugHUDSectionRegistration kRegistration{ "testunload", "Unload", &DebugHUDTestUtil::writeTestSection, 0, sw::DebugHUDSectionFlag::kDefaultOn };
        const sw::DebugHUDSectionRegistrar           registrar{ &kRegistration };
        fixture.runFrames( DebugHUDTestUtil::kRefreshFrameCount );
        SW_EXPECT_TRUE( fixture._hud.isSectionDrawn( "testunload" ) );
    }
    fixture.runFrames( 2 );
    SW_EXPECT_FALSE( fixture._hud.isSectionDrawn( "testunload" ) );
    SW_EXPECT_EQUAL( sw::DebugHUD::getShownSectionCount(), fixture._hud.getDrawnSectionCount() );
}

/** @brief [DebugHUDTest] 설정 창(문서)은 모달 — 열린 동안만 게임 입력을 막고, 섹션마다 체크 상자를 짓고, 닫으면 막지 않는다 */
SW_TEST_CASE( DebugHUDTest, WindowBlocksGameInputOnlyWhileOpen )
{
    DebugHUDFixture fixture;
    SW_ASSERT_TRUE( fixture._hud.openWindow() );
    fixture.runFrames( 2 );
    SW_EXPECT_TRUE( fixture._hud.isWindowOpen() );
    SW_EXPECT_TRUE( fixture._ui.isGameInputBlocked() );
    const sw::UIScreen* pWindow = fixture._ui.getActiveScreen();
    SW_ASSERT_NOT_NULL( pWindow );
    const sw::PanelWidget* pSections = pWindow->getTree().findWidget<sw::PanelWidget>( sw::hashed_string( "Sections" ) );
    SW_ASSERT_NOT_NULL( pSections );
    SW_EXPECT_EQUAL( sw::DebugHUDRegistry::get().getCount(), pSections->getChildCount() );

    fixture._hud.closeWindow();
    fixture.runFrames( 2 );
    SW_EXPECT_FALSE( fixture._hud.isWindowOpen() );
    SW_EXPECT_FALSE( fixture._ui.isGameInputBlocked() );
}
