/**
 * @file BehaviorTreeEditModel.h
 * @brief 행동 트리 편집 창의 화면 없는 편집 모델입니다. 데이터 파일(`*.bt.xml`)을 고칠 수 있는 트리로 읽고, 노드 더하기 · 지우기 · 순서 바꾸기 · 속성 고치기를 받고, 파일 글로 되씁니다.
 * @details ImGui 를 모른다. 로더(`BehaviorTreeAsset`)는 틀린 파일을 읽지 않지만 편집기는 틀린 중간 상태(자식 없는 컴포짓, 아직 안 고른 작업 이름)를
 *          들고 있어야 하므로 트리 짓기는 따로 두고, 구조 문제는 `collectIssues` 가 알린다. 노드 원소의 칸 읽기 · 쓰기는 로더와 같은 `BehaviorTreeXML` 이다(스키마는 `BehaviorTreeNodeParams.h`).
 *          **저장하면 주석이 사라진다**(트리를 글로 새로 쓴다) - 설명은 노드 `name` 에 적는다. 노드 번호(`_id`)는 편집 중에만 안정이고, 파일에는 쓰지 않는다.
 *          깊이 우선(앞 자식부터) 순서가 런타임 `BehaviorTree` 의 노드 번호와 같다(`collectPreorder`) - 플레이 중 활성 노드 표시가 이 대응을 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTree.h"
#include "GameFramework/Runtime/Character/AI/BehaviorTreeNodeParams.h"

namespace sw
{
    class XMLNode;
} // namespace sw

namespace sw::editor
{
    /** @brief 편집 중인 노드 하나입니다. 칸은 런타임과 같은 `BehaviorTreeNodeData` 이고(종류에 맞는 칸만 뜻이 있다), 편집기는 번호 · 부모 · 자식 목록만 더한다. */
    struct BehaviorTreeEditNode : BehaviorTreeNodeData
    {
        int32         _id{ 0 };      ///< 편집 중에만 안정인 번호입니다.
        int32         _parent{ -1 }; ///< 부모 번호입니다(루트는 -1).
        vector<int32> _listChild{};  ///< 자식 번호를 앞(우선순위 높음)에서 뒤로 늘어놓았습니다.
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 저장 전에 고쳐야 할 문제 하나입니다. */
    struct BehaviorTreeEditIssue
    {
        int32  _nodeID{ -1 };
        string _message{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class BehaviorTreeEditModel
     * @brief 노드 트리 하나의 편집 상태입니다. 루트는 하나다.
     */
    class BehaviorTreeEditModel
    {
    public:
        /** @brief 노드가 없는 빈 문서로 되돌립니다. */
        void clear();
        /**
         * @brief 파일 글을 읽어 모델을 바꿉니다. 모르는 노드 · 속성 이름, 루트가 둘이거나 없는 글은 읽지 않고 이유(`Error`)를 돌려줍니다(모델은 그대로).
         * @details 동작 이름이 레지스트리에 있는지는 보지 않는다 - 그것은 `collectIssues` 의 문제 표시다(편집기는 아직 안 만든 작업의 트리도 열 수 있어야 한다).
         */
        [[nodiscard]] expected<void> parse( string_view xmlText );
        /** @brief 파일 글로 씁니다(`<BehaviorTree>` 루트, 노드마다 한 줄, 탭 들여쓰기). 루트가 없으면 빈 `<BehaviorTree/>` 입니다. */
        string write() const;
        /**
         * @brief 파일에 쓸 글을 만듭니다. 부모(`_base`)를 가리키던 글에서 읽었으면 부모와의 차이(`DataInheritance::makeDelta`, 노드 `id` 가 열쇠)이고, 아니면 `write` 와 같습니다.
         * @details 차이를 다시 읽어 같은 트리가 나올 때만 차이를 돌려줍니다 — 열쇠(`id`) 없는 노드를 고친 편집처럼 차이로 적을 수 없으면 이유(`Error`)를 돌려준다(부모를 끊지 않는다).
         */
        [[nodiscard]] expected<string> makeSaveText() const;
        /** @brief 읽은 글이 가리킨 부모의 리소스 id 입니다. 부모 없이 읽었거나 비웠으면 비어 있습니다. */
        const string& getBaseID() const { return _baseID; }
        /** @brief 부모를 정합니다. 되돌리기가 펼친 글로 복원한 모델에 문서의 부모를 다시 단다. */
        void setBaseID( string_view baseID ) { _baseID = string{ baseID }; }

        int32                       getRootID() const { return _rootID; }
        uint32                      getNodeCount() const { return static_cast<uint32>( _listNode.size() ); }
        const BehaviorTreeEditNode* find( int32 id ) const;
        BehaviorTreeEditNode*       find( int32 id );

        /**
         * @brief @p parentID 아래 맨 뒤에 @p type 노드를 더합니다. @p parentID 가 -1 이면 루트이고 이미 루트가 있으면 거절합니다.
         * @return 새 노드 번호입니다. 거절하면(잎 아래, 자식이 이미 있는 데코레이터 아래, 루트 중복, 없는 부모) -1 입니다.
         */
        int32 addNode( int32 parentID, BehaviorNodeKind type );
        /** @brief 노드와 그 아래를 모두 지웁니다. 없는 번호면 false 입니다. 루트를 지우면 빈 문서가 됩니다. */
        [[nodiscard]] bool removeNode( int32 id );
        /** @brief 형제 사이에서 @p delta 칸(음수는 앞으로 = 우선순위 높게) 옮깁니다. 끝에 닿으면 거기서 멈추고, 움직였으면 true 입니다. */
        [[nodiscard]] bool moveChild( int32 id, int32 delta );
        /**
         * @brief 노드와 그 아래를 @p newParentID 의 맨 뒤 자식으로 옮깁니다(캔버스에서 링크를 새로 이을 때). 루트, 제 자손 아래, 잎이나 이미 찬 데코레이터 아래,
         *        깊이 한도(`BehaviorTreeNames::kMaxDepth`)를 넘는 자리, 지금과 같은 부모는 거절하고 false 입니다.
         */
        [[nodiscard]] bool reparent( int32 id, int32 newParentID );
        /** @brief 노드 @p id 를 루트로 한 하위 트리를 파일 글로 씁니다(`write` 와 같은 모양 — 복사 버퍼가 이 글을 둔다). 없는 번호면 빈 트리 글입니다. */
        string writeSubtree( int32 id ) const;
        /**
         * @brief 트리 글 @p xmlText(`writeSubtree` · 파일)의 루트를 @p parentID 의 자식 @p insertIndex 자리(-1 이면 맨 뒤)에 새 번호로 붙입니다(언리얼 BT 의 붙여 넣기).
         * @details 글은 `parse` 와 같은 검사를 따로 거친 뒤에 붙인다 — 틀린 글이나 놓을 수 없는 자리(잎 · 찬 데코레이터 아래, 깊이 한도)면 모델을 그대로 두고 이유(`Error`)를 돌려준다.
         *          @p parentID 가 -1 이면 빈 트리의 루트로 붙인다(루트가 있으면 거절).
         * @return 붙인 하위 트리 루트의 새 번호입니다.
         */
        [[nodiscard]] expected<int32> pasteSubtree( int32 parentID, string_view xmlText, int32 insertIndex );
        /** @brief 노드 @p id 와 그 아래를 바로 뒤 형제로 복제합니다(언리얼 BT 의 Duplicate — `writeSubtree` + `pasteSubtree`). 루트이거나 부모가 더 받지 못하면 이유(`Error`)입니다. */
        [[nodiscard]] expected<int32> duplicateSubtree( int32 id );
        /** @brief 루트가 0 인 깊이입니다. 없는 번호면 0 입니다. */
        uint32 getDepth( int32 id ) const;

        /** @brief 저장 전에 고쳐야 할 문제를 모읍니다: 루트 없음, 자식이 모자란 컴포짓, 자식 수가 하나가 아닌 데코레이터, 빈 작업 · 키, 틀린 조건식, 등록되지 않은 동작 이름, 음수 시간. */
        void collectIssues( vector<BehaviorTreeEditIssue>& outListIssue ) const;
        /** @brief 앞 자식부터 깊이 우선 순서의 노드 번호입니다 - 읽은 `BehaviorTree` 의 노드 번호(`getNode( i )`)와 i 번째가 같은 노드다. */
        void collectPreorder( vector<int32>& outListID ) const;
        /** @brief 노드의 표시 이름입니다(이름이 있으면 그것, 없으면 종류 이름). */
        string makeDisplayName( int32 id ) const;

    private:
        [[nodiscard]] bool canHoldChild( const BehaviorTreeEditNode& parent ) const;
        uint32             getSubtreeHeight( int32 id ) const;
        void               detachFromParent( BehaviorTreeEditNode& node );
        void               collectSubtree( int32 id, vector<int32>& outListID ) const;
        /** @brief @p source 의 노드 @p sourceID 와 그 아래를 새 번호로 이 모델에 더하고(부모 목록에는 넣지 않는다) 새 번호를 돌려줍니다. 자리 검사는 부르는 쪽이 했다. */
        int32                         graftSubtree( const BehaviorTreeEditModel& source, int32 sourceID, int32 parentID );
        [[nodiscard]] expected<int32> loadNodeFromXML( const XMLNode& node, int32 parentID, uint32 depth );
        void                          writeNode( int32 id, const XMLNode& parentXML ) const;

    private:
        vector<BehaviorTreeEditNode> _listNode{};
        int32                        _rootID{ -1 };
        int32                        _nextID{ 1 };
        string                       _baseID{}; ///< 읽은 글의 `_base`. `makeSaveText` 가 부모와의 차이로 쓴다
    };
} // namespace sw::editor
