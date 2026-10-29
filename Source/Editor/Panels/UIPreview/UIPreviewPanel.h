/**
 * @file UIPreviewPanel.h
 * @brief 런타임 UI 문서(`*.ui.xml`) 미리보기 창입니다 — 해상도 견본 · UI/글자 배율 · 안전 영역 · 테마 · 언어 · 레이아웃 사각형 · 위젯 트리 선택 ·
 *        고른 위젯의 프로퍼티 편집과 저장 · 입력 흉내(마우스, 탐색 방향) · 애니메이션 미리 보기.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/UIPreview/Model/UIPreviewModel.h"

namespace sw
{
    struct PropertyInfo;

    class IRHIDevice;
    class UIScreen;
    class UISystem;
    class UIThemeController;
    class Widget;
} // namespace sw

namespace sw::editor
{
    /**
     * @class UIPreviewPanel
     * @brief UI 문서를 게임 UI 와 따로 **오프스크린 화면**(`UIOffscreenScreens::open` — 렌더 텍스처 대상 캔버스)으로 지어 ImGui 이미지로 봅니다
     *        (유니티 UI Builder · UMG 디자이너의 미리보기에 해당합니다).
     * @details 해상도 견본은 게임과 같은 배율 규칙(`UISystem::getScaleSettings`)으로 뷰포트를 만들고, UI 배율 · 글자 배율 · 안전 영역 · 테마 · 언어는 이 미리보기에만 겁니다.
     *          언어는 문화권이 전역이라 게임 UI 를 건드리지 않도록 **관리자 사본**(`LocalizationManager::copyStateFrom`)을 모델이 소유하고 화면에 겁니다(`qps-ploc`, `qps-plocm` 포함).
     *          문서를 고치면 핫 리로드가 미리보기도 다시 만듭니다. 레이아웃 사각형 · 고른 위젯은 ImGui 그리기 목록으로 이미지 위에 추가합니다(캔버스를 바꾸지 않는다).
     *
     *          고른 위젯의 프로퍼티는 `EditorPropertyGrid` 로 고치고, 고칠 때마다 되돌리기 항목이 하나 쌓입니다(`EditorTransaction::push`). Ctrl+S 나 Save 는 `UIDocumentWriter` 로
     *          문서를 다시 씁니다 — 파일 감시가 미리보기를 다시 만든다. 입력 흉내(마우스 호버 · 클릭 · 탐색 방향)와 애니메이션 재생은 미리보기 트리의 값을 바꾸므로, 그 뒤에는
     *          Reload 로 문서 값을 되찾을 때까지 저장을 막는다(바뀐 슬라이더 값 · 애니메이션 중간 값이 문서에 쓰이지 않게 한다).
     *
     *          디자이너(유니티 UI Builder 의 Library · UMG 의 Palette 와 Canvas 슬롯 손잡이): 팔레트는 만들 수 있는 모든 `Widget` 타입이고, 항목을 트리 줄이나 이미지에 끌어 놓거나
     *          Add 로 고른 위젯에 붙인다(패널이면 그 안, 아니면 그 뒤). Delete 는 고른 위젯과 그 자식을 지우며 Undo 가 같은 위젯을 같은 위치에 되돌린다. 부모가 캔버스 패널인
     *          위젯에는 슬롯 손잡이(가운데 옮기기 · 네 변)가 떠서 끌면 `_slot` 의 오프셋이 바뀐다. 구조를 고치면 문서 순서 번호가 밀리므로 되돌리기 항목은 부모의 번호와 자식 번호로 위치를 가리키고,
     *          지운 위젯은 되돌리기 항목이 쥔다. 구조 편집은 입력 흉내 · 애니메이션과 달리 문서를 바꾸는 편집이라 저장을 막지 않는다.
     *          판단(뷰포트 · 맞춤 · 점 옮기기 · 트리 줄 · 언어 목록 · 속성 텍스트 · 끼우기 · 분리하기 · 슬롯 끌기)은 `UIPreviewLogic`(ImGui 없음 — EditorTest).
     *          상태 · 명령(문서 · 화면 · 보기 설정 · 고름 · 입력 흉내 · 애니메이션 · 되돌리기 · 저장)은 모델(`UIPreviewModel`, ImGui 없음)이 갖고, 이 패널은 ImGui 로 그리고
     *          입력을 모델 명령으로 옮기며 ImGui 텍스처 등록만 직접 든다(뷰).
     */
    class UIPreviewPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "ui_preview";

        UIPreviewPanel();
        ~UIPreviewPanel() override;

        /** @brief 제목은 애셋 종류 등록의 UIDocument 패널 제목입니다(경로로 여는 요청과 Assets 메뉴가 같은 등록을 봅니다). */
        const utf8*      getPanelTitle() const override;
        EditorDockSlot   getDefaultDockSlot() const override { return EditorDockSlot::Center; }
        EditorAssetType  getDocumentKind() const override;
        void             drawContent() override;
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 147.0f, 36.0f ); }
        bool             isToolPanel() const override { return true; }
        /** @brief 미리보기 화면을 닫고 ImGui 텍스처를 놓습니다. */
        void shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 마지막으로 그린 프레임의 값입니다(자동화 감시 값). */
        const UIPreviewProbe& getProbe() const { return _model.getProbe(); }

    protected:
        /** @brief 고친 문서를 `UIDocumentWriter` 로 파일에 씁니다. 입력 흉내나 애니메이션으로 트리 값이 바뀐 상태면 `canSaveDocument` 가 막습니다. */
        [[nodiscard]] bool saveDocument() override;
        [[nodiscard]] bool canSaveDocument() const override;
        /** @brief 디스크 문서로 미리보기를 다시 만들고 고친 값을 버립니다. */
        void revertDocument() override;

    private:
        /** @brief Slot 필드 한 번 그리기의 상태(슬롯 사본 · 필터 · 직전 컨트롤을 그렸는지)와 컨트롤 그룹별 그리기입니다. */
        struct SlotFieldPass;

        /** @brief 모델이 문서 dirty 를 바꿀 때 부릅니다(패널의 dirty 가 원본 하나다). */
        void setDocumentDirty( bool bDirty );
        /** @brief 렌더 텍스처를 ImGui 텍스처로 등록합니다(핸들이 바뀌면 다시). */
        void syncTexture();
        void releaseTexture();
        void drawToolbar( const UIThemeController& themes );
        void drawLanguageCombo();
        void drawInteractionBar( UIScreen& screen );
        void drawAnimationBar( UIScreen& screen );
        void drawWidgetList( UIScreen& screen );
        void drawInspector( UIScreen& screen );
        void drawImage( UIScreen& screen );
        /** @brief 이번 프레임의 에디터 포인터를 UI 단위로 옮겨 모델의 입력 흉내에 넣습니다. */
        void feedPointer( UIScreen& screen, const float2& drawSize, const float2& origin, bool bHovered );
        /** @brief 그리드가 프로퍼티 하나를 고쳤다고 알립니다(모델이 되돌리기 항목을 남긴다). */
        void onPropertyEdited( const PropertyInfo& prop );

        // --- 디자이너: 팔레트 · 끌어 놓기 · 슬롯 손잡이 ---------------------------------------------------------------
        /** @brief 팔레트(위젯 타입 목록)를 그립니다. 항목을 끌어 트리 줄이나 이미지에 놓거나, 고른 뒤 Add 로 고른 위젯에 붙입니다. */
        void drawPalette( UIScreen& screen );
        /** @brief 트리 줄 @p pTarget 에 팔레트 항목을 놓을 수 있게 합니다(바로 앞에 그린 항목이 놓는 위치). */
        void acceptPaletteDrop( UIScreen& screen, Widget* pTarget );
        /** @brief 선택한 위젯의 슬롯 손잡이를 이미지 위에 그리고 끄는 대로 슬롯을 고칩니다(부모가 캔버스 패널일 때). */
        void drawSlotHandles( UIScreen& screen, const float2& drawSize, const float2& origin );
        /**
         * @brief 부모가 상자 · 격자 패널인 선택 위젯에 옮기기 손잡이 하나를 그립니다. 끌어 놓은 점으로 상자는 자식 순서를, 격자는 행 · 열을 바꿉니다.
         * @details 끄는 동안은 놓일 위치만 표시하고 트리를 바꾸지 않는다 — 놓을 때 한 번 바꾸고 되돌리기 항목 하나를 남긴다(상자는 `reorderChild`, 격자는 `commitSlotEdit`).
         */
        void drawLayoutHandle( UIScreen& screen, const float2& drawSize, const float2& origin );
        /**
         * @brief 인스펙터 위에 고른 위젯의 Slot 필드를 그립니다 — 부모 패널이 읽는 필드만(`UIPreviewLogic::findSlotFields`) 보입니다(상자 · 겹침 · 격자 · 흐름 · 캔버스 …).
         * @details 되돌리기는 슬롯 손잡이와 같다 — 끄는 동안은 슬롯을 바로 고치고, 손을 놓으면 `_slot` 텍스트 전후 한 쌍을 `commitSlotEdit` 로 남긴다.
         */
        void drawSlotSection( Widget& widget );
        /** @brief 방금 그린 Slot 필드 컨트롤 하나의 편집을 맺습니다. @p frameText 는 그 컨트롤을 그리기 전 `_slot` 텍스트, @p bImmediate 는 콤보 · 체크처럼 한 번에 끝나는 편집입니다. */
        void finishSlotField( Widget& widget, const PropertyInfo& slotProp, const string& frameText, bool bChanged, bool bImmediate );

    private:
        UIPreviewModel     _model;             ///< 상태와 명령(ImGui 없음)
        EditorPropertyGrid _grid;              ///< 고른 위젯의 프로퍼티 그리드
        string             _paletteFilter;     ///< 팔레트 검색어
        string             _slotDragBaseline;  ///< 슬롯 손잡이를 끌기 시작할 때의 `_slot` 텍스트
        string             _slotFieldBaseline; ///< Slot 필드 컨트롤을 끌기 시작할 때의 `_slot` 텍스트
        void*              _pTextureID;        ///< ImGui 텍스처 id(이 패널 소유)
        uint64             _texture;           ///< 그 id 의 RHI 텍스처
        uint32             _slotDragOrdinal;   ///< 슬롯 손잡이를 끄는 위젯의 문서 순서 번호
        bool               _bShowLayout;       ///< 레이아웃 사각형을 겹쳐 그린다
        bool               _bSlotDragging;     ///< 슬롯 손잡이를 끄는 중인지
        bool               _bLayoutDragging;   ///< 상자 · 격자 옮기기 손잡이를 끄는 중인지
    };
} // namespace sw::editor
