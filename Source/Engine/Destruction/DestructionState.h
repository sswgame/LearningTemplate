/**
 * @file DestructionState.h
 * @brief 파괴되는 오브젝트 하나의 구조 상태 — 어느 묶음이 갈라졌는지(활성 노드), 어느 연결이 끊겼는지, 그래서 무엇이 한 덩어리(그룹)로 움직이고
 *        무엇이 땅(앵커)에 붙어 있는지입니다. 2D · 3D 공용이고 물리 · 렌더를 모릅니다.
 * @details **세 가지가 따로 있습니다.**
 *          - **계층(묶음 레벨)** 은 나눔의 굵기입니다. 활성 노드 하나가 한 몸으로 움직이는 단위이고, 묶음이 갈라지면 그 자식들이 활성 노드가 됩니다
 *            (`breakNode`). 갈라졌다고 떨어지지는 않습니다 — 자식끼리의 연결이 그대로면 같은 덩어리입니다.
 *          - **연결(맞닿은 면)** 이 떨어짐을 정합니다. 활성 노드가 다른 두 잎 사이의 연결만 끊길 수 있습니다(갈라지지 않은 묶음 안은 한 몸).
 *          - **앵커**(땅 · 고정 볼륨에 닿은 잎)가 지지를 정합니다(레드 팩션 게릴라). 연결로 앵커까지 이어진 덩어리는 붙어 있고(정적), 끊겨 앵커와 떨어진
 *            덩어리는 떨어집니다(동적 그룹). 붙어 있는 덩어리는 무게를 앵커 쪽으로 흘려 보내 지지 세기를 넘는 연결을 끊습니다(무너짐).
 *
 *          모든 처리는 정해진 순서(노드 · 연결 · 그룹 번호 순)라 같은 그래프 · 표 · 앵커 · 같은 순서의 사건이면 어느 기계에서도 같은 상태입니다
 *          (`computeStateHash`) — 네트워크는 변환이 아니라 씨앗과 사건을 보냅니다. 그룹 번호는 처음 1 부터 늘기만 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Destruction/DestructionProfile.h"

namespace sw
{
    struct DestructionDamageEvent;
    struct FractureGraph;

    /** @brief 한 몸으로 움직이는 활성 노드들입니다. */
    struct DestructionGroup
    {
        vector<uint32> _listNode;              ///< 활성 노드(오름차순)
        uint32         _id{ 0 };               ///< 늘기만 하는 번호
        uint32         _parentID{ 0 };         ///< 갈라져 나온 그룹(처음 그룹은 0)
        uint32         _leafCount{ 0 };        ///< 품은 잎 수
        uint8          _bAnchored{ SW_FALSE }; ///< 앵커까지 이어져 붙어 있다(정적)
    };
} // namespace sw

namespace sw
{
    /** @brief 한 번의 변경(파괴 · 끊김 · 피해)이 낸 결과입니다. 런타임은 이것으로 바디 · 그림을 고칩니다. */
    struct DestructionChange
    {
        vector<uint32> _listRemovedGroup; ///< 사라진 그룹 번호(갈라졌다)
        vector<uint32> _listCreatedGroup; ///< 새 그룹 번호(만든 순서)
        uint32         _brokenNodeCount{ 0 };
        uint32         _brokenLinkCount{ 0 };
        uint32         _overloadedLinkCount{ 0 }; ///< 무게로 끊긴 연결 수(`_brokenLinkCount` 에 포함)

        /** @brief 그룹이 바뀌었으면(떨어진 것이 있다) true 입니다. */
        bool hasGroupChange() const { return _listRemovedGroup.empty() == false || _listCreatedGroup.empty() == false; }
        void clear()
        {
            _listRemovedGroup.clear();
            _listCreatedGroup.clear();
            _brokenNodeCount     = 0;
            _brokenLinkCount     = 0;
            _overloadedLinkCount = 0;
        }
    };
} // namespace sw

namespace sw
{
    /** @class DestructionState @brief 파일 머리말 참고. */
    class SW_API DestructionState
    {
    public:
        /** @brief 무너짐 계산을 되풀이하는 상한입니다(한 번 끊기면 하중이 다시 흐른다). */
        static constexpr uint32 kMaxSupportPass = 8;

        DestructionState();

        /**
         * @brief 그래프 · 표 · 앵커로 시작합니다(뿌리 하나가 활성, 그룹 하나). 그래프는 빌립니다 — 이 상태보다 오래 살아야 합니다.
         * @param listAnchoredLeaf 잎마다 0 이 아니면 앵커(비면 앵커 없음 — 통째로 동적).
         */
        void initialize( const FractureGraph& graph, const DestructionProfile& profile, vector_reference<const uint8> listAnchoredLeaf );
        /** @brief 처음 상태로 되돌립니다(같은 그래프 · 표 · 앵커). */
        void reset();
        /** @brief 시작했으면 true 입니다. */
        bool isInitialized() const { return _pGraph != nullptr; }

        /** @brief 활성 묶음 노드를 자식들로 가릅니다. 잎이거나 활성이 아니면 false 입니다. 그룹은 다시 짓습니다. */
        bool breakNode( uint32 node, DestructionChange& outChange );
        /** @brief 잎을 떼어 냅니다 — 그 잎이 활성이 될 때까지 위 묶음을 가르고, 그 잎의 연결을 모두 끊고, 앵커에서도 뗍니다. */
        bool detachLeaf( uint32 leaf, DestructionChange& outChange );
        /** @brief 연결 하나를 끊습니다. 이미 끊겼거나 두 잎이 같은 활성 노드 안이면 false 입니다. */
        bool breakLink( uint32 link, DestructionChange& outChange );
        /**
         * @brief 피해 사건 하나를 적용합니다(`DestructionDamage.cpp`). 바뀐 것이 있으면 true 입니다.
         * @details 1) 잎마다 변형(`DestructionDamageUtil::computeLeafStrain`). 2) 활성 노드마다 그 잎들의 최대 변형을 쌓고, 깊이의 문턱을 넘으면 묶음은
         *          자식으로 갈라지며 넘친 몫(쌓인 값 - 문턱)의 비율만큼 자식에게 다시 준다 — 센 피해 한 번은 여러 레벨을 지나고, 약한 피해는 큰 덩어리만
         *          가른다. 잎이 문턱을 넘으면 떨어져 나간다(`detachLeaf` 와 같다). 3) 활성 노드가 다른 두 잎 사이 연결은 두 잎 변형의 평균을 쌓아
         *          넓이 × `linkStrength` 를 넘으면 끊긴다. 4) 맞은 그룹을 다시 나누고 지지를 잰다. 모든 순서는 노드 · 연결 번호 순입니다.
         */
        [[nodiscard]] bool applyDamage( const DestructionDamageEvent& event, DestructionChange& outChange );

        const vector<DestructionGroup>& getGroups() const { return _listGroup; }
        /** @brief 번호의 그룹입니다. 없으면 nullptr 입니다. */
        const DestructionGroup* findGroup( uint32 groupID ) const;
        /** @brief 잎이 속한 그룹 번호입니다. */
        uint32 getGroupOfLeaf( uint32 leaf ) const { return _listLeafGroup[leaf]; }
        /** @brief 잎을 품은 활성 노드입니다. */
        uint32  getActiveNodeOfLeaf( uint32 leaf ) const { return _listLeafActive[leaf]; }
        bool    isNodeBroken( uint32 node ) const { return _listNodeBroken[node] != SW_FALSE; }
        bool    isLinkBroken( uint32 link ) const { return _listLinkBroken[link] != SW_FALSE; }
        bool    isLeafAnchored( uint32 leaf ) const { return _listLeafAnchored[leaf] != SW_FALSE; }
        float32 getNodeStrain( uint32 node ) const { return _listNodeStrain[node]; }
        float32 getLinkStrain( uint32 link ) const { return _listLinkStrain[link]; }
        /** @brief 지난 지지 계산에서 연결이 받던 하중(뉴턴)입니다(진단 · 시험). */
        float32                   getLinkLoad( uint32 link ) const { return _listLinkLoad[link]; }
        const FractureGraph&      getGraph() const { return *_pGraph; }
        const DestructionProfile& getProfile() const { return _profile; }
        /** @brief 그룹의 질량(밀도 × 부피)과 질량 중심(메시 공간)입니다. */
        float32 computeGroupMass( const DestructionGroup& group, float3& outCenter ) const;
        /** @brief 상태 전체의 해시입니다(변형 · 끊김 · 그룹). 같은 사건열이면 같은 값입니다. */
        uint64 computeStateHash() const;
        /** @brief 적용한 피해 사건 수입니다. */
        uint32 getEventCount() const { return _eventCount; }

        /**
         * @brief 지금 상태를 바이트로 씁니다(늦은 참가 · 어긋남 바로잡기 — 사건열을 처음부터 다시 돌리지 않는다).
         * @details 끊긴 노드 · 연결 · 앵커 잎 비트, 0 이 아닌 노드 · 연결 변형(비트 그대로), 그룹(번호 · 부모 · 앵커 · 활성 노드), 다음 그룹 번호,
         *          사건 수. 잎 → 활성 노드 · 그룹 표는 그룹에서 다시 짓는다. 그래서 읽은 쪽의 `computeStateHash` 가 쓴 쪽과 같고, 뒤따르는 사건도
         *          같은 결과를 낸다(변형이 쌓인 채 넘어간다).
         */
        void writeSnapshot( vector<uint8>& outBytes ) const;
        /**
         * @brief `writeSnapshot` 의 바이트로 상태를 바꿉니다. 같은 그래프로 시작한(`initialize`) 상태여야 합니다.
         * @return 매직 · 판 · 노드 · 연결 · 잎 수가 맞지 않거나 잘렸거나 그룹이 잎을 다 덮지 않으면 false 이고 상태는 그대로입니다.
         */
        [[nodiscard]] bool readSnapshot( const uint8* pData, size_t size );

    private:
        /** @brief 잎의 연결 번호들입니다. */
        vector_reference<const uint32> getLeafLinks( uint32 leaf ) const;
        /** @brief 노드를 갈라 자식들을 활성으로 둡니다(그룹은 다시 짓지 않는다 — 부른 쪽이 `regroup`). */
        void openNode( uint32 node, DestructionChange& outChange );
        /** @brief 잎의 연결을 모두 끊고 앵커에서 뗍니다. */
        void severLeaf( uint32 leaf, DestructionChange& outChange );
        /** @brief 표시된 그룹들을 연결로 다시 나누고, 붙은 덩어리의 지지를 계산해 무너뜨린 뒤 그룹을 바꿉니다. */
        void regroup( vector<uint32>& inoutListDirtyGroupID, DestructionChange& outChange );
        /** @brief 활성 노드들을 연결로 이어진 덩어리들로 나눕니다(덩어리마다 가장 작은 노드 순). */
        void splitComponents( const vector<uint32>& listNode, vector<vector<uint32>>& outListComponent ) const;
        /** @brief 붙은 덩어리의 하중을 앵커 쪽으로 흘려 지지 세기를 넘는 연결을 끊습니다. 끊었으면 true 입니다. */
        bool relieveOverload( const vector<uint32>& listNode, DestructionChange& outChange );
        /** @brief 활성 노드가 앵커 잎을 품었는지입니다. */
        bool hasAnchoredLeaf( uint32 node ) const;
        /** @brief 그룹 번호의 자리입니다. 없으면 -1 입니다. */
        int32 findGroupIndex( uint32 groupID ) const;

    private:
        const FractureGraph*     _pGraph;
        DestructionProfile       _profile;
        vector<uint8>            _listInitialAnchor;
        vector<float32>          _listNodeStrain;
        vector<uint8>            _listNodeBroken;
        vector<uint32>           _listLeafActive;
        vector<uint32>           _listLeafGroup;
        vector<uint8>            _listLeafAnchored;
        vector<float32>          _listLinkStrain;
        vector<float32>          _listLinkLoad;
        vector<uint8>            _listLinkBroken;
        vector<uint32>           _listLeafLinkStart; ///< 잎마다 `_listLeafLinkIndex` 의 시작(잎 수 + 1)
        vector<uint32>           _listLeafLinkIndex;
        vector<DestructionGroup> _listGroup; ///< 번호 오름차순
        uint32                   _nextGroupID;
        uint32                   _eventCount;        ///< 적용한 피해 사건 수(해시에 든다)
        vector<float32>          _listScratchStrain; ///< applyDamage 의 잎별 변형(재사용)
    };
} // namespace sw
