/**
 * @file WidgetWatches.cpp
 * @brief 화면 마커의 자동화 감시 값입니다(`UI.*`). 감시 값 레지스트리(`Engine/Automation/Watch`)가 UI 보다 아래라 UI 가 자기 감시 값을 등록합니다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/UI/Service/UISubtitleService.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/World/WidgetComponent.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 감시 값 `UI.MarkerVisible` 입니다. 대상 오브젝트의 위젯 컴포넌트(목표 마커 포함)가 화면 마커로 지금 보이면 1 입니다.
         * @details 보임은 마커 내용 위젯의 가시성이다(`applyPlacement` 가 숨길 때 접는다) — 카메라 밖 · 최대 거리 밖 · 코드가 숨김 · 목표가 닫힘이 모두 0 이다.
         *          Screen 공간만 뜻이 있다(World 위젯은 마커가 아니라 0 이다). 마커는 UI 갱신이 놓으므로 단언은 그 갱신이 지난 프레임에 둔다.
         */
        [[nodiscard]] bool readWidgetMarkerVisible( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const WidgetComponent* pWidget = query.findTargetComponent<WidgetComponent>( "widget component", outValue );
            if ( pWidget == nullptr )
                return false;
            const Widget* pContent = pWidget->getSpace() == WidgetSpace::Screen ? pWidget->getContent() : nullptr;
            outValue.setNumber( pContent != nullptr && pContent->getVisibility() != WidgetVisibility::Collapsed ? 1.0 : 0.0 );
            return true;
        }

        /**
         * @brief 감시 값 `UI.DeferredGlyphs` 입니다. 지난 UI 그리기가 글리프 래스터화 예산 때문에 미룬 글리프 수이며 0 이면 화면의 글자가 다 그려졌다.
         * @details 예산이 다음 프레임에 살아나지 않거나 미룬 위젯이 다시 칠해지지 않으면 이 값이 0 이 되지 않아 `WaitUntil` 이 시한으로 지고, 글자가 빈 화면이 통과하지 못한다.
         */
        [[nodiscard]] bool readDeferredGlyphs( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const UISystem* pUI = engine::findSubsystem<UISystem>();
            outValue            = pUI != nullptr ? static_cast<float64>( pUI->getPaintState().getDeferredGlyphCount() ) : 0.0;
            return pUI != nullptr && pUI->isInitialized();
        }

        /** @brief 지금 UI 뷰포트를 읽습니다(초기화 전이면 false). 아래 화면 대응 감시 값이 함께 쓴다. */
        [[nodiscard]] bool readViewport( UIViewport& outViewport )
        {
            const UISystem* pUI = engine::findSubsystem<UISystem>();
            if ( pUI == nullptr || pUI->isInitialized() == false )
                return false;
            outViewport = pUI->getViewport();
            return true;
        }

        /** @brief 감시 값 `UI.SafeInsetTop` · `UI.SafeInsetBottom` 입니다 — 안전 영역 위 · 아래(UI 단위). 노치 · 시스템 바가 SafeZonePanel 에 닿는지 본다. */
        [[nodiscard]] bool readSafeInsetTop( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            UIViewport viewport{};
            const bool bRead = readViewport( viewport );
            outValue         = viewport._safeInsets._y;
            return bRead;
        }

        [[nodiscard]] bool readSafeInsetBottom( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            UIViewport viewport{};
            const bool bRead = readViewport( viewport );
            outValue         = viewport._safeInsets._w;
            return bRead;
        }

        /** @brief 감시 값 `UI.ViewportWidth` · `UI.ViewportHeight` 입니다 — UI 단위 크기(회전 · 크기 변경 뒤 배치가 따라왔는지). */
        [[nodiscard]] bool readViewportWidth( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            UIViewport viewport{};
            const bool bRead = readViewport( viewport );
            outValue         = viewport._size._x;
            return bRead;
        }

        [[nodiscard]] bool readViewportHeight( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            UIViewport viewport{};
            const bool bRead = readViewport( viewport );
            outValue         = viewport._size._y;
            return bRead;
        }

        /** @brief 감시 값 `UI.TouchTargetSize` 입니다 — 손가락 조작의 가장 작은 상자(UI 단위, 데이터 `uiscale.xml`). */
        [[nodiscard]] bool readTouchTargetSize( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            UIViewport viewport{};
            const bool bRead = readViewport( viewport );
            outValue         = viewport._touchTargetSize;
            return bRead;
        }

        /** @brief 감시 값 `UI.SubtitleSpeaker` 입니다 — 가장 최근에 보이기 시작한 자막 줄의 화자(현지화한 글)이고, 보이는 줄이 없으면 `none` 입니다. 시퀀서 자막 · 대화가 화자를 바인딩 표시 이름으로 냈는지 본다. */
        [[nodiscard]] bool readSubtitleSpeaker( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            (void)query;
            const UISubtitleService* pSubtitles = engine::findSubsystem<UISubtitleService>();
            if ( pSubtitles == nullptr )
            {
                outValue.setText( "no subtitle service" );
                return false;
            }
            const uint32 count = pSubtitles->getActiveLineCount();
            outValue.setText( count > 0 ? string_view{ pSubtitles->getActiveLine( count - 1 )._speaker } : string_view{ "none" } );
            return true;
        }

        /** @brief 자막 서비스입니다. 없으면(UI 없는 실행) 까닭을 적고 nullptr 입니다. */
        const UISubtitleService* findSubtitles( AutomationWatchValue& outValue )
        {
            const UISubtitleService* pSubtitles = engine::findSubsystem<UISubtitleService>();
            if ( pSubtitles == nullptr )
                outValue.setText( "no subtitle service" );
            return pSubtitles;
        }

        /** @brief 감시 값 `UI.SubtitleLineCount` 입니다 — 지금 보이는(시간이 흐르는) 자막 줄 수. 설정으로 화면을 꺼도 센다. */
        [[nodiscard]] bool readSubtitleLineCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            (void)query;
            const UISubtitleService* pSubtitles = findSubtitles( outValue );
            if ( pSubtitles == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pSubtitles->getActiveLineCount() ) );
            return true;
        }

        /** @brief 감시 값 `UI.SubtitleShown` 입니다 — 자막 화면이 열려 있으면 1(사용자 설정 `accessibility.subtitles` 를 끄면 줄이 있어도 0). */
        [[nodiscard]] bool readSubtitleShown( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            (void)query;
            const UISubtitleService* pSubtitles = findSubtitles( outValue );
            if ( pSubtitles == nullptr )
                return false;
            outValue.setNumber( pSubtitles->getScreen() != kInvalidUIScreenHandle ? 1.0 : 0.0 );
            return true;
        }

        /** @brief 감시 값 `UI.SubtitleTextSize` 입니다 — 가장 최근 줄의 본문 글 크기(문서 크기 × 크기 설정 배율, UI 단위). 화면이 없으면 0. */
        [[nodiscard]] bool readSubtitleTextSize( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            (void)query;
            const UISubtitleService* pSubtitles = findSubtitles( outValue );
            if ( pSubtitles == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pSubtitles->makeLayoutReport()._newestTextFontSize ) );
            return true;
        }

        /** @brief 감시 값 `UI.SubtitleWrappedLines` 입니다 — 보이는 줄 가운데 가장 많이 접힌 본문의 줄 수(긴 언어가 최대 폭에서 접히는지). */
        [[nodiscard]] bool readSubtitleWrappedLines( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            (void)query;
            const UISubtitleService* pSubtitles = findSubtitles( outValue );
            if ( pSubtitles == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pSubtitles->makeLayoutReport()._maxWrappedLineCount ) );
            return true;
        }

        /** @brief 감시 값 `UI.SubtitleOverflow` 입니다 — 보이는 본문이 잘렸거나 자기 칸 · 화면 폭을 넘었으면 1(의사 문화권 qps-ploc 처럼 길어지는 언어). */
        [[nodiscard]] bool readSubtitleOverflow( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            (void)query;
            const UISubtitleService* pSubtitles = findSubtitles( outValue );
            if ( pSubtitles == nullptr )
                return false;
            outValue.setNumber( pSubtitles->makeLayoutReport()._bOverflow ? 1.0 : 0.0 );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( uiSafeInsetTop, "UI.SafeInsetTop", "Top safe-area inset of the UI viewport in UI units (notch or status bar, or the debug safe zone)", &readSafeInsetTop );
    SW_AUTOMATION_WATCH( uiSafeInsetBottom, "UI.SafeInsetBottom", "Bottom safe-area inset of the UI viewport in UI units (gesture bar or the debug safe zone)", &readSafeInsetBottom );
    SW_AUTOMATION_WATCH( uiViewportWidth, "UI.ViewportWidth", "UI viewport width in UI units (follows resize and rotation)", &readViewportWidth );
    SW_AUTOMATION_WATCH( uiViewportHeight, "UI.ViewportHeight", "UI viewport height in UI units (follows resize and rotation)", &readViewportHeight );
    SW_AUTOMATION_WATCH( uiTouchTargetSize, "UI.TouchTargetSize", "Smallest touch target in UI units (uiscale.xml _minTouchTargetSize)", &readTouchTargetSize );
    SW_AUTOMATION_WATCH( uiDeferredGlyphs, "UI.DeferredGlyphs", "Glyphs the last UI paint left for the next frame because the glyph raster budget ran out (0: all text is drawn)", &readDeferredGlyphs );
    SW_AUTOMATION_QUERY_WATCH( widgetMarkerVisible, "UI.MarkerVisible", "1 while the target object's screen-space widget marker is shown (0: collapsed, world space or no content)",
                               &readWidgetMarkerVisible, true, false );
    SW_AUTOMATION_QUERY_WATCH( uiSubtitleSpeaker, "UI.SubtitleSpeaker", "Speaker of the newest visible subtitle line (none: no line is shown)", &readSubtitleSpeaker, false, false );
    SW_AUTOMATION_QUERY_WATCH( uiSubtitleLineCount, "UI.SubtitleLineCount", "Subtitle lines on screen whose time runs (counted even while the subtitles setting is off)", &readSubtitleLineCount, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( uiSubtitleShown, "UI.SubtitleShown", "1 while the subtitle screen is open (0 when the subtitles setting is off, even with lines)", &readSubtitleShown, false, false );
    SW_AUTOMATION_QUERY_WATCH( uiSubtitleTextSize, "UI.SubtitleTextSize", "Body font size of the newest subtitle line (document size times the size setting scale; 0 without the screen)",
                               &readSubtitleTextSize, false, false );
    SW_AUTOMATION_QUERY_WATCH( uiSubtitleWrappedLines, "UI.SubtitleWrappedLines", "Most wrapped text lines among the visible subtitle lines (last UI layout)", &readSubtitleWrappedLines, false, false );
    SW_AUTOMATION_QUERY_WATCH( uiSubtitleOverflow, "UI.SubtitleOverflow", "1 when a visible subtitle body is truncated or wider than its box or the screen (long languages such as qps-ploc)",
                               &readSubtitleOverflow, false, false );
} // namespace sw
