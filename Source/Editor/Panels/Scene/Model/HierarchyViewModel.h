/**
 * @file HierarchyViewModel.h
 * @brief 계층 창의 ViewModel — 행 트리(`HierarchyRowModel`)에서 화면에 보이는 범위의 행만 세부 값으로 모으고(`gather`), 선택 · 펼침 · 이름 바꾸기 · 끌어 놓기 · 눈 · 자물쇠를 id 명령으로 받습니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 상태는 에디터 범위의 상태 저장소가 든다(`get`) — 맥락 메뉴 명령(이름 바꾸기 요청 · 하위 메뉴 검색어)이 패널 인스턴스 없이 닿기 때문이다.
 *          화면 밖의 행은 세부(라벨 · 배지 · 선택 · 프리팹 판정)를 만들지 않고 높이만 묶어 넘긴다(`HierarchyRowEntry::_skippedObjectRows`). 큐브 8000 개 루트 씬에서 프레임의 일은
 *          보이는 행 수에 비례하고 전체 행 수에 비례하지 않는다(언리얼 `STreeView` 의 가상화와 같은 몫).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Panels/Scene/Model/HierarchyRowModel.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /** @brief 행 위에 끌어 놓은 위치입니다. */
    enum class HierarchyDropZone : uint8
    {
        Before = 0, ///< 행의 위쪽 가장자리. 그 오브젝트 앞의 형제로 옮긴다
        Onto,       ///< 행의 가운데. 그 오브젝트의 자식으로 옮긴다
        After       ///< 행의 아래쪽 가장자리. 그 오브젝트 뒤의 형제로 옮긴다
    };

    /**
     * @struct HierarchyRowEntry
     * @brief 창이 한 프레임 그릴 항목 하나입니다(값만). 세부가 있는 행 하나이거나, 세부 없이 높이만 차지하는 행 묶음입니다(`_skippedObjectRows` · `_skippedComponentRows` 가 0 이 아니다).
     * @details 묶음은 화면 밖의 행이다 — 창은 같은 높이의 빈 공간만 두어 스크롤 길이와 다음 행 위치를 지킨다. 묶음 안의 깊이는 창이 몰라도 된다(빈 공간은 들여쓰기와 무관하다).
     */
    struct HierarchyRowEntry
    {
        fixed_string<constant::buffer::kMax256> _label{};                   ///< 트리 노드 라벨(아이콘 · 이름 · `###` 뒤 id — 이름이 바뀌어도 노드 id 가 같다)
        fixed_string<constant::buffer::kMax256> _badge{};                   ///< 이름 뒤에 회색으로 얹는 범주 배지(오브젝트 행만)
        fixed_string<constant::buffer::kMax128> _name{};                    ///< 오브젝트 이름(자체 테스트 표식 · 이름 바꾸기 칸)
        uint64                                  _id{ 0 };                   ///< 오브젝트 행은 오브젝트 id, 컴포넌트 행은 컴포넌트 id
        uint64                                  _objectID{ 0 };             ///< 행의 오브젝트(주인) id
        uint32                                  _skippedObjectRows{ 0 };    ///< 묶음: 세부 없이 넘긴 오브젝트 행 수
        uint32                                  _skippedComponentRows{ 0 }; ///< 묶음: 세부 없이 넘긴 컴포넌트 행 수
        uint16                                  _depth{ 0 };
        HierarchyRowKind                        _kind{ HierarchyRowKind::Object };
        uint8                                   _bExpanded{ SW_FALSE };       ///< 펼쳐져 있다(창이 `SetNextItemOpen` 으로 넘긴다)
        uint8                                   _bLeaf{ SW_FALSE };           ///< 펼칠 것이 없다
        uint8                                   _bSelected{ SW_FALSE };       ///< 선택한 행
        uint8                                   _bPrefabInstance{ SW_FALSE }; ///< 프리팹 · 씬 인스턴스(이름을 파랗게)
        uint8                                   _bHiddenInEditor{ SW_FALSE }; ///< 눈 토글이 꺼져 있다(에디터에서만 숨김)
        uint8                                   _bLocked{ SW_FALSE };         ///< 자물쇠 토글이 켜져 있다(뷰포트 선택 잠금)
        uint8                                   _bRenaming{ SW_FALSE };       ///< 이 행에서 이름을 바꾸는 중이다(화면 밖이어도 그린다 — 입력 칸이 살아 있어야 확정된다)
        uint8                                   _bScrollTo{ SW_FALSE };       ///< 선택 따라가기: 이 행으로 스크롤한다(화면 밖이어도 그린다)

        /** @brief 세부 없이 높이만 차지하는 묶음이면 true 입니다. */
        bool isSkippedRun() const { return _skippedObjectRows != 0 || _skippedComponentRows != 0; }
    };

    /** @brief 창이 gather 에 넘기는 트리 영역의 모양입니다(이번 프레임의 스크롤 — 세부를 채울 행 범위를 정한다). */
    struct HierarchyRowWindow
    {
        HierarchyRowMetrics _stride{};                      ///< 행 높이(스타일)
        float32             _scrollY{ 0.0f };               ///< 트리 영역의 스크롤 위치
        float32             _viewHeight{ 0.0f };            ///< 트리 영역의 보이는 높이
        uint8               _bSkipOffscreenRows{ SW_TRUE }; ///< 화면 밖의 행을 묶음으로 넘긴다(끄면 모든 행에 세부를 채운다 — 자체 테스트가 위치가 같은지 본다)
    };

    /** @brief 계층 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct HierarchyViewState
    {
        vector<HierarchyRowEntry>               _listEntry{};            ///< 위에서 아래 순서의 행 · 묶음
        vector<uint64>                          _listSelectedObjectID{}; ///< 메인 선택의 오브젝트 id(맥락 메뉴의 대상 스냅숏)
        fixed_string<constant::buffer::kMax256> _renameStartText{};      ///< 이번 프레임에 시작한 이름 바꾸기의 처음 글(`_bRenameStarted`)
        uint64                                  _selectedComponentID{ 0 };
        uint32                                  _visibleRootCount{ 0 };      ///< 필터를 지난 루트 수
        uint8                                   _bHasScene{ SW_FALSE };      ///< 편집 중인 씬이 있다
        uint8                                   _bFilterActive{ SW_FALSE };  ///< 검색어가 걸려 있다
        uint8                                   _bRenameStarted{ SW_FALSE }; ///< 이름 바꾸기 요청이 이번 프레임에 그 행의 입력 칸을 연다
    };

    /** @brief "모두 선택" 하위 메뉴(이 오브젝트의 컴포넌트 종류 · 태그를 가진 오브젝트 모두)의 항목입니다. 앞 `_typeCount` 개가 종류, 나머지가 태그입니다. */
    struct HierarchySelectSameMenu
    {
        vector<string> _listLabel{};
        uint32         _typeCount{ 0 };
    };

    /**
     * @class HierarchyViewModel
     * @brief 계층 창의 ViewModel 입니다. 파일 머리말 참고.
     */
    class HierarchyViewModel
    {
    public:
        /** @brief 행 위쪽 몇 분의 일이 Before, 아래쪽 몇 분의 일이 After 인지입니다. 가운데는 Onto 입니다. */
        static constexpr float32 kDropEdgeFraction = 0.3f;

        HierarchyViewModel();

        /** @brief 에디터 범위의 상태 저장소가 가진 하나입니다. */
        static HierarchyViewModel& get();
        /** @brief 편집 중인 씬이 있으면 true 입니다(창이 툴바를 그리기 전에 묻는다). */
        static bool hasActiveScene();
        /** @brief 커서 높이 @p cursorY 가 행 [@p rowMinY, @p rowMaxY] 의 어느 위치인지 정합니다. 행 높이가 0 이면 Onto 입니다. */
        static HierarchyDropZone computeDropZone( float32 cursorY, float32 rowMinY, float32 rowMaxY );

        /**
         * @brief 창이 그릴 값을 모읍니다(@p outState 를 비우고 채운다). 한 프레임에 한 번, 트리 영역을 연 뒤 부른다 — 이번 프레임의 스크롤(휠 · 스크롤 막대)로 세부를 채울 행을 고른다.
         * @param filterText 검색 칸의 글(이번 프레임에 친 글이 이번 트리에 닿는다).
         * @details 주 선택이 밖(뷰포트 · 다른 창)에서 바뀌었으면 그 조상을 펼치고 그 행으로 스크롤하게 한다. 세부는 보이는 범위 · 이름 바꾸는 행 · 스크롤할 행과 그 조상에만 채운다.
         */
        void gather( string_view filterText, const HierarchyRowWindow& window, HierarchyViewState& outState );

        // ------------------------------------------------------------------------------
        // 보기만 바꾸는 명령 — 되돌리기 · 씬 dirty 에 남지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 행을 펼치거나 접습니다(트리 노드의 결과). */
        void setExpanded( HierarchyRowKind kind, uint64 id, bool bExpanded ) { _rowModel.setExpanded( kind, id, bExpanded ); }
        /** @brief 이름 바꾸기를 요청합니다(맥락 메뉴 · F2). 다음 gather 가 그 행의 입력 칸을 엽니다. */
        void requestRename( uint64 objectID ) { _pendingRenameID = objectID; }
        /** @brief 이름 바꾸기를 끝냅니다(확정 · 취소 뒤). */
        void endRename() { _renamingObjectID = 0; }
        /** @brief "Add Component" 메뉴의 검색 칸 글입니다(메뉴를 닫았다 열어도 남는다). */
        fixed_string<constant::buffer::kMax64>& editAddComponentSearch() { return _addComponentSearch; }
        /** @brief 메인 월드를 보여 주는 이 창이 포커스다 — 인스펙터 · 전역 명령이 다시 메인 선택을 겨눈다(미리보기 창이 등록해 둔 것을 해제한다). */
        void focusMainSelection();
        /** @brief 눈 토글 — 에디터에서만 숨긴다. 저장 · 되돌리기 · 게임 동작에 남지 않는다(언리얼 · 유니티와 같다). 활성 비트는 인스펙터의 Active 체크박스다. */
        void setHiddenInEditor( uint64 objectID, bool bHidden );
        /** @brief 자물쇠 토글 — 뷰포트에서 선택하지 못하게 한다(계층에서는 고를 수 있다). 저장하지 않는다. */
        void setObjectLocked( uint64 objectID, bool bLocked );

        // ------------------------------------------------------------------------------
        // 선택 — 편 행 순서(접힌 자식 제외)를 쓴다
        // ------------------------------------------------------------------------------
        /** @brief 행을 누른 것을 선택으로 옮깁니다. Shift 는 행 순서로 범위(유니티 · 언리얼 · 윈도 탐색기와 같다), Ctrl 은 토글입니다. */
        void clickRow( uint64 objectID, bool bCtrl, bool bShift );
        /** @brief 컴포넌트 행을 누른 것을 선택으로 옮깁니다. */
        void selectComponent( uint64 objectID, uint64 componentID );
        /** @brief 보이는 오브젝트 행을 모두 고릅니다. 접힌 자식은 빠집니다(유니티와 같다). */
        void selectAllRows();
        /** @brief 주 선택을 행 순서에서 한 칸 위나 아래로 옮깁니다. 행이 없으면 아무것도 하지 않습니다. */
        void selectStep( bool bUp );

        // ------------------------------------------------------------------------------
        // 문서를 고치는 명령 — 씬 명령(`EditorSceneCommands` · `EditorSiblingOrder` · `EditorAssetCommands`)이 되돌리기 한 단계와 씬 dirty 를 남긴다
        // ------------------------------------------------------------------------------
        /** @brief 루트에 빈 오브젝트를 만듭니다. */
        void createObject();
        /** @brief 이름을 바꿉니다. 빈 이름은 거절해 이름이 그대로 남는다. */
        void renameObject( uint64 objectID, const utf8* pName );
        /** @brief 끌어 놓은 오브젝트를 @p targetID 행의 앞 · 뒤 형제로(형제 순서를 저장한다), 가운데면 자식으로 옮깁니다(유니티 하이어라키의 끌기와 같다). */
        void dropObject( uint64 draggedID, uint64 targetID, HierarchyDropZone zone );
        /** @brief 끌어 놓은 오브젝트를 루트로 뗍니다(트리 아래 빈 곳에 놓기). 이미 루트면 아무것도 하지 않는다. */
        void detachToRoot( uint64 draggedID );
        /** @brief 끌어 놓은 프리팹 에셋을 @p parentID 아래(0 이면 루트)에 스폰합니다. */
        void spawnPrefab( const utf8* pPath, uint64 parentID );
        /** @brief @p objectID 오브젝트에 @p typeName 컴포넌트를 더합니다("Add Component" 하위 메뉴). */
        void addComponent( uint64 objectID, const hashed_string& typeName );
        /** @brief "모두 선택" 하위 메뉴의 @p itemIndex 번째 항목(`collectSelectSameMenu` 순서)을 가진 오브젝트를 모두 고릅니다. */
        void selectAllWith( uint64 objectID, uint32 itemIndex );

        // ------------------------------------------------------------------------------
        // 메뉴가 열렸을 때만 묻는 값 — 씬 전체를 훑으므로 프레임마다 모으지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 형식 필터 목록(씬에 있는 컴포넌트 종류)입니다. */
        void collectTypeFilterNames( vector<string>& outListName ) const;
        /** @brief "모두 선택" 하위 메뉴의 항목입니다(오브젝트의 컴포넌트 종류, 그다음 태그). 오브젝트가 없으면 비어 있다. */
        void collectSelectSameMenu( uint64 objectID, HierarchySelectSameMenu& outMenu ) const;

        // ------------------------------------------------------------------------------
        // 감시 값
        // ------------------------------------------------------------------------------
        /** @brief 이름이 @p objectName 인 오브젝트가 오브젝트 행 사이에서 몇 번째인지(0 부터)입니다. 행에 없으면(접힌 조상 아래 · 필터 밖 · 없음) 없음입니다. */
        [[nodiscard]] optional<uint32> findObjectRowIndexByName( string_view objectName ) const;
        /** @brief 루트 · 필터를 다시 지은 횟수입니다(감시 값 `Editor.HierarchyRowRebuildCount`). */
        uint32 getRebuildCount() const { return _rowModel.getRebuildCount(); }

    private:
        /** @brief 주 선택이 밖에서 바뀌었으면 그 조상을 펼치고 따라갈 행으로 둡니다. */
        void revealExternalSelection();
        /** @brief 세부를 채울 행 번호를 모읍니다(보이는 범위 · 이름 바꾸는 행 · 스크롤할 행과 그 조상, 정렬 · 겹침 없이). */
        void collectDetailRows( const HierarchyRowWindow& window, vector<uint32>& outListRow ) const;
        /** @brief @p row 행의 세부를 @p outEntry 에 채웁니다. */
        void fillRowDetail( uint32 row, HierarchyRowEntry& outEntry ) const;
        /** @brief 행 [@p firstRow, @p endRow) 를 묶음 하나로 더합니다. 비면 더하지 않는다. */
        void appendSkippedRun( uint32 firstRow, uint32 endRow, vector<HierarchyRowEntry>& outListEntry ) const;

    private:
        HierarchyRowModel                      _rowModel;
        vector<uint32>                         _listDetailRow;         ///< 세부를 채울 행 번호(gather 마다 다시 쓴다 — 저장소를 재사용한다)
        vector<uint64>                         _listRowIDScratch;      ///< 범위 선택 · 위아래 이동에 쓰는 오브젝트 행 id(저장소를 재사용한다)
        uint64                                 _anchorObjectID;        ///< 범위 선택의 시작 행. 마지막으로 그냥 클릭하거나 Ctrl 을 누르고 클릭한 행이다
        uint64                                 _revealObjectID;        ///< 따라갈 오브젝트. 뷰포트에서 선택했다
        uint64                                 _lastPrimaryID;         ///< 지난 gather 의 주 선택. 외부에서 바뀌었는지 구분하는 데 쓴다
        uint64                                 _pendingRenameID;       ///< 이름 바꾸기 명령이 고른 오브젝트. 다음 gather 가 입력 칸을 연다
        uint64                                 _renamingObjectID;      ///< 이름을 바꾸는 중인 오브젝트(0 이면 없음)
        fixed_string<constant::buffer::kMax64> _addComponentSearch;    ///< "Add Component" 메뉴의 검색 칸 글(메뉴를 닫았다 열어도 남는다)
        uint8                                  _bClickedThisFrame : 1; ///< 이 창에서 고른 선택이다(따라가기 대상이 아니다)
    };

    /**
     * @struct HierarchyCommands
     * @brief 계층 창이 등록하는 명령의 본문입니다(형제 순서 이동 · 폴더 만들기 · 맥락 메뉴 대상 고르기 · 이름 바꾸기 요청).
     */
    struct HierarchyCommands
    {
        /** @brief 단일 선택(주 선택)된 오브젝트를 형제 사이에서 한 단계 옮깁니다(`scene.moveSiblingUp` · `Down`). 끝이면 아무것도 하지 않습니다. */
        static void moveSelectionStep( bool bDown );
        /** @brief 주 선택이 폴더면 그 안에, 아니면 루트에 정리용 폴더를 만듭니다(언리얼 아웃라이너의 새 폴더). */
        static void createFolder();
        /** @brief 맥락 메뉴의 대상과 같은 선택을 만듭니다(R2 — 선택 밖 행에서 열면 그 행만 고른다). */
        static void selectOnlyObject( uint64 objectID );
        /** @brief 메인 선택의 오브젝트 id 들을 @p outListID 에 모읍니다(먼저 비운다 — 맥락 메뉴의 대상 스냅숏). */
        static void collectSelectedObjectIDs( vector<uint64>& outListID );
        /** @brief 계층 창이 보이지 않는 에디터 카메라 오브젝트입니다. 형제 번호를 다시 매길 때 빼야 한다. 없으면 nullptr 입니다. */
        static const GameObject* findEditorCameraObject();
        /** @brief 메뉴 대상 오브젝트(누른 행, 빈 곳이면 주 선택)의 id 입니다. 없으면 0 입니다(`EditorSceneObjectCommands::findTargetObject`). */
        static uint64 findTargetObjectID( const EditorCommandTarget& target );
        /** @brief 메뉴 대상이 오브젝트 하나면 true 입니다(이름 바꾸기 · 하위 메뉴의 켜짐). */
        static bool hasSingleTargetObject( const EditorCommandTarget& target ) { return findTargetObjectID( target ) != 0; }
        /** @brief 이름 바꾸기를 요청합니다(맥락 메뉴 · F2 — `scene.object.rename`). 계층 창이 다음 프레임에 그 행의 입력 칸을 엽니다. */
        static void requestRename( const EditorCommandTarget& target );
    };
} // namespace sw::editor
