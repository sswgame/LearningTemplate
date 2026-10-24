/**
 * @file BehaviorTreeEditModel.h
 * @brief 행동 트리 편집 창의 화면 없는 편집 모델입니다. 데이터 파일(`*.bt.xml`)을 고칠 수 있는 트리로 읽고, 노드 더하기 · 지우기 · 순서 바꾸기 · 속성 고치기를 받고, 파일 글로 되씁니다.
 * @details ImGui 를 모르므로 EngineTest 가 직접 컴파일해 테스트한다. 로더(`BehaviorTreeAsset`)는 틀린 파일을 읽지 않지만 편집기는 틀린 중간 상태(자식 없는 컴포짓, 아직 안 고른 작업 이름)를
 *          들고 있어야 하므로 읽기를 따로 두고, 구조 문제는 `collectIssues` 가 알린다. 노드 종류 · 비교 · 중단 이름은 로더와 같은 표(`BehaviorTreeNames`)를 쓴다.
 *          **저장하면 주석이 사라진다**(트리를 글로 새로 쓴다) - 설명은 노드 `name` 에 적는다. 노드 번호(`_id`)는 편집 중에만 안정이고, 파일에는 쓰지 않는다.
 *          깊이 우선(앞 자식부터) 순서가 런타임 `BehaviorTree` 의 노드 번호와 같다(`collectPreorder`) - 플레이 중 활성 노드 표시가 이 대응을 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Actor/AI/BehaviorTree.h"

namespace sw
{
    class XMLNode;
} // namespace sw

namespace sw::editor
{
    /** @brief 편집 중인 노드 하나입니다. 종류에 맞는 칸만 뜻이 있다(나머지는 기본값으로 두고 파일에 쓰지 않는다). */
    struct BehaviorTreeEditNode
    {
        int32             _id{ 0 };      ///< 편집 중에만 안정인 번호입니다.
        int32             _parent{ -1 }; ///< 부모 번호입니다(루트는 -1).
        BehaviorNodeType  _type{ BehaviorNodeType::Sequence };
        string            _name{}; ///< Selector · Sequence · Action · Condition 의 이름입니다(비면 종류 이름).
        string            _task{}; ///< Action · Condition 이 부를 함수 이름입니다(`BehaviorTaskRegistry`).
        string            _key{};  ///< BlackboardCondition · MoveTo 의 블랙보드 키입니다.
        BlackboardCompare _compare{ BlackboardCompare::IsSet };
        BehaviorAbortMode _abort{ BehaviorAbortMode::None };
        float32           _value{ 0.0f };          ///< BlackboardCondition 의 비교 값입니다.
        float32           _seconds{ 0.0f };        ///< Cooldown · TimeLimit · Wait 입니다.
        float32           _repathDistance{ 0.5f }; ///< MoveTo 입니다.
        int32             _count{ 0 };             ///< Repeat 입니다(0 이면 실패할 때까지).
        bool              _bReactive{ false };     ///< Selector 입니다.
        vector<int32>     _listChild{};            ///< 자식 번호를 앞(우선순위 높음)에서 뒤로 늘어놓았습니다.
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
         * @brief 파일 글을 읽어 모델을 바꿉니다. 모르는 노드 · 속성 이름, 루트가 둘이거나 없는 글은 읽지 않고 false 이며 @p outError 에 이유를 둡니다(모델은 그대로).
         * @details 작업 이름이 레지스트리에 있는지는 보지 않는다 - 그것은 `collectIssues` 의 문제 표시다(편집기는 아직 안 만든 작업의 트리도 열 수 있어야 한다).
         */
        [[nodiscard]] bool parse( string_view xmlText, string& outError );
        /** @brief 파일 글로 씁니다(`<BehaviorTree>` 루트, 노드마다 한 줄, 탭 들여쓰기). 루트가 없으면 빈 `<BehaviorTree/>` 입니다. */
        string write() const;

        int32                       getRootID() const { return _rootID; }
        uint32                      getNodeCount() const { return static_cast<uint32>( _listNode.size() ); }
        const BehaviorTreeEditNode* find( int32 id ) const;
        BehaviorTreeEditNode*       find( int32 id );

        /**
         * @brief @p parentID 아래 맨 뒤에 @p type 노드를 더합니다. @p parentID 가 -1 이면 루트이고 이미 루트가 있으면 거절합니다.
         * @return 새 노드 번호입니다. 거절하면(잎 아래, 자식이 이미 있는 데코레이터 아래, 루트 중복, 없는 부모) -1 입니다.
         */
        int32 addNode( int32 parentID, BehaviorNodeType type );
        /** @brief 노드와 그 아래를 모두 지웁니다. 없는 번호면 false 입니다. 루트를 지우면 빈 문서가 됩니다. */
        [[nodiscard]] bool removeNode( int32 id );
        /** @brief 형제 사이에서 @p delta 칸(음수는 앞으로 = 우선순위 높게) 옮깁니다. 끝에 닿으면 거기서 멈추고, 움직였으면 true 입니다. */
        [[nodiscard]] bool moveChild( int32 id, int32 delta );
        /**
         * @brief 노드와 그 아래를 @p newParentID 의 맨 뒤 자식으로 옮깁니다(캔버스에서 링크를 새로 이을 때). 루트, 제 자손 아래, 잎이나 이미 찬 데코레이터 아래,
         *        깊이 한도(`BehaviorTreeNames::kMaxDepth`)를 넘는 자리, 지금과 같은 부모는 거절하고 false 입니다.
         */
        [[nodiscard]] bool reparent( int32 id, int32 newParentID );
        /** @brief 루트가 0 인 깊이입니다. 없는 번호면 0 입니다. */
        uint32 getDepth( int32 id ) const;

        /** @brief 저장 전에 고쳐야 할 문제를 모읍니다: 루트 없음, 자식이 모자란 컴포짓, 자식 수가 하나가 아닌 데코레이터, 빈 작업 · 키, 레지스트리에 없는 작업 이름, 음수 시간. */
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
        int32              parseNode( const XMLNode& node, int32 parentID, uint32 depth, string& outError );
        void               writeNode( int32 id, const XMLNode& parentXML ) const;

        vector<BehaviorTreeEditNode> _listNode{};
        int32                        _rootID{ -1 };
        int32                        _nextID{ 1 };
    };
} // namespace sw::editor
