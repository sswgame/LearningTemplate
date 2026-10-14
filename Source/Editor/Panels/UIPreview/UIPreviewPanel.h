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
    struct TypeInfo;

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
        uint32  _firstTextBytes{ 0 };        ///< 문서 순서로 첫 글 위젯의 화면 글자 UTF-8 바이트 수(언어를 바꾸면 길이가 달라진다)
        int32   _hoveredOrdinal{ -1 };       ///< 포인터 아래 위젯의 문서 순서 번호, 없으면 -1
        int32   _focusedOrdinal{ -1 };       ///< 포커스 위젯의 문서 순서 번호, 없으면 -1
        float32 _selectedOpacity{ 0.0f };    ///< 고른 위젯의 불투명도
        float32 _animationTime{ 0.0f };      ///< 고른 애니메이션의 지금 시각(재생 중이 아니면 0)
        uint32  _languageIndex{ 0 };         ///< 언어 콤보에서 고른 줄(0 = 게임 언어)
        bool    _bSlotEditable{ false };     ///< 고른 위젯의 슬롯을 손잡이로 고칠 수 있는지(부모가 캔버스 패널)
        float32 _selectedOffsetMinX{ 0.0f }; ///< 고른 위젯의 슬롯 `_offsetMin._x`(손잡이로 옮기면 바뀐다)
        uint32  _paletteCount{ 0 };          ///< 팔레트가 보이는 위젯 타입 수
        uint32  _slotEditCount{ 0 };         ///< 적용된 슬롯 편집 수(캔버스 손잡이 · Slot 칸 · 상자 순서 · 격자 칸 손잡이, 되돌리기 항목 하나가 하나)
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
     *
     *          디자이너(유니티 UI Builder 의 Library · UMG 의 Palette 와 Canvas 슬롯 손잡이): 팔레트는 만들 수 있는 모든 `Widget` 타입이고, 항목을 트리 줄이나 그림에 끌어 놓거나
     *          Add 로 고른 위젯에 붙인다(패널이면 그 안, 아니면 그 뒤). Delete 는 고른 위젯과 그 자식을 지우며 Undo 가 같은 위젯을 같은 자리에 되돌린다. 부모가 캔버스 패널인
     *          위젯에는 슬롯 손잡이(가운데 옮기기 · 네 변)가 떠서 끌면 `_slot` 의 오프셋이 바뀐다. 구조를 고치면 문서 순서 번호가 밀리므로 되돌리기 항목은 부모의 번호와 자식 번호로 자리를 가리키고,
     *          지운 위젯은 되돌리기 항목이 쥔다. 구조 편집은 입력 흉내 · 애니메이션과 달리 문서를 바꾸는 편집이라 저장을 막지 않는다.
     *          판단(뷰포트 · 맞춤 · 점 옮기기 · 트리 줄 · 언어 목록 · 속성 텍스트 · 끼우기 · 떼기 · 슬롯 끌기)은 `UIPreviewLogic`(ImGui 없음 — EditorTest).
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

        /** @brief 떼어 낸 위젯을 되돌리기 항목이 들고 있는 상자입니다(지우기를 되돌리면 같은 위젯을 다시 끼운다). */
        struct RemovedWidget;

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
        void drawWidgetList( UIScreen& screen );
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

        // --- 디자이너: 팔레트 · 끌어 놓기 · 지우기 · 슬롯 손잡이 ---------------------------------------------------------------
        /** @brief 팔레트(위젯 타입 목록)를 그립니다. 항목을 끌어 트리 줄이나 그림에 놓거나, 고른 뒤 Add 로 고른 위젯에 붙입니다. */
        void drawPalette( UIScreen& screen );
        /** @brief 트리 줄 @p pTarget 에 팔레트 항목을 놓을 수 있게 합니다(바로 앞에 그린 항목이 놓는 자리). */
        void acceptPaletteDrop( UIScreen& screen, Widget* pTarget );
        /** @brief 위젯 타입 @p typeName 을 @p pTarget 에 붙이고(패널이면 그 안에, 아니면 그 뒤에) 되돌리기 항목을 남깁니다. 붙였으면 true 입니다. */
        bool addWidget( UIScreen& screen, const hashed_string& typeName, Widget* pTarget );
        /** @brief 고른 위젯을 트리에서 떼어 되돌리기 항목에 맡깁니다. 루트는 지우지 않습니다. 지웠으면 true 입니다. */
        [[nodiscard]] bool deleteSelected( UIScreen& screen );
        /** @brief 선택한 위젯의 슬롯 손잡이를 그림 위에 그리고 끄는 대로 슬롯을 고칩니다(부모가 캔버스 패널일 때). */
        void drawSlotHandles( UIScreen& screen, const float2& drawSize, const float2& origin );
        /**
         * @brief 부모가 상자 · 격자 패널인 선택 위젯에 옮기기 손잡이 하나를 그립니다. 끌어 놓은 점으로 상자는 자식 순서를, 격자는 행 · 열을 바꿉니다.
         * @details 끄는 동안은 놓일 자리만 표시하고 트리를 바꾸지 않는다 — 놓을 때 한 번 바꾸고 되돌리기 항목 하나를 남긴다(상자는 `applyMove`, 격자는 `pushPropertyEdit`).
         */
        void drawLayoutHandle( UIScreen& screen, const float2& drawSize, const float2& origin );
        /** @brief 되돌리기 항목이 부르는 순서 바꾸기입니다 — 문서 순서 번호 @p parentOrdinal 인 패널의 @p fromIndex 번째 자식을 @p toIndex 번째로 옮깁니다. 다른 세대의 항목이면 버립니다. */
        void applyMove( uint32 generation, uint32 parentOrdinal, uint32 fromIndex, uint32 toIndex );
        /**
         * @brief 인스펙터 위에 고른 위젯의 Slot 칸을 그립니다 — 부모 패널이 읽는 필드만(`UIPreviewLogic::findSlotFields`) 보입니다(상자 · 겹침 · 격자 · 흐름 · 캔버스 …).
         * @details 되돌리기는 슬롯 손잡이와 같다 — 끄는 동안은 슬롯을 바로 고치고, 손을 떼면 `_slot` 텍스트 전후 한 쌍을 `pushPropertyEdit` 로 남긴다.
         */
        void drawSlotSection( Widget& widget );
        /** @brief 방금 그린 Slot 칸 컨트롤 하나의 편집을 맺습니다. @p frameText 는 그 컨트롤을 그리기 전 `_slot` 텍스트, @p bImmediate 는 콤보 · 체크처럼 한 번에 끝나는 편집입니다. */
        void finishSlotField( Widget& widget, const PropertyInfo& slotProp, const string& frameText, bool bChanged, bool bImmediate );
        /** @brief 되돌리기 항목이 부르는 구조 편집입니다 — 문서 순서 번호 @p parentOrdinal 인 패널의 @p index 번째에 @p typeName 위젯을 끼웁니다. 다른 세대의 항목이면 버립니다. */
        void applyInsert( uint32 generation, const hashed_string& typeName, uint32 parentOrdinal, uint32 index );
        /** @brief 그 자리의 위젯을 뗍니다. @p pHolder 가 있으면 떼어 낸 위젯을 맡깁니다(지우기를 다시 하기). 없으면 버립니다(추가를 되돌리기). */
        void applyDetach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder );
        /** @brief @p pHolder 가 든 위젯을 그 자리에 다시 끼웁니다(지우기를 되돌리기). */
        void applyReattach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder );
        /** @brief 구조를 고친 뒤 고름 · 기준 값 · 호버를 정리합니다 — @p selectedOrdinal 의 위젯을 고릅니다. */
        void afterStructureEdit( UIScreen& screen, uint32 selectedOrdinal );
        /** @brief 프로퍼티 편집 하나를 되돌리기 항목으로 남깁니다(인스펙터와 슬롯 손잡이가 함께 씁니다). */
        void      pushPropertyEdit( uint32 ordinal, const string& propertyName, const string& oldText, const string& newText, const string& coalesceKey );
        UIScreen* findPreviewScreen() const;

    private:
        vector<UIPreviewWidgetRow>      _listRow;
        vector<string>                  _listLanguage;      ///< 언어 콤보의 코드(첫 줄 = 게임 언어)
        vector<BaselineValue>           _listBaseline;      ///< 고른 위젯의 프로퍼티 기준 값
        vector<const TypeInfo*>         _listPaletteType;   ///< 팔레트의 위젯 타입(이름순)
        string                          _paletteFilter;     ///< 팔레트 검색어
        string                          _slotDragBaseline;  ///< 슬롯 손잡이를 끌기 시작할 때의 `_slot` 텍스트
        string                          _slotFieldBaseline; ///< Slot 칸 컨트롤을 끌기 시작할 때의 `_slot` 텍스트
        hashed_string                   _paletteType;       ///< 팔레트에서 고른 타입(Add 가 붙인다)
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
        uint32                          _slotDragOrdinal; ///< 슬롯 손잡이를 끄는 위젯의 문서 순서 번호
        uint32                          _paletteFrame;    ///< 팔레트를 마지막으로 모은 ImGui 프레임
        uint32                          _slotEditCount;   ///< 적용된 슬롯 편집 수(탐침)
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
        bool                            _bSlotDragging;      ///< 슬롯 손잡이를 끄는 중인지
        bool                            _bLayoutDragging;    ///< 상자 · 격자 옮기기 손잡이를 끄는 중인지
    };
} // namespace sw::editor
