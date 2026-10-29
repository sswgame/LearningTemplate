/**
 * @file HierarchyRowModel.h
 * @brief 계층 창의 모델 — 루트 순서 · 필터 결과(행 모델), 그린 행 순서, 범위 선택 · 모두 선택 · 위아래 이동, 펼쳐 보이기, 이름 바꾸기 요청, 계층 창 명령. ImGui 를 모릅니다.
 * @details 패널(`HierarchyPanel`)은 트리 노드 · 토글 · 끌어 놓기 · 맥락 메뉴를 그리고 클릭 · 단축키를 읽어 이 모델에 넘긴다 — 인스펙터 · 프로파일러 `Model/` 과 같은 선이다
 *          (언리얼 Scene Outliner 가 트리 항목 모델(`ISceneOutlinerTreeItem`)과 Slate 위젯을 나누는 것과 같다). 상태는 에디터 범위의 상태 저장소가 든다(`getEditorState`)
 *          — 맥락 메뉴 명령(이름 바꾸기 요청 · 하위 메뉴 검색어)이 패널 인스턴스 없이 닿기 때문이다.
 */
#pragma once
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct HierarchyFilter
     * @brief 계층 창 검색어 문법입니다 — `t:컴포넌트` 는 컴포넌트 종류 앞부분, `tag:태그` 는 태그(하위 태그 포함), 그 밖은 이름(`EditorListFilter`)입니다.
     */
    struct HierarchyFilter
    {
        /** @brief @p pObj 가 검색어 @p pFilter 에 맞으면 true 입니다. 검색어가 비면 모두 맞습니다. */
        static bool matches( const GameObject* pObj, const utf8* pFilter );
    };

    /**
     * @class HierarchyRowModel
     * @brief 계층 창의 행 상태와 명령입니다. 파일 머리말 참고.
     * @details 프레임마다 전 오브젝트를 훑어 루트를 모으고 정렬하면 큐브 수천 개 씬에서 그것만으로 프레임을 먹는다. 필터를 켜면 노드마다 서브트리를
     *          재귀로 다시 물었다. 그래서 루트 순서와 필터 결과는 구조 세대 · 태그 세대 · 필터 글이 바뀔 때만 다시 짓는다(`refresh`). 언리얼 아웃라이너도 트리를 이벤트로 고친다.
     *          포인터를 들어도 된다. 오브젝트는 해체 전에 늘 구조 세대를 올리고(`GameObjectStore::destroyObject`), 그리기 전에 세대를 먼저 본다.
     */
    class HierarchyRowModel
    {
    public:
        /** @brief 에디터 범위의 상태 저장소가 가진 하나입니다. */
        static HierarchyRowModel& get();

        // ------------------------------------------------------------------------------
        // 1) 행 모델 — 루트 순서와 필터 결과
        // ------------------------------------------------------------------------------
        /**
         * @brief 행 모델이 지금 매니저 · 구조 세대 · 태그 세대 · 필터 글과 맞지 않으면 다시 짓습니다. 그리기 전에 한 번 부릅니다.
         * @details 필터는 걸린 오브젝트마다 조상을 거슬러 올라가며 표시한다(이미 표시된 조상에서 멈춘다). 노드마다 서브트리를 다시 묻지 않는다.
         */
        void refresh( GameObjectManager& manager, string_view filterText );
        /** @brief 필터가 꺼져 있거나, 이 오브젝트나 후손이 필터에 걸렸으면 true 입니다. */
        bool isRowVisible( const GameObject* pObj ) const;
        /** @brief 형제 순서의 루트(삭제 대기 제외)입니다. */
        const vector<GameObject*>& getRoots() const { return _listRoot; }
        /** @brief 행 모델을 다시 지은 횟수입니다(감시 값 `Editor.HierarchyRowRebuildCount`). 구조가 그대로인 프레임에는 오르지 않아야 한다. */
        uint32 getRebuildCount() const { return _rebuildCount; }

        // ------------------------------------------------------------------------------
        // 2) 그린 행 · 펼쳐 보이기
        // ------------------------------------------------------------------------------
        /** @brief 프레임 시작에 행 순서를 다시 모읍니다(지난 프레임의 것은 이전 행으로 넘긴다). 주 선택이 외부(뷰포트, 다른 패널)에서 바뀌었으면 그 행을 펼쳐 보이게 합니다. */
        void beginFrame();
        /** @brief 이번 프레임에 그린 오브젝트 행을 위에서 아래 순서로 더합니다. */
        void noteDrawnRow( uint64 objectID ) { _listDrawnRow.push_back( objectID ); }
        /** @brief 마지막으로 그린 프레임에서 @p objectID 의 행 번호(0 부터)입니다. 그리지 않았으면 -1 입니다. */
        int32 findDrawnRowIndex( uint64 objectID ) const;
        /** @brief 펼쳐서 보여 줄 오브젝트(뷰포트에서 고른 것)입니다. 없으면 0 입니다. */
        uint64 getRevealObjectID() const { return _revealObjectID; }
        /** @brief 그 행을 보였으니 펼쳐 보이기를 끝냅니다. */
        void clearReveal() { _revealObjectID = 0; }

        // ------------------------------------------------------------------------------
        // 3) 선택 — 지난 프레임에 그린 행 순서를 쓴다
        // ------------------------------------------------------------------------------
        /** @brief 행을 누른 것을 선택으로 옮깁니다. Shift 는 지난 프레임에 그린 순서로 범위(유니티 · 언리얼 · 윈도 탐색기와 같다), Ctrl 은 토글입니다. */
        void clickRow( GameObjectManager& manager, GameObject* pObj, bool bCtrl, bool bShift );
        /** @brief 지난 프레임에 보인 행을 모두 고릅니다. 접힌 자식은 빠집니다(유니티와 같다). */
        void selectAllRows( GameObjectManager& manager ) const;
        /** @brief 주 선택을 지난 프레임에 보인 행에서 한 칸 위나 아래로 옮깁니다. 행이 없으면 아무것도 하지 않습니다. */
        void selectStep( GameObjectManager& manager, bool bUp );

        // ------------------------------------------------------------------------------
        // 4) 이름 바꾸기 요청 · 메뉴 검색어
        // ------------------------------------------------------------------------------
        /** @brief 이름 바꾸기를 요청합니다(맥락 메뉴 · F2). 계층 창이 다음 프레임에 그 행의 입력 칸을 엽니다. */
        void requestRename( uint64 objectID ) { _pendingRenameID = objectID; }
        /** @brief 남은 이름 바꾸기 요청을 꺼냅니다. 없으면 0 입니다. */
        uint64 takePendingRename();
        /** @brief "Add Component" 메뉴의 검색 칸 글입니다(메뉴를 닫았다 열어도 남는다). */
        fixed_string<constant::kMaxBuffer64>& editAddComponentSearch() { return _addComponentSearch; }

    private:
        // 행 모델
        vector<GameObject*>                   _listRoot{};            ///< 형제 순서의 루트(삭제 대기 제외)
        unordered_set<uint64>                 _uniqueFilterMatchID{}; ///< 자기나 후손이 필터에 걸린 오브젝트 id. 필터가 꺼져 있으면 비어 있다
        fixed_string<constant::kMaxBuffer128> _filterText{};          ///< 지을 때의 필터 글
        const GameObjectManager*              _pManager{ nullptr };   ///< 지을 때의 매니저
        uint64                                _structureGeneration{ 0 };
        uint64                                _tagGeneration{ 0 };
        uint32                                _rebuildCount{ 0 }; ///< 다시 지은 횟수(감시 값 `Editor.HierarchyRowRebuildCount`)
        bool                                  _bFilterActive{ false };

        // 그린 행 · 선택
        vector<uint64>                       _listDrawnRow{};
        vector<uint64>                       _listPreviousRow{};
        uint64                               _anchorObjectID{ 0 };  ///< 범위 선택의 시작 행. 마지막으로 그냥 클릭하거나 Ctrl 을 누르고 클릭한 행이다
        uint64                               _revealObjectID{ 0 };  ///< 펼쳐서 보여 줄 오브젝트. 뷰포트에서 선택했다
        uint64                               _lastPrimaryID{ 0 };   ///< 지난 프레임의 주 선택. 외부에서 바뀌었는지 구분하는 데 쓴다
        uint64                               _pendingRenameID{ 0 }; ///< 이름 바꾸기 명령이 고른 오브젝트. 다음 그리기에서 입력 칸을 연다
        fixed_string<constant::kMaxBuffer64> _addComponentSearch{}; ///< "Add Component" 메뉴의 검색 칸 글(메뉴를 닫았다 열어도 남는다)
        bool                                 _bClickedThisFrame{ false };
    };

    /**
     * @struct HierarchyCommands
     * @brief 계층 창이 등록하는 명령의 본문입니다(형제 순서 이동 · 폴더 만들기 · 맥락 메뉴 대상 고르기).
     */
    struct HierarchyCommands
    {
        /** @brief 단일 선택(주 선택)된 오브젝트를 형제 사이에서 한 단계 옮깁니다(`scene.moveSiblingUp` · `Down`). 끝이면 아무것도 하지 않습니다. */
        static void moveSelectionStep( bool bDown );
        /** @brief 주 선택이 폴더면 그 안에, 아니면 루트에 정리용 폴더를 만듭니다(언리얼 아웃라이너의 새 폴더). */
        static void createFolder();
        /** @brief 맥락 메뉴의 대상과 같은 선택을 만듭니다(R2 — 선택 밖 행에서 열면 그 행만 고른다). */
        static void selectOnlyObject( uint64 objectID );
        /** @brief 메인 선택의 오브젝트 id 들입니다(맥락 메뉴의 대상 스냅숏). */
        static vector<uint64> collectSelectedObjectIDs();
        /** @brief @p pObj 가 @p pTarget 의 조상이면 true 입니다. */
        static bool isAncestorOf( const GameObject* pObj, const GameObject* pTarget );
        /** @brief 계층 창이 보이지 않는 에디터 카메라 오브젝트입니다. 형제 번호를 다시 매길 때 빼야 한다. 없으면 nullptr 입니다. */
        static const GameObject* findEditorCameraObject();
    };
} // namespace sw::editor
