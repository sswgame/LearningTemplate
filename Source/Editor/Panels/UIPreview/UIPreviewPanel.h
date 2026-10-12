/**
 * @file UIPreviewPanel.h
 * @brief 런타임 UI 문서(`*.ui.xml`) 미리보기 창입니다 — 해상도 견본 · UI/글자 배율 · 안전 영역 · 테마 · 언어 · 레이아웃 사각형 · 위젯 트리 선택 ·
 *        고른 위젯의 프로퍼티 편집과 저장 · 입력 흉내(마우스, 탐색 방향) · 애니메이션 미리 보기.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/UIPreview/UIPreviewLogic.h"

#include "Engine/UI/Base/UIFocusManager.h"
#include "Engine/UI/Base/UIPointerState.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    struct PropertyInfo;

    class IRHIDevice;
    class LocalizationManager;
    class UISystem;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct UIPreviewProbe
     * @brief 패널이 마지막으로 그린 프레임에서 본 값입니다(자동화 탐침 `Editor.UIPreview*` 가 읽는다). 창이 닫혔거나 문서가 없으면 `_bValid` 가 false 입니다.
     */
    struct UIPreviewProbe
    {
        bool    _bValid{ false };
        bool    _bDirty{ false };
        bool    _bRightToLeft{ false };      ///< 루트 위젯이 오른쪽에서 왼쪽 흐름으로 배치됐는지
        bool    _bHoveredFocusable{ false }; ///< 포인터 아래 위젯이 포커스를 받는 종류(버튼 …)인지
        bool    _bSelectedValid{ false };    ///< 고른 위젯이 있는지
        bool    _bAnimationPlaying{ false };
        bool    _bTextureReady{ false };      ///< 렌더 텍스처가 ImGui 에 등록돼 그림이 보이는지
        bool    _bReloadedAfterSave{ false }; ///< 저장한 뒤 핫 리로드가 문서를 다시 읽어 트리를 새로 지었는지(저장이 파일까지 갔다는 증거)
        uint32  _widgetCount{ 0 };
        uint32  _firstTextBytes{ 0 };     ///< 문서 순서로 첫 글 위젯의 화면 글자 UTF-8 바이트 수(언어를 바꾸면 길이가 달라진다)
        int32   _hoveredOrdinal{ -1 };    ///< 포인터 아래 위젯의 문서 순서 번호, 없으면 -1
        int32   _focusedOrdinal{ -1 };    ///< 포커스 위젯의 문서 순서 번호, 없으면 -1
        float32 _selectedOpacity{ 0.0f }; ///< 고른 위젯의 불투명도
        float32 _animationTime{ 0.0f };   ///< 고른 애니메이션의 지금 시각(재생 중이 아니면 0)
        uint32  _languageIndex{ 0 };      ///< 언어 콤보에서 고른 줄(0 = 게임 언어)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class UIPreviewPanel
     * @brief UI 문서를 게임 UI 와 따로 **오프스크린 화면**(`UISystem::openOffscreenScreen` — 렌더 텍스처 대상 캔버스)으로 지어 ImGui 이미지로 봅니다
     *        (유니티 UI Builder · UMG 디자이너의 미리보기 자리).
     * @details 해상도 견본은 게임과 같은 배율 규칙(`UISystem::getScaleSettings`)으로 뷰포트를 만들고, UI 배율 · 글자 배율 · 안전 영역 · 테마 · 언어는 이 미리보기에만 겁니다.
     *          언어는 문화권이 전역이라 게임 UI 를 건드리지 않도록 **관리자 사본**(`LocalizationManager::copyStateFrom`)을 이 패널이 소유하고 화면에 겁니다(`qps-ploc`, `qps-plocm` 포함).
     *          문서를 고치면 핫 리로드가 미리보기도 다시 짓습니다. 레이아웃 사각형 · 고른 위젯은 ImGui 그리기 목록으로 그림 위에 얹습니다(캔버스를 바꾸지 않는다).
     *
     *          고른 위젯의 프로퍼티는 `EditorPropertyGrid` 로 고치고, 고칠 때마다 되돌리기 항목이 하나 쌓입니다(`EditorTransaction::push`). Ctrl+S 나 Save 는 `UIDocumentWriter` 로
     *          문서를 다시 씁니다 — 파일 감시가 미리보기를 다시 짓는다. 입력 흉내(마우스 호버 · 클릭 · 탐색 방향)와 애니메이션 재생은 미리보기 트리의 값을 바꾸므로, 그 뒤에는
     *          Reload 로 문서 값을 되찾을 때까지 저장을 막는다(바뀐 슬라이더 값 · 애니메이션 중간 값이 문서에 쓰이지 않게 한다).
     *          판단(뷰포트 · 맞춤 · 점 옮기기 · 트리 줄 · 언어 목록 · 속성 텍스트)은 `UIPreviewLogic`(ImGui 없음 — EditorTest).
     */
    class UIPreviewPanel : public IEditorPanel
    {
    public:
        UIPreviewPanel();
        ~UIPreviewPanel() override;

        const utf8* getPanelTitle() const override { return "UI Preview"; }
        void        drawContent() override;
        float2      getInitialPanelSize() const override { return float2{ 1100.0f, 680.0f }; }
        bool        isToolPanel() const override { return true; }
        /** @brief 미리보기 화면을 닫고 ImGui 텍스처를 놓습니다. */
        void shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 마지막으로 그린 프레임의 값입니다(자동화 탐침). */
        const UIPreviewProbe& getProbe() const { return _probe; }

    protected:
        /** @brief 고친 문서를 `UIDocumentWriter` 로 파일에 씁니다. 입력 흉내나 애니메이션으로 트리 값이 바뀐 상태면 `canSaveDocument` 가 막습니다. */
        [[nodiscard]] bool saveDocument() override;
        [[nodiscard]] bool canSaveDocument() const override;
        /** @brief 디스크 문서로 미리보기를 다시 짓고 고친 값을 버립니다. */
        void revertDocument() override;

    private:
        /** @brief 되돌리기 항목이 패널을 가리키는 길입니다. 패널이 닫히면 끊어, 옛 항목이 사라진 패널에 값을 쓰지 않게 합니다. */
        struct EditSession;

        /** @brief 프로퍼티 하나의 기준 값입니다. */
        struct BaselineValue
        {
            string _name{};
            string _text{};
        };

        /** @brief 콘텐츠 브라우저에서 연 문서(작업공간의 포커스)가 바뀌었으면 그것을 미리볼 문서로 정합니다. */
        void syncFocusedDocument();
        /** @brief 문서 · 뷰포트가 바뀌었으면 오프스크린 화면을 다시 열고(크기가 바뀌면 렌더 텍스처 경로도), 이번 프레임의 보기를 겁니다. */
        void syncPreview( UISystem& ui );
        /** @brief 화면 트리가 새로 지어졌으면(핫 리로드) 고른 위젯 · 포인터 · 포커스를 새 트리로 이어 갑니다. */
        void syncTree( UIScreen& screen );
        /** @brief 고른 언어에 맞는 문화권 관리자 사본을 만들고(게임 텍스트가 바뀌면 다시) 화면에 겁니다. */
        void syncLanguage( UISystem& ui );
        /** @brief 렌더 텍스처를 ImGui 텍스처로 등록합니다(핸들이 바뀌면 다시). */
        void syncTexture();
        void releaseTexture();
        void closePreview();
        void drawToolbar( UISystem& ui );
        void drawLanguageCombo();
        void drawInteractionBar( UIScreen& screen );
        void drawAnimationBar( UIScreen& screen );
        void drawWidgetList( const UIScreen& screen );
        void drawInspector( UIScreen& screen );
        void drawImage( UIScreen& screen );
        /** @brief 입력 흉내를 이번 프레임의 ImGui 마우스로 미리보기 트리에 넣습니다(호버 · 누름 · 뗌 · 휠). */
        void feedPointer( UIScreen& screen, const float2& drawSize, const float2& origin, bool bHovered );
        /** @brief 탐색 방향(`UINavigationDirection` 값)으로 포커스를 옮깁니다. 포커스가 없으면 첫 포커스 위젯을 잡습니다. 옮겼으면 true 입니다. */
        bool navigateFocus( UIScreen& screen, uint32 direction );
        /** @brief 포커스 위젯 가운데를 누른 것처럼 포인터 이벤트를 보냅니다(확인 입력 흉내). */
        void acceptFocus( UIScreen& screen );
        /** @brief 입력 흉내나 애니메이션이 트리 값을 바꿨음을 기록합니다(저장을 막고 Reload 로 되찾게 합니다). */
        void markPreviewMutated();
        /** @brief 고른 위젯의 프로퍼티 값을 기준으로 잡아 둡니다 — 그리드가 값을 바꿨을 때 이전 값을 알기 위해서입니다. */
        void captureBaseline( const Widget& widget );
        /** @brief 그리드가 프로퍼티 하나를 고쳤다고 알릴 때 되돌리기 항목을 남깁니다. */
        void onPropertyEdited( const PropertyInfo& prop );
        /** @brief 되돌리기와 다시 하기가 부릅니다 — 문서 순서 번호 @p ordinal 의 위젯 프로퍼티 @p propertyName 에 @p text 를 씁니다. 트리를 새로 지은 뒤의 항목(@p generation 이 다르다)은 버립니다. */
        void applyEditedText( uint32 generation, uint32 ordinal, const string& propertyName, const string& text );
        void updateProbe( const UIScreen* pScreen );

    private:
        vector<UIPreviewWidgetRow>      _listRow;
        vector<string>                  _listLanguage; ///< 언어 콤보의 코드(첫 줄 = 게임 언어)
        vector<BaselineValue>           _listBaseline; ///< 고른 위젯의 프로퍼티 기준 값
        string                          _documentPath;
        string                          _openedDocument; ///< 지금 미리보기 화면의 문서
        string                          _targetPath;     ///< 지금 렌더 텍스처 경로
        string                          _error;          ///< 마지막으로 열지 못한 까닭
        string                          _lastFocusKey;   ///< 작업공간 포커스 확인 키(`consumeWorkspaceFocusKey`)
        string                          _language;       ///< 고른 언어 코드. 빈 값 = 게임 언어 그대로
        hashed_string                   _theme;          ///< 빈 이름 = 게임의 지금 테마
        hashed_string                   _animation;      ///< 고른 애니메이션(빈 이름 = 없음)
        UIViewport                      _viewport;
        UIPointerState                  _pointer; ///< 입력 흉내의 포인터 상태(미리보기 트리 하나만 본다)
        UIFocusManager                  _focus;   ///< 입력 흉내의 포커스(탐색 방향)
        EditorPropertyGrid              _grid;
        unique_ptr<LocalizationManager> _pLocalization; ///< 이 미리보기의 문화권 관리자 사본(언어를 골랐을 때만)
        shared_ptr<EditSession>         _pSession;      ///< 되돌리기 항목이 패널을 가리키는 길
        UIPreviewProbe                  _probe;
        void*                           _pTextureID; ///< ImGui 텍스처 id(이 패널 소유)
        uint64                          _texture;    ///< 그 id 의 RHI 텍스처
        UIScreenHandle                  _screen;
        WidgetID                        _selected;
        WidgetID                        _seenRootID;       ///< 마지막으로 본 트리 루트(바뀌면 문서를 다시 읽은 것)
        WidgetID                        _baselineWidget;   ///< 기준 값을 잡은 위젯
        uint32                          _selectedOrdinal;  ///< 고른 위젯의 문서 순서 번호(트리를 새로 지어도 이어 간다)
        uint32                          _generation;       ///< 트리가 새로 지어질 때마다 오른다(옛 되돌리기 항목을 끊는다)
        uint32                          _gameTextRevision; ///< 사본을 만들 때 본 게임 텍스트 버전
        uint32                          _languageIndex;
        uint32                          _resolutionIndex;
        float32                         _uiScale;
        float32                         _textScale;
        float32                         _safeZone;
        float32                         _animationScrub; ///< 시간 막대 값(초)
        float2                          _lastPointer;    ///< 마지막으로 보낸 포인터 위치(UI 단위)
        bool                            _bShowLayout;
        bool                            _bInteract;       ///< 입력 흉내를 켰는지(그림 위 클릭이 선택 대신 UI 로 간다)
        bool                            _bNavigationMode; ///< 탐색 방식으로 보이는지(포커스 테두리)
        bool                            _bMutated;        ///< 입력 흉내나 애니메이션이 트리 값을 바꿨는지
        bool                            _bAnimationPaused;
        bool                            _bAnimationLoop;
        bool                            _bPointerInside;
        bool                            _bPointerPressed;    ///< 포인터 버튼을 누른 채인지(이미지 밖에서 떼도 Up 을 보낸다)
        bool                            _bApplyLocalization; ///< 화면에 문화권 관리자를 다시 걸어야 하는지(화면을 새로 열었거나 언어를 바꿨다)
        bool                            _bAwaitReload;       ///< 저장했고 핫 리로드가 트리를 다시 짓기를 기다리는지
        bool                            _bReloadedAfterSave; ///< 그 뒤 트리가 새로 지어졌는지(탐침)
    };
} // namespace sw::editor
