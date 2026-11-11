/**
 * @file HierarchyRowModel.h
 * @brief 계층 창의 행 트리 — 루트 순서 · 필터 결과 · 펼침 상태와, 그것으로 편 보이는 행 목록(오브젝트 · 씬 컴포넌트 · 컴포넌트, 깊이). ImGui 를 모릅니다.
 * @details 계층 창 ViewModel(`HierarchyViewModel`)이 이 모델을 들고 프레임마다 화면에 보이는 범위의 행만 세부 값으로 옮긴다. 펼침 상태는 ImGui 저장소가 아니라 여기가 든다
 *          (언리얼 아웃라이너의 `ExpansionState` — 창이 `SetNextItemOpen` 으로 넘기고 트리 노드의 결과를 `setExpanded` 로 돌려준다). 행 목록은 구조 · 태그 세대 · 필터 글 ·
 *          펼침이 바뀔 때만 다시 편다. 큐브 8000 개 루트 씬에서도 구조가 그대로인 프레임에는 행 수에 비례하는 일이 없다.
 */
#pragma once
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

namespace sw
{
    class Component;
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
        /**
         * @brief 씬에 있는 컴포넌트 종류 이름을 이름순 · 겹침 없이 모읍니다. 형식 필터 목록(`t:<종류>`)이 씁니다.
         * @details 고를 종류를 표로 두지 않고 씬에서 모은다 — 유니티 계층 창의 형식 검색처럼 지금 씬에 있는 것만 보이고, 게임이 더한 컴포넌트도 바로 든다.
         */
        static void collectComponentTypeNames( const GameObjectManager& manager, vector<string>& outListName );
        /** @brief 검색어가 형식 필터(`t:<종류>`)이면 종류 이름을, 아니면 빈 글을 돌려줍니다. */
        static string_view findTypeFilter( string_view filter );
    };

    /** @brief 계층 창 행의 종류입니다. 높이가 다르다 — 오브젝트 행은 토글 단추가 있어 프레임 높이, 컴포넌트 행은 글 높이다. */
    enum class HierarchyRowKind : uint8
    {
        Object = 0,     ///< 게임 오브젝트(id 는 오브젝트 id)
        SceneComponent, ///< 씬 컴포넌트(id 는 컴포넌트 id, 자식 씬 컴포넌트를 펼친다)
        Component       ///< 씬 컴포넌트가 아닌 컴포넌트(id 는 컴포넌트 id, 잎)
    };

    /**
     * @struct HierarchyTreeRow
     * @brief 편 행 하나입니다(모델 안의 캐시). 포인터는 구조 세대가 그대로인 동안만 유효하다 — `HierarchyRowModel::refresh` 직후에만 읽는다. 그 밖에서는 id 를 쓴다.
     */
    struct HierarchyTreeRow
    {
        GameObject*      _pObject{ nullptr };    ///< 행의 오브젝트(컴포넌트 행이면 주인)
        Component*       _pComponent{ nullptr }; ///< 컴포넌트 행의 컴포넌트. 오브젝트 행이면 nullptr
        uint64           _id{ 0 };               ///< 오브젝트 행은 오브젝트 id, 컴포넌트 행은 컴포넌트 id
        uint64           _objectID{ 0 };         ///< 행의 오브젝트(주인) id
        uint32           _parentRow{ 0 };        ///< 부모 행 번호. 루트면 `kNoRow`
        uint32           _objectRowsBefore{ 0 }; ///< 이 행 위의 오브젝트 행 수(행의 높이 위치를 계산한다 — 나머지는 컴포넌트 행)
        uint16           _depth{ 0 };            ///< 트리 깊이(루트 0)
        HierarchyRowKind _kind{ HierarchyRowKind::Object };
        uint8            _bExpanded{ SW_FALSE }; ///< 펼쳐져 자식 행이 뒤따른다
        uint8            _bLeaf{ SW_FALSE };     ///< 펼칠 것이 없다(화살표 없음)
    };

    /** @brief 행의 높이입니다(창이 스타일에서 정한다). 오브젝트 행과 컴포넌트 행의 간격을 포함한 높이다. */
    struct HierarchyRowMetrics
    {
        float32 _object{ 0.0f };    ///< 오브젝트 행(토글 단추 줄 — 프레임 높이 + 줄 간격)
        float32 _component{ 0.0f }; ///< 컴포넌트 행(글 높이 + 줄 간격)
    };

    /**
     * @class HierarchyRowModel
     * @brief 계층 창의 행 트리입니다. 파일 머리말 참고.
     * @details 프레임마다 전 오브젝트를 훑어 루트를 모으고 정렬하면 큐브 수천 개 씬에서 그것만으로 프레임을 먹는다. 필터를 켜면 노드마다 서브트리를
     *          재귀로 다시 물었다. 그래서 루트 순서와 필터 결과는 구조 세대 · 태그 세대 · 필터 글이 바뀔 때만 다시 짓고(`refresh`), 행 목록은 그때와 펼침이 바뀔 때만 다시 편다.
     *          언리얼 아웃라이너도 트리를 이벤트로 고친다. 오브젝트는 해체 전에 늘 구조 세대를 올리고(`GameObjectStore::destroyGameObject`), 그리기 전에 세대를 먼저 본다.
     */
    class HierarchyRowModel
    {
    public:
        /** @brief 부모가 없는 행의 `_parentRow` 입니다. */
        static constexpr uint32 kNoRow = 0xFFFFFFFFu;

        HierarchyRowModel();

        /**
         * @brief 행 모델이 지금 매니저 · 구조 세대 · 태그 세대 · 필터 글 · 숨길 루트와 맞지 않으면 다시 짓고, 그것이나 펼침이 바뀌었으면 행을 다시 폅니다. 행을 읽기 전에 한 번 부릅니다.
         * @param hiddenRootID 목록에 넣지 않을 루트 오브젝트 id(화면을 그리는 에디터 카메라 — 편집 대상이 아니고 저장에서도 빠진다). 0 이면 없다.
         * @details 필터는 걸린 오브젝트마다 조상을 거슬러 올라가며 표시한다(이미 표시된 조상에서 멈춘다). 노드마다 서브트리를 다시 묻지 않는다. 펼친 오브젝트의 컴포넌트 행은 필터와 무관하게 모두 든다.
         */
        void refresh( GameObjectManager& manager, string_view filterText, uint64 hiddenRootID );
        /** @brief 필터가 꺼져 있거나, 이 오브젝트나 후손이 필터에 걸렸으면 true 입니다. */
        bool isRowVisible( const GameObject* pObj ) const;
        /** @brief 편 행입니다(위에서 아래 순서). */
        const vector<HierarchyTreeRow>& getRows() const { return _listRow; }
        /** @brief 필터를 지난 루트 수입니다(숨길 루트 제외). */
        uint32 getVisibleRootCount() const { return _visibleRootCount; }
        /** @brief 필터가 켜져 있으면 true 입니다(지을 때의 글). */
        bool isFilterActive() const { return _bFilterActive != 0; }
        /** @brief 루트 · 필터를 다시 지은 횟수입니다(감시 값 `Editor.HierarchyRowRebuildCount`). 구조가 그대로인 프레임에는 오르지 않아야 한다. */
        uint32 getRebuildCount() const { return _rebuildCount; }

        // ------------------------------------------------------------------------------
        // 펼침 — 창이 트리 노드의 결과로 알려 준다. 보기만 바꾸므로 되돌리기에 남지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 행이 펼쳐져 있는지입니다(@p kind 는 오브젝트 또는 씬 컴포넌트). */
        bool isExpanded( HierarchyRowKind kind, uint64 id ) const;
        /** @brief 행을 펼치거나 접습니다. 바뀌면 다음 `refresh` 가 행을 다시 폅니다. */
        void setExpanded( HierarchyRowKind kind, uint64 id, bool bExpanded );
        /** @brief @p object 의 조상을 모두 펼칩니다(선택 따라가기 — 언리얼 Outliner 와 같다). */
        void expandAncestors( const GameObject& object );

        // ------------------------------------------------------------------------------
        // 행 찾기 — id 만 읽는다(명령이 프레임 중간에 구조를 바꿔도 낡은 포인터를 읽지 않는다)
        // ------------------------------------------------------------------------------
        /** @brief 오브젝트 행 사이에서 @p objectID 의 번호(0 부터)입니다. 행 목록에 없으면(접힌 조상 아래 · 필터 밖) 없음입니다. */
        [[nodiscard]] optional<uint32> findObjectRowIndex( uint64 objectID ) const;
        /** @brief @p objectID 의 오브젝트 행 번호(편 목록 안)입니다. 없으면 없음입니다. */
        [[nodiscard]] optional<uint32> findRowOfObject( uint64 objectID ) const;
        /** @brief 오브젝트 행의 id 를 위에서 아래 순서로 모읍니다(범위 선택 · 모두 선택 · 위아래 이동). 접힌 자식은 빠진다(유니티와 같다). */
        void collectObjectRowIDs( vector<uint64>& outListID ) const;
        /** @brief @p row 행 위의 오브젝트 행 수입니다(@p row 가 행 수면 전체 오브젝트 행 수). 나머지 행은 컴포넌트 행이다. */
        uint32 countObjectRowsBefore( uint32 row ) const { return row < _listRow.size() ? _listRow[row]._objectRowsBefore : _objectRowCount; }
        /** @brief @p row 행의 위쪽 높이 위치입니다(트리 맨 위부터, @p row 가 행 수면 마지막 행의 아래 끝). */
        float32 computeRowOffset( uint32 row, const HierarchyRowMetrics& stride ) const;
        /** @brief 높이 위치 @p offset 에 놓인 행 번호입니다(위치가 첫 행 위면 0, 끝 아래면 마지막 행). 행이 없으면 0 입니다. 이분 탐색이다. */
        uint32 findRowAtOffset( float32 offset, const HierarchyRowMetrics& stride ) const;

    private:
        /** @brief 루트부터 행을 다시 폅니다. */
        void rebuildRows();
        /** @brief 오브젝트 행과, 펼쳤으면 그 자식 오브젝트 · 컴포넌트 행을 더합니다. */
        void appendObjectRows( GameObject* pObj, uint32 parentRow, uint16 depth );
        /** @brief 씬 컴포넌트 행과, 펼쳤으면 같은 오브젝트의 자식 씬 컴포넌트 행을 더합니다. */
        void appendSceneComponentRows( GameObject* pObj, Component* pSceneComponent, uint32 parentRow, uint16 depth );
        /** @brief 행 하나를 더하고 그 번호를 돌려줍니다. */
        uint32 pushRow( HierarchyTreeRow row );

    private:
        // 루트 · 필터
        vector<GameObject*>                     _listRoot;            ///< 형제 순서의 루트(삭제 대기 제외)
        unordered_set<uint64>                   _uniqueFilterMatchID; ///< 자기나 후손이 필터에 걸린 오브젝트 id. 필터가 꺼져 있으면 비어 있다
        fixed_string<constant::buffer::kMax128> _filterText;          ///< 지을 때의 필터 글
        const GameObjectManager*                _pManager;            ///< 지을 때의 매니저
        uint64                                  _structureGeneration;
        uint64                                  _tagGeneration;
        uint64                                  _hiddenRootID; ///< 지을 때 뺀 루트(에디터 카메라)
        uint32                                  _rebuildCount; ///< 다시 지은 횟수(감시 값 `Editor.HierarchyRowRebuildCount`)

        // 펼침 · 편 행
        unordered_set<uint64>    _uniqueExpandedObjectID;    ///< 펼친 오브젝트 id
        unordered_set<uint64>    _uniqueExpandedComponentID; ///< 펼친 씬 컴포넌트 id
        vector<HierarchyTreeRow> _listRow;                   ///< 편 행(위에서 아래)
        uint32                   _objectRowCount;            ///< 편 행 중 오브젝트 행 수
        uint32                   _visibleRootCount;          ///< 필터를 지난 루트 수
        uint8                    _bFilterActive : 1;
        uint8                    _bRowsDirty    : 1; ///< 펼침이 바뀌어 행을 다시 펴야 한다
    };
} // namespace sw::editor
