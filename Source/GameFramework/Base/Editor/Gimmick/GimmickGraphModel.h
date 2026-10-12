/**
 * @file GimmickGraphModel.h
 * @brief 기믹 회로를 노드 그래프로 편집하는 모델입니다 — 노드 · 배선 · 매개변수 · 대상 오브젝트와 검증 문제. ImGui 와 컴포넌트를 모릅니다.
 * @details 회로는 `GimmickCircuitComponent` 가 노드 목록(`GimmickNodeDesc`), 대상 목록(`GameObjectHandle`), 배선 목록(`GimmickWireDesc`)으로 저장한다. 배선은 "node.Port" 글이라
 *          노드 이름을 바꾸면 끊긴다 — 이 모델은 노드에 안정된 그래프 번호를 주고 배선이 그 번호를 가리키게 해 두었다가 내보낼 때 글로 되돌린다(노드 이름 바꾸기가 배선을 따라간다).
 *          불러올 때 가리키는 노드가 없는 배선은 지우지 않고 따로 보관해 그대로 내보내며 문제로 보인다(데이터를 조용히 잃지 않는다). 검증은 컴포넌트가 로드할 때 쓰는 것과 같은
 *          `GimmickCircuit::validate` 입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickNodeRegistry.h"

namespace sw::editor
{
    /** @brief 그래프의 노드 하나입니다. */
    struct GimmickGraphNode
    {
        hashed_string    _id{};
        hashed_string    _kind{};
        string           _params{}; ///< "name=value; name=value" (컴포넌트 저장 형식 그대로)
        GameObjectHandle _target{}; ///< 비면 회로를 가진 오브젝트
        float2           _position{};
        int32            _graphID{ 0 }; ///< 안정된 번호입니다. 캔버스 노드 id 로 씁니다
        bool             _bPlaced{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 그래프의 배선 하나입니다. 양 끝은 노드의 그래프 번호입니다. */
    struct GimmickGraphWire
    {
        string _fromPort{};
        string _toPort{};
        int32  _fromNode{ 0 };
        int32  _toNode{ 0 };
        int32  _graphID{ 0 }; ///< 캔버스 링크 id 로 씁니다
        bool   _bInvert{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 검증 문제 하나입니다. 어느 노드의 문제인지 알면 `_nodeGraphID` 가 그 번호이고, 회로 전체의 문제이면 0 입니다. */
    struct GimmickGraphIssue
    {
        string _message{};
        int32  _nodeGraphID{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class GimmickGraphModel
     * @brief 편집 중인 회로 한 개의 그래프 상태입니다.
     */
    class GimmickGraphModel
    {
    public:
        /** @brief 그래프 번호 0 은 "없음" 입니다. */
        static constexpr int32 kNoGraphID = 0;
        /** @brief 입력 · 출력 포트는 노드 종류마다 최대 32 개입니다(`GimmickNodeKind`). 핀 번호 계산이 이 값에 기댑니다. */
        static constexpr int32 kMaxPortCount = 32;

        explicit GimmickGraphModel( const GimmickNodeRegistry& registry );

        void clear();
        /** @brief 컴포넌트가 저장한 목록으로 모델을 채웁니다. 대상 목록이 노드보다 짧으면 빈 대상으로 채웁니다. 배치는 `autoLayout` 이 합니다. */
        void populate( const vector<GimmickNodeDesc>& listNode, const vector<GameObjectHandle>& listTarget, const vector<GimmickWireDesc>& listWire );
        /** @brief 컴포넌트가 저장하는 목록으로 내보냅니다(가리키는 노드가 없던 배선도 그대로 냅니다). */
        void exportTo( vector<GimmickNodeDesc>& outListNode, vector<GameObjectHandle>& outListTarget, vector<GimmickWireDesc>& outListWire ) const;
        /** @brief 내보낼 내용의 해시입니다(위치는 빼고). 컴포넌트가 바깥에서 바뀌었는지(되돌리기) 알아볼 때 씁니다. */
        uint64 computeSignature() const;

        // ------------------------------------------------------------------------------
        // 편집 — 실패하면 아무것도 바꾸지 않고 false 입니다(@p outError 가 있으면 이유를 적습니다).
        // ------------------------------------------------------------------------------
        /** @brief 종류 @p kind 의 새 노드를 더하고 그래프 번호를 반환합니다. 모르는 종류이면 `kNoGraphID` 입니다. 이름은 `<종류>_<n>` 입니다. */
        int32 addNode( const hashed_string& kind );
        /** @brief 노드와 그 노드에 닿은 배선을 지웁니다. */
        [[nodiscard]] bool removeNode( int32 graphID );
        /** @brief 노드 이름을 바꿉니다. 비었거나 다른 노드가 이미 쓰면 거절합니다. 배선은 따라갑니다. */
        [[nodiscard]] bool renameNode( int32 graphID, const hashed_string& newID, string& outError );
        /** @brief 매개변수 하나를 바꿉니다. @p value 가 비면 그 매개변수를 지웁니다(종류의 기본값을 씁니다). */
        [[nodiscard]] bool setNodeParam( int32 graphID, string_view name, string_view value );
        [[nodiscard]] bool setNodeTarget( int32 graphID, GameObjectHandle target );
        /**
         * @brief 출력 포트에서 입력 포트로 배선을 더합니다. 포트가 종류에 없거나 같은 배선이 이미 있으면 거절합니다.
         * @details 같은 입력 포트로 여러 배선이 들어오는 것은 허용한다(OR). 순환은 여기서 막지 않는다 — 지연(Delay)을 지나는 고리는 정상이라 `collectIssues` 가 봅니다.
         */
        [[nodiscard]] bool addWire( int32 fromNode, string_view fromPort, int32 toNode, string_view toPort, int32& outWireGraphID, string& outError );
        [[nodiscard]] bool removeWire( int32 wireGraphID );
        [[nodiscard]] bool setWireInvert( int32 wireGraphID, bool bInvert );
        /** @brief 가리키는 노드가 없는 배선을 지웁니다. 지운 수를 반환합니다. */
        int32 removeOrphanWires();
        /** @brief 입력 방향(왼쪽 → 오른쪽) 층으로 노드를 배치합니다. 순환은 층 계산을 노드 수에서 끊습니다. 모든 노드의 `_bPlaced` 를 false 로 둡니다. */
        void autoLayout();

        // ------------------------------------------------------------------------------
        // 조회
        // ------------------------------------------------------------------------------
        const vector<GimmickGraphNode>& getNodes() const { return _listNode; }
        vector<GimmickGraphNode>&       getMutableNodes() { return _listNode; } ///< 위치와 `_bPlaced` 만 고칩니다(캔버스가 끌린 위치를 되돌려 줍니다)
        const vector<GimmickGraphWire>& getWires() const { return _listWire; }
        const vector<GimmickWireDesc>&  getOrphanWires() const { return _listOrphanWire; }
        int32                           findNodeIndex( int32 graphID ) const;
        int32                           findNodeIndexByID( const hashed_string& id ) const;
        int32                           findWireIndex( int32 wireGraphID ) const;
        const GimmickNodeKind*          findKind( const GimmickGraphNode& node ) const;
        /** @brief 매개변수 값입니다. 적혀 있지 않으면 빈 문자열입니다. */
        static string readParam( string_view params, string_view name );
        /** @brief "a=1; b=2" 를 이름과 값 쌍으로 나눕니다. 이름 없는 조각은 건너뜁니다. */
        static void parseParams( string_view params, vector<string>& outListName, vector<string>& outListValue );
        /** @brief 이름과 값 쌍을 "a=1; b=2" 로 잇습니다. */
        static string formatParams( const vector<string>& listName, const vector<string>& listValue );

        /** @brief 회로 검증 문제를 모읍니다. 컴포넌트가 로드할 때 쓰는 검증(`GimmickCircuit::validate`)과 가리키는 노드가 없는 배선입니다. */
        void collectIssues( vector<GimmickGraphIssue>& outListIssue ) const;

    private:
        void   exportDef( GimmickCircuitDef& outDef, vector<string>& outListError ) const;
        string makeUniqueID( const hashed_string& kind ) const;
        string makePortReference( const GimmickGraphNode& node, string_view port ) const;

        const GimmickNodeRegistry* _pRegistry;
        vector<GimmickGraphNode>   _listNode;
        vector<GimmickGraphWire>   _listWire;
        vector<GimmickWireDesc>    _listOrphanWire;
        int32                      _nextNodeGraphID;
        int32                      _nextWireGraphID;
    };
} // namespace sw::editor
