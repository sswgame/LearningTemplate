/**
 * @file UISystem.h
 * @brief 런타임 UI 의 엔진 서비스입니다. 화면 스택, 입력에서 UI 이벤트로의 변환, 갱신 순서를 담당합니다.
 */
#pragma once
#include "Core/Console/DevCommandsEnabled.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Document/UIBindingDesc.h"
#include "Engine/UI/Input/UIActionGlyphSource.h"
#include "Engine/UI/Input/UIInputRouter.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Render/UICanvasPaintState.h"
#include "Engine/UI/Render/UIPaintPass.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UIEngineDocuments.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    class FontService;
    class InputManager;
    class InputMap;
    class LocalizationManager;
    class TextLayoutEngine;
    class UIDocumentCache;
    class UIStyleSheetCache;
    class UIThemeController;
    class UserSettingsManager;
    class GameObjectManager;

} // namespace sw

namespace sw
{
    /** @brief UI 갱신(`UISystem::update`)의 정해진 자리에서 도는 UI 서비스의 함수 칸입니다. 인자는 실제 프레임 시간(초)입니다. */
    using UIUpdateDelegate = MulticastDelegate<void( float32 deltaSeconds )>;

    /**
     * @class UISystem
     * @brief 런타임 UI 의 엔진 서비스입니다(`engine::getSubsystem<UISystem>()`, 게임은 `game::findSubsystem<UISystem>()`).
     * @details 언리얼의 `FSlateApplication` 과 CommonUI 의 `UCommonUIActionRouter` 에 해당한다.
     *          틱은 둘로 나뉜다. `processInput` 은 입력 갱신 직후, 게임 틱 앞에서 돌아서 UI 가 처리한 입력을 폰이 보지 못하게 한다.
     *          `update` 는 게임 틱 뒤, 렌더 패킷 앞에서 돌며 이번 프레임의 게임 상태로 애니메이션, 바인딩, 스타일, 레이아웃, 그리기를 순서대로 처리한다.
     *          엔진에서는 서브시스템 `UI`(Client 대상)입니다 — 배율 규칙 · 기본 메뉴를 엔진 데이터(팩 설정 `PackEngineSettings` 가 덮어씀)로 정하고
     *          문서 · 스타일 시트 캐시(서브시스템 `UIDocuments` · `UIStyleSheets`)의 다시 읽기 알림을 받으며, Dev 에서는 디버그 HUD 를 붙입니다(`initializeSubsystem`).
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
     *          핫 리로드: 언로드되는 모듈 라이브러리에 vtable 이 있는 화면과 위젯(게임이나 키트가 만든 타입)을 가진 화면은 그 자리에서 닫는다. 게임 스레드에서만 호출한다.
     *
     *          **나눈 책임**(값 멤버, 비가상 직접 호출): 입력 라우팅은 `UIInputRouter`, 테마 고르기와 스타일 세트 짓기는 `UIThemeController`,
     *          그리기 목록과 재사용 판정 상태 · 레이어 숨김 요청 수는 `UICanvasPaintState`,
     *          화면 스택과 프레임 순서는 이 클래스에 남는다(순서는 `processInput` 과 `update` 본문이 기준이다).
     *          **따로 선 서브시스템**(값 멤버가 아니다 — `engine::getSubsystem<T>()`): 클립보드 `UIClipboardService` · 전역 명령 표 `UIGlobalCommandRegistry` ·
     *          바인딩 변환기 `UIBindingConverterRegistry`(이 시스템이 의존), 알림 `UINotificationService` · 자막 `UISubtitleService` · 툴팁 `UITooltipService`
     *          (이 시스템에 의존하고 UI 갱신 칸 `onUpdateBeforeScreens` · `onUpdateAfterScreens` 에 건다).
     */
    class SW_API UISystem final : public ISubsystem
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
        [[nodiscard]] bool initialize( InputManager& inputManager, FontService* pFontService, const AssetID& assetID = {} );
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
        /** @brief 스택의 화면들입니다(그리기 순서 — 레이어, 쌓인 순서. 입력은 역순). 화면 스택 위의 서비스가 읽기만 합니다(접근성 트리 뽑기). */
        const vector<unique_ptr<UIScreen>>& getScreens() const { return _listScreen; }

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
        /** @brief `[action=이름]` 태그의 글리프 출처입니다(게임 입력 맵, UI 맵, 지금 장치 종류). */
        const UIActionGlyphSource& getActionGlyphs() const { return _actionGlyphs; }

        // --- 스택 밖 화면(오프스크린 화면 `UIOffscreenScreens` 가 쓴다) -------------------------------------------
        /**
         * @brief 문서 @p documentPath 로 화면을 생성하되 스택에 올리지 않습니다. 핸들(스택 화면과 같은 번호 줄)을 주고, 문서 애니메이션을 등록하고(틀지 않는다),
         *        지금 테마로 스타일 세트를 짓습니다.
         * @details 문서 생성은 스택 화면과 같은 경로다(`openScreen`). 입력 · 포커스 · 틱은 없다 — 그리기는 받은 쪽이 한다.
         * @return 읽지 못하거나 생성할 수 없으면 nullptr 이며 오류를 로그에 남깁니다.
         */
        unique_ptr<UIScreen> createDocumentScreen( string_view documentPath );
        /** @brief 화면 @p screen 의 트리를 그 문서로 새로 생성합니다(포커스와 스크롤을 이름으로 이어 갑니다). 실패하면 기존 트리를 두고 false 를 반환합니다. 스택 밖 화면도 같은 길이다. */
        bool rebuildScreenFromDocument( UIScreen& screen );
        /** @brief 문서 @p documentPath 가 조각 연쇄로 @p usedPath 를 쓰는지(자기 자신 포함) 반환합니다. */
        bool isDocumentUsing( const string& documentPath, string_view usedPath );

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

        // --- 화면 스택 위의 UI 서비스 -------------------------------------------------------
        /**
         * @brief 화면 틱 **앞**에서 도는 UI 서비스의 칸입니다(자막 `UISubtitleService` · 툴팁 `UITooltipService`). 인자는 실제 프레임 시간입니다.
         * @details 화면 스택 위에서 자기 화면을 여닫는 서비스는 이 시스템의 값 멤버가 아니라 서브시스템이다(UI 에 의존 — `engine::getSubsystem<UINotificationService>()` 처럼
         *          찾는다). 기동에서 이 칸에 걸고 종료에서 푼다 — 언리얼 Slate 의 `OnPreTick` · Godot `SceneTree.process_frame` 신호와 같은 자리다.
         *          화면 그리기를 막은 동안(`isOnScreenSuppressed`)에도 불린다 — 포인터를 보는 서비스(툴팁)는 스스로 거른다.
         */
        UIUpdateDelegate& onUpdateBeforeScreens() { return _onUpdateBeforeScreens; }
        /** @brief 화면 틱 **뒤**에서 도는 UI 서비스의 칸입니다(알림 `UINotificationService` — 이번 프레임에 화면이 올린 알림까지 본다). */
        UIUpdateDelegate& onUpdateAfterScreens() { return _onUpdateAfterScreens; }
        /**
         * @brief 스타일 뒤, 레이아웃 **앞**에서 도는 UI 서비스의 칸입니다(위젯 컴포넌트 `UIWidgetComponentService` 의 화면 마커 투영 — 같은 프레임에 배치된다).
         * @details 게임 틱과 트랜스폼 적용 뒤의 월드 점을 이번 뷰포트(`getViewport`)로 투영하는 자리다. 화면 그리기를 막은 동안에도 불린다.
         */
        UIUpdateDelegate& onUpdateBeforeLayout() { return _onUpdateBeforeLayout; }
        /**
         * @brief 화면 스택을 다 그린 **뒤**에서 도는 UI 서비스의 칸입니다(오프스크린 화면 `UIOffscreenScreens` — 이번 프레임의 레이아웃 · 그리기 컨텍스트로 자기 화면을 그린다).
         * @details 화면 그리기를 막은 동안(`isOnScreenSuppressed`)에도 불린다 — 스택 밖 화면은 계속 그린다. 언리얼 Slate `OnPostTick` 자리다.
         */
        UIUpdateDelegate& onUpdateAfterPaint() { return _onUpdateAfterPaint; }
        /**
         * @brief 입력 처리(`processInput`)에서 화면 위젯의 포인터를 보낸 **뒤**에 도는 칸입니다(위젯 컴포넌트 서비스 — 화면 위젯 아래가 아닌 마우스 이벤트를 월드 광선으로).
         * @details 이번 프레임의 포인터 이벤트는 `getInputRouter().getFramePointerEvents()` 다. 화면 그리기를 막은 동안(`isOnScreenSuppressed`)에는 포인터를 보내지 않으므로 이 칸도 불리지 않는다.
         */
        UIUpdateDelegate& onPointerProcessed() { return _onPointerProcessed; }

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
        /**
         * @brief 레이어 집합 @p layers 의 화면을 숨기는 요청을 하나 겁니다(포토 모드의 "UI 없이 보기" 같은 곳). 요청은 레이어마다 세어 쌓이므로 둘이 같은 레이어를 숨겨도 하나를 풀 때 다시 보이지 않습니다.
         * @details `GameTimeScale::addPauseRequest` 와 같은 모양입니다. 숨긴 레이어의 화면은 그리지 않고 포인터를 받지 않으며(`UIScreen::isHiddenByLayer`), 화면 상태 · 포커스 · 틱 · 애니메이션은 그대로이고
         *          요청 뒤에 열린 화면도 숨깁니다. 엔진은 레이어 가시성 기능만 주고 어느 레이어를 숨길지는 거는 쪽의 데이터가 정합니다(Godot `CanvasLayer.visible`, 언리얼 `AHUD::bShowHUD`, 유니티 `Canvas.enabled`).
         *          건 쪽이 같은 집합으로 `removeLayerHideRequest` 를 꼭 부릅니다.
         */
        void addLayerHideRequest( UILayerMask layers );
        /** @brief `addLayerHideRequest` 로 건 요청을 하나 풉니다. 건 적 없는 레이어를 풀면 단언으로 멈춥니다. */
        void removeLayerHideRequest( UILayerMask layers );
        /** @brief 레이어 @p layer 에 숨김 요청이 하나라도 걸려 있으면 true 입니다. */
        bool isLayerHidden( UILayer layer ) const { return _paintState.isLayerHidden( layer ); }
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
        const CanvasDrawList& getCanvas() const { return _paintState.getCanvas(); }
        /** @brief 그리기 목록 내용이 바뀔 때만 오르는 번호입니다(1 부터 시작하며, 같으면 렌더러가 사각형을 다시 업로드하지 않습니다). */
        uint64 getCanvasRevision() const { return _paintState.getCanvasRevision(); }
        /**
         * @brief 지난 그리기가 글리프 래스터화 예산(`gv_uiGlyphRasterBudgetMicros`) 때문에 못 그리고 다음 프레임으로 미룬 글리프 수입니다. 0 이면 화면의 글자가 다 그려졌다.
         * @details 지난 그리기를 그대로 재사용한 프레임은 0 이다(미룬 위젯은 다시 무효화되므로 재사용할 수 없다). 자동화 감시 값 `UI.DeferredGlyphs` 가 읽는다.
         */
        uint32 getDeferredGlyphCount() const { return _paintState.getDeferredGlyphCount(); }
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

        /**
         * @brief 클래스(`UIScreen::getTypeInfo`)나 위젯(`Widget::getTypeInfo`)이 @p moduleName 의 타입인 화면을 그 자리에서 닫습니다. 닫은 화면 수를 반환합니다.
         * @details 모듈을 내리기 전의 알림(`TypeRegistry::subscribeModuleUnloading`)이 부른다. 문서로 연 기본 화면은 다음 update 가 다시 연다(새 이미지의 위젯 타입으로).
         *          뷰모델은 게임이 소유해 게임과 함께 사라지고, 사라질 때 바인딩이 놓는다 — 여기서 볼 것이 없다.
         */
        uint32 closeScreensOfModule( string_view moduleName );

    private:
        /** @brief 문서를 캐시에서 찾아 위젯 트리를 생성합니다. 스타일 시트는 문서와 조각(끼워 넣은 순서)의 것을 모읍니다. 실패하면 오류를 로그에 남기고 nullptr 를 반환합니다. */
        /** @brief 모듈 내림 알림(`TypeRegistry::subscribeModuleUnloading`)입니다 — `closeScreensOfModule` 을 부릅니다. */
        void               onModuleUnloadingNotice( string_view moduleName ) { (void)closeScreensOfModule( moduleName ); }
        unique_ptr<Widget> instantiateDocument( string_view documentPath, UIScreenDesc& outDesc, vector<UIBindingDesc>& outListBinding, vector<string>& outListStyleSheet );
        /** @brief 문서로 생성한 화면에 문서 경로, 바인딩, 스타일 시트를 기록하고 올립니다. */
        UIScreenHandle pushDocumentScreen( unique_ptr<UIScreen> screen, string_view documentPath, vector<UIBindingDesc> listBinding, vector<string> listStyleSheet );
        /** @brief 화면의 스타일 시트를 테마 @p themeName(빈 이름이면 지금 테마) 시트, 화면 시트 순서로 다시 적용하고 트리 전체를 다시 맞추게 합니다. */
        void rebuildStyleSet( UIScreen& screen, const hashed_string& themeName = hashed_string{} );
        /** @brief 테마가 바뀐 알림(`UIThemeController::onThemeChanged`)입니다 — 모든 화면의 스타일 시트를 다시 적용합니다. */
        void onThemeChanged();
        /** @brief 화면 문서(`_documentPath`)의 애니메이션을 재생기에 등록합니다. 재생 중인 애니메이션과 트윈은 멈춥니다(기존 트리의 위젯 번호를 가리키기 때문입니다). */
        void applyDocumentAnimations( UIScreen& screen );
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
        /** @brief 화면마다 `_bHiddenByLayer` 를 `_paintState` 의 숨긴 레이어 집합에 맞춥니다. */
        void refreshHiddenLayers();
        /** @brief 화면을 그리기 순서로 칠해 그리기 목록을 만들고, 내용이 바뀌었으면 번호를 올립니다. */
        void paintScreens();
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

        vector<unique_ptr<UIScreen>> _listScreen;                     ///< 그리기 순서(레이어, 쌓인 순서)로 정렬한 목록입니다. 입력은 역순으로 처리합니다.
        UIDocumentCache*             _pDocumentCache;                 ///< 문서 캐시(서브시스템 `UIDocuments` — 기동이 찾는다)
        UIStyleSheetCache*           _pStyleSheetCache;               ///< 스타일 시트 캐시(서브시스템 `UIStyleSheets` — 기동이 찾는다)
        Subscription                 _uiKeyBindingRegistration;       ///< UI 행동 맵의 키 바인딩 대상 구독(`UserSettingsManager::addKeyBindingInputMap`)
        Subscription                 _documentReloadedSubscription;   ///< 문서 캐시의 다시 읽기 알림 칸 구독(`initialize` 가 걸고 `shutdown` 이 놓는다)
        Subscription                 _styleSheetReloadedSubscription; ///< 스타일 시트 캐시의 다시 읽기 알림 칸 구독
        Subscription                 _moduleUnloadingSubscription;    ///< 모듈 내림 알림 구독(`initializeSubsystem` 이 걸고 `shutdownSubsystem` 이 놓는다)
        UIThemeController*           _pThemes;                        ///< 테마(서브시스템 `UIThemes` — 기동이 찾는다)
        Subscription                 _themeChangedSubscription;       ///< 테마가 바뀐 알림 구독(`initialize` 가 걸고 `shutdown` 이 놓는다)
        vector<string>               _listReopenDocument;             ///< 모듈 다시 로드로 닫은 문서 화면입니다(다음 update 가 다시 엽니다).
        UIUpdateDelegate             _onUpdateBeforeScreens;          ///< 화면 틱 앞의 UI 서비스(자막 · 툴팁)
        UIUpdateDelegate             _onUpdateAfterScreens;           ///< 화면 틱 뒤의 UI 서비스(알림)
        UIUpdateDelegate             _onUpdateBeforeLayout;           ///< 레이아웃 앞의 UI 서비스(화면 마커)
        UIUpdateDelegate             _onUpdateAfterPaint;             ///< 화면 그리기 뒤의 UI 서비스(화면 마커 앵커 · World 위젯 · 오프스크린 화면)
        UIUpdateDelegate             _onPointerProcessed;             ///< 포인터 처리 뒤의 UI 서비스(World 위젯 광선)
        UIActionGlyphSource          _actionGlyphs;                   ///< 입력 관리자와 UI 맵에서 가져오는 글리프 출처입니다(초기화 때 채웁니다).
        UIInputRouter                _input;                          ///< 포커스, 포인터, 소비된 입력, UI 액션 맵, 입력 방식입니다.
        FontService*                 _pFontService;
        const LocalizationManager*   _pLocalization; ///< 문화권 출처입니다(기동이 엔진 서비스를 넘긴다, nullptr 이면 없음).
        UserSettingsManager*         _pUserSettings; ///< 설정 바인딩 출처입니다(기동이 엔진 서비스를 넘긴다, nullptr 이면 없음).
        unique_ptr<TextLayoutEngine> _textLayout;
        UIScaleSettings              _scaleSettings;
        UIViewport                   _viewport;
        UICanvasPaintState           _paintState; ///< 그리기 목록, 재사용 판정 상태, 레이어 숨김 요청 수입니다.
        UIScreenHandle               _activeScreen;
        vector<UIScreenHandle>       _listTickScratch;     ///< `tickScreens` 가 도는 번호입니다(프레임마다 다시 씁니다).
        vector<SwitchScreen>         _listSwitchScreen;    ///< 화면 스위치(`UIScreenSwitches`)마다 연 화면입니다(이름으로 찾습니다).
        UIEngineDocuments            _engineDocuments;     ///< 엔진 UI 문서 경로입니다(`setEngineDocuments`).
        string                       _optionsMenuDocument; ///< 옵션 메뉴 문서입니다.
        string                       _pauseMenuDocument;   ///< 일시정지 메뉴 문서입니다(비면 일시정지 메뉴가 없습니다).
        UIScreenHandle               _nextScreenHandle;
        uint32                       _nextPushOrder;
        uint32                       _textRevision;            ///< 마지막 바인딩 단계가 본 텍스트 버전입니다.
        uint32                       _converterRevision;       ///< 마지막 바인딩 단계가 본 변환기 표 번호입니다(`UIBindingConverterRegistry::getRevision`).
        InputGlyphStyle              _glyphStyle;              ///< 마지막으로 본 입력 장치 종류입니다(`refreshInputGlyphs`).
        uint8                        _bPauseRequested     : 1; ///< 게임 정지 요청을 걸어 두었는지 나타냅니다(정지 화면이 하나라도 있으면 true).
        uint8                        _bPendingClose       : 1; ///< 닫기를 요청한 화면이 있는지 나타냅니다.
        uint8                        _bTextRevisionKnown  : 1; ///< `_textRevision` 을 한 번 읽었는지 나타냅니다.
        uint8                        _bGlyphStyleKnown    : 1; ///< `_glyphStyle` 을 한 번 읽었는지 나타냅니다.
        uint8                        _bOnScreenSuppressed : 1; ///< 화면 그리기와 포인터 입력을 막는지 나타냅니다(`setOnScreenSuppressed`).
    };
} // namespace sw
