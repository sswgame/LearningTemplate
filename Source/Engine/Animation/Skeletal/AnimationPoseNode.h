/**
 * @file AnimationPoseNode.h
 * @brief 포즈를 직접 평가해 내는 그래프 노드(`IAnimationPoseNode`)와, 그래프 에셋의 "pose_node" 정의로 그 노드를 만드는 표(`AnimationPoseNodeSet`)입니다.
 * @details 그래프의 상태는 "이름 → 재생할 것(`IAnimationPlayable`, 클립 + 시각)"이다. 클립은 시각만 주면 포즈가 정해지지만, 두 포즈 섞기 · 모션 매칭처럼
 *          매 프레임 포즈를 계산하는 노드는 클립이 아니다. 이 노드도 `IAnimationPlayable` 이라 상태 기계 · 플레이어 · 동기 그룹은 그대로 쓰고,
 *          포즈를 내는 쪽은 `IAnimationPlayable::findPoseNode` 로 클립과 가른다(형식으로 가르므로 클립으로 잘못 캐스팅할 자리가 없다).
 *          언리얼 애님 그래프의 포즈 노드(`FAnimNode_Base::Evaluate`), 유니티 Playables 의 `AnimationScriptPlayable` 에 해당한다.
 *          노드는 애니메이터 인스턴스마다 만든다(평가 중 쓰는 임시 포즈를 노드가 가진다 — 한 인스턴스는 한 스레드에서 평가된다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/AnimationPlayback.h"
#include "Engine/Animation/Graph/AnimationGraphPlayer.h"
#include "Engine/Animation/Skeletal/Pose.h"

namespace sw
{
    struct AnimationGraphNode;

    class AnimationGraphAsset;
    class AnimationParameterSet;

    /**
     * @class IAnimationPoseSampler
     * @brief 재생할 것(클립) 하나를 시각에서 포즈로 샘플합니다. 클립 · 트랙 맵 · 본 LOD 마스크를 가진 쪽(스켈레탈 애니메이터)이 구현합니다.
     */
    class SW_API IAnimationPoseSampler
    {
    public:
        IAnimationPoseSampler()                                          = default;
        virtual ~IAnimationPoseSampler()                                 = default;
        IAnimationPoseSampler( const IAnimationPoseSampler& )            = default;
        IAnimationPoseSampler& operator=( const IAnimationPoseSampler& ) = default;

        /** @brief @p playable 을 @p time 에서 샘플해 @p inoutPose 에 씁니다(트랙이 없는 본은 들어온 값을 둡니다). */
        virtual void samplePose( const IAnimationPlayable& playable, float32 time, Pose& inoutPose ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationPoseParams
     * @brief 포즈 노드 평가에 넘기는 값 묶음입니다. 샘플러와 파라미터는 반드시 있어야 합니다.
     */
    struct AnimationPoseParams
    {
        IAnimationPoseSampler*       _pSampler{ nullptr };   ///< 입력 클립을 샘플하는 쪽
        const AnimationParameterSet* _pParameter{ nullptr }; ///< 노드가 읽는 상태 기계 파라미터(섞기 가중치 등)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAnimationPoseNode
     * @brief 포즈를 평가해 내는 재생할 것입니다. 길이 · 반복 · 알림은 `IAnimationPlayable` 그대로이고, 샘플 대신 `evaluatePose` 로 포즈를 냅니다.
     */
    class SW_API IAnimationPoseNode : public IAnimationPlayable
    {
    public:
        /** @brief 포즈 노드는 자기 자신입니다. */
        const IAnimationPoseNode* findPoseNode() const override { return this; }

        /**
         * @brief @p time 의 포즈를 평가해 @p inoutPose 에 씁니다. @p inoutPose 는 호출하는 쪽이 레퍼런스(본 수 포함)로 채워 둡니다.
         * @return 샘플러나 파라미터가 없으면 오류를 남기고 false 입니다(포즈는 들어온 그대로입니다).
         */
        [[nodiscard]] virtual bool evaluatePose( const AnimationPoseParams& params, float32 time, Pose& inoutPose ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class BlendPosesNode
     * @brief 입력 클립 둘을 파라미터 가중치로 섞습니다(그래프 JSON "pose_node": { "type": "blend_poses" }). 포즈 노드의 첫 구현이자 테스트용 노드입니다.
     * @details 길이와 기본 반복은 첫 입력을 따른다. 둘째 입력은 같은 정규화 위치에서 샘플한다(길이가 달라도 위상이 맞는다).
     *          가중치는 파라미터 값을 0..1 로 자른 값이다(0 = 첫 입력, 1 = 둘째 입력).
     */
    class SW_API BlendPosesNode final : public IAnimationPoseNode
    {
    public:
        BlendPosesNode( const IAnimationPlayable& inputA, const IAnimationPlayable& inputB, const hashed_string& weightParameter );

        float32 getPlayLength() const override { return _pInputA->getPlayLength(); }
        bool    isLoopingByDefault() const override { return _pInputA->isLoopingByDefault(); }

        [[nodiscard]] bool evaluatePose( const AnimationPoseParams& params, float32 time, Pose& inoutPose ) const override;

    private:
        const IAnimationPlayable* _pInputA;
        const IAnimationPlayable* _pInputB;
        hashed_string             _weightParameter;
        mutable Pose              _scratchPose; ///< 둘째 입력을 샘플할 임시 포즈다(인스턴스마다 한 스레드에서 평가하므로 공유되지 않는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationPoseNodeSet
     * @brief 그래프 에셋의 "pose_node" 정의로 포즈 노드를 만들어 가지고, 상태 이름을 재생할 것으로 풉니다.
     * @details 포즈 노드가 있는 상태 이름은 그 노드를, 나머지 이름은 클립 풀이(@p clipSource)를 돌려준다. 노드의 입력 이름도 클립 풀이로 푼다.
     *          그래프 플레이어에는 클립 풀이 대신 이 표를 건다(`AnimationGraphPlayer::setPlayableSource`).
     */
    class SW_API AnimationPoseNodeSet final : public IAnimationPlayableSource
    {
    public:
        AnimationPoseNodeSet();
        // 노드를 소유하고(unique_ptr) 노드가 다른 노드의 주소를 빌리므로 복사하지 않는다. SW_API 클래스는 복사 생성자도 내보내므로 명시해 지운다.
        AnimationPoseNodeSet( const AnimationPoseNodeSet& )            = delete;
        AnimationPoseNodeSet& operator=( const AnimationPoseNodeSet& ) = delete;

        /**
         * @brief @p graph 의 포즈 노드를 모두 다시 만듭니다. @p clipSource 는 빌리며, 이 표보다 오래 살아야 합니다.
         * @return 입력 이름을 클립 풀이에서 찾지 못하면 오류를 남기고 false 입니다(표는 빕니다).
         */
        [[nodiscard]] bool rebuild( const AnimationGraphAsset& graph, const IAnimationPlayableSource& clipSource );
        /** @brief 포즈 노드가 있는 이름이면 그 노드, 아니면 클립 풀이의 결과입니다. */
        const IAnimationPlayable* findPlayable( const hashed_string& name ) const override;
        /** @brief 만든 포즈 노드 수입니다. */
        uint32 getNodeCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        /** @brief 노드 정의 하나로 포즈 노드를 만듭니다. 입력을 찾지 못하면 오류를 남기고 nullptr 입니다. */
        unique_ptr<IAnimationPoseNode> createNode( const AnimationGraphNode& node, const IAnimationPlayableSource& clipSource ) const;

    private:
        /** @brief 상태 이름과 그 포즈 노드입니다. */
        struct Entry
        {
            hashed_string                  _name;
            unique_ptr<IAnimationPoseNode> _node;
        };
        vector<Entry>                   _listEntry;
        const IAnimationPlayableSource* _pClipSource;
    };
} // namespace sw
