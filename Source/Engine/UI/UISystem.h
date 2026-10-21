/**
 * @file UISystem.h
 * @brief 런타임 UI 의 엔진 서비스입니다. 화면 스택, 입력에서 UI 이벤트로의 변환, 갱신 순서를 담당합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Console/DevCommandsEnabled.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Binding/UIBindingConverterRegistry.h"
#include "Engine/UI/Document/UIBindingDesc.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Input/UIActionGlyphSource.h"
#include "Engine/UI/Input/UIClipboardService.h"
#include "Engine/UI/Input/UIInputRouter.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Render/UIPaintPass.h"
#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"
#include "Engine/UI/Screen/UINotificationService.h"
#include "Engine/UI/Screen/UIOffscreenScreens.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Screen/UISubtitleService.h"
#include "Engine/UI/Screen/UITooltipService.h"
#include "Engine/UI/Style/UIStyleSheetCache.h"
#include "Engine/UI/Style/UIThemeController.h"
#include "Engine/UI/UIEngineDocuments.h"

namespace sw
{
    class DebugHUD;
    class FontService;
    class InputManager;
    class InputMap;
    class LocalizationManager;
    class TextLayoutEngine;
    class UserSettingsManager;
    class WidgetComponent;
} // namespace sw

namespace sw
{
    /**
     * @class UISystem
     * @brief 런타임 UI 의 엔진 서비스입니다(`engine::getSubsystem<UISystem>()`, 게임은 `game::findSubsystem<UISystem>()`).
     * @details 언리얼의 `FSlateApplication` 과 CommonUI 의 `UCommonUIActionRouter` 에 해당한다.
     *          틱은 둘로 나뉜다. `processInput` 은 입력 갱신 직후, 게임 틱 앞에서 돌아서 UI 가 처리한 입력을 폰이 보지 못하게 한다.
     *          `update` 는 게임 틱 뒤, 렌더 패킷 앞에서 돌며 이번 프레임의 게임 상태로 애니메이션, 바인딩, 스타일, 레이아웃, 그리기를 순서대로 처리한다.
     *          엔진에서는 서브시스템 `UI`(Client 대상)입니다 — 배율 규칙 · 테마 · 기본 메뉴를 엔진 데이터(팩 설정 `PackEngineSettings` 가 덮어씀)로 정하고
     *          문서 · 스타일 시트 캐시를 에셋 캐시 레지스트리에 올리며, Dev 에서는 디버그 HUD 를 붙입니다(`initializeSubsystem`).
     *
     *          **화면 스택**: 화면은 레이어(`UILayer`)마다 쌓인다. 그리기 순서는 레이어 순서, 같은 레이어 안에서는 쌓인 순서이고, 입력은 그 역순이다.
     *          맨 위에서 포커스를 받는 화면이 **활성 화면**이며 포커스와 UI 액션을 받는다.
     *          모달 화면과 로딩 화면은 아래 화면과 게임의 입력을 막는다(`isGameInputBlocked`, 플레이어 컨트롤러가 확인한다).
     *          다른 화면에 덮인 화면은 그때의 포커스를 기억했다가 다시 활성이 되면 돌려준다. 닫기는 지연 처리하며, 이번 입력 처리가 끝난 뒤에 실행한다.
     *
     *          **액션 입력**: UI 는 키와 버튼을 직접 보지 않고 자기 입력 맵(`EngineDefaultAssets::_uiInputMap`, 레이어 `UI`, 활성 화면이 있을 때만 켠다)의
     *          액션(`UI.Navigate*`, `UI.Accept`, `UI.Back`, `UI.FocusNext` 등)을 받아 활성 화면의 포커스 경로로 보낸다.
     *          UI 가 사용한 액션의 물리 입력은 뗄 때까지 **소비된 입력**이다(`isActionConsumed`). 플레이어 컨트롤러가 의도를 만들 때 이것만 확인하면 되므로,
     *          패드 A 로 메뉴를 눌러도 점프하지 않는다. 위젯이 처리한 마우스 버튼도 소비된다.
     *          입력 방식(포인터 또는 탐색)은 이번 프레임에 마지막으로 쓴 장치로 정한다. 텍스트 입력 위젯이 포커스를 가지면
     *          키보드 포커스 `UI` 를 잡는다. 그동안 게임은 키를 보지 못하고, UI 액션은 Back 과 Tab 만 받는다.
     *
     *          핫 리로드: 언로드되는 모듈 이미지에 vtable 이 있는 화면과 위젯(게임이나 키트가 만든 타입)을 가진 화면은 그 자리에서 닫는다. 게임 스레드에서만 호출한다.
     *
     *          **나눈 책임**(값 멤버, 비가상 직접 호출): 입력 라우팅은 `UIInputRouter`, 테마 고르기와 스타일 세트 짓기는 `UIThemeController`,
     *          화면 스택 밖 미리보기는 `UIOffscreenScreens` 가 한다. 화면 스택과 프레임 순서는 이 클래스에 남는다(순서는 `processInput` 과 `update` 본문이 정본이다).
     */
    class SW_API UISystem final : public ISubsystem, public IModuleUnloadListener
    {
    public:
        UISystem();
        ~UISystem() override;
        UISystem( const UISystem& )            = delete;
        UISystem& operator=( const UISystem& ) = delete;

        /**
         * @brief 입력을 연결하고 시작합니다.
         * @param inputManager 액션과 포인터를 읽는 입력입니다(테스트는 자기 것을 넘깁니다).
         * @param pFontService 텍스트 측정과 그리기가 쓰는 글꼴입니다(레이아웃부터 사용). 없으면 텍스트 위젯의 크기가 0 입니다.
         * @param uiInputMapPath UI 액션 맵 리소스(`engine/input/ui.input.xml`)입니다. 비우면 UI 액션이 없습니다(포인터만 동작).
         * @return 액션 맵을 지정했는데 읽지 못하면 false 입니다.
         */
        [[nodiscard]] bool initialize( InputManager& inputManager, FontService* pFontService, string_view uiInputMapPath = {} );
        /** @brief 화면을 모두 즉시 닫고 입력을 놓습니다. 걸어 둔 게임 정지 요청도 해제합니다. */
        void shutdown();
        bool isInitialized() const { return _input.getInputManager() != nullptr; }

        // ISubsystem — 기동 단계 `UI`. 배율 규칙 · 테마 목록 · UI 행동 맵을 못 읽으면 데이터 오류라 기동 실패다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "UI";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        /** @brief 입력(행동 · 포인터)을 읽고 글꼴로 측정한다. UI 배율 · 글자 크기는 사용자 설정이다. */
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
#if SW_DEV_COMMANDS_ENABLED
        /** @brief 서브시스템이 붙인 디버그 HUD 입니다(Dev 전용, 서브시스템으로 서지 않았으면 nullptr). 엔진 루프가 `update` 앞에서 셸 맵 단축키와 함께 돌린다. */
        DebugHUD* getDebugHUD() const { return _debugHUD.get(); }
#endif

        /** @brief 입력을 UI 이벤트로 처리합니다(게임 틱 앞). 순서는 뗀 입력 해제, UI 맵 갱신, 입력 방식 결정, 포인터, 액션, 텍스트 포커스, 닫기 요청 적용입니다. */
        void processInput( float32 deltaSeconds );
        /**
         * @brief 애니메이션, 바인딩, 스타일, 레이아웃, 그리기를 순서대로 처리합니다(게임 틱 뒤). @p viewport 는 이번 프레임에 UI 를 그릴 화면입니다.
         * @details 레이아웃은 화면 트리마다 `UILayoutPass::update( 트리, makeLayoutContext() )` 로 돌린다. 화면마다 뷰포트(UI 단위) 전체가 루트 사각형이다.
         *          포인터 위치는 다음 `processInput` 이 이 뷰포트의 배율로 UI 단위로 바꾼다.
         */
        void update( float32 deltaSeconds, const UIViewport& viewport );

        // --- 화면 스택 ---------------------------------------------------------------
        /** @brief 화면을 해당 레이어의 맨 위에 올립니다. 활성 화면이 바뀌면 포커스를 옮깁니다(탐색 방식이면 기본 포커스). */
        UIScreenHandle pushScreen( unique_ptr<UIScreen> screen );
        /**
         * @brief 화면을 닫습니다. 지연 처리하므로 이벤트 처리 중에 닫아도 이번 경로가 끝난 뒤, 다음 `processInput` 끝과 `update` 앞에서 지웁니다.
         * @details 화면에 `Close` 애니메이션이 있으면 재생하고 끝난 뒤 지운다. 그동안 화면은 입력과 포커스를 받지 않고 그리기만 한다.
         */
        void closeScreen( UIScreenHandle handle );
        /** @brief 화면을 찾습니다. 없거나 이미 지웠으면 nullptr 입니다(닫는 중인 화면은 찾을 수 있습니다). */
        UIScreen* findScreen( UIScreenHandle handle ) const;
        /** @brief 지금 활성 화면(맨 위에서 포커스를 받는 화면)을 반환합니다. 입력을 막는 화면 아래로는 내려가지 않습니다. 없으면 nullptr 입니다. */
        UIScreen* getActiveScreen() const;
        uint32    getScreenCount() const { return static_cast<uint32>( _listScreen.size() ); }

        // --- 문서 ---------------------------------------------------------------
        /**
         * @brief UI 문서 @p documentPath 로 화면을 생성해 올립니다. 문서의 `UIScreenDesc` 로 만들며, 바인딩 식은 화면이 보관합니다(`UIScreen::getBindings`).
         * @return 읽지 못하거나 생성할 수 없으면(모르는 타입, 속성, 값) 무효 핸들이며, 오류(파일, 줄)를 로그에 남깁니다.
         */
        [[nodiscard]] UIScreenHandle openScreen( string_view documentPath ) { return openScreen<UIScreen>( documentPath ); }
        /** @brief 문서를 C++ 화면 클래스 @p ScreenType 으로 엽니다. `onCommand` 와 `onBack` 을 재정의한 클래스여야 하며, 생성자는 `( const UIScreenDesc&, unique_ptr<Widget> )` 형태입니다. */
        template <typename ScreenType>
        [[nodiscard]] UIScreenHandle openScreen( string_view documentPath )
        {
            UIScreenDesc          desc{};
            vector<UIBindingDesc> listBinding{};
            vector<string>        listStyleSheet{};
            unique_ptr<Widget>    root = instantiateDocument( documentPath, desc, listBinding, listStyleSheet );
            if ( root == nullptr )
                return kInvalidUIScreenHandle;
            return pushDocumentScreen( sw::make_unique<ScreenType>( desc, std::move( root ) ), documentPath, std::move( listBinding ), std::move( listStyleSheet ) );
        }
        /** @brief 바인딩 변환기 레지스트리입니다(`{bind:필드, converter=이름}`). 게임과 키트가 자기 변환기를 추가하며, 모듈이 언로드되면 그 함수는 제거됩니다. */
        UIBindingConverterRegistry&       getBindingConverters() { return _bindingConverters; }
        const UIBindingConverterRegistry& getBindingConverters() const { return _bindingConverters; }
        /** @brief UI 문서 캐시입니다. 기동 단계 `UI` 가 에셋 캐시 레지스트리에 등록하며, 핫 리로드와 진단에 쓰입니다. */
        UIDocumentCache&       getDocumentCache() { return _documentCache; }
        const UIDocumentCache& getDocumentCache() const { return _documentCache; }

        // --- 엔진 기본 메뉴(옵션, 일시정지) ---------------------------------------------------
        /**
         * @brief 엔진 UI 문서 경로(옵션 메뉴 · 행 견본 · 확인 창 · 알림 · 자막 · 디버그 HUD)를 받습니다. 옵션 메뉴 문서도 그 값으로 정합니다.
         * @details 기동 단계 `UI` 가 엔진 기본 에셋 표에서 넘긴다. 받지 않으면 모두 비어 그 화면들은 열리지 않는다 — 코드에 기본 경로가 없다.
         */
        void setEngineDocuments( const UIEngineDocuments& documents )
        {
            _engineDocuments     = documents;
            _optionsMenuDocument = documents._optionsMenu;
        }
        const UIEngineDocuments& getEngineDocuments() const { return _engineDocuments; }
        /** @brief 옵션 메뉴 문서입니다. 기동 단계 `UI` 가 엔진 기본 에셋(`_uiOptionsMenu`)으로 정하며, 팩 설정의 `_uiOptionsMenu` 가 덮어씁니다. */
        const string& getOptionsMenuDocument() const { return _optionsMenuDocument; }
        void          setOptionsMenuDocument( string_view documentPath ) { _optionsMenuDocument = documentPath; }
        /**
         * @brief 일시정지 메뉴 문서를 지정합니다. 비우면(기본) 일시정지 메뉴가 없습니다.
         * @details 지정하면 화면이 없을 때 UI 맵의 `UI.Pause`(Esc, 패드 Start)가 `PauseMenuScreen` 을 엽니다. 그 입력은 UI 가 소비하므로 게임 액션(마우스 잠금 토글 등)은 보지 못한다.
         */
        void          setPauseMenuDocument( string_view documentPath );
        const string& getPauseMenuDocument() const { return _pauseMenuDocument; }
        /** @brief 설정 출처입니다. `setUserSettings` 로 받은 것(기동이 엔진 서비스를 넘긴다)이고, 받지 않았으면 nullptr 입니다. 옵션 메뉴와 설정 바인딩이 씁니다. */
        UserSettingsManager* findUserSettings() const;
        /**
         * @brief 화면이 처리하지 않은 UI 명령의 전역 표입니다. 게임 흐름이 `ReturnToTitle` · 호스트가 `QuitGame` 같은 명령을 등록하면 일시정지 메뉴가 그 버튼을 보입니다.
         * @details 함수가 모듈 이미지 안에 있으면 그 모듈이 언로드될 때 제거됩니다. 등록한 쪽은 끝날 때 `unregisterCommand` 로 거둡니다.
         */
        UIGlobalCommandRegistry&       getGlobalCommands() { return _globalCommands; }
        const UIGlobalCommandRegistry& getGlobalCommands() const { return _globalCommands; }
        /** @brief 글 클립보드입니다. 글 입력 위젯의 복사 · 잘라내기 · 붙여넣기가 쓰며, 창 계층이 운영체제 연결(`UIClipboardAPI`)을 끼웁니다. */
        UIClipboardService&       getClipboard() { return _clipboard; }
        const UIClipboardService& getClipboard() const { return _clipboard; }
        /** @brief 알림과 토스트입니다. 오버레이 레이어에서 세 개까지 쌓고, 대기열을 두며, 같은 글은 합칩니다. 게임과 키트가 `post` 합니다. */
        UINotificationService&       getNotifications() { return _notifications; }
        const UINotificationService& getNotifications() const { return _notifications; }
        /** @brief `[action=이름]` 태그의 글리프 출처입니다(게임 입력 맵, UI 맵, 지금 장치 종류). */
        const UIActionGlyphSource& getActionGlyphs() const { return _actionGlyphs; }

        // --- 스타일과 테마 ---------------------------------------------------------------
        /** @brief 스타일 시트 캐시입니다. 기동 단계 `UI` 가 에셋 캐시 레지스트리에 등록합니다. */
        UIStyleSheetCache& getStyleSheetCache() { return _styleSheetCache; }
        /**
         * @brief 선택할 수 있는 테마를 등록합니다(기동 단계 `UI`, `engine/ui/uithemes.xml`, 팩 설정 `_uiThemes`).
         * @details 사용자 설정 `gv_uiTheme`(accessibility.uiTheme)의 테마가 목록에 있으면 그 테마로, 없으면 목록의 기본 테마로 바꾼다. 그 뒤 설정이 바뀌면
         *          `update` 가 따라간다(모르는 이름이면 경고하고 지금 테마를 유지한다).
         */
        void                  setThemeCatalog( const UIThemeCatalog& catalog );
        const UIThemeCatalog& getThemeCatalog() const { return _theme.getCatalog(); }
        /**
         * @brief 테마를 선택합니다. 모든 화면의 스타일 시트를 테마 시트, 문서 시트 순서로 다시 적용하고 트리 전체를 다시 맞춥니다. 모르는 이름이면 false 입니다.
         * @details 읽지 못한 시트는 오류를 남기고 제외한다(나머지 시트로 그린다).
         */
        [[nodiscard]] bool   setTheme( const hashed_string& name );
        const hashed_string& getThemeName() const { return _theme.getThemeName(); }
        // --- 화면 마커(WidgetComponent Screen) -------------------------------------------
        /**
         * @brief 마커 화면(HUD 레이어, 캔버스 패널 루트, 클릭을 막지 않음)에 위젯을 붙이고 그 번호를 반환합니다. 마커 화면은 처음 붙일 때 만들고, 마지막 위젯을 뗄 때 닫습니다.
         * @details 위젯마다 화면 앵커 하나를 주고(`Widget::getPaintAnchor`) 위젯은 앵커 점을 원점으로 배치한다. 화면 위치와 배율은 붙인 쪽이 매 프레임
         *          앵커로 정한다(`setScreenMarkerAnchor`, `WidgetComponent::applyPlacement`). 마커가 움직여도 배치 · 칠하기 · 사각형 업로드가 없다.
         */
        WidgetID addScreenMarker( unique_ptr<Widget> widget );
        /** @brief 마커 위젯을 떼어 지웁니다(그 앵커 번호는 다음 마커가 다시 쓴다). */
        void removeScreenMarker( WidgetID widget );
        /** @brief 마커 위젯을 반환합니다(없으면 nullptr). */
        Widget* findScreenMarker( WidgetID widget ) const;
        /** @brief 마커 위젯의 앵커 @p anchor(`Widget::getPaintAnchor`)를 이번 프레임 화면 위치(UI 단위)와 배율로 정합니다. 그리기 단계가 물리 픽셀 정수로 바꿔 냅니다. */
        void setScreenMarkerAnchor( uint32 anchor, const float2& position, float32 scale );
        /** @brief 마지막 그리기 단계가 낸 화면 앵커 표입니다(번호 = `CanvasQuad::_anchor`, 물리 픽셀). `Engine` 가 렌더 패킷 캔버스에 주 출력과 함께 싣습니다. */
        const vector<CanvasAnchor>& getCanvasAnchors() const { return _listCanvasAnchor; }
        /** @brief 위젯 컴포넌트를 등록합니다. 컴포넌트가 시작할 때 부르며, `update` 가 레이아웃 앞에서 화면 마커 위치를 갱신합니다. */
        void   registerWidgetComponent( WidgetComponent& component );
        void   unregisterWidgetComponent( WidgetComponent& component );
        uint32 getWidgetComponentCount() const { return static_cast<uint32>( _listWidgetComponent.size() ); }
        /** @brief World 위젯 컴포넌트의 렌더 텍스처 목록을 덧붙입니다. `Engine` 가 렌더 패킷 캔버스의 대상 목록에 넣으며, 렌더러가 장면 앞에서 그립니다. */
        void collectWorldCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const;

        // --- 오프스크린 화면(에디터 UI 미리보기) -----------------------------------------------
        /**
         * @brief 문서 @p documentPath 를 화면 스택 밖에서 생성해 렌더 텍스처 @p targetPath(`rendertarget/...`)에 그립니다. 입력과 포커스가 없고 게임 UI 와 섞이지 않습니다.
         * @details 스타일, 테마, 핫 리로드는 스택의 화면과 같은 경로로 처리한다(문서를 고치면 미리보기도 다시 생성한다). 그리기 목록은 `collectWorldCanvases` 가 렌더 텍스처 대상으로 내보낸다.
         * @return 생성하지 못하면 무효 핸들입니다(오류는 로그에 남깁니다).
         */
        UIScreenHandle openOffscreenScreen( string_view documentPath, string_view targetPath );
        /** @brief 스택 밖 화면 목록입니다. 닫기 · 찾기 · 문화권 · 대상 경로 · 탐색 방식은 목록에 직접 묻습니다(`remove` · `findScreen` · `setLocalization` · `setTargetPath` · `setNavigationMode`). */
        UIOffscreenScreens&       getOffscreenScreens() { return _offscreen; }
        const UIOffscreenScreens& getOffscreenScreens() const { return _offscreen; }
        /**
         * @brief 오프스크린 화면의 뷰포트(UI 단위 크기, 배율, 안전 영역. 렌더 텍스처 크기가 물리 크기), 글자 배율, 테마(빈 이름이면 지금 테마)를 정합니다.
         * @details 테마가 바뀌면 그 화면의 스타일 시트만 다시 적용한다(게임 UI 의 테마는 그대로다).
         */
        void setOffscreenView( UIScreenHandle handle, const UIViewport& viewport, float32 textScale, const hashed_string& theme );

        // --- 핫 리로드 ---------------------------------------------------------------
        // 문서(`.ui.xml`)와 스타일 시트의 파일 변화는 **에디터**의 파일 감시가 캐시 다시 읽기로 이어 준다(`EditorAssetType` 표의 `kAnyCacheHoldingPath`).
        // 에디터 없이(`-EnableEditor` 없이) 띄운 App 은 파일을 고쳐도 반영되지 않는다 — 정상이다.
        /**
         * @brief 문서 @p documentPath 가 바뀌었을 때, 그 문서(또는 그것을 조각으로 쓰는 문서)로 연 화면을 새로 생성합니다(캐시의 다시 읽기 알림이 부릅니다).
         * @details 포커스 위젯과 스크롤 패널의 스크롤 오프셋을 이름으로 기억했다가 새 트리의 같은 이름에 복원한다. 생성이 실패하면 기존 트리를 그대로 둔다.
         *          화면 서술(레이어, 모달 등)은 바꾸지 않는다. 스택 순서가 흔들리지 않게 하기 위해서이며, 다음에 열 때 새 서술을 쓴다.
         */
        void onDocumentReloaded( string_view documentPath );
        /** @brief 스타일 시트 @p sheetPath 가 바뀌었을 때, 그 시트를 쓰는 화면의 스타일 시트를 다시 적용하고 위젯을 다시 맞춥니다(트리는 그대로). */
        void onStyleSheetReloaded( string_view sheetPath );

        // --- 애니메이션 ---------------------------------------------------------------
        /**
         * @brief 위젯 @p widget 의 프로퍼티 @p propertyPath 를 지금 값에서 @p endValue(텍스트 표기)까지 @p duration 초 동안 옮깁니다(코드 한 줄 트윈, Godot 의 `Tween` 에 해당).
         * @details 그 위젯이 속한 화면의 재생기(`UIScreen::getAnimationPlayer`)가 돌린다. 같은 위젯과 경로의 트윈은 새 것이 대신한다.
         * @return 위젯이 어느 화면에도 없거나 경로나 값을 읽지 못하면 경고하고 false 입니다.
         */
        bool tween( WidgetID widget, string_view propertyPath, string_view endValue, float32 duration, BlendCurve curve = BlendCurve::EaseOut );

        // --- 자막 ---------------------------------------------------------------
        /** @brief 자막입니다. 오버레이 레이어 아래 가운데에 최대 둘까지 표시하며, 사용자 설정의 끔/켬, 크기, 배경을 따릅니다. */
        UISubtitleService&       getSubtitles() { return _subtitles; }
        const UISubtitleService& getSubtitles() const { return _subtitles; }
        /** @brief 툴팁과 커서 모양입니다. 호버 경로의 위젯 설명 글을 머문 뒤 띄우고, 위젯 커서 모양을 OS 커서로 냅니다. */
        const UITooltipService& getTooltips() const { return _tooltips; }

        // --- 게임 쪽이 묻는 것 -----------------------------------------------------------
        /** @brief 모달이나 로딩 화면이 떠 있어 게임 입력을 막아야 하면 true 입니다(플레이어 컨트롤러가 의도를 0 으로 둡니다). */
        bool isGameInputBlocked() const;
        /** @brief Loading 레이어 화면(로딩 화면)이 떠 있으면 true 입니다(닫는 중인 화면은 제외). 자동화의 "씬 플레이 중" 판정은 이 값이 false 가 된 뒤에 합니다. */
        bool isLoadingScreenShown() const;
        /**
         * @brief 화면(로딩, HUD, 메뉴) 그리기와 포인터 입력을 막거나 해제합니다. 화면 상태(스택, 포커스)는 그대로이고, 오프스크린 미리보기는 계속 그립니다.
         * @details 게임 update 가 돌지 않는 동안(에디터 멈춤, Simulate) 호스트가 켠다. 그 화면을 닫을 게임 코드가 돌지 않으므로, 그리면 편집 화면을 덮은 채 남기 때문이다
         *          (언리얼 에디터 뷰포트도 PIE 전에는 UMG 위젯이 없다).
         */
        void setOnScreenSuppressed( bool bSuppressed ) { _bOnScreenSuppressed = bSuppressed ? SW_TRUE : SW_FALSE; }
        bool isOnScreenSuppressed() const { return _bOnScreenSuppressed == SW_TRUE; }
        /** @brief 활성 화면이 OS 커서를 필요로 하면 true 입니다(플레이어 컨트롤러가 마우스 잠금을 쉽니다). */
        bool wantsCursor() const;
        /**
         * @brief UI 입력 라우터입니다 — 포커스, 포인터, 소비된 입력(`isActionConsumed` · `consumeAction` · `getConsumption`), UI 액션 맵, 입력 방식.
         * @details 플레이어 컨트롤러가 의도를 만들 때 `getInputRouter().isActionConsumed( map, action )` 만 확인하면 된다. 입력 방식을 바꾸는 일은 화면 포커스를 함께
         *          옮기므로 `setInputMode` 가 맡는다.
         */
        UIInputRouter&       getInputRouter() { return _input; }
        const UIInputRouter& getInputRouter() const { return _input; }

        // --- 입력 방식 ---------------------------------------------------------------------
        /** @brief 입력 방식을 바꿉니다. 탐색으로 바뀌는 순간 활성 화면에 포커스가 없으면 기본 포커스로 옮깁니다. */
        void setInputMode( UIInputMode mode );

        InputManager*     getInputManager() const { return _input.getInputManager(); }
        FontService*      getFontService() const { return _pFontService; }
        const UIViewport& getViewport() const { return _viewport; }
        /** @brief 텍스트 측정 엔진입니다(글꼴이 없으면 nullptr). 레이아웃 컨텍스트가 넘깁니다. */
        TextLayoutEngine* getTextLayout() const { return _textLayout.get(); }

        /** @brief 해상도에서 배율을 구하는 규칙입니다. 기동 단계 `UI` 가 `engine/ui/uiscale.xml` 과 팩 설정 `_uiScaleSettings` 로 정합니다. */
        const UIScaleSettings& getScaleSettings() const { return _scaleSettings; }
        void                   setScaleSettings( const UIScaleSettings& settings ) { _scaleSettings = settings; }
        /**
         * @brief 물리 화면 크기로 이번 프레임의 UI 뷰포트를 만듭니다. 배율은 규칙 × gv_uiScale(설정이 켜면 × 창 배율)이고, 안전 영역은 gv_uiDebugSafeZone 입니다.
         * @param contentScale 창의 OS 배율입니다(`IWindow::getContentScale`).
         */
        UIViewport computeViewport( const float2& physicalSize, float32 contentScale ) const;
        /**
         * @brief 지금 뷰포트(`update` 가 받은 것), gv_uiTextScale, 텍스트 측정, 문화권 방향으로 레이아웃 컨텍스트를 만듭니다(`UILayoutPass::update` 에 넘깁니다).
         * @details 루트의 흐름 방향은 문화권이 오른쪽에서 왼쪽인지(`UILayoutPass::isCultureRightToLeft`)로 정한다. 문화권이 바뀌면 다음 순회에서 트리를 다시 배치한다.
         */
        UILayoutContext makeLayoutContext() const;
        /** @brief 지금 글리프 캐시, 텍스트 레이아웃, 배율, 프레임 번호로 그리기 컨텍스트를 만듭니다(그리기 순회에 넘깁니다). */
        UIPaintContext makePaintContext() const;
        /**
         * @brief 이번 프레임의 UI 그리기 목록입니다(물리 픽셀, 대상 크기는 뷰포트의 물리 크기). `Engine` 가 렌더 패킷의 주 출력 캔버스에 싣습니다.
         * @details 화면을 그리기 순서(레이어, 쌓인 순서)로 이어 붙이면서 화면마다 위젯 그리기 캐시를 연결하고, 탐색 입력 방식이면 포커스 테두리를 그 화면 위에 덧그린다.
         */
        const CanvasDrawList& getCanvas() const { return _canvas; }
        /** @brief 그리기 목록 내용이 바뀔 때만 오르는 번호입니다(1 부터 시작하며, 같으면 렌더러가 사각형을 다시 업로드하지 않습니다). */
        uint64 getCanvasRevision() const { return _canvasRevision; }
        /**
         * @brief 스택의 화면마다 레이아웃 덤프(`UILayoutDump`, 물리 픽셀)를 그리기 순서로 이어 씁니다. 화면마다 첫 줄은 `## <문서 경로 또는 (code)>` 입니다.
         * @details 실기동 테스트(`AppUITest`, 시나리오 단계 `UILayoutDump`)가 위젯 이름으로 스크린샷 안의 사각형을 찾는 경로다.
         */
        string makeLayoutDump() const;
        /**
         * @brief 문화권 출처를 정합니다. 기동(`initializeSubsystem`)이 엔진 서비스를 넘기고, 테스트는 자기 것을 넘겨 전역 문화권을 건드리지 않게 합니다.
         * @details nullptr 이면 문화권이 없다(기본 문화권 · 원문). 전역 표로 물러나 찾지 않는다.
         */
        void setLocalization( const LocalizationManager* pLocalization ) { _pLocalization = pLocalization; }
        /** @brief 설정 바인딩(`{setting:id}`)의 출처를 정합니다. 기동이 엔진 서비스를 넘기고 테스트는 자기 것을 넘깁니다. nullptr 이면 설정 바인딩이 없습니다. 화면 바인딩은 다시 연결됩니다. */
        void setUserSettings( UserSettingsManager* pSettings );

        const utf8* getModuleUnloadListenerName() const override { return "ui screens"; }
        /** @brief vtable 이 [@p pBegin, @p pEnd) 안에 있는 화면과 위젯을 가진 화면을 그 자리에서 닫습니다. 닫은 화면 수를 반환합니다. */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

    private:
        /** @brief 문서를 캐시에서 찾아 위젯 트리를 생성합니다. 스타일 시트는 문서와 조각(끼워 넣은 순서)의 것을 모읍니다. 실패하면 오류를 로그에 남기고 nullptr 를 반환합니다. */
        unique_ptr<Widget> instantiateDocument( string_view documentPath, UIScreenDesc& outDesc, vector<UIBindingDesc>& outListBinding, vector<string>& outListStyleSheet );
        /** @brief 문서로 생성한 화면에 문서 경로, 바인딩, 스타일 시트를 기록하고 올립니다. */
        UIScreenHandle pushDocumentScreen( unique_ptr<UIScreen> screen, string_view documentPath, vector<UIBindingDesc> listBinding, vector<string> listStyleSheet );
        /** @brief 화면의 스타일 시트를 테마 @p themeName(빈 이름이면 지금 테마) 시트, 화면 시트 순서로 다시 적용하고 트리 전체를 다시 맞추게 합니다. */
        void rebuildStyleSet( UIScreen& screen, const hashed_string& themeName = hashed_string{} );
        /** @brief 화면 @p screen 의 트리를 그 문서로 새로 생성합니다(포커스와 스크롤을 이름으로 이어 갑니다). 실패하면 기존 트리를 두고 false 를 반환합니다. */
        bool rebuildScreenFromDocument( UIScreen& screen );
        /** @brief 화면 문서(`_documentPath`)의 애니메이션을 재생기에 등록합니다. 재생 중인 애니메이션과 트윈은 멈춥니다(기존 트리의 위젯 번호를 가리키기 때문입니다). */
        void applyDocumentAnimations( UIScreen& screen );
        /** @brief 문서 @p documentPath 가 조각 연쇄로 @p usedPath 를 쓰는지(자기 자신 포함) 반환합니다. */
        bool isDocumentUsing( const string& documentPath, string_view usedPath );
        /** @brief 모듈 다시 로드로 닫은 문서 화면을 다시 엽니다(다음 `update` 앞). */
        void reopenClosedScreens();
        /**
         * @brief 바인딩 단계입니다. 텍스트 버전(현지화)이 바뀌었으면 글꼴 대체 연쇄를 비우고 모든 위젯에 알린 뒤(`Widget::onTextRevisionChanged`),
         *        화면마다 바인딩 집합을 돌립니다. 연결되지 않았으면 연결하고, 바뀐 소스의 필드만 씁니다. 텍스트 버전이 바뀌면 형식 바인딩도 다시 평가합니다.
         */
        void updateBindings();
        /** @brief 입력 장치 종류(글리프)가 바뀌었으면 화면 위젯마다 `onInputGlyphsChanged` 를 부릅니다(액션 태그가 있는 텍스트를 다시 배치합니다). */
        void refreshInputGlyphs();
        /** @brief 지금 문화권 출처입니다. `setLocalization` 으로 받은 것(기동이 엔진 서비스를 넘긴다)이고, 받지 않았으면 nullptr 입니다. */
        const LocalizationManager* findLocalization() const;
        /** @brief 화면을 그리기 순서로 칠해 그리기 목록을 만들고, 내용이 바뀌었으면 번호를 올립니다. */
        void paintScreens();
        /** @brief 화면 마커 앵커(UI 단위)를 이번 프레임 배율의 물리 픽셀 정수로 바꿔 `getCanvasAnchors` 표로 냅니다. */
        void publishCanvasAnchors();
        /** @brief 사용자 설정 `gv_uiTheme`(accessibility.uiTheme)이 바뀌었으면 그 테마로 바꿉니다(빈 값이거나 테마 목록이 없으면 그대로 둡니다). */
        void syncThemeSetting();
        /** @brief 등록된 화면 스위치(`UIScreenSwitches`, 견본 · 벤치 · 옵션 메뉴 등)를 순서대로 돌려 개발 스위치에 맞게 화면을 열고 닫습니다. */
        void syncScreenSwitches();
        /** @brief 닫기를 요청한 화면을 지웁니다. */
        void applyPendingCloses();
        /** @brief 닫는 중이 아닌 화면마다 `UIScreen::onTick` 을 부릅니다(틱이 화면을 올리고 닫아도 되도록 번호로 돕니다). */
        void updateScreens( float32 deltaSeconds );
        /** @brief 화면 @p index 를 바로 지웁니다(포인터와 포커스가 그 트리를 놓게 합니다). */
        void destroyScreenAt( uint32 index );
        /** @brief 활성 화면과 게임 정지를 다시 정합니다(화면을 올리고 지운 뒤 부릅니다). */
        void refreshActiveScreen();

    private:
        /** @struct SwitchScreen @brief 화면 스위치 하나가 연 화면입니다. 스위치 이름(등록할 때의 이름 사본)으로 찾아, 모듈이 다시 올라와도 같은 화면을 이어 갑니다. */
        struct SwitchScreen
        {
            string         _name{};
            UIScreenHandle _screen{ kInvalidUIScreenHandle };
        };

        vector<unique_ptr<UIScreen>> _listScreen; ///< 그리기 순서(레이어, 쌓인 순서)로 정렬한 목록입니다. 입력은 역순으로 처리합니다.
        UIOffscreenScreens           _offscreen;  ///< 스택 밖의 화면입니다(에디터 미리보기이며 입력이 없습니다).
        UIDocumentCache              _documentCache;
        UIStyleSheetCache            _styleSheetCache;
        UIThemeController            _theme;
        vector<string>               _listReopenDocument; ///< 모듈 다시 로드로 닫은 문서 화면입니다(다음 update 가 다시 엽니다).
        UIBindingConverterRegistry   _bindingConverters;
        UIClipboardService           _clipboard;      ///< 글 입력 위젯의 복사 · 붙여넣기 창구입니다.
        UIGlobalCommandRegistry      _globalCommands; ///< 화면이 처리하지 않은 명령을 받는 전역 표입니다.
        UINotificationService        _notifications;
        UIActionGlyphSource          _actionGlyphs; ///< 입력 관리자와 UI 맵에서 가져오는 글리프 출처입니다(초기화 때 채웁니다).
        UIInputRouter                _input;        ///< 포커스, 포인터, 소비된 입력, UI 액션 맵, 입력 방식입니다.
        FontService*                 _pFontService;
        const LocalizationManager*   _pLocalization; ///< 문화권 출처입니다(기동이 엔진 서비스를 넘긴다, nullptr 이면 없음).
        UserSettingsManager*         _pUserSettings; ///< 설정 바인딩 출처입니다(기동이 엔진 서비스를 넘긴다, nullptr 이면 없음).
        unique_ptr<TextLayoutEngine> _textLayout;
#if SW_DEV_COMMANDS_ENABLED
        unique_ptr<DebugHUD> _debugHUD; ///< 서브시스템이 붙인 디버그 HUD 입니다(Dev 전용).
#endif
        UIScaleSettings           _scaleSettings;
        UISubtitleService         _subtitles;
        UITooltipService          _tooltips;
        UIViewport                _viewport;
        CanvasDrawList            _canvas;              ///< 이번 프레임의 그리기 목록입니다.
        CanvasDrawList            _canvasScratch;       ///< 칠하는 중인 목록입니다(같은 내용이면 버립니다).
        uint64                    _canvasRevision;      ///< `_canvas` 내용의 번호입니다.
        vector<const WidgetTree*> _listPaintedTree;     ///< 지난 그리기의 화면 트리 순서입니다(같고 모두 재사용이면 이어 붙이기를 건너뛴다).
        uint8                     _bLastCanvasReusable; ///< 지난 그리기 목록이 트리 출력만으로 이루어졌다(포커스 테두리 · 숨김 없음).
        UIScreenHandle            _activeScreen;
        UIScreenHandle            _markerScreen;         ///< 화면 마커를 담는 HUD 화면입니다(없으면 무효).
        vector<CanvasAnchor>      _listMarkerAnchor;     ///< 화면 마커의 앵커입니다(번호 = 마커 위젯의 그리기 앵커, UI 단위).
        vector<CanvasAnchor>      _listCanvasAnchor;     ///< 그리기 단계가 낸 앵커입니다(물리 픽셀 정수, `getCanvasAnchors`).
        vector<uint32>            _listFreeMarkerAnchor; ///< 뗀 마커가 돌려준 앵커 번호입니다(다음 마커가 다시 쓴다).
        vector<WidgetComponent*>  _listWidgetComponent;  ///< 등록된 위젯 컴포넌트입니다(소유하지 않으며, 끝날 때 스스로 뺍니다).
        vector<UIScreenHandle>    _listTickScratch;      ///< `tickScreens` 가 도는 번호입니다(프레임마다 다시 씁니다).
        vector<SwitchScreen>      _listSwitchScreen;     ///< 화면 스위치(`UIScreenSwitches`)마다 연 화면입니다(이름으로 찾습니다).
        UIEngineDocuments         _engineDocuments;      ///< 엔진 UI 문서 경로입니다(`setEngineDocuments`).
        string                    _optionsMenuDocument;  ///< 옵션 메뉴 문서입니다.
        string                    _pauseMenuDocument;    ///< 일시정지 메뉴 문서입니다(비면 일시정지 메뉴가 없습니다).
        UIScreenHandle            _nextScreenHandle;
        uint32                    _nextPushOrder;
        uint32                    _textRevision;            ///< 마지막 바인딩 단계가 본 텍스트 버전입니다.
        InputGlyphStyle           _glyphStyle;              ///< 마지막으로 본 입력 장치 종류입니다(`refreshInputGlyphs`).
        uint8                     _bPauseRequested     : 1; ///< 게임 정지 요청을 걸어 두었는지 나타냅니다(정지 화면이 하나라도 있으면 true).
        uint8                     _bPendingClose       : 1; ///< 닫기를 요청한 화면이 있는지 나타냅니다.
        uint8                     _bTextRevisionKnown  : 1; ///< `_textRevision` 을 한 번 읽었는지 나타냅니다.
        uint8                     _bGlyphStyleKnown    : 1; ///< `_glyphStyle` 을 한 번 읽었는지 나타냅니다.
        uint8                     _bOnScreenSuppressed : 1; ///< 화면 그리기와 포인터 입력을 막는지 나타냅니다(`setOnScreenSuppressed`).
    };
} // namespace sw
