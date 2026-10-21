/**
 * @file RigInstance.h
 * @brief 리그 에셋 하나를 유닛 하나에 묶은 실행 상태입니다. 복제한 노드, 대상 값, 가중치(커브, 시퀀서 슬롯), 모프 출력을 담습니다.
 * @details 오브젝트를 모르는 순수 실행기다. 호스트(`PoseModifierComponent`)가 게임 스레드에서 `prepare` 를, 워커에서 `evaluate` 를 호출하고,
 *          바깥 대상(다른 유닛이나 오브젝트)의 값을 프레임마다 넣는다. 그래서 오브젝트 없이 테스트할 수 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Rig/RigSpringChain.h"

namespace sw
{
    class RigAsset;

    /** @brief 소켓 이름을 푼 결과입니다. */
    enum class RigSocketLookup : uint8
    {
        Missing = 0, ///< 소켓이 없다.
        OwnUnit,     ///< 이 유닛의 소켓이다. 부모 본과 로컬로 작업 포즈에서 바로 읽는다.
        OtherUnit,   ///< 해석된 외형에 있는 다른 유닛의 소켓(`MainHand.Grip`)이다. 호스트가 바깥 대상 값으로 넣는다.
    };

    /** @brief 유닛의 소켓 이름을 (부모 본, 로컬 변환)으로 풀어 주는 인터페이스입니다. 소켓 에셋이나 해석된 소켓 테이블을 가진 쪽이 구현합니다. */
    class SW_API IRigSocketResolver
    {
    public:
        IRigSocketResolver()                                       = default;
        virtual ~IRigSocketResolver()                              = default;
        IRigSocketResolver( const IRigSocketResolver& )            = delete;
        IRigSocketResolver& operator=( const IRigSocketResolver& ) = delete;

        /** @brief 소켓의 부모 본과 부모 기준 로컬 변환입니다(자기 유닛일 때). 다른 유닛 소켓이면 `OtherUnit` 만 알립니다. */
        virtual RigSocketLookup findSocket( const hashed_string& socketName, hashed_string& outParentBone, BoneTransform& outLocal ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 포즈 구동이 낸 모프 가중치 하나입니다. */
    struct RigMorphWeight
    {
        hashed_string _name{};
        float32       _weight{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @class RigInstance @brief 파일 머리말 참고. */
    class SW_API RigInstance
    {
    public:
        RigInstance();
        ~RigInstance();
        RigInstance( const RigInstance& )            = delete;
        RigInstance& operator=( const RigInstance& ) = delete;

        /**
         * @brief 에셋의 노드를 복제해 스켈레톤에 바인딩합니다. 모르는 본, 대상, 소켓은 오류이며 false 를 반환합니다(인스턴스는 빈 채로 남습니다).
         * @param pOwnSockets 자기 유닛의 소켓을 찾아 주는 인터페이스입니다(소켓 대상이 없으면 nullptr 이어도 됩니다).
         */
        [[nodiscard]] bool initialize( shared_ptr<const RigAsset> asset, const Skeleton& skeleton, const IRigSocketResolver* pOwnSockets, string_view label );
        /** @brief 비웁니다. */
        void                              shutdown();
        bool                              isInitialized() const { return _asset != nullptr; }
        const shared_ptr<const RigAsset>& getAsset() const { return _asset; }

        // --- 대상 ---
        uint32              getTargetCount() const { return static_cast<uint32>( _listTargetDef.size() ); }
        const RigTargetDef& getTargetDef( uint32 targetIndex ) const { return _listTargetDef[targetIndex]; }
        /** @brief 이름의 대상 번호입니다. 없으면 -1 입니다. */
        int32 findTargetIndex( const hashed_string& name ) const;
        /** @brief 호스트가 값을 넣어야 하는 대상인지 반환합니다(다른 유닛, 오브젝트, 다른 유닛의 해석된 소켓). */
        bool isTargetExternal( uint32 targetIndex ) const { return _listTargetBone[targetIndex] == kExternalBone; }
        /** @brief 바깥 대상의 이번 프레임 값을 넣습니다(평가 전). */
        void setExternalTarget( uint32 targetIndex, const RigTargetValue& value );
        /**
         * @brief 자기 유닛 대상(본 · 소켓)이 붙은 본과 그 본 기준의 합친 오프셋(데이터 오프셋 × 소켓 로컬)입니다. 에디터의 대상 오프셋 기즈모가 부모 공간을 되짚는 데 씁니다.
         * @param outBone 대상의 본 번호입니다. 부모가 빈 소켓(유닛 뿌리 기준)이면 -1 입니다.
         * @return 바깥 대상(다른 유닛 · 오브젝트)이거나 번호가 범위 밖이면 false 입니다.
         */
        [[nodiscard]] bool tryGetOwnTargetBinding( uint32 targetIndex, int32& outBone, BoneTransform& outCombinedOffset ) const;
        /** @brief 대상의 지금 모델 공간 변환입니다(워커). 값이 없으면 false 입니다. */
        [[nodiscard]] bool resolveTarget( uint32 targetIndex, RigPoseBuffer& pose, float3& outPosition, quaternion& outRotation ) const;

        // --- 가중치 ---
        /** @brief 노드 가중치를 움직일 커브 출처입니다(빌림). */
        void setCurveSource( const IRigCurveSource* pSource ) { _pCurveSource = pSource; }
        /** @brief 시퀀서 슬롯 가중치를 정합니다(그 슬롯을 쓰는 노드에 곱해집니다). */
        void setSlotWeight( const hashed_string& slotName, float32 weight );
        /** @brief 시퀀서 슬롯을 해제합니다(가중치가 다시 1 이 됩니다). */
        void clearSlotWeight( const hashed_string& slotName );
        /** @brief 시퀀서 슬롯 가중치입니다. 정하지 않았거나 해제한 슬롯은 1 입니다(곱해도 노드 가중치가 그대로인 값 — `clearSlotWeight` 와 같은 뜻). */
        float32 getSlotWeight( const hashed_string& slotName ) const;
        /** @brief 노드의 이번 프레임 가중치입니다(0..1). */
        float32 computeNodeWeight( uint32 nodeIndex ) const;

        // --- 노드 ---
        uint32         getNodeCount() const { return static_cast<uint32>( _listNode.size() ); }
        RigNode&       getNode( uint32 nodeIndex ) { return *_listNode[nodeIndex]; }
        const RigNode& getNode( uint32 nodeIndex ) const { return *_listNode[nodeIndex]; }
        /** @brief 이름의 노드입니다. 없으면 nullptr 입니다. */
        RigNode* findNode( const hashed_string& name ) const;
        /** @brief 노드의 조절 값을 정합니다(부모 바꾸기의 활성 부모 등). 노드나 조절이 없으면 false 를 반환합니다. */
        bool setNodeControl( const hashed_string& nodeName, const hashed_string& control, float32 value );

        // --- 출력 ---
        /** @brief 포즈 구동이 낸 모프 가중치입니다(평가마다 다시 씁니다). */
        const vector<RigMorphWeight>& getMorphWeights() const { return _listMorphWeight; }
        /** @brief 모프 가중치를 더합니다(노드가 평가 중에 부릅니다). */
        void addMorphWeight( const hashed_string& name, float32 weight );
        /** @brief 공유 스프링 충돌체입니다(물리 에셋이나 호스트 데이터). `use_shared_colliders` 인 스프링 노드가 씁니다. */
        void                             setSharedColliders( const vector<RigSpringCollider>& listCollider ) { _listSharedCollider = listCollider; }
        const vector<RigSpringCollider>& getSharedColliders() const { return _listSharedCollider; }
        /** @brief 풀이 공간(2D 면 평면)입니다. */
        const RigSolveSpace& getSolveSpace() const { return _space; }

        // --- 실행 ---
        /**
         * @brief 게임 스레드 준비 단계입니다. 프레임마다 호출하며, LOD 로 평가를 건너뛰는 프레임에도 호출합니다. 지난 평가 뒤의 시간을 모읍니다.
         * @details 화면 크기가 노드의 `min_screen_size` 보다 작으면 그 노드를 끄고, 꺼지는 순간 상태를 버린다(`RigNode::reset`).
         */
        void prepare( const RigPrepareContext& context );
        /** @brief 노드가 화면 크기 LOD 로 꺼져 있는지 반환합니다(마지막 `prepare` 기준). */
        bool isNodeLODOff( uint32 nodeIndex ) const { return _listNodeLODOff[nodeIndex] == SW_TRUE; }
        /** @brief 노드를 순서대로 평가해 @p inoutLocalPose 를 고칩니다(워커). */
        void evaluate( Pose& inoutLocalPose, const vector<int32>& listParent, const float4x4& worldFromModel );
        /** @brief 노드 상태를 모두 버립니다(순간이동이나 다시 켤 때). */
        void reset();
        /** @brief 평가한 횟수를 반환합니다(테스트와 진단용). */
        uint32 getEvaluationCount() const { return _evaluationCount; }

    private:
        static constexpr int32 kExternalBone    = -1; ///< `_listTargetBone` 에 해당하며, 호스트가 값을 넣는 대상이다.
        static constexpr int32 kModelOriginBone = -2; ///< `_listTargetBone` 에 해당하며, 유닛 루트(모델 원점) 기준 소켓이다.

        struct SlotWeight
        {
            hashed_string _name{};
            float32       _weight{ 1.0f };
        };

        shared_ptr<const RigAsset>  _asset;
        vector<RigTargetDef>        _listTargetDef;
        vector<RigTargetValue>      _listTargetValue;
        vector<int32>               _listTargetBone;   ///< 자기 유닛 대상의 본이다(`kExternalBone` 과 `kModelOriginBone` 도 있다).
        vector<BoneTransform>       _listTargetOffset; ///< 자기 유닛 대상의 본 기준 오프셋이다(소켓 로컬이나 데이터 오프셋).
        vector<unique_ptr<RigNode>> _listNode;
        vector<vector<uint32>>      _listNodeWrittenBone;
        vector<uint8>               _listNodeLODOff; ///< 노드마다 화면 크기 LOD 로 꺼졌는지다(`_listNode` 와 인덱스가 같다).
        vector<SlotWeight>          _listSlotWeight;
        vector<RigMorphWeight>      _listMorphWeight;
        vector<RigSpringCollider>   _listSharedCollider;
        vector<BoneTransform>       _listBlendScratch;
        RigPoseBuffer               _pose;
        RigSolveSpace               _space;
        const IRigCurveSource*      _pCurveSource;
        float3                      _worldGravity; ///< 마지막 준비 단계가 받은 월드 중력이다.
        float32                     _pendingDeltaSeconds;
        uint32                      _evaluationCount;
    };
} // namespace sw
