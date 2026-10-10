#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/UI/Automation/UIAutomationSteps.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/UINavigationSolver.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Screen/OptionsMenuScreen.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "TestFramework/TestFramework.h"

// UINavigationScriptTest — 탐색 열(자동화 시나리오 형식 — `<At frame><Tap slot/></At>` + `<ExpectUi focus screen screens/>`)을 가상 입력으로 재생해
// 포커스 결과를 단언하고(닫으면 돌아오는가 · 모달 밖으로 새지 않는가), 모든 엔진 문서에서 패드 네 방향만으로 포커스 가능한 위젯 전부에 닿는지 본다.
// ExpectUi 판정은 실기동 시나리오 단계와 같은 함수(UIAutomationSteps::isExpectUIMet)다. 디바이스 없음(nogpu).

namespace
{
    struct UINavigationScriptTestUtil
    {
        static constexpr float32     kFrameSeconds = 1.0f / 60.0f;
        static constexpr const utf8* kUIInputMap   = "engine/input/ui.input.xml";
        static constexpr const utf8* kSchemaXML    = R"(
<UserSettingsSchema version="1">
    <Category id="audio" text="t.audio"/>
    <Category id="video" text="t.video"/>
    <Category id="controls" text="t.controls"/>
    <Setting id="audio.master" category="audio" type="float" default="0.5" min="0" max="1" step="0.25" text="t.master"/>
    <Setting id="audio.music" category="audio" type="float" default="1" min="0" max="1" step="0.25" text="t.music"/>
    <Setting id="audio.voices" category="audio" type="int" default="8" min="1" max="16" step="1" text="t.voices"/>
    <Setting id="video.mode" category="video" type="enum" default="windowed" text="t.mode">
        <Option value="windowed"/>
        <Option value="fullscreen"/>
    </Setting>
    <Setting id="video.vsync" category="video" type="bool" default="true" text="t.vsync"/>
    <Setting id="controls.jump" category="controls" type="keyBinding" action="Jump" default="" text="t.jump"/>
</UserSettingsSchema>)";
    };

    /** @brief 설정(시험 스키마) · 입력 · UI 행동 맵을 든 UI 시스템 — 1920×1080 뷰포트로 프레임을 돌립니다. */
    struct UINavigationFixture
    {
        sw::InputMap            _bindingMap;
        sw::UserSettingsManager _settings;
        sw::InputManager        _input;
        sw::UISystem            _ui;
        sw::UIViewport          _viewport;
        uint32                  _frame;

        UINavigationFixture()
            : _bindingMap{}
            , _settings{}
            , _input{}
            , _ui{}
            , _viewport{}
            , _frame{ 0 }
        {
            _bindingMap.registerLayer( "Gameplay", 0, true, false, false );
            _bindingMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Pressed, "Gameplay" );
            sw::UserSettingsTargets targets{};
            targets._pInputMap = &_bindingMap;
            _settings.initialize( targets );
            SW_EXPECT_TRUE( _settings.loadSchemaFromXMLText( UINavigationScriptTestUtil::kSchemaXML, "test.settings.xml" ) );
            _settings.reapplyAll();
            _viewport._size         = sw::float2{ 1920.0f, 1080.0f };
            _viewport._physicalSize = _viewport._size;
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr, UINavigationScriptTestUtil::kUIInputMap ) );
            _ui.setUserSettings( &_settings );
        }

        ~UINavigationFixture()
        {
            _ui.shutdown();
            _input.shutdown();
            _settings.shutdown();
        }

        UINavigationFixture( const UINavigationFixture& )            = delete;
        UINavigationFixture& operator=( const UINavigationFixture& ) = delete;

        void runFrame()
        {
            _input.beginFrame( UINavigationScriptTestUtil::kFrameSeconds );
            _ui.processInput( UINavigationScriptTestUtil::kFrameSeconds );
            _ui.update( UINavigationScriptTestUtil::kFrameSeconds, _viewport );
            _input.endFrame();
        }

        /**
         * @brief 시나리오 형식의 탐색 열 @p xmlText 를 재생합니다 — 입력 단계(`Tap` · `Press` · `Release`)는 가상 입력으로 그 프레임의 `beginFrame` 에,
         *        `ExpectUi` 는 그 프레임의 UI update 뒤에(실행기와 같은 자리). `Pass` 에서 끝납니다. 다른 단계 · 형식 오류는 실패입니다.
         */
        void runScript( sw::string_view xmlText )
        {
            sw::AutomationScenario scenario;
            sw::string             error;
            SW_ASSERT_TRUE_MSG( scenario.parse( xmlText, "navigation script", error ), error.c_str() );
            sw::VirtualInputScript inputScript;
            uint32                 lastFrame = 0;
            bool                   bPass     = false;
            for ( const sw::AutomationStep& step : scenario.getSteps() )
            {
                lastFrame = step._frameIndex > lastFrame ? step._frameIndex : lastFrame;
                if ( step._kind == "Tap" || step._kind == "Press" || step._kind == "Release" )
                {
                    sw::InputSlot slot{};
                    SW_ASSERT_TRUE_MSG( sw::InputSlotUtil::tryParse( *step.findAttribute( "slot" ), slot ), step.describe().c_str() );
                    const bool bAdded = step._kind == "Tap" ? inputScript.addTap( step._frameIndex, slot, 1 )
                                                            : inputScript.addSlot( step._frameIndex, slot, step._kind == "Press" );
                    SW_ASSERT_TRUE_MSG( bAdded, step.describe().c_str() );
                    continue;
                }
                if ( step._kind == sw::UIAutomationSteps::kExpectUIKind )
                {
                    SW_ASSERT_TRUE_MSG( sw::UIAutomationSteps::validateExpectUI( step, error ), error.c_str() );
                    continue;
                }
                SW_ASSERT_TRUE_MSG( step._kind == "Pass", ( step.describe() + ": navigation scripts use Tap · Press · Release · ExpectUi · Pass" ).c_str() );
                bPass = true;
            }
            SW_ASSERT_TRUE_MSG( bPass, "navigation script has no <Pass/>" );
            _input.attachVirtualInput( &inputScript );
            for ( uint32 frame = 0; frame <= lastFrame; ++frame )
            {
                runFrame();
                for ( const sw::AutomationStep& step : scenario.getSteps() )
                {
                    if ( step._frameIndex != frame || step._kind != sw::UIAutomationSteps::kExpectUIKind )
                        continue;
                    sw::string failure;
                    SW_EXPECT_TRUE_MSG( sw::UIAutomationSteps::isExpectUIMet( _ui, step, failure ), ( step.describe() + ": " + failure ).c_str() );
                }
            }
            _input.detachVirtualInput();
        }
    };

    struct UIReachabilityUtil
    {
        static void collectFocusable( const sw::Widget& widget, sw::vector<const sw::Widget*>& outList )
        {
            if ( sw::UINavigationSolver::canReceiveFocus( widget ) )
                outList.push_back( &widget );
            const sw::PanelWidget* pPanel = sw::castTo<const sw::PanelWidget>( &widget );
            if ( pPanel == nullptr )
                return;
            for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
            {
                collectFocusable( *pPanel->getChild( index ), outList );
            }
        }

        static sw::string describe( const sw::Widget& widget ) { return widget.getName().empty() ? sw::string( "(unnamed)" ) : sw::string( widget.getName().c_str() ); }

        /**
         * @brief 화면 @p screen 의 시작 위젯(지금 포커스, 없으면 문서 순서 첫 위젯)에서 네 방향 BFS 로 닿지 않는 포커스 가능한 위젯을 실패로 적습니다.
         * @return 포커스 가능한 위젯 수(0 이면 탐색할 것이 없는 화면 — HUD · 자막 · 로딩)
         */
        static uint32 expectAllReachable( const sw::UISystem& ui, const sw::UIScreen& screen, const sw::string& label )
        {
            const sw::WidgetTree&         tree = screen.getTree();
            sw::vector<const sw::Widget*> listFocusable;
            if ( tree.getRoot() != nullptr )
                collectFocusable( *tree.getRoot(), listFocusable );
            if ( listFocusable.empty() )
                return 0;
            sw::WidgetId start = ui.getFocusManager().getFocusedTree() == &tree ? ui.getFocusManager().getFocusedWidget() : sw::kInvalidWidgetId;
            if ( start == sw::kInvalidWidgetId )
                start = sw::UINavigationSolver::findFirstFocusable( *tree.getRoot() );
            sw::vector<sw::WidgetId>        listQueue{ start };
            sw::unordered_set<sw::WidgetId> uniqueVisited{ start };
            for ( size_t index = 0; index < listQueue.size(); ++index )
            {
                for ( const sw::UINavigationDirection direction : { sw::UINavigationDirection::Up, sw::UINavigationDirection::Down,
                                                                    sw::UINavigationDirection::Left, sw::UINavigationDirection::Right } )
                {
                    const sw::WidgetId next = sw::UINavigationSolver::findNextWidget( tree, listQueue[index], direction );
                    if ( next != sw::kInvalidWidgetId && uniqueVisited.insert( next ).second )
                        listQueue.push_back( next );
                }
            }
            for ( const sw::Widget* pWidget : listFocusable )
            {
                SW_EXPECT_TRUE_MSG( uniqueVisited.count( pWidget->getId() ) != 0,
                                    ( label + ": '" + describe( *pWidget ) + "' cannot be reached with the gamepad from '" +
                                      describe( *tree.findWidgetById( start ) ) + "'" )
                                        .c_str() );
            }
            return static_cast<uint32>( listFocusable.size() );
        }
    };
} // namespace

/**
 * @brief [UINavigationScriptTest] 일시정지 → 옵션 → 뒤로: 닫힌 창 아래 화면의 포커스가 연 단추로 돌아오고, 일시정지를 닫으면 화면도 포커스도 없다
 * @details 키보드(Esc · 화살표 · Enter)와 패드(DPad · A · B)를 섞는다 — UI 맵의 두 바인딩이 같은 행동이다.
 */
SW_TEST_CASE( UINavigationScriptTest, PauseOptionsBackRestoresFocus )
{
    UINavigationFixture fixture;
    fixture._ui.setPauseMenuDocument( "engine/ui/pause.ui.xml" );
    fixture.runFrame();
    fixture.runScript( R"(<Scenario name="ui.pause.back" startAfter="Immediately">
        <At frame="1"><Tap slot="Key.Escape"/></At>
        <At frame="3"><ExpectUi screen="engine/ui/pause.ui.xml" focus="Resume" screens="1"/></At>
        <At frame="4"><Tap slot="Gamepad.DPadDown"/></At>
        <At frame="6"><ExpectUi focus="Options"/></At>
        <At frame="7"><Tap slot="Gamepad.A"/></At>
        <At frame="9"><ExpectUi screen="engine/ui/options.ui.xml" focus="audio.master.Value" screens="2"/></At>
        <At frame="10"><Tap slot="Key.Down"/></At>
        <At frame="12"><ExpectUi focus="audio.music.Value"/></At>
        <At frame="13"><Tap slot="Gamepad.B"/></At>
        <At frame="15"><ExpectUi screen="engine/ui/pause.ui.xml" focus="Options" screens="1"/></At>
        <At frame="16"><Tap slot="Key.Escape"/></At>
        <At frame="40"><ExpectUi screens="0" focus="none" screen="none"/><Pass/></At>
    </Scenario>)" );
}

/**
 * @brief [UINavigationScriptTest] 모달(적용하지 않은 변경 창)은 포커스를 가두고 — 네 방향 어느 쪽도 아래 옵션 메뉴로 새지 않는다 — 취소하면 메뉴의 그 행으로 돌아온다
 * @details 슬라이더를 오른쪽으로 한 칸 옮겨 보류 값을 만들고 뒤로(B)를 누르면 창이 뜬다.
 */
SW_TEST_CASE( UINavigationScriptTest, ModalKeepsFocusInsideAndReturnsOnCancel )
{
    UINavigationFixture fixture;
    fixture._ui.setInputMode( sw::UIInputMode::Navigation ); // 패드 사용자 — 메뉴가 첫 행에 포커스를 둔다
    (void)sw::OptionsMenuScreen::open( fixture._ui );        // 핸들은 쓰지 않는다 — 열린 화면은 아래 ExpectUi 가 본다
    fixture.runFrame();
    fixture.runScript( R"(<Scenario name="ui.modal" startAfter="Immediately">
        <At frame="1"><ExpectUi screen="engine/ui/options.ui.xml" focus="audio.master.Value"/></At>
        <At frame="2"><Tap slot="Gamepad.DPadRight"/></At>
        <At frame="4"><Tap slot="Gamepad.B"/></At>
        <At frame="6"><ExpectUi screen="engine/ui/confirm_unsaved.ui.xml" focus="Apply" screens="2"/></At>
        <At frame="7"><Tap slot="Gamepad.DPadUp"/></At>
        <At frame="9"><Tap slot="Gamepad.DPadLeft"/></At>
        <At frame="11"><Tap slot="Gamepad.DPadDown"/></At>
        <At frame="13"><ExpectUi screen="engine/ui/confirm_unsaved.ui.xml" focus="Apply"/></At>
        <At frame="14"><Tap slot="Gamepad.DPadRight"/></At>
        <At frame="16"><Tap slot="Gamepad.DPadRight"/></At>
        <At frame="18"><Tap slot="Gamepad.DPadRight"/></At>
        <At frame="20"><ExpectUi screen="engine/ui/confirm_unsaved.ui.xml" focus="Cancel"/></At>
        <At frame="21"><Tap slot="Gamepad.A"/></At>
        <At frame="23"><ExpectUi screen="engine/ui/options.ui.xml" focus="audio.master.Value" screens="1"/><Pass/></At>
    </Scenario>)" );
}

/**
 * @brief [UINavigationScriptTest] 모든 엔진 문서(engine/ui 폴더의 .ui.xml — 조각 폴더 빼고)에서 시작 위젯부터 패드 네 방향만으로 포커스 가능한 위젯 전부에 닿는다
 * @details 새 문서를 더하면 자동으로 검사된다 — 패드로 못 가는 단추의 게이트. 옵션 문서는 코드가 행을 지으므로 `OptionsMenuScreen` 으로 열어 탭마다 본다.
 *          포커스 가능한 위젯이 없는 문서(HUD · 자막 · 로딩 · 알림)는 건너뛴다. 탐색할 문서가 하나 이상인지도 본다(폴더를 못 찾으면 실패).
 */
SW_TEST_CASE( UINavigationScriptTest, EveryEngineDocumentIsReachableByGamepad )
{
    const sw::string directory = sw::ResourceUtil::getDomainFolderPath( "engine", "ui" );
    SW_ASSERT_TRUE_MSG( directory.empty() == false, "engine/ui folder not found" );
    sw::vector<sw::string> listPath;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( directory, ".xml", listPath, false ) );
    uint32 checkedCount = 0;
    for ( const sw::string& path : listPath )
    {
        const sw::string fileName = sw::FileUtil::getFileNamePart( path );
        if ( fileName.size() < 7 || fileName.compare( fileName.size() - 7, 7, ".ui.xml" ) != 0 )
            continue; // uiscale.xml · uithemes.xml
        const sw::string    documentPath = "engine/ui/" + fileName;
        UINavigationFixture fixture;
        if ( documentPath == "engine/ui/options.ui.xml" )
        {
            const sw::UIScreenHandle handle = sw::OptionsMenuScreen::open( fixture._ui );
            fixture.runFrame();
            sw::OptionsMenuScreen* pMenu = static_cast<sw::OptionsMenuScreen*>( fixture._ui.findScreen( handle ) );
            SW_ASSERT_NOT_NULL( pMenu );
            for ( uint32 tab = 0; tab < pMenu->getTabCount(); ++tab )
            {
                pMenu->selectTab( tab );
                fixture.runFrame();
                if ( UIReachabilityUtil::expectAllReachable( fixture._ui, *pMenu, documentPath + " tab " + sw::to_string( tab ) ) > 0 )
                    ++checkedCount;
            }
            continue;
        }
        const sw::UIScreenHandle handle = fixture._ui.openScreen( documentPath );
        fixture.runFrame();
        const sw::UIScreen* pScreen = fixture._ui.findScreen( handle );
        SW_EXPECT_TRUE_MSG( pScreen != nullptr, documentPath.c_str() );
        if ( pScreen != nullptr && UIReachabilityUtil::expectAllReachable( fixture._ui, *pScreen, documentPath ) > 0 )
            ++checkedCount;
    }
    SW_EXPECT_TRUE( checkedCount >= 4 ); // 일시정지 · 옵션(탭마다) · 확인 둘 · 키 바인딩
}

/** @brief [UINavigationScriptTest] 엔진 시나리오의 UI 단계 형식 — 모르는 속성 · 빈 단계 · 수가 아닌 screens 는 시작 전에 거절한다 */
SW_TEST_CASE( UINavigationScriptTest, ExpectUIRejectsMalformedSteps )
{
    sw::AutomationScenario scenario;
    sw::string             error;
    SW_ASSERT_TRUE_MSG( scenario.parse( R"(<Scenario name="ui.bad" startAfter="Immediately">
        <At frame="0"><ExpectUi/><ExpectUi focused="Resume"/><ExpectUi screens="two"/><ExpectUi focus="Resume" screens="1"/><Pass/></At>
    </Scenario>)",
                                        "bad", error ),
                        error.c_str() );
    const sw::vector<sw::AutomationStep>& listStep = scenario.getSteps();
    SW_ASSERT_EQUAL( 5u, static_cast<uint32>( listStep.size() ) );
    SW_EXPECT_FALSE( sw::UIAutomationSteps::validateExpectUI( listStep[0], error ) );
    SW_EXPECT_FALSE( sw::UIAutomationSteps::validateExpectUI( listStep[1], error ) );
    SW_EXPECT_TRUE( error.find( "focused" ) != sw::string::npos );
    SW_EXPECT_FALSE( sw::UIAutomationSteps::validateExpectUI( listStep[2], error ) );
    SW_EXPECT_TRUE( sw::UIAutomationSteps::validateExpectUI( listStep[3], error ) );
}
