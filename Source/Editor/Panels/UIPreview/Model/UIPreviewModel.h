/**
 * @file UIPreviewModel.h
 * @brief UI 미리보기 패널(`UIPreviewPanel`)의 모델입니다 — 미리볼 문서 · 오프스크린 화면 · 보기 설정 · 고른 위젯 · 입력 흉내 · 애니메이션 · 편집 명령(되돌리기)과 저장.
 *        ImGui 를 모른다. 패널은 이것을 가지고 그리기만 한다(언리얼 에셋 편집기의 툴킷 · 뷰 분리, 유니티 UI Builder 의 문서 모델과 같은 선).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Widgets/EditableAssetPath.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"
#include "Editor/Panels/UIPreview/Model/UIPreviewLogic.h"

#include "Engine/UI/Layout/UIResolutionPresetTable.h"
#include "Engine/UI/Screen/UIScreenTypes.h"
#include "Engine/UI/WidgetTree/UIFocusManager.h"
#include "Engine/UI/WidgetTree/UIPointerState.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;

    class DataPath;
    class LocalizationManager;
    class UIAnimationPlayer;
    class UIScreen;
    class UISystem;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct UIPreviewProbe
     * @brief 패널이 마지막으로 그린 프레임에서 본 값입니다(자동화 감시 값 `Editor.UIPreview*` 가 읽는다). 창이 닫혔거나 문서가 없으면 `_bValid` 가 false 입니다.
     */
    struct UIPreviewProbe
    {
        bool    _bValid{ false };
        bool    _bDirty{ false };
        bool    _bRightToLeft{ false };      ///< 루트 위젯이 오른쪽에서 왼쪽 흐름으로 배치됐는지
        bool    _bHoveredFocusable{ false }; ///< 포인터 아래 위젯이 포커스를 받는 종류(버튼 …)인지
        bool    _bSelectedValid{ false };    ///< 고른 위젯이 있는지
        bool    _bAnimationPlaying{ false };
        bool    _bTextureReady{ false };      ///< 렌더 텍스처가 ImGui 에 등록돼 이미지가 보이는지
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
        uint32  _slotEditCount{ 0 };         ///< 적용된 슬롯 편집 수(캔버스 손잡이 · Slot 필드 · 상자 순서 · 격자 셀 손잡이, 되돌리기 항목 하나가 하나)
    };

    /**
     * @struct UIPreviewPointerInput
     * @brief 입력 흉내 한 프레임의 포인터 입력입니다. 뷰가 에디터 입력에서 읽어 UI 단위로 옮겨 넘깁니다(모델은 에디터 입력을 모른다).
     */
    struct UIPreviewPointerInput
    {
        static constexpr uint32 kButtonCount = 3; ///< 왼쪽 · 오른쪽 · 가운데 순서

        float2  _position{};                  ///< UI 단위 포인터 위치
        float32 _wheel{ 0.0f };               ///< 이번 프레임의 휠
        bool    _bHovered{ false };           ///< 포인터가 이미지 위에 있다
        bool    _arrPressed[kButtonCount]{};  ///< 이번 프레임에 누른 버튼
        bool    _arrReleased[kButtonCount]{}; ///< 이번 프레임에 뗀 버튼
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class UIPreviewModel
     * @brief UI 미리보기의 상태와 명령입니다. 문서를 오프스크린 화면(`UISystem::openOffscreenScreen`)으로 짓고, 보기 설정을 걸고, 고친 값을 되돌리기 항목으로 남기고, 문서로 씁니다.
     * @details 문서 dirty 는 패널(`IEditorPanel`)이 가진 하나다 — 모델은 바뀔 때마다 `setDirtyListener` 로 받은 함수로 알린다(값을 따로 두지 않는다).
     *          되돌리기 항목은 모델을 가리키는 길(`EditSession`)을 쥐고, 모델이 사라지면 그 길이 끊겨 옛 항목은 아무것도 하지 않는다.
     *          트리가 새로 지어질 때마다 세대(`_generation`)가 올라 옛 항목이 새 트리에 값을 쓰지 않는다. 판단(뷰포트 · 끼우기 · 슬롯 끌기 …)은 `UIPreviewLogic` 이다.
     */
    class UIPreviewModel
    {
    public:
        /** @brief 처음 여는 문서 @p initialDocument(editortooldefaults 의 칸)로 만듭니다. */
        explicit UIPreviewModel( const DataPath& initialDocument );
        ~UIPreviewModel();
        UIPreviewModel( const UIPreviewModel& )            = delete;
        UIPreviewModel& operator=( const UIPreviewModel& ) = delete;

        /** @brief 문서 dirty 가 바뀔 때 부를 함수입니다(패널의 `markDocumentDirty` · `clearDocumentDirty`). */
        void setDirtyListener( const Delegate<void( bool )>& listener ) { _onDirtyChanged = listener; }
        /** @brief 렌더 텍스처를 놓기 직전(화면을 다시 열거나 새 크기로 옮길 때)에 부를 함수입니다 — 뷰가 등록한 ImGui 텍스처를 먼저 놓는다. */
        void setTargetReleaseListener( const Delegate<void()>& listener ) { _onTargetReleasing = listener; }

        // --- 문서 · 화면 ---------------------------------------------------------------
        /** @brief 미리보는 문서 경로입니다. 뷰가 경로 칸을 그리고, 확정하면 `onDocumentPathCommitted` 를 부릅니다. */
        EditableAssetPath& getDocumentPath() { return _documentPath; }
        /** @brief 경로 칸이 새 경로로 확정됐습니다. 고친 값이 있었으면(@p bDirty) 버린다고 알리고 지난 오류를 지웁니다. */
        void onDocumentPathCommitted( const string& previous, bool bDirty );
        /** @brief 콘텐츠 브라우저에서 연 문서(작업공간의 포커스)가 바뀌었으면 그것을 미리볼 문서로 정합니다. */
        void syncFocusedDocument( bool bDirty );
        /** @brief 문서 · 뷰포트가 바뀌었으면 오프스크린 화면을 다시 열고(크기가 바뀌면 렌더 텍스처 경로도), 이번 프레임의 보기를 겁니다. 렌더 텍스처를 바꾸기 전에 `setTargetReleaseListener` 의 함수를 부릅니다. */
        void syncPreview( UISystem& ui );
        /** @brief 고른 언어에 맞는 문화권 관리자 사본을 만들고(게임 텍스트가 바뀌면 다시) 화면에 겁니다. */
        void syncLanguage( UISystem& ui );
        /** @brief 화면 트리가 새로 지어졌으면(핫 리로드) 고른 위젯 · 포인터 · 포커스를 새 트리로 이어 갑니다. */
        void syncTree( UIScreen& screen );
        /** @brief 미리보기 화면을 닫습니다(포인터 · 포커스를 놓고 되돌리기 세대를 올린다). */
        void closePreview();
        /** @brief 화면을 닫고 문화권 사본과 감시 값을 놓습니다(패널 종료). */
        void shutdown();
        /** @brief 지금 미리보기 화면입니다. 없으면 nullptr 입니다. */
        UIScreen* findPreviewScreen() const;
        /** @brief 지금 렌더 텍스처 경로입니다. */
        const string& getTargetPath() const { return _targetPath; }
        /** @brief 마지막으로 열지 못한 까닭입니다. 비면 오류가 없다. */
        const string& getError() const { return _error; }

        // --- 저장 · 되돌리기 ---------------------------------------------------------------
        /** @brief 입력 흉내나 애니메이션이 트리 값을 바꾸지 않았고 문서가 열려 있으면 true 입니다. */
        bool canSaveDocument() const;
        /** @brief 미리보기 트리를 `UIDocumentWriter` 로 문서 파일에 씁니다. 핫 리로드가 다시 읽기를 기다립니다. */
        [[nodiscard]] bool saveDocument();
        /** @brief 디스크 문서로 미리보기를 다시 만들고 고친 값을 버립니다. */
        void revertDocument();

        // --- 보기 설정 ---------------------------------------------------------------
        /** @brief 고른 해상도 견본입니다. 테이블을 처음 쓸 때 읽고, 읽지 못했으면 nullptr 입니다. */
        const UIResolutionPreset*      findSelectedResolution();
        const UIResolutionPresetTable& getResolutionTable() const { return _resolutionTable; }
        uint32                         getResolutionIndex() const { return _resolutionIndex; }
        void                           setResolutionIndex( uint32 index ) { _resolutionIndex = index; }
        const hashed_string&           getTheme() const { return _theme; }
        void                           setTheme( const hashed_string& theme ) { _theme = theme; }
        /** @brief 언어 콤보의 코드 목록입니다(첫 줄 = 게임 언어). */
        const vector<string>& getLanguageChoices() const { return _listLanguage; }
        /** @brief 고른 언어 코드입니다. 비면 게임 언어 그대로입니다. */
        const string& getLanguage() const { return _language; }
        /** @brief 언어 콤보의 @p index 번째 줄을 고릅니다. */
        void              selectLanguage( uint32 index );
        float32&          editUIScale() { return _uiScale; }
        float32&          editTextScale() { return _textScale; }
        float32&          editSafeZone() { return _safeZone; }
        const UIViewport& getViewport() const { return _viewport; }

        // --- 고른 위젯 · 위젯 목록 ---------------------------------------------------------------
        WidgetID getSelected() const { return _selected; }
        uint32   getSelectedOrdinal() const { return _selectedOrdinal; }
        /** @brief 위젯 @p widget(문서 순서 번호 @p ordinal)을 고릅니다. */
        void select( WidgetID widget, uint32 ordinal );
        /** @brief UI 점 @p point 아래의 맨 위 위젯을 고릅니다(없으면 고름을 비운다). */
        void selectAt( const WidgetTree& tree, const float2& point );
        /** @brief 위젯 목록 행입니다. 트리나 그 구조 세대가 바뀔 때만 다시 짓는다. */
        const vector<UIPreviewWidgetRow>& getWidgetRows( const WidgetTree& tree );

        // --- 프로퍼티 편집 ---------------------------------------------------------------
        /** @brief 고른 위젯 @p widget 의 프로퍼티 기준 값이 그 위젯 것이 아니면 다시 잡습니다 — 그리드가 값을 바꿨을 때 이전 값을 알기 위해서입니다. */
        void syncBaseline( const Widget& widget );
        /** @brief 그리드가 고른 위젯의 프로퍼티 하나를 고쳤다고 알릴 때 되돌리기 항목을 남깁니다. */
        void onPropertyEdited( const PropertyInfo& prop );
        /**
         * @brief 슬롯 편집 하나(손잡이 · Slot 필드 · 격자 셀)를 맺습니다 — `_slot` 텍스트 전후가 다르면 되돌리기 항목을 남기고 dirty 로 하고 기준 값을 다시 잡게 합니다.
         * @return 항목을 남겼으면 true 입니다.
         */
        bool commitSlotEdit( uint32 ordinal, const string& oldText, const string& newText );

        // --- 디자이너: 팔레트 · 구조 편집 ---------------------------------------------------------------
        /** @brief 팔레트(위젯 타입 목록)를 처음이거나 @p frame 이 지난번보다 충분히 지났으면 다시 모읍니다(게임 모듈이 위젯 타입을 더하거나 뺄 수 있다). */
        void                           refreshPalette( uint32 frame );
        const vector<const TypeInfo*>& getPaletteTypes() const { return _listPaletteType; }
        const hashed_string&           getPaletteType() const { return _paletteType; }
        void                           setPaletteType( const hashed_string& typeName ) { _paletteType = typeName; }
        /** @brief 위젯 타입 @p typeName 을 @p pTarget 에 붙이고(패널이면 그 안에, 아니면 그 뒤에) 되돌리기 항목을 남깁니다. 붙였으면 true 입니다. */
        bool addWidget( UIScreen& screen, const hashed_string& typeName, Widget* pTarget );
        /** @brief 고른 위젯을 지울 수 있으면 true 입니다(루트가 아니다). */
        bool canDeleteSelected( const WidgetTree& tree ) const;
        /** @brief 고른 위젯을 트리에서 분리해 되돌리기 항목에 맡깁니다. 루트는 지우지 않습니다. 지웠으면 true 입니다. */
        [[nodiscard]] bool deleteSelected( UIScreen& screen );
        /** @brief 문서 순서 번호 @p parentOrdinal 인 상자 패널의 @p fromIndex 번째 자식을 @p toIndex 번째로 옮기고 되돌리기 항목을 남깁니다. */
        void reorderChild( uint32 parentOrdinal, uint32 fromIndex, uint32 toIndex );

        // --- 입력 흉내 ---------------------------------------------------------------
        bool isInteracting() const { return _bInteract; }
        /** @brief 입력 흉내를 켜거나 끕니다. 탐색 방식을 끄고, 끄면 호버를 비웁니다. */
        void setInteracting( bool bInteract );
        /** @brief 탐색 방향(`UINavigationDirection` 값)으로 포커스를 옮깁니다. 포커스가 없으면 첫 포커스 위젯을 잡습니다. 옮겼으면 true 입니다. */
        bool navigateFocus( UIScreen& screen, uint32 direction );
        /** @brief 포커스 위젯 가운데를 누른 것처럼 포인터 이벤트를 보냅니다(확인 입력 흉내). */
        void acceptFocus( UIScreen& screen );
        /** @brief 입력 흉내 한 프레임을 미리보기 트리에 넣습니다(호버 · 누름 · 뗌 · 휠). */
        void feedPointer( UIScreen& screen, const UIPreviewPointerInput& input );

        // --- 애니메이션 ---------------------------------------------------------------
        /** @brief 고른 애니메이션입니다. 화면 재생기에 없으면 첫 애니메이션으로 고칩니다. 애니메이션이 없으면 빈 이름입니다. */
        const hashed_string& syncAnimation( UIAnimationPlayer& player );
        void                 chooseAnimation( UIAnimationPlayer& player, const hashed_string& name );
        void                 playAnimation( UIAnimationPlayer& player );
        bool                 isAnimationPaused() const { return _bAnimationPaused; }
        void                 togglePauseAnimation( UIAnimationPlayer& player );
        void                 stopAnimation( UIAnimationPlayer& player );
        bool&                editAnimationLoop() { return _bAnimationLoop; }
        /** @brief 시간 막대에 보일 시각과 길이입니다. 재생 중이면 지금 시각을 따라가고 true 입니다. */
        bool readAnimationTime( const UIAnimationPlayer& player, float32& outTime, float32& outDuration );
        /** @brief 시간 막대를 끌었습니다 — 그 시각에서 멈춘 채 값을 보입니다(이벤트는 보내지 않는다). */
        void scrubAnimation( UIAnimationPlayer& player, float32 seconds );

        // --- 감시 값 ---------------------------------------------------------------
        /** @brief 이번 프레임의 감시 값을 모읍니다. 화면이 없으면 비웁니다. */
        void                  updateProbe( const UIScreen* pScreen, bool bDirty, bool bTextureReady );
        void                  resetProbe() { _probe = UIPreviewProbe{}; }
        const UIPreviewProbe& getProbe() const { return _probe; }

    private:
        /** @brief 되돌리기 항목이 모델을 가리키는 길입니다. 모델이 사라지면 끊어, 옛 항목이 사라진 모델에 값을 쓰지 않게 합니다. */
        struct EditSession;
        /** @brief 분리한 위젯을 되돌리기 항목이 가지고 있는 상자입니다(지우기를 되돌리면 같은 위젯을 다시 끼운다). */
        struct RemovedWidget;

        /** @brief 프로퍼티 하나의 기준 값입니다. */
        struct BaselineValue
        {
            string _name{};
            string _text{};
        };

        void setDirty( bool bDirty );
        void notifyTargetReleasing();
        /** @brief 입력 흉내나 애니메이션이 트리 값을 바꿨음을 기록합니다(저장을 막고 Reload 로 되찾게 합니다). */
        void markPreviewMutated() { _bMutated = true; }
        void setNavigationMode( bool bNavigation );
        void captureBaseline( const Widget& widget );
        /** @brief 프로퍼티 편집 하나를 되돌리기 항목으로 남깁니다(인스펙터와 슬롯 편집이 함께 씁니다). */
        void pushPropertyEdit( uint32 ordinal, const string& propertyName, const string& oldText, const string& newText, const string& coalesceKey );
        /** @brief 구조를 고친 뒤 고름 · 기준 값 · 호버를 정리합니다 — @p selectedOrdinal 의 위젯을 고릅니다. */
        void afterStructureEdit( UIScreen& screen, uint32 selectedOrdinal );
        /** @brief 되돌리기와 다시 하기가 부릅니다 — 문서 순서 번호 @p ordinal 의 위젯 프로퍼티 @p propertyName 에 @p text 를 씁니다. 다른 세대의 항목이면 버립니다. */
        void applyEditedText( uint32 generation, uint32 ordinal, const string& propertyName, const string& text );
        /** @brief 문서 순서 번호 @p parentOrdinal 인 패널의 @p index 번째에 @p typeName 위젯을 끼웁니다. 다른 세대의 항목이면 버립니다. */
        void applyInsert( uint32 generation, const hashed_string& typeName, uint32 parentOrdinal, uint32 index );
        /** @brief 그 위치의 위젯을 분리합니다. @p pHolder 가 있으면 분리한 위젯을 맡깁니다(지우기를 다시 하기). 없으면 버립니다(추가를 되돌리기). */
        void applyDetach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder );
        /** @brief @p pHolder 가 가진 위젯을 그 위치에 다시 끼웁니다(지우기를 되돌리기). */
        void applyReattach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder );
        /** @brief 패널 @p parentOrdinal 의 @p fromIndex 번째 자식을 @p toIndex 번째로 옮깁니다. 다른 세대의 항목이면 버립니다. */
        void applyMove( uint32 generation, uint32 parentOrdinal, uint32 fromIndex, uint32 toIndex );

    private:
        UndoStepOwner                   _undoOwner;         ///< 이 미리보기가 남긴 코드 되돌리기 단계의 주인(모델이 사라지면 그 명령이 빠진다)
        Delegate<void( bool )>          _onDirtyChanged;    ///< 문서 dirty 를 패널에 알린다
        Delegate<void()>                _onTargetReleasing; ///< 렌더 텍스처를 놓기 직전에 뷰에 알린다
        vector<UIPreviewWidgetRow>      _listRow;           ///< 위젯 목록 행. 트리나 그 구조 세대가 바뀔 때만 다시 짓는다
        const WidgetTree*               _pRowTree;          ///< `_listRow` 를 지은 트리
        uint64                          _rowTreeGeneration; ///< `_listRow` 를 지은 때의 `WidgetTree::getStructureGeneration`(세대는 1 부터)
        vector<string>                  _listLanguage;      ///< 언어 콤보의 코드(첫 줄 = 게임 언어)
        vector<BaselineValue>           _listBaseline;      ///< 고른 위젯의 프로퍼티 기준 값
        vector<const TypeInfo*>         _listPaletteType;   ///< 팔레트의 위젯 타입(이름순)
        hashed_string                   _paletteType;       ///< 팔레트에서 고른 타입(Add 가 붙인다)
        EditableAssetPath               _documentPath;      ///< 미리보는 문서. 경로 칸은 확정할 때만 바꾼다(글자마다 다시 짓지 않는다)
        string                          _openedDocument;    ///< 지금 미리보기 화면의 문서
        string                          _targetPath;        ///< 지금 렌더 텍스처 경로
        string                          _error;             ///< 마지막으로 열지 못한 까닭
        string                          _lastFocusKey;      ///< 작업공간 포커스 확인 키(`consumeWorkspaceFocusKey`)
        string                          _language;          ///< 고른 언어 코드. 빈 값 = 게임 언어 그대로
        hashed_string                   _theme;             ///< 빈 이름 = 게임의 지금 테마
        hashed_string                   _animation;         ///< 고른 애니메이션(빈 이름 = 없음)
        UIViewport                      _viewport;
        UIPointerState                  _pointer;       ///< 입력 흉내의 포인터 상태(미리보기 트리 하나만 본다)
        UIFocusManager                  _focus;         ///< 입력 흉내의 포커스(탐색 방향)
        unique_ptr<LocalizationManager> _pLocalization; ///< 이 미리보기의 문화권 관리자 사본(언어를 골랐을 때만)
        shared_ptr<EditSession>         _pSession;      ///< 되돌리기 항목이 모델을 가리키는 길
        UIPreviewProbe                  _probe;
        UIScreenHandle                  _screen;
        WidgetID                        _selected;
        WidgetID                        _seenRootID;       ///< 마지막으로 본 트리 루트(바뀌면 문서를 다시 읽은 것)
        WidgetID                        _baselineWidget;   ///< 기준 값을 잡은 위젯
        uint32                          _selectedOrdinal;  ///< 고른 위젯의 문서 순서 번호(트리를 새로 지어도 이어 간다)
        uint32                          _generation;       ///< 트리가 새로 지어질 때마다 오른다(옛 되돌리기 항목을 끊는다)
        uint32                          _gameTextRevision; ///< 사본을 만들 때 본 게임 텍스트 버전
        uint32                          _languageIndex;
        uint32                          _paletteFrame;    ///< 팔레트를 마지막으로 모은 프레임
        uint32                          _slotEditCount;   ///< 적용된 슬롯 편집 수(감시 값)
        UIResolutionPresetTable         _resolutionTable; ///< 해상도 견본 — 처음 쓸 때 한 번 읽는다
        uint32                          _resolutionIndex;
        bool                            _bResolutionTableAttempted; ///< 테이블을 읽어 봤다(실패도 한 번만 알린다)
        float32                         _uiScale;
        float32                         _textScale;
        float32                         _safeZone;
        float32                         _animationScrub;  ///< 시간 막대 값(초)
        float2                          _lastPointer;     ///< 마지막으로 보낸 포인터 위치(UI 단위)
        bool                            _bInteract;       ///< 입력 흉내를 켰는지(이미지 위 클릭이 선택 대신 UI 로 간다)
        bool                            _bNavigationMode; ///< 탐색 방식으로 보이는지(포커스 테두리)
        bool                            _bMutated;        ///< 입력 흉내나 애니메이션이 트리 값을 바꿨는지
        bool                            _bAnimationPaused;
        bool                            _bAnimationLoop;
        bool                            _bPointerInside;
        bool                            _bPointerPressed;    ///< 포인터 버튼을 누른 채인지(이미지 밖에서 놓아도 Up 을 보낸다)
        bool                            _bApplyLocalization; ///< 화면에 문화권 관리자를 다시 등록해야 하는지(화면을 새로 열었거나 언어를 바꿨다)
        bool                            _bAwaitReload;       ///< 저장했고 핫 리로드가 트리를 다시 만들기를 기다리는지
        bool                            _bReloadedAfterSave; ///< 그 뒤 트리가 새로 지어졌는지(감시 값)
    };
} // namespace sw::editor
