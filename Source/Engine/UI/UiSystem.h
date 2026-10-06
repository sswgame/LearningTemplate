/**
 * @file UiSystem.h
 * @brief 런타임 UI 의 엔진 서비스입니다 — 화면 스택 · 입력 → 사건 · 갱신 순서를 돕니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Core/UiFocusManager.h"
#include "Engine/UI/Core/UiPointerState.h"
#include "Engine/UI/Core/WidgetNavigation.h"
#include "Engine/UI/Core/WidgetTypes.h"
#include "Engine/UI/Document/UiBindingDesc.h"
#include "Engine/UI/Document/UiDocumentCache.h"
#include "Engine/UI/Input/UiInputConsumption.h"
#include "Engine/UI/Layout/UiLayoutPass.h"
#include "Engine/UI/Layout/UiScale.h"
#include "Engine/UI/Render/UiPaintPass.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/Style/UiStyleSheetCache.h"
#include "Engine/UI/Style/UiTheme.h"

namespace sw
{
    struct UiStyleSheetAsset;

    class FontSystem;
    class InputManager;
    class InputMap;
    class LocalizationManager;
    class TextLayoutEngine;
    class WidgetComponent;

    /** @brief UI 를 지금 무엇으로 다루는가입니다(CommonUI 의 입력 방식). 탐색이면 포커스 테두리를 보이고, 포인터면 숨긴다. */
    enum class UiInputMode : uint8
    {
        Pointer,   ///< 마우스 · 터치
        Navigation ///< 키보드 · 게임패드
    };
} // namespace sw

namespace sw
{
    /**
     * @class UiSystem
     * @brief 런타임 UI 의 엔진 서비스입니다(`engine::getUiSystem()` · 게임은 `game::getService<UiSystem>()`).
     * @details 언리얼 `FSlateApplication` + CommonUI `UCommonUIActionRouter` 의 자리입니다. 틱은 둘로 나뉩니다 —
     *          `processInput`(입력 갱신 직후, 게임 틱 **앞** — UI 가 먹은 입력을 폰이 못 보게)과 `update`(게임 틱 **뒤**, 렌더 패킷 앞 —
     *          이번 프레임의 게임 상태로 애니메이션 → 바인딩 → 스타일 → 레이아웃 → 그리기). 기동 단계 `Ui`(Client 대상)가 `initialize` 합니다.
     *
     *          **화면 스택**: 화면은 층(`UiLayer`)마다 쌓입니다 — 그리기 순서 = 층 순서 → 층 안 쌓인 순서, 입력은 그 역순입니다. 맨 위의 포커스 받는 화면이
     *          **활성 화면**이고 포커스 · UI 행동을 받습니다. 모달 · 로딩 화면은 아래 화면과 게임의 입력을 막습니다(`isGameInputBlocked` — 플레이어 조종자가 본다).
     *          덮인 화면은 그때의 포커스를 기억했다가 다시 활성이 되면 돌려줍니다. 닫기는 지연입니다(이번 입력 처리가 끝난 뒤).
     *
     *          **행동 입력**: UI 는 키 · 버튼을 직접 보지 않고 자기 입력 맵(`EngineDefaultAssets::_uiInputMap` — 레이어 `UI`, 활성 화면이 있을 때만 켠다)의
     *          행동(`UI.Navigate*` · `UI.Accept` · `UI.Back` · `UI.FocusNext` …)을 받아 활성 화면의 포커스 경로로 보냅니다. UI 가 쓴 행동의 물리 입력은
     *          뗄 때까지 **먹힌 입력**입니다(`isActionConsumed` — 플레이어 조종자가 의도를 만들 때 이것을 보는 한 자리라, 패드 A 로 메뉴를 누르면 점프하지 않는다).
     *          위젯이 처리한 마우스 버튼도 먹힌다. 입력 방식(포인터 / 탐색)은 이번 프레임 마지막으로 쓴 장치로 정합니다. 글 입력 칸이 포커스를 쥐면
     *          키보드 포커스 `Ui` 를 잡습니다(그동안 게임은 키를 보지 못하고, UI 행동은 Back · Tab 만).
     *
     *          핫 리로드: 내려가는 모듈 이미지에 vtable 이 있는 화면 · 위젯(게임 · 키트가 만든 타입)이 든 화면은 그 자리에서 닫습니다. 게임 스레드만.
     */
    class SW_API UiSystem final : public IModuleUnloadListener
    {
    public:
        UiSystem();
        ~UiSystem() override;
        UiSystem( const UiSystem& )            = delete;
        UiSystem& operator=( const UiSystem& ) = delete;

        /**
         * @brief 입력을 묶고 일을 시작합니다.
         * @param inputManager 행동 · 포인터를 읽는 입력(시험은 자기 것을 넘긴다).
         * @param pFontSystem 글 측정 · 그리기가 쓰는 글꼴(레이아웃부터). 없으면 글 위젯이 크기 0 입니다.
         * @param uiInputMapPath UI 행동 맵 리소스(`engine/input/ui.input.xml`). 비우면 UI 행동이 없습니다(포인터만).
         * @return 행동 맵을 주었는데 읽지 못하면 false 입니다.
         */
        [[nodiscard]] bool initialize( InputManager& inputManager, FontSystem* pFontSystem, string_view uiInputMapPath = {} );
        /** @brief 화면을 모두 닫고(지연 없이) 입력을 놓습니다. 건 게임 정지 요청도 풉니다. */
        void shutdown();
        bool isInitialized() const { return _pInput != nullptr; }

        /** @brief 입력 → UI 사건(게임 틱 앞). 뗀 입력 풀기 → UI 맵 갱신 → 입력 방식 → 포인터 → 행동 → 글 포커스 → 닫기 요청 적용 순서입니다. */
        void processInput( float32 deltaSeconds );
        /**
         * @brief 애니메이션 → 바인딩 → 스타일 → 레이아웃 → 그리기(게임 틱 뒤). @p viewport 는 이번 프레임에 UI 를 그릴 화면입니다.
         * @details 레이아웃은 화면 트리마다 `UiLayoutPass::update( 트리, makeLayoutContext() )` — 화면마다 뷰포트(UI 단위) 전체가 루트 사각형입니다.
         *          포인터 위치는 다음 `processInput` 이 이 뷰포트의 배율로 UI 단위로 바꿉니다.
         */
        void update( float32 deltaSeconds, const UiViewport& viewport );

        // --- 화면 스택 ---------------------------------------------------------------
        /** @brief 화면을 그 층의 맨 위에 올립니다. 활성 화면이 바뀌면 포커스를 옮깁니다(탐색 방식이면 기본 포커스). */
        UiScreenHandle pushScreen( unique_ptr<UiScreen> screen );
        /** @brief 화면을 닫습니다(지연 — 사건 처리 중에 닫아도 이번 경로가 끝난 뒤 · 다음 `processInput` 끝 · `update` 앞에서 지운다). */
        void closeScreen( UiScreenHandle handle );
        /** @brief 화면을 찾습니다. 없거나 이미 지웠으면 nullptr 입니다(닫는 중인 화면은 찾는다). */
        UiScreen* findScreen( UiScreenHandle handle ) const;
        /** @brief 지금 활성 화면(맨 위의 포커스 받는 화면 — 막는 화면 아래로는 내려가지 않는다)입니다. 없으면 nullptr 입니다. */
        UiScreen* getActiveScreen() const;
        uint32    getScreenCount() const { return static_cast<uint32>( _listScreen.size() ); }

        // --- 문서 ---------------------------------------------------------------
        /**
         * @brief UI 문서 @p documentPath 로 화면을 지어 올립니다 — 문서의 `UiScreenDesc` 로, 바인딩 식은 화면이 듭니다(`UiScreen::getBindings`).
         * @return 읽지 못하거나 지을 수 없으면(모르는 타입 · 속성 · 값) 무효 핸들이고, 오류(파일 · 줄)를 로그에 남깁니다.
         */
        [[nodiscard]] UiScreenHandle openScreen( string_view documentPath ) { return openScreen<UiScreen>( documentPath ); }
        /** @brief 문서를 C++ 화면 클래스 @p ScreenType(`onCommand` · `onBack` 을 덮어쓴 것 — 생성자는 `( const UiScreenDesc&, unique_ptr<Widget> )`)으로 엽니다. */
        template <typename ScreenType>
        [[nodiscard]] UiScreenHandle openScreen( string_view documentPath )
        {
            UiScreenDesc          desc{};
            vector<UiBindingDesc> listBinding{};
            vector<string>        listStyleSheet{};
            unique_ptr<Widget>    root = instantiateDocument( documentPath, desc, listBinding, listStyleSheet );
            if ( root == nullptr )
                return kInvalidUiScreenHandle;
            return pushDocumentScreen( sw::make_unique<ScreenType>( desc, std::move( root ) ), documentPath, std::move( listBinding ), std::move( listStyleSheet ) );
        }
        /** @brief UI 문서 캐시입니다(기동 단계 `Ui` 가 에셋 캐시 등록부에 올린다 — 핫 리로드 · 진단). */
        UiDocumentCache&       getDocumentCache() { return _documentCache; }
        const UiDocumentCache& getDocumentCache() const { return _documentCache; }

        // --- 스타일 · 테마 ---------------------------------------------------------------
        /** @brief 스타일 시트 캐시입니다(기동 단계 `Ui` 가 에셋 캐시 등록부에 올린다). */
        UiStyleSheetCache& getStyleSheetCache() { return _styleSheetCache; }
        /** @brief 고를 수 있는 테마를 겁니다(기동 단계 `Ui` — `engine/ui/uithemes.xml` · 게임 프리셋 `_uiThemes`). 기본 테마로 바꿉니다. */
        void                  setThemeCatalog( const UiThemeCatalog& catalog );
        const UiThemeCatalog& getThemeCatalog() const { return _themeCatalog; }
        /**
         * @brief 테마를 고릅니다 — 모든 화면의 스타일 묶음을 테마 시트 → 문서 시트로 다시 걸고 트리 전체를 다시 맞춥니다. 모르는 이름이면 false 입니다.
         * @details 읽지 못한 시트는 오류를 남기고 뺍니다(나머지 시트로 그린다).
         */
        [[nodiscard]] bool   setTheme( const hashed_string& name );
        const hashed_string& getThemeName() const { return _themeName; }
        // --- 화면 마커(WidgetComponent Screen) -------------------------------------------
        /**
         * @brief 마커 화면(Hud 층 · 캔버스 패널 루트 · 클릭을 막지 않음)에 위젯을 붙이고 그 번호를 돌려줍니다. 마커 화면은 처음 붙일 때 만들고 마지막을 뗄 때 닫는다.
         * @details 위치는 붙인 쪽이 슬롯 앵커 · 오프셋으로 매 프레임 정한다(`WidgetComponent::applyPlacement`).
         */
        WidgetId addScreenMarker( unique_ptr<Widget> widget );
        /** @brief 마커 위젯을 떼어 지웁니다. */
        void removeScreenMarker( WidgetId widget );
        /** @brief 마커 위젯입니다(없으면 nullptr). */
        Widget* findScreenMarker( WidgetId widget ) const;
        /** @brief 위젯 컴포넌트를 등록합니다 — `update` 가 레이아웃 앞에서 화면 마커 자리를 갱신한다(시작할 때 컴포넌트가 부른다). */
        void   registerWidgetComponent( WidgetComponent& component );
        void   unregisterWidgetComponent( WidgetComponent& component );
        uint32 getWidgetComponentCount() const { return static_cast<uint32>( _listWidgetComponent.size() ); }
        /** @brief World 위젯 컴포넌트의 렌더 텍스처 목록을 덧붙입니다(`EngineLoop` 가 렌더 패킷 캔버스의 대상 목록에 — 렌더러가 장면 앞에서 그린다). */
        void collectWorldCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const;

        // --- 게임 쪽이 묻는 것 -----------------------------------------------------------
        /** @brief 모달 · 로딩 화면이 떠 있어 게임 입력을 막아야 하면 true 입니다(플레이어 조종자가 의도를 0 으로 둔다). */
        bool isGameInputBlocked() const;
        /** @brief 활성 화면이 OS 커서를 바라면 true 입니다(플레이어 조종자가 마우스 잠금을 쉰다). */
        bool wantsCursor() const;
        /** @brief @p inputMap 의 행동 @p action 을 지금 누르는 물리 입력이 모두 UI 가 먹은 것이면 true 입니다(플레이어 조종자가 묻는다). */
        bool isActionConsumed( const InputMap& inputMap, const hashed_string& action ) const;
        /** @brief @p inputMap 의 행동 @p action 이 지금 쓰는 물리 입력을 먹습니다(UI 가 그 행동을 처리했다). */
        void consumeAction( const InputMap& inputMap, const hashed_string& action );
        /** @brief 마우스 버튼 @p button 을 먹습니다. */
        void                      consumeMouseButton( MouseButton button ) { _consumption.consumeMouseButton( button ); }
        const UiInputConsumption& getInputConsumption() const { return _consumption; }
        /** @brief UI 행동 맵입니다(행동 맵을 주지 않았으면 nullptr). */
        InputMap* getUiInputMap() const { return _uiInputMap.get(); }

        // --- 포커스 · 입력 방식 ---------------------------------------------------------------
        UiFocusManager&       getFocusManager() { return _focus; }
        const UiFocusManager& getFocusManager() const { return _focus; }
        const UiPointerState& getPointerState() const { return _pointer; }
        UiInputMode           getInputMode() const { return _inputMode; }
        /** @brief 입력 방식을 바꿉니다. 탐색으로 바뀌는 순간 활성 화면에 포커스가 없으면 기본 포커스로 옮깁니다. */
        void setInputMode( UiInputMode mode );

        InputManager*     getInputManager() const { return _pInput; }
        FontSystem*       getFontSystem() const { return _pFontSystem; }
        const UiViewport& getViewport() const { return _viewport; }
        /** @brief 글 측정 엔진입니다(글꼴이 없으면 nullptr). 레이아웃 문맥이 넘긴다. */
        TextLayoutEngine* getTextLayout() const { return _textLayout.get(); }

        /** @brief 해상도 → 배율 규칙입니다(기동 단계 `Ui` 가 `engine/ui/uiscale.xml` · 게임 프리셋 `_uiScaleSettings` 로 정한다). */
        const UiScaleSettings& getScaleSettings() const { return _scaleSettings; }
        void                   setScaleSettings( const UiScaleSettings& settings ) { _scaleSettings = settings; }
        /**
         * @brief 물리 화면 크기로 이번 프레임의 UI 뷰포트를 만듭니다 — 배율 = 규칙 × gv_uiScale(× 창 배율, 설정이 켤 때), 안전 영역 = gv_uiDebugSafeZone.
         * @param contentScale 창의 OS 배율(`IWindow::getContentScale`).
         */
        UiViewport computeViewport( const float2& physicalSize, float32 contentScale ) const;
        /**
         * @brief 지금 뷰포트(`update` 가 받은 것) · gv_uiTextScale · 글 측정 · 문화권 방향으로 레이아웃 문맥을 만듭니다(`UiLayoutPass::update` 에 넘긴다).
         * @details 루트의 흐름 방향 = 문화권이 오른쪽에서 왼쪽인가(`UiLayoutPass::isCultureRightToLeft`) — 문화권이 바뀌면 다음 걷기가 트리를 다시 놓는다.
         */
        UiLayoutContext makeLayoutContext() const;
        /** @brief 지금 글리프 캐시 · 글 배치 · 배율 · 프레임 번호로 그리기 문맥을 만듭니다(그리기 걷기에 넘긴다). */
        UiPaintContext makePaintContext() const;
        /**
         * @brief 이번 프레임의 UI 그리기 목록입니다(물리 픽셀 — 대상 크기 = 뷰포트 물리 크기). `EngineLoop` 가 렌더 패킷의 주 출력 캔버스에 싣는다.
         * @details 화면을 그리기 순서(층 → 쌓인 순서)로, 화면마다 위젯 그림 캐시를 이어 붙이고, 탐색 입력 방식이면 포커스 테두리를 그 화면 위에 얹는다.
         */
        const CanvasDrawList& getCanvas() const { return _canvas; }
        /** @brief 그리기 목록 내용이 바뀔 때만 오르는 번호입니다(1 부터 — 렌더러가 같으면 사각형을 다시 올리지 않는다). */
        uint64 getCanvasRevision() const { return _canvasRevision; }
        /** @brief 문화권 출처를 정합니다(시험이 자기 것을 넘긴다 — 전역 문화권을 건드리지 않게). nullptr 이면 바인딩된 엔진 서비스입니다. */
        void setLocalization( const LocalizationManager* pLocalization ) { _pLocalization = pLocalization; }

        const utf8* getModuleUnloadListenerName() const override { return "ui screens"; }
        /** @brief vtable 이 [@p pBegin, @p pEnd) 안인 화면 · 위젯이 든 화면을 그 자리에서 닫습니다. 닫은 화면 수를 반환합니다. */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

    private:
        /** @brief 문서를 캐시에서 찾아 위젯 트리를 짓습니다. 스타일 시트는 문서와 조각(끼운 순서)의 것을 모읍니다. 실패하면 오류를 로그에 남기고 nullptr 입니다. */
        unique_ptr<Widget> instantiateDocument( string_view documentPath, UiScreenDesc& outDesc, vector<UiBindingDesc>& outListBinding, vector<string>& outListStyleSheet );
        /** @brief 문서로 지은 화면에 문서 경로 · 바인딩 · 스타일 시트를 적고 올립니다. */
        UiScreenHandle pushDocumentScreen( unique_ptr<UiScreen> screen, string_view documentPath, vector<UiBindingDesc> listBinding, vector<string> listStyleSheet );
        /** @brief 화면의 스타일 묶음을 지금 테마 시트 → 화면 시트로 다시 걸고 트리 전체를 다시 맞추게 합니다. */
        void rebuildStyleSet( UiScreen& screen );
        /** @brief 시트들을 캐시에서 읽습니다(읽지 못한 것은 오류를 남기고 뺀다). */
        void appendStyleSheets( const vector<string>& listPath, vector<shared_ptr<const UiStyleSheetAsset>>& inoutListSheet );
        /** @brief 이번 프레임 원시 사건에서 마지막으로 쓴 장치로 입력 방식을 정합니다(커서는 자리가 실제로 바뀐 이동만). */
        void updateInputMode();
        /** @brief UI 행동을 활성 화면으로 보냅니다(탐색 · 스틱 · 확인 · 뒤로 · 탭). */
        void processActions( float32 deltaSeconds );
        /** @brief 탐색 행동 하나 — 포커스 경로에 먼저, 아무도 안 먹으면 포커스를 옮긴다. 썼으면 true 입니다. */
        bool handleNavigation( UiScreen& screen, const hashed_string& action, UiNavigationDirection direction );
        /** @brief 스틱 탐색 — 크게 기울면 큰 축 방향으로 한 번, 그 뒤 탐색 반복 간격으로 반복합니다. */
        void processStickNavigation( UiScreen& screen, float32 deltaSeconds );
        /** @brief 스크롤 행동(`UI.Scroll` — 오른쪽 스틱)을 포커스 경로(없으면 포인터가 올라간 경로)로 보냅니다. 스크롤 패널이 쓰면 먹습니다. */
        void processScroll( UiScreen& screen );
        /** @brief 행동 사건 하나를 활성 화면의 포커스 경로로 보냅니다. 처리됐으면 true 입니다. */
        bool routeAction( UiScreen& screen, const hashed_string& action, const float2& value );
        /** @brief 행동 사건 하나를 @p path 로 보냅니다(이번 입력 프레임 시간을 싣는다). 처리됐으면 true 입니다. */
        bool routeActionAlong( UiScreen& screen, const UiWidgetPath& path, const hashed_string& action, const float2& value );
        /** @brief 포커스 위젯이 글 입력 칸이면 키보드 포커스 `Ui` 를 잡고, 아니면 놓습니다. */
        void updateKeyboardFocus();
        /** @brief 키보드 포커스가 `Ui` 일 때 오는 글자를 포커스 위젯에 보냅니다. */
        void onTextInput( string_view text );
        /** @brief 키보드 포커스가 `Ui` 일 때 오는 조합 중 글자를 포커스 위젯에 보냅니다. */
        void onTextComposition( string_view text );
        /** @brief 글자 사건을 포커스 위젯 하나에 보냅니다. */
        void dispatchTextEvent( string_view text, bool bComposition );
        /** @brief 마우스 상태를 포인터 사건으로 바꿔 화면들로 보냅니다. */
        void processPointer();
        /** @brief 포인터 사건 하나를 받을 화면으로 보냅니다. 처리했으면 true 입니다. */
        bool dispatchPointerEvent( const UiPointerEvent& event );
        /** @brief 점 아래의 맨 위 화면입니다(막는 화면 아래로는 내려가지 않는다). 없으면 nullptr 입니다. */
        UiScreen* findPointerScreen( const float2& point ) const;
        /** @brief 화면을 그리기 순서로 칠해 그리기 목록을 만들고, 내용이 바뀌었으면 번호를 올립니다. */
        void paintScreens();
        /** @brief `gv_uiDemo` 를 따라 개발 시험 화면을 열고 닫습니다(열면 입력 방식을 탐색으로 — 첫 버튼에 포커스 테두리). */
        void syncDemoScreen();
        /** @brief 닫기를 요청한 화면을 지웁니다. */
        void applyPendingCloses();
        /** @brief 화면 @p index 를 바로 지웁니다(포인터 · 포커스가 그 트리를 놓게). */
        void destroyScreenAt( uint32 index );
        /** @brief 활성 화면 · 게임 정지를 다시 정합니다(화면을 올리고 지운 뒤). */
        void refreshActiveScreen();
        /** @brief @p screen 이 활성이 될 때의 포커스 — 기억한 위젯, 없고 탐색 방식이면 기본 포커스. */
        void restoreFocus( UiScreen& screen );

    private:
        vector<unique_ptr<UiScreen>> _listScreen; ///< 그리기 순서(층 → 쌓인 순서). 입력은 역순.
        UiDocumentCache              _documentCache;
        UiStyleSheetCache            _styleSheetCache;
        UiThemeCatalog               _themeCatalog;
        hashed_string                _themeName; ///< 지금 테마(없으면 빈 이름 — 문서 시트만)
        UiFocusManager               _focus;
        UiPointerState               _pointer;
        UiInputConsumption           _consumption;
        unique_ptr<InputMap>         _uiInputMap; ///< UI 행동 맵(레이어 `UI` — 활성 화면이 있을 때만 켠다)
        InputManager*                _pInput;
        FontSystem*                  _pFontSystem;
        const LocalizationManager*   _pLocalization; ///< 문화권 출처(nullptr = 엔진 서비스)
        unique_ptr<TextLayoutEngine> _textLayout;
        UiScaleSettings              _scaleSettings;
        UiViewport                   _viewport;
        CanvasDrawList               _canvas;         ///< 이번 프레임 그리기 목록
        CanvasDrawList               _canvasScratch;  ///< 칠하는 중의 목록(같은 내용이면 버린다)
        uint64                       _canvasRevision; ///< `_canvas` 내용 번호
        float2                       _lastPointerPosition;
        int2                         _lastMousePixel;     ///< 입력 방식을 정할 때 본 마지막 커서 자리(창 픽셀)
        float32                      _stickRepeatSeconds; ///< 스틱 탐색의 다음 반복까지 남은 시간
        float32                      _inputDeltaSeconds;  ///< 이번 `processInput` 의 프레임 시간(행동 사건에 싣는다)
        UiNavigationDirection        _stickDirection;     ///< 스틱이 지금 가리키는 탐색 방향(기울지 않았으면 Next — 쓰지 않는 값)
        UiScreenHandle               _activeScreen;
        UiScreenHandle               _demoScreen;          ///< `-gv_uiDemo` 가 연 시험 화면(없으면 무효)
        UiScreenHandle               _markerScreen;        ///< 화면 마커를 담는 Hud 화면(없으면 무효)
        vector<WidgetComponent*>     _listWidgetComponent; ///< 등록된 위젯 컴포넌트(소유하지 않는다 — 끝날 때 스스로 뺀다)
        UiScreenHandle               _nextScreenHandle;
        uint32                       _nextPushOrder;
        UiInputMode                  _inputMode;
        uint8                        _bPauseRequested  : 1; ///< 게임 정지 요청을 걸어 두었다(정지 화면이 하나라도 있다)
        uint8                        _bPendingClose    : 1; ///< 닫기를 요청한 화면이 있다
        uint8                        _bPointerKnown    : 1; ///< 마우스 위치를 한 번 읽었다(첫 프레임의 Move 기준)
        uint8                        _bStickHeld       : 1; ///< 스틱이 탐색 문턱 너머로 기울어 있다
        uint8                        _bMousePixelKnown : 1; ///< `_lastMousePixel` 을 한 번 읽었다
        [[maybe_unused]] uint8       _reserved         : 3;
    };
} // namespace sw
