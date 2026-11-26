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
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Animation/AnimationPlayback.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Graph/AnimationGraphPlayer.h"
#include "Engine/Animation/Skeletal/Pose.h"

namespace sw
{
    struct AnimationGraphNode;
    struct AnimationGraphPoseNodeDef;

    class AnimationGraphAsset;
    class AnimationParameterSet;
    class Skeleton;

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

    /**
     * @class IAnimationRootMotionSource
     * @brief 재생할 것(입력) 하나가 걸음 하나 동안 낸 루트 모션을 답합니다. 포즈 노드가 입력의 루트 모션을 모아 제 루트 모션을 낼 때 부릅니다(스켈레탈 애니메이터가 구현).
     */
    class SW_API IAnimationRootMotionSource
    {
    public:
        IAnimationRootMotionSource()                                               = default;
        virtual ~IAnimationRootMotionSource()                                      = default;
        IAnimationRootMotionSource( const IAnimationRootMotionSource& )            = default;
        IAnimationRootMotionSource& operator=( const IAnimationRootMotionSource& ) = default;

        /** @brief @p playable 이 @p step 동안 낸 루트 모션(루트 로컬 이동 · 회전)입니다. */
        virtual BoneTransform computeRootMotion( const IAnimationPlayable& playable, const AnimationTimeStep& step ) const = 0;
    };

    /**
     * @class IAnimationCurveSink
     * @brief 재생할 것(입력) 하나의 커브를 가중치를 곱해 이번 프레임 커브에 더합니다. 포즈 노드가 입력의 커브를 섞을 때 부릅니다(스켈레탈 애니메이터가 구현).
     */
    class SW_API IAnimationCurveSink
    {
    public:
        IAnimationCurveSink()                                        = default;
        virtual ~IAnimationCurveSink()                               = default;
        IAnimationCurveSink( const IAnimationCurveSink& )            = default;
        IAnimationCurveSink& operator=( const IAnimationCurveSink& ) = default;

        /** @brief @p playable 의 @p time 커브 값에 @p weight 를 곱해 더합니다. */
        virtual void accumulateCurves( const IAnimationPlayable& playable, float32 time, float32 weight ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationPoseParams
     * @brief 포즈 노드 평가에 넘기는 값 묶음입니다. 샘플러와 파라미터는 반드시 있어야 하고, 스켈레톤 · 미러 본 표는 그것을 읽는 노드(`Mirror`)만 요구합니다.
     */
    struct AnimationPoseParams
    {
        IAnimationPoseSampler*       _pSampler{ nullptr };      ///< 입력 클립을 샘플하는 쪽
        const AnimationParameterSet* _pParameter{ nullptr };    ///< 노드가 읽는 상태 기계 파라미터(섞기 가중치 등)
        const Skeleton*              _pSkeleton{ nullptr };     ///< 평가하는 스켈레톤(부모 · 레퍼런스 포즈)
        const vector<uint32>*        _pMirroredBone{ nullptr }; ///< 본마다 짝 본 번호(`SkeletonMirrorTable::buildBoneMap`) — 스켈레톤 옆 미러 표에서 만든다
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
        /**
         * @brief 걸음 @p step(이 노드의 시각) 동안의 루트 모션입니다 — 입력마다 그 입력의 시각 구간으로 옮겨 @p source 에 묻고 노드의 규칙(섞기 · 비추기 · 섹션 잇기)으로 합친다.
         * @details 언리얼 애님 그래프가 루트 모션을 포즈와 같은 가중치로 입력에서 뽑아 섞는 것과 같다.
         */
        [[nodiscard]] virtual BoneTransform computeRootMotion( const AnimationParameterSet& parameter, const AnimationTimeStep& step, const IAnimationRootMotionSource& source ) const = 0;
        /** @brief @p time 의 커브를 입력마다 그 입력의 시각 · 포즈와 같은 가중치로 @p sink 에 더합니다(@p weight 는 이 노드의 섞임 가중치). 언리얼 포즈 노드의 커브 블렌드와 같다. */
        virtual void accumulateCurves( const AnimationParameterSet& parameter, float32 time, float32 weight, IAnimationCurveSink& sink ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class BlendPosesNode
     * @brief 입력 클립 둘을 파라미터 가중치로 섞습니다(그래프 XML `<pose_node type="BlendPoses">`). 포즈 노드의 첫 구현이자 테스트용 노드입니다.
     * @details 길이와 기본 반복은 첫 입력을 따른다. 둘째 입력은 같은 정규화 위치에서 샘플한다(길이가 달라도 위상이 맞는다).
     *          가중치는 파라미터 값을 0..1 로 자른 값이다(0 = 첫 입력, 1 = 둘째 입력).
     */
    class SW_API BlendPosesNode final : public IAnimationPoseNode
    {
    public:
        BlendPosesNode( const IAnimationPlayable& inputA, const IAnimationPlayable& inputB, const hashed_string& weightParameter );

        float32 getPlayLength() const override { return _pInputA->getPlayLength(); }
        bool    isLoopingByDefault() const override { return _pInputA->isLoopingByDefault(); }

        [[nodiscard]] bool          evaluatePose( const AnimationPoseParams& params, float32 time, Pose& inoutPose ) const override;
        [[nodiscard]] BoneTransform computeRootMotion( const AnimationParameterSet& parameter, const AnimationTimeStep& step, const IAnimationRootMotionSource& source ) const override;
        void                        accumulateCurves( const AnimationParameterSet& parameter, float32 time, float32 weight, IAnimationCurveSink& sink ) const override;

    private:
        /** @brief 첫 입력의 시각 @p time 을 둘째 입력의 같은 정규화 위치로 옮깁니다. */
        float32 mapTimeToInputB( float32 time ) const;
        /** @brief 파라미터 가중치(0..1)입니다. */
        float32 computeWeight( const AnimationParameterSet& parameter ) const;

    private:
        const IAnimationPlayable* _pInputA;
        const IAnimationPlayable* _pInputB;
        hashed_string             _weightParameter;
        mutable Pose              _scratchPose; ///< 둘째 입력을 샘플할 임시 포즈다(인스턴스마다 한 스레드에서 평가하므로 공유되지 않는다)
    };

    /**
     * @class MirrorPoseNode
     * @brief 입력 클립 하나의 포즈를 좌우로 뒤집습니다(그래프 XML `<pose_node type="Mirror" mirror_axis="X">`). 언리얼 Mirror 노드(`FAnimNode_Mirror` + `UMirrorDataTable`) 자리다.
     * @details 본 i 는 짝 본 p(`AnimationPoseParams::_pMirroredBone`, 스켈레톤 옆 미러 표)의 모델 공간 변환을 축 면으로 비춘 것을 받는다. 짝의 본 축이 서로 거울상이 아닐 수 있어
     *          레퍼런스 포즈로 보정한다 — 레퍼런스 포즈를 넣으면 레퍼런스 포즈가 나온다. 결과는 부모 기준 로컬로 되돌린다. 길이 · 반복은 입력을 따른다.
     */
    class SW_API MirrorPoseNode final : public IAnimationPoseNode
    {
    public:
        MirrorPoseNode( const IAnimationPlayable& input, AnimationMirrorAxis axis );

        float32 getPlayLength() const override { return _pInput->getPlayLength(); }
        bool    isLoopingByDefault() const override { return _pInput->isLoopingByDefault(); }

        [[nodiscard]] bool evaluatePose( const AnimationPoseParams& params, float32 time, Pose& inoutPose ) const override;
        /** @brief 입력의 루트 모션을 축 면으로 비춥니다(이동은 축 성분을, 회전은 축이 아닌 두 성분을 뒤집는다 — 언리얼 Mirror 노드의 루트 모션과 같다). */
        [[nodiscard]] BoneTransform computeRootMotion( const AnimationParameterSet& parameter, const AnimationTimeStep& step, const IAnimationRootMotionSource& source ) const override;
        void                        accumulateCurves( const AnimationParameterSet& parameter, float32 time, float32 weight, IAnimationCurveSink& sink ) const override;

    private:
        const IAnimationPlayable* _pInput;
        AnimationMirrorAxis       _axis;
        mutable Pose              _scratchPose;        ///< 레퍼런스 포즈(평가 임시)
        mutable vector<float4x4>  _listModel;          ///< 입력 포즈의 모델 공간(평가 임시)
        mutable vector<float4x4>  _listReferenceModel; ///< 레퍼런스 포즈의 모델 공간(평가 임시)
        mutable vector<float4x4>  _listMirroredModel;  ///< 뒤집은 모델 공간(평가 임시)
    };

    /**
     * @class MontagePoseNode
     * @brief 입력 클립 하나를 섹션으로 나눠 다음 섹션 링크 · 분기를 따라 재생합니다(그래프 XML `<pose_node type="Montage"><sections>…`). 언리얼 AnimMontage 섹션 자리다.
     * @details 섹션은 `AnimationMontageSection` 이다. 플레이어가 주는 시각이 앞으로 간 만큼 클립 시각을 섹션 링크를 따라 옮긴다(시각이 뒤로 가면 첫 섹션부터 다시).
     *          분기 파라미터는 섹션 끝에 닿는 순간에 읽는다. 다음 섹션이 없으면 그 섹션 끝에서 멈춘다.
     *          길이는 첫 섹션에서 `next` 만 따라간 길이의 합이고, 그 길이 순환하면(반복 섹션) 끝이 없어 `kEndlessLength` 다 — "끝나면 다음" 전이는 분기가 끝 섹션으로 보낼 때만 탄다.
     */
    class SW_API MontagePoseNode final : public IAnimationPoseNode
    {
    public:
        /** @brief 반복 섹션이 있는 몽타주의 길이입니다. */
        static constexpr float32 kEndlessLength = 3.0e38f;
        /** @brief 다음 섹션이 없음(멈춤)입니다. */
        static constexpr uint32 kNoSection = 0xFFFFFFFFu;

        /** @brief 섹션 하나의 풀이입니다(이름 대신 번호, 끝 시각). */
        struct Section
        {
            float32       _startSeconds{ 0.0f };
            float32       _endSeconds{ 0.0f };
            uint32        _next{ kNoSection };
            uint32        _branch{ kNoSection };
            hashed_string _branchParameter{};
        };

        MontagePoseNode( const IAnimationPlayable& input, vector<Section>&& listSection );

        float32 getPlayLength() const override { return _playLength; }
        bool    isLoopingByDefault() const override { return false; }

        [[nodiscard]] bool evaluatePose( const AnimationPoseParams& params, float32 time, Pose& inoutPose ) const override;
        /** @brief 걸음이 지난 클립 시각 구간(섹션 링크 · 분기를 따라 끊긴 조각)마다 입력의 루트 모션을 차례로 잇습니다(언리얼 몽타주의 섹션 위치 구간 루트 모션과 같다). */
        [[nodiscard]] BoneTransform computeRootMotion( const AnimationParameterSet& parameter, const AnimationTimeStep& step, const IAnimationRootMotionSource& source ) const override;
        void                        accumulateCurves( const AnimationParameterSet& parameter, float32 time, float32 weight, IAnimationCurveSink& sink ) const override;

    private:
        friend struct MontagePoseNodeTestAccess; ///< 테스트만 안을 본다 — 배포 공개 API 에 진단 조회를 두지 않는다
        /** @brief 섹션 위의 자리입니다. */
        struct Cursor
        {
            uint32  _section{ 0 };
            float32 _clipTime{ 0.0f };
        };
        /** @brief 클립 시각 구간 하나입니다(한 섹션 안의 `[_start, _end]`). */
        struct ClipSpan
        {
            float32 _start{ 0.0f };
            float32 _end{ 0.0f };
        };

        /** @brief @p cursor 를 @p delta 초만큼 섹션 링크를 따라 옮긴 자리입니다. @p pOutListSpan 이 있으면 지난 클립 구간을 차례로 덧붙입니다. 상태를 바꾸지 않습니다. */
        Cursor advanceCursor( Cursor cursor, float32 delta, const AnimationParameterSet& parameter, vector<ClipSpan>* pOutListSpan ) const;
        /** @brief 플레이어 시각 @p time 의 자리입니다 — 마지막 평가 뒤면 그 자리에서, 아니면 첫 섹션부터 걷는다. 상태를 바꾸지 않습니다. */
        Cursor findCursor( float32 time, const AnimationParameterSet& parameter ) const;

    private:
        const IAnimationPlayable* _pInput;
        vector<Section>           _listSection;
        float32                   _playLength;
        mutable float32           _lastTime;        ///< 마지막으로 받은 플레이어 시각(음수 = 아직 평가하지 않음)
        mutable float32           _clipTime;        ///< 입력 클립 시각
        mutable uint32            _currentSection;  ///< 지금 섹션 번호
        mutable vector<ClipSpan>  _listScratchSpan; ///< 루트 모션 구간 임시(인스턴스마다 한 스레드에서 쓴다)
    };
} // namespace sw

namespace sw
{
    /** @brief 포즈 노드 하나를 만드는 함수입니다. @p listInput 은 정의의 입력을 클립 풀이로 푼 것이고, 개수는 종류의 `_inputCount` 와 같습니다. 입력과 정의가 맞지 않으면 오류를 남기고 nullptr 입니다. */
    using AnimationPoseNodeFactory = unique_ptr<IAnimationPoseNode> ( * )( const vector<const IAnimationPlayable*>& listInput, const AnimationGraphPoseNodeDef& def, string_view nodeName );
    /** @brief 종류가 요구하는 칸(weight · 축 · 섹션)을 봅니다. 어긴 까닭을 오류로 남기고 false 입니다. 입력 수는 표가 따로 본다. */
    using AnimationPoseNodeValidator = bool ( * )( const AnimationGraphPoseNodeDef& def, string_view nodeName );
    /** @brief 종류가 요구하는 칸에 계약을 지키는 첫 값을 씁니다(에디터의 "노드 더하기" — 언리얼 AnimGraph 노드의 기본 핀 값 자리). 입력 이름은 표가 채운다. */
    using AnimationPoseNodeDefaultFiller = void ( * )( AnimationGraphPoseNodeDef& inoutDef );

    /**
     * @struct AnimationPoseNodeType
     * @brief 포즈 노드 종류 하나의 계약입니다 — 입력 수 · 종류가 요구하는 칸의 검사 · 생성 함수. 로드(`AnimationGraphAsset::parsePoseNode`)와 생성(`AnimationPoseNodeSet`)이 같은 계약을 본다.
     */
    struct AnimationPoseNodeType
    {
        uint32                         _inputCount{ 0 };        ///< 정확한 입력 수
        AnimationPoseNodeFactory       _create{ nullptr };      ///< 노드를 만드는 함수
        AnimationPoseNodeValidator     _validate{ nullptr };    ///< 종류가 요구하는 칸의 검사(없으면 입력만 본다)
        AnimationPoseNodeDefaultFiller _fillDefault{ nullptr }; ///< 새 노드의 첫 값(없으면 입력만 채운다). `_validate` 를 지켜야 한다
    };

    /**
     * @class AnimationPoseNodeRegistry
     * @brief 포즈 노드 종류 이름(XML `<pose_node type="…">`)에서 계약 · 생성 함수로 가는 표입니다. 모르는 이름은 로드 오류입니다.
     * @details 리그 노드(`RigNodeRegistry`)와 같은 틀 — 새 블렌드(모션 매칭 등)를 더할 때 열거 · 로더 · 생성 switch 를 고치지 않고 등록 한 줄을 더한다.
     *          엔진 노드(BlendPoses · Mirror · Montage)는 만들 때 등록되고, 게임 · 키트는 `registerType` 으로 더한다(언리얼 애님 노드가 형식 등록으로 모이는 것, 유니티 Playable 형식과 같은 자리).
     */
    class SW_API AnimationPoseNodeRegistry final : public Subsystem
    {
    public:
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::Animation; ///< 할당 용도(`Subsystem::getSubsystemMemoryTag`)
        static constexpr const utf8* kSubsystemName      = "AnimationPoseNodeRegistry";

        /** @brief 엔진 노드가 든 표를 만듭니다. */
        AnimationPoseNodeRegistry();

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 종류를 등록합니다. 이름이나 생성 함수가 없거나 같은 이름이 이미 있으면 오류를 남기고 false 입니다(먼저 것을 둔다). */
        [[nodiscard]] bool registerType( const hashed_string& typeName, const AnimationPoseNodeType& type );
        /** @brief 이름의 종류입니다. 모르는 이름이면 nullptr 입니다. */
        const AnimationPoseNodeType* findType( const hashed_string& typeName ) const;
        /** @brief 등록된 종류 이름들을 등록 순서로 반환합니다(진단과 에디터용). */
        vector<hashed_string> getTypeNames() const;
        /** @brief 정의 @p def 가 종류 @p type 의 계약(입력 수 · 빈 입력 이름 없음 · 종류의 검사 `_validate`)을 지키면 true 입니다. 어기면 그 까닭을 노드 이름 @p nodeName 과 함께 오류로 남깁니다. */
        [[nodiscard]] static bool isSatisfiedBy( const AnimationPoseNodeType& type, const AnimationGraphPoseNodeDef& def, string_view nodeName );
        /**
         * @brief 종류 @p typeName 의 새 노드 정의를 만듭니다 — 입력은 `Input1` … 의 자리 이름(클립 이름으로 바꿔 쓴다), 나머지 칸은 종류의 `_fillDefault`. 만든 정의는 계약을 지킨다.
         * @return 모르는 종류면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool makeDefaultDef( const hashed_string& typeName, AnimationGraphPoseNodeDef& outDef ) const;

    private:
        NameRegistry<AnimationPoseNodeType> _registry; ///< 종류 이름 → 계약(등록 순서)

        SW_SUBSYSTEM_SLOT( AnimationPoseNodeRegistry, Required );
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
        /** @brief 이 셋이 만든 포즈 노드만 찾습니다(클립 셋으로 넘기지 않는다). 상태 이름을 해석하는 쪽이 클립 셋을 따로 들 때 씁니다(`SkeletalAnimatorClipSource`). */
        const IAnimationPoseNode* findNode( const hashed_string& name ) const;
        /** @brief 노드를 모두 놓습니다(입력으로 빌린 클립 · 상태를 먼저 놓아야 할 때). */
        void clear();
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
