/**
 * @file UIScenarioSteps.cpp
 * @brief 자동화 시나리오의 UI 단계와 감시 값을 정의합니다. `ExpectUI`(포커스 위젯, 활성 화면 문서, 화면 수)를 단언하고 `UILayoutDump` 로 위젯 사각형을 파일에 씁니다.
 *        감시 값: 활성 화면 · 포커스 위젯 · 입력 칸의 글과 조합 · 액션 글리프 · 입력 장치 글리프 종류 · 글 넘침 수 · 게임 시간 정지.
 * @details 단계는 시나리오 실행기(`AutomationRunner`)가 레지스트리에서 찾는다(언리얼 Automation Driver 가 위젯을 찾아 단언하는 것에 해당한다).
 *          단계는 등록(`AutomationStepRegistry`)으로만 닿고 이름으로 부르는 곳이 없어 헤더를 두지 않는다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/InputGlyphStyle.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/UIFocusManager.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/TextInputWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    struct AutomationStep;

    class UISystem;

    /**
     * @struct UIScenarioSteps
     * @brief `<ExpectUI focus="이름|none" screen="문서 경로|none" screens="수"/>` 입니다. 적은 속성만 확인하며 하나 이상 적어야 합니다.
     * @details `focus` 는 포커스 위젯의 이름(조각 안이면 `조각.이름`)이고 `none` 은 포커스 없음이다. `screen` 은 활성 화면(입력과 포커스를 받는 맨 위 화면)의 문서 경로
     *          (`normalizePath` 를 쓰며 대소문자 무시)이고, 코드 화면이거나 없으면 `none` 이다. `screens` 는 메뉴 스택 깊이(`UISystem::getMenuStackDepth` — 포커스를 받는 열린 화면, HUD · Overlay · 닫는 중 제외)다.
     */
    struct UIScenarioSteps
    {
        static constexpr utf8 kExpectUIKind[]   = "ExpectUI";
        static constexpr utf8 kLayoutDumpKind[] = "UILayoutDump"; ///< `<UILayoutDump file="x.txt"/>` 입니다. `UISystem::makeLayoutDump` 를 산출물 폴더에 씁니다.

        /** @brief 속성을 검사합니다. 모르는 속성, 빈 단계, 수가 아닌 `screens` 면 그 이유입니다. */
        [[nodiscard]] static expected<void> validateExpectUI( const AutomationStep& step );
        /** @brief @p ui 가 단계의 기대와 같으면 true 입니다. 다르면 false 를 반환하고 @p outFailure 에 기대와 실제를 둡니다. */
        [[nodiscard]] static bool isExpectUIMet( const UISystem& ui, const AutomationStep& step, string& outFailure );
    };
} // namespace sw

namespace sw
{
    namespace
    {
        struct UIScenarioStepsInternal
        {
            static constexpr utf8 kNone[] = "none";

            static string describeFocus( const UISystem& ui )
            {
                const UIFocusManager& focus   = ui.getInputRouter().getFocusManager();
                const WidgetTree*     pTree   = focus.getFocusedTree();
                const Widget*         pWidget = pTree != nullptr ? pTree->findWidgetByID( focus.getFocusedWidget() ) : nullptr;
                if ( pWidget == nullptr )
                    return kNone;
                return pWidget->getName().empty() ? string( "(unnamed)" ) : string( pWidget->getName().c_str() );
            }

            static string describeActiveScreen( const UISystem& ui )
            {
                const UIScreen* pScreen = ui.getActiveScreen();
                if ( pScreen == nullptr || pScreen->getDocumentPath().empty() )
                    return kNone;
                return pScreen->getDocumentPath();
            }

            /** @brief 감시 값 `UI.ActiveScreen` 입니다. 활성 화면의 문서 경로이고, 코드 화면이거나 없으면 `none` 입니다(`ExpectUI screen` 과 같은 규칙). */
            [[nodiscard]] static bool readActiveScreen( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const UISystem* pUISystem = engine::findSubsystem<UISystem>();
                if ( pUISystem == nullptr || pUISystem->isInitialized() == false )
                {
                    outValue.setText( "no UI system" );
                    return false;
                }
                outValue.setText( describeActiveScreen( *pUISystem ) );
                return true;
            }

            /** @brief 초기화된 UI 시스템입니다. 없으면 @p outValue 에 까닭을 적고 nullptr 입니다. */
            static const UISystem* findReadyUI( AutomationWatchValue& outValue )
            {
                const UISystem* pUISystem = engine::findSubsystem<UISystem>();
                if ( pUISystem == nullptr || pUISystem->isInitialized() == false )
                {
                    outValue.setText( "no UI system" );
                    return nullptr;
                }
                return pUISystem;
            }

            /** @brief 포커스 위젯입니다(없으면 nullptr). */
            static const Widget* findFocusedWidget( const UISystem& ui )
            {
                const UIFocusManager& focus = ui.getInputRouter().getFocusManager();
                const WidgetTree*     pTree = focus.getFocusedTree();
                return pTree != nullptr ? pTree->findWidgetByID( focus.getFocusedWidget() ) : nullptr;
            }

            /** @brief 감시 값 `UI.FocusedWidget` 입니다. 포커스 위젯 이름이고 없으면 `none` 입니다(`ExpectUI focus` 와 같은 규칙 — `WaitUntil` 로 포커스 이동을 기다린다). */
            [[nodiscard]] static bool readFocusedWidget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const UISystem* pUISystem = findReadyUI( outValue );
                if ( pUISystem == nullptr )
                    return false;
                outValue.setText( describeFocus( *pUISystem ) );
                return true;
            }

            /** @brief 포커스 위젯이 입력 칸이면 그것입니다. 아니면 @p outValue 에 까닭을 적고 nullptr 입니다. */
            static const TextInputWidget* findFocusedTextInput( AutomationWatchValue& outValue )
            {
                const UISystem* pUISystem = findReadyUI( outValue );
                if ( pUISystem == nullptr )
                    return nullptr;
                const TextInputWidget* pInput = castTo<TextInputWidget>( findFocusedWidget( *pUISystem ) );
                if ( pInput == nullptr )
                    outValue.setText( "the focused widget is not a text input" );
                return pInput;
            }

            /** @brief 감시 값 `UI.FocusedText` 입니다. 포커스를 가진 입력 칸의 확정 글입니다. */
            [[nodiscard]] static bool readFocusedText( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const TextInputWidget* pInput = findFocusedTextInput( outValue );
                if ( pInput == nullptr )
                    return false;
                outValue.setText( pInput->getText() );
                return true;
            }

            /** @brief 감시 값 `UI.FocusedComposition` 입니다. 포커스를 가진 입력 칸의 IME 조합 글(확정 전)이고, 조합이 없으면 빈 글입니다. */
            [[nodiscard]] static bool readFocusedComposition( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const TextInputWidget* pInput = findFocusedTextInput( outValue );
                if ( pInput == nullptr )
                    return false;
                outValue.setText( pInput->getComposition() );
                return true;
            }

            /** @brief 감시 값 `UI.ActionGlyph`(key = 액션 이름)입니다. 지금 입력 장치로 그 액션을 그리는 글리프 글입니다(리치 텍스트 `[action=…]` 이 보이는 것과 같다). */
            [[nodiscard]] static bool readActionGlyph( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const UISystem* pUISystem = findReadyUI( outValue );
                if ( pUISystem == nullptr )
                    return false;
                outValue.setText( pUISystem->getActionGlyphs().findGlyph( query._key ) );
                return true;
            }

            /** @brief 감시 값 `Input.GlyphStyle` 입니다. 버튼 표시의 장치 종류(0 키보드 · 마우스, 1 Xbox, 2 PlayStation, 3 Switch — `InputGlyphStyle`)이며 마지막으로 쓴 장치를 따릅니다. */
            [[nodiscard]] static bool readGlyphStyle( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const UISystem* pUISystem = findReadyUI( outValue );
                if ( pUISystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pUISystem->getActionGlyphs().getStyle() ) );
                return true;
            }

            /** @brief 글이 칸을 넘었다고 보는 여유(UI 단위)입니다 — 반올림 차이를 넘침으로 세지 않는다. */
            static constexpr float32 kOverflowSlack = 1.0f;

            /** @brief @p widget 아래(자기 포함)에서 보이는 글 위젯 가운데 원하는 크기가 배치 크기보다 큰 것을 셉니다. 접히거나 숨긴 가지는 건너뜁니다. */
            static uint32 countTextOverflow( const Widget& widget )
            {
                if ( widget.isVisible() == false )
                    return 0;
                uint32            count = 0;
                const TextWidget* pText = castTo<TextWidget>( &widget );
                if ( pText != nullptr )
                {
                    const float2& desired = pText->getDesiredSize();
                    const float2& size    = pText->getGeometry()._size;
                    count += ( desired._x > size._x + kOverflowSlack || desired._y > size._y + kOverflowSlack ) ? 1u : 0u;
                }
                for ( const unique_ptr<Widget>& child : widget.getChildren() )
                {
                    count += countTextOverflow( *child );
                }
                return count;
            }

            /**
             * @brief 감시 값 `UI.TextOverflowCount` 입니다. 게임 화면(개발 겹치기 · 닫는 중 · 레이어로 숨긴 화면 제외)에서 글이 제 칸을 넘은 글 위젯 수입니다.
             * @details 안전 영역 · 글자 배율 · 의사 현지화(`qps-ploc` — 글이 30~40 % 길어진다)를 걸고 0 을 단언한다. 언리얼 위젯 리플렉터의 잘림 표시 · 유니티 UI Toolkit 의 넘침 검사 자리다.
             */
            [[nodiscard]] static bool readTextOverflowCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const UISystem* pUISystem = findReadyUI( outValue );
                if ( pUISystem == nullptr )
                    return false;
                uint32 count = 0;
                for ( const unique_ptr<UIScreen>& screen : pUISystem->getScreens() )
                {
                    const bool bSkipped = screen->isDevOverlay() || screen->isClosing() || screen->isHiddenByLayer() || screen->getTree().getRoot() == nullptr;
                    if ( bSkipped == false )
                        count += countTextOverflow( *screen->getTree().getRoot() );
                }
                outValue.setNumber( static_cast<float64>( count ) );
                return true;
            }

            /** @brief 감시 값 `Time.Paused` 입니다. 게임 시간 정지 요청(일시정지 메뉴 · 로딩 화면 — `GameTimeScale::addPauseRequest`)이 하나라도 걸려 있으면 1 입니다. */
            [[nodiscard]] static bool readTimePaused( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                outValue.setNumber( engine::getSubsystem<GameTimeScale>().isPaused() ? 1.0 : 0.0 );
                return true;
            }

            static bool runExpectUI( AutomationStepContext& runner, const AutomationStep& step )
            {
                const UISystem& ui = engine::getSubsystem<UISystem>();
                string          failure;
                if ( UIScenarioSteps::isExpectUIMet( ui, step, failure ) == false )
                    runner.recordFailure( step, failure );
                return true;
            }

            /** @brief 상대 경로면 시나리오 산출물 폴더(`Saved/Automation/<이름>/`) 아래에 둡니다. 스크린샷과 같은 위치입니다. */
            static bool runLayoutDump( AutomationStepContext& runner, const AutomationStep& step )
            {
                const string file = step.readValidatedProperties<AutomationFileStepParams>()._file;
                const string path = FileUtil::isAbsolutePath( file ) ? file : FileUtil::joinPath( runner.getOutputDirectory(), file );
                if ( FileUtil::ensureParentDirectoryExists( path ) == false || FileUtil::writeTextFile( path, engine::getSubsystem<UISystem>().makeLayoutDump() ) == false )
                    runner.recordFailure( step, "could not write " + path );
                return true;
            }

            /**
             * @brief 로딩 화면이 떠 있으면 게임 입력이 막혀 있고 그림이 덮여 있습니다. `ScenePlaying` 시나리오는 로딩 화면이 사라질 때까지 시작하지 않고, 화면 캡처도 미뤄집니다.
             * @details 화면 UI 를 막은 동안(멈춘 에디터, `isOnScreenSuppressed`)은 로딩 화면이 그려지지도 입력을 막지도 않는다 — 그동안 닫을 게임 update 가 돌지 않아 남아 있을 뿐이다.
             */
            static AutomationReadiness readLoadingScreen()
            {
                const UISystem* pUISystem = engine::findSubsystem<UISystem>();
                if ( pUISystem == nullptr || pUISystem->isInitialized() == false )
                    return AutomationReadiness::notApplicable();
                const bool bCovering = pUISystem->isOnScreenSuppressed() == false && pUISystem->isLoadingScreenShown();
                return bCovering ? AutomationReadiness::waiting( 0, "the loading screen is shown" ) : AutomationReadiness::ready();
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( uiActiveScreen, "UI.ActiveScreen", "Document path of the active UI screen (the top screen taking input and focus); none for a code screen or no screen",
                               &UIScenarioStepsInternal::readActiveScreen, false, false );
    SW_AUTOMATION_QUERY_WATCH( uiFocusedWidget, "UI.FocusedWidget", "Name of the focused UI widget; none without focus", &UIScenarioStepsInternal::readFocusedWidget, false, false );
    SW_AUTOMATION_QUERY_WATCH( uiFocusedText, "UI.FocusedText", "Committed text of the focused text input", &UIScenarioStepsInternal::readFocusedText, false, false );
    SW_AUTOMATION_QUERY_WATCH( uiFocusedComposition, "UI.FocusedComposition", "IME composition (not yet committed) of the focused text input; empty without one",
                               &UIScenarioStepsInternal::readFocusedComposition, false, false );
    SW_AUTOMATION_QUERY_WATCH( uiActionGlyph, "UI.ActionGlyph", "Glyph text of the action named by key for the device in use (the [action=] rich text)", &UIScenarioStepsInternal::readActionGlyph,
                               false, true );
    SW_AUTOMATION_QUERY_WATCH( inputGlyphStyle, "Input.GlyphStyle", "Button glyph device kind: 0 keyboard and mouse, 1 Xbox, 2 PlayStation, 3 Switch (follows the last used device)",
                               &UIScenarioStepsInternal::readGlyphStyle, false, false );
    SW_AUTOMATION_QUERY_WATCH( uiTextOverflowCount, "UI.TextOverflowCount", "Text widgets on game screens whose desired size is larger than their arranged size",
                               &UIScenarioStepsInternal::readTextOverflowCount, false, false );
    SW_AUTOMATION_QUERY_WATCH( timePaused, "Time.Paused", "1 while a game time pause request is held (pause menu, loading screen)", &UIScenarioStepsInternal::readTimePaused, false, false );
    SW_AUTOMATION_START_BLOCKER( uiLoadingScreen, "UILoadingScreen", &UIScenarioStepsInternal::readLoadingScreen );
    SW_AUTOMATION_STEP( uiLayoutDump, UIScenarioSteps::kLayoutDumpKind, &UIScenarioStepsInternal::runLayoutDump, &AutomationStep::validateProperties<AutomationFileStepParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( uiExpect, UIScenarioSteps::kExpectUIKind, &UIScenarioStepsInternal::runExpectUI, &UIScenarioSteps::validateExpectUI, AutomationStepOptions{} );
} // namespace sw

namespace sw
{
    expected<void> UIScenarioSteps::validateExpectUI( const AutomationStep& step )
    {
        if ( step._listAttribute.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "needs focus, screen or screens" } );
        // 칸(모르는 이름 · 개수 · 범위)은 리플렉션 구조체가 정한다 — 화면은 경로 칸이라 참조 검사 · 옮기기가 따라간다.
        AutomationExpectUIParams params;
        return step.readProperties( params );
    }

    bool UIScenarioSteps::isExpectUIMet( const UISystem& ui, const AutomationStep& step, string& outFailure )
    {
        using Internal = UIScenarioStepsInternal;
        bool bPassed   = true;
        if ( const string* pFocus = step.findAttribute( "focus" ); pFocus != nullptr )
        {
            const string focused = Internal::describeFocus( ui );
            if ( focused != *pFocus )
            {
                outFailure += "focus is '" + focused + "', expected '" + *pFocus + "'. ";
                bPassed = false;
            }
        }
        if ( const string* pScreen = step.findAttribute( "screen" ); pScreen != nullptr )
        {
            const string active   = Internal::describeActiveScreen( ui );
            const bool   bNone    = *pScreen == Internal::kNone;
            const bool   bMatches = bNone ? active == Internal::kNone : FileUtil::normalizePath( *pScreen ).str() == active;
            if ( bMatches == false )
            {
                outFailure += "active screen is '" + active + "', expected '" + *pScreen + "'. ";
                bPassed = false;
            }
        }
        if ( const string* pCount = step.findAttribute( "screens" ); pCount != nullptr )
        {
            int32 expected = 0;
            if ( StringUtil::parseInt( string_view{ *pCount }, expected ) == false )
            {
                outFailure += "screens '" + *pCount + "' is not a number. "; // 검사 뒤 단계가 바뀌었다(핫 리로드)
                bPassed = false;
            }
            else if ( ui.getMenuStackDepth() != static_cast<uint32>( expected ) )
            {
                outFailure += "menu stack depth is " + to_string( ui.getMenuStackDepth() ) + ", expected " + *pCount + ". ";
                bPassed = false;
            }
        }
        return bPassed;
    }
} // namespace sw
