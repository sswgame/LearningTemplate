/**
 * @file InspectorPanel.h
 * @brief 선택한 GameObject / Component 의 프로퍼티를 편집하는 인스펙터입니다(View — 값은 `InspectorViewModel`, 프로퍼티는 `EditorPropertyGrid`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/Inspector/Model/InspectorViewModel.h"

namespace sw::editor
{
    struct EditorCommandTarget;

    /** @brief 아웃라이너에서 현재 선택한 것을 살펴보고 편집하는 패널입니다. */
    class InspectorPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "inspector";
        /** @brief 창 제목입니다(ImGui 창 이름 — 자체 테스트가 창을 찾을 때 이 상수를 쓴다). */
        static constexpr const utf8* kPanelTitle = "Inspector";

        InspectorPanel();
        ~InspectorPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel: 제목과 그리기
        // ------------------------------------------------------------------------------
        /** @brief 패널 제목을 반환합니다. */
        const utf8*    getPanelTitle() const override { return kPanelTitle; }
        EditorDockSlot getDefaultDockSlot() const override { return EditorDockSlot::Right; }
        /** @brief 인스펙터 UI 를 그립니다. 맨 위에서 `InspectorViewModel::gather` 를 한 번 부릅니다. */
        void drawContent() override;
        /** @brief 프로퍼티 그리드가 배치 묶음을 새로 지은 횟수입니다(프로브 `Editor.InspectorLayoutRebuildCount`). */
        uint32 getLayoutRebuildCount() const { return _propertyGrid.getLayoutRebuildCount(); }
        /** @brief 그리드가 마지막 프레임에 그린 메서드 행 수입니다(`EditorPropertyGrid::getMethodRowCount`). */
        uint32 getMethodRowCount() const { return _propertyGrid.getMethodRowCount(); }
        /** @brief 그리드가 마지막 프레임에 숨긴(`CallInEditor` 없는) 메서드 수입니다. */
        uint32 getHiddenMethodCount() const { return _propertyGrid.getHiddenMethodCount(); }
        /** @brief 그리드가 마지막 프레임에 값을 채운 프로퍼티 행 수입니다(`EditorPropertyGrid::getFilledRowCount`). */
        uint32 getFilledRowCount() const { return _propertyGrid.getFilledRowCount(); }
        /** @brief 그리드가 마지막 프레임에 화면 밖이라 높이만 둔 행 수입니다(`EditorPropertyGrid::getSkippedRowCount`). */
        uint32 getSkippedRowCount() const { return _propertyGrid.getSkippedRowCount(); }
        /** @brief 컴포넌트 메뉴의 Presets 하위 메뉴 안(이름 붙여 저장, 저장한 프리셋 적용)을 그립니다. 하위 메뉴 명령 `inspector.component.presets` 가 부릅니다. */
        void drawComponentPresetItems( const EditorCommandTarget& target );

    private:
        /** @brief 현재 선택 섹션을 그립니다. */
        void drawSelectionSection();
        /** @brief 플레이 중이면 "편집은 Stop 에 되돌아간다" 안내를 그립니다. 단일 · 다중 선택이 함께 씁니다. */
        void drawPlayModeBanner() const;
        /** @brief 둘 이상 선택했을 때의 영역을 그립니다. 공통 컴포넌트만 그리고, 다른 값은 혼합으로 보여 주며, 고친 프로퍼티를 나머지에 적용합니다. 언리얼과 유니티와 같습니다. */
        void drawMultiSelection();
        /** @brief 프리팹 인스턴스일 때 적용, 되돌리기, 연결 해제 버튼을 그립니다. */
        void drawPrefabLinkSection();
        /** @brief 씬 인스턴스의 앵커 · 멤버 표시와 차이 저장 단추, 씬이 선언한 데이터 레이어의 소속을 그립니다. 해당 없으면 아무것도 그리지 않습니다. */
        void drawSceneAssemblySection();
        /** @brief 선택한 오브젝트의 컴포넌트 카드 목록을 그립니다. */
        void drawComponentList();
        /** @brief 직전 카드 헤더에 컴포넌트 맥락 메뉴(`scene.component.*` 명령 — 계층 창의 컴포넌트 행과 같은 메뉴)를 엽니다. */
        static void drawComponentContextMenu( uint64 componentID );
        /** @brief 컴포넌트 목록 끝의 Add Component 단추와 팝업입니다(유니티 Inspector 맨 아래, 언리얼 Details 의 Add). */
        void drawAddComponentButton( const utf8* pPopupID, const utf8* pMarkPrefix, bool bCentered );
        /** @brief GameObject 헤더(이름 등)를 그립니다. */
        void drawGameObjectHeader();
        /** @brief 컴포넌트 카드 안(확장 구역 · 프로퍼티 · 메서드)을 그립니다. */
        void drawComponentSection( const InspectorComponentCard& card );
        /**
         * @brief 맨 위 Favorites 구역을 그립니다. 오브젝트와 그 컴포넌트의 즐겨찾기 프로퍼티를 타입마다 모아 보입니다. 즐겨찾기가 없으면 그리지 않습니다.
         * @details 언리얼 Details 의 Favorites 에 해당한다. 다중 선택에서는 주 선택 오브젝트 기준이다.
         */
        void drawFavoritesSection();
        /**
         * @brief 미리보기 창의 선택을 그립니다. 값 편집만 낸다 — 기록은 그 창의 스택이다(`InspectorEditRecorder`).
         * @details 프리팹 줄은 활성 씬의 명령이라 내지 않는다. 컴포넌트 카드 메뉴는 메인 카드와 같은 컴포넌트 메뉴다.
         */
        void drawPreviewSelection();

    private:
        /** @brief 리플렉션 프로퍼티, 메서드, 이벤트를 그립니다. 검색 입력란을 포함하며, 컴포넌트와 오브젝트마다 대상을 바꿔 호출합니다. */
        EditorPropertyGrid _propertyGrid;
        InspectorViewModel _viewModel;
        InspectorViewState _state; ///< 프레임마다 `gather` 가 채운다 — 벡터의 저장 공간을 다시 쓴다
        /** @brief 이름 입력란에서 편집 중인 텍스트입니다. 편집 중이 아니면 프레임마다 오브젝트 이름으로 채우며, 입력란을 떠날 때 적용할 텍스트를 보관합니다. */
        fixed_string<constant::buffer::kMax256> _nameEditBuffer;
        vector<InspectorPresetRow>              _listPreset;         ///< Presets 메뉴를 그리는 동안의 줄
        uint64                                  _nameEditObjectID;   ///< `_nameEditBuffer` 가 가리키는 오브젝트다. 선택이 바뀌면 버린다
        fixed_string<constant::buffer::kMax64>  _addComponentSearch; ///< Add Component 팝업의 검색어
        fixed_string<constant::buffer::kMax64>  _presetNameBuffer;   ///< Presets 메뉴의 새 프리셋 이름 칸 글
    };
} // namespace sw::editor
