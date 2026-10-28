/**
 * @file SkeletalAnimatorComponent.h
 * @brief 스켈레탈 애니메이터입니다. 그래프(상태 기계) 인스턴스, 파라미터, 레이어, 동기 그룹, 루트 모션, 시퀀서 덮어쓰기 슬롯을 가집니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Graph/AnimationGraphPlayer.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Animation/Skeletal/CurveAccumulator.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/PoseInertializer.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Resource/Cache/SharedAssetCache.h"

namespace sw
{
    class AnimationClip;
    class IAnimationNotifyListener;
    class IRootMotionModifier;
    class SkeletalAnimatorComponent;

    /**
     * @struct AnimationLayerDesc
     * @brief 코드가 더하는 레이어 하나의 서술입니다(`addLayer`). 마스크는 본 하나와 그 자손입니다(비었으면 모든 본).
     * @details 합치는 방법(`AnimationLayerBlend`)은 그래프 에셋 헤더에 있다. 데이터로 정하는 레이어(본 마스크 프리셋, 본별 가중치)는 그래프의 "layers" 다.
     */
    struct AnimationLayerDesc
    {
        hashed_string       _clipName;
        hashed_string       _maskRootBone;
        float32             _weight{ 1.0f };
        AnimationLayerBlend _blend{ AnimationLayerBlend::Override };
        uint8               _bLoop{ SW_TRUE };
        /** @brief 다시 시작(`restartLayer`)할 때 끊긴 레이어 포즈에서 이어 갈 inertialization 반감기(초)입니다. 0 이면 즉시 새 포즈입니다(뒤에 두어 기존 집합 초기화가 그대로 맞습니다). */
        float32 _halfLifeSeconds{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletalBlendSpaceState
     * @brief 그래프의 블렌드 스페이스 상태(노드의 "blend_space") 하나를 재생할 것으로 냅니다. 애니메이터가 노드마다 하나 가지고 상태 기계에 넘깁니다.
     * @details 시간은 표본 길이를 가중치로 평균한 한 바퀴(`BlendSpace::computeBlendedLength`) 위에서 흐르고, 표본 클립은 같은 위상(정규화 시간)으로 샘플한다.
     *          가중치는 매 시간 단계 앞에서 축 파라미터로 다시 구한다(`updateWeights`). 길이가 바뀌면 애니메이터가 현재 슬롯의 위상을 지킨다.
     *          표본 클립의 알림은 데이터("notify", `AnimationBlendSpaceNotifyMode`)가 고른 표본에서 낸다. 출처는 표본 클립이라 받는 쪽은 클립 하나를 재생할 때와
     *          같은 규칙으로 구간 알림을 닫는다 — 그래서 애니메이터는 알림을 내는 표본 클립을 재생 중 목록(`appendNotifySources`)에 함께 넣는다.
     */
    class SW_API SkeletalBlendSpaceState final : public IAnimationPlayable
    {
    public:
        /** @brief @p listClip 은 표본 순서의 클립이고 모두 있어야 합니다(빌린 포인터, 애니메이터의 클립 표가 소유). */
        SkeletalBlendSpaceState( const hashed_string& stateName, const AnimationGraphBlendSpaceDef& def, vector<const AnimationClip*>&& listClip );

        float32 getPlayLength() const override { return _playLength; }
        bool    isLoopingByDefault() const override { return true; }
        /** @brief 알림을 내는 표본마다 걸음을 그 클립의 시각으로 옮겨 그 클립의 알림을 냅니다(가중치는 @p weight × 표본 가중치). */
        void collectFiredNotifies( const AnimationTimeStep& step, float32 weight, vector<AnimationFiredNotify>& outListFired ) const override;
        /** @brief 지금 알림을 내는 표본 클립을 @p inoutListPlayable 에 덧붙입니다(알림의 출처와 같은 집합). */
        void appendNotifySources( vector<const IAnimationPlayable*>& inoutListPlayable ) const;

        /** @brief 축 파라미터로 표본 가중치와 한 바퀴 길이를 다시 구합니다. */
        void updateWeights( const AnimationParameterSet& parameters );
        /** @brief 이 상태의 이름(그래프 노드 이름)입니다. */
        const hashed_string& getStateName() const { return _stateName; }
        uint32               getSampleCount() const { return static_cast<uint32>( _listClip.size() ); }
        const AnimationClip& getSampleClip( uint32 sampleIndex ) const { return *_listClip[sampleIndex]; }
        float32              getSampleWeight( uint32 sampleIndex ) const { return _listWeight[sampleIndex]; }
        /** @brief 상태 시각 @p time(한 바퀴 위의 초)을 표본 클립의 시각으로 옮깁니다(같은 위상). */
        float32 computeSampleTime( uint32 sampleIndex, float32 time ) const;

    private:
        /** @brief 표본 @p sampleIndex 가 지금 알림을 내는지 봅니다(데이터의 알림 방법과 가중치). */
        bool isNotifySample( uint32 sampleIndex ) const;

        hashed_string                      _stateName;
        const AnimationGraphBlendSpaceDef* _pDef; ///< 애니메이터의 그래프 노드 데이터다. 그래프를 다시 읽으면 상태도 다시 만든다
        vector<const AnimationClip*>       _listClip;
        vector<float32>                    _listLength;
        vector<float32>                    _listWeight;
        float32                            _playLength;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletalAnimatorBinding
     * @brief 애니메이터가 유닛과 상태 기계에 노출하는 인터페이스 구현입니다(단계 작업, 클립 해석). 컴포넌트가 하나 보관하고 그대로 넘깁니다.
     * @details 리플렉션 컴포넌트는 기반 클래스 하나만 둔다(타입 정보와 `castTo` 가 단일 상속을 가정한다). 그래서 인터페이스는 이 작은 객체가 구현한다.
     */
    class SW_API SkeletalAnimatorBinding final : public IAnimationPhaseTask, public IAnimationPlayableSource
    {
    public:
        explicit SkeletalAnimatorBinding( SkeletalAnimatorComponent& owner );

        bool                            isAnimationActive() const override;
        AnimationPoseAccess             getPoseAccess() const override { return AnimationPoseAccess::Writes; }
        bool                            pollAnimationAssetsReady() override;
        void                            runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        AnimationPlayer*                findSyncPlayer( hashed_string& outGroupName, float32& outWeight ) override;
        void                            finishAnimationFrame( SkeletalMeshComponent& unit ) override;
        void                            onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;
        bool                            describeSharedPose( AnimationSharedPoseRequest& outRequest ) const override;
        shared_ptr<const AnimationClip> findSharedPoseClip( const AnimationClip* pClip ) const override;
        const IAnimationPlayable*       findPlayable( const hashed_string& name ) const override;
        void                            collectDebugState( AnimationDebugState& inoutState ) const override;

    private:
        SkeletalAnimatorComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletalAnimatorComponent
     * @brief 같은 오브젝트의 `SkeletalMeshComponent`(유닛)에 단계 작업으로 붙어 포즈를 냅니다. 자기 onTick 에서 평가하지 않습니다.
     * @details - **클립**: 상태 이름이 곧 클립 이름이고, 파일은 `_clipFolder/<소문자 이름>.animclip` 입니다(임포트가 그렇게 씁니다).
     *          - **그래프**: `_animationGraphPath` 의 노드가 상태, 링크가 전이입니다(`AnimationGraphPlayer`). 그래프가 없으면 `play( 이름 )` 이 바로 재생합니다.
     *          - **단계**: 시간(상태 기계, 알림, 루트 모션, 커브. 매 프레임) → 기본 포즈(샘플, 크로스페이드, 레이어, 시퀀서 덮어쓰기. LOD 가
     *            허락한 프레임) → 게임 스레드에서 루트 모션을 오브젝트 트랜스폼에 씁니다.
     *          - **핫 리로드**: 현재 상태와 시간을 PROPERTY(`_currentState`, `_stateTime`)로 보관해 모듈이 바뀌어도 그 자리에서 이어 갑니다.
     *          - **루트 모션**: `_bExtractRootMotion` 이면 클립의 루트 모션 트랙 움직임을 오브젝트에 옮기고, 그 본은 클립 시작 위치에 고정합니다.
     *            옮기기 전에 고치는 쪽(`IRootMotionModifier`. 모션 워핑)이 월드 이동과 회전을 바꿀 수 있고, 같은 오브젝트에 캐릭터 컨트롤러가 있으면
     *            이동은 컨트롤러가 질의로 처리합니다(`_bRootMotionThroughController`. 중력은 컨트롤러가 더합니다). 회전은 트랜스폼에 씁니다.
     */
    REFLECT( Category = "Animation 3D", DisplayName = "Skeletal Animator Component", Tooltip = "Plays skeletal clips and an animation graph on the sibling skeletal mesh", Meta = "Icon=Animation" )
    class SW_API SkeletalAnimatorComponent : public Component
    {
        friend class SkeletalAnimatorBinding;

    public:
        REFLECT_BODY();

        SkeletalAnimatorComponent();
        virtual ~SkeletalAnimatorComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /**
         * @brief 상태(또는 그래프 밖 클립)를 재생합니다. @p blendSeconds 가 음수면 `_blendSeconds` 입니다. 클립을 찾았으면 true 입니다.
         * @details 시작 때 요청한 클립을 아직 읽는 중이면(`isWaitingForAnimationAssets`) 재생을 미루고 true 를 반환한다 — 다 읽히면 그때 재생하고, 클립이 없으면 그때 오류를 남긴다.
         */
        bool play( const hashed_string& stateName, bool bLoop = true, float32 blendSeconds = -1.0f );
        FUNCTION( Category = "Playback", DisplayName = "Stop", CallInEditor )
        void stop();
        /** @brief 클립 폴더를 바꿉니다(읽어 둔 클립은 비웁니다). */
        void          setClipFolder( string_view folder );
        const string& getClipFolder() const { return _clipFolder; }
        /** @brief 시작 상태를 바꿉니다(다음 시작부터). */
        void setInitialState( string_view stateName ) { _initialState = string{ stateName }; }
        /** @brief 시작 상태를 이 시각(초)부터 재생합니다(다음 시작부터 적용하며, 군중이 모두 같은 프레임으로 시작하지 않게 합니다). */
        void setInitialTime( float32 seconds ) { _initialTime = seconds > 0.0f ? seconds : 0.0f; }
        /**
         * @brief 재생 중인 클립(현재 슬롯)의 시각을 @p seconds 로 옮깁니다(네트워크 따라잡기, 프레임 지정). 건너뛴 구간의 알림은 울리지 않습니다.
         * @return 재생 중인 것이 없거나, @p seconds 가 유한하지 않거나 음수이거나 클립 길이를 넘으면 오류를 남기고 false 입니다(자르지 않으며 시각은 그대로입니다).
         */
        [[nodiscard]] bool seekCurrentTime( float32 seconds );
        /** @brief 그래프 경로를 바꾸고 다시 읽습니다. */
        void setAnimationGraphPath( string_view path );
        /** @brief 클립을 미리 읽어 둡니다(게임 스레드). 워커의 전이는 읽어 둔 클립만 찾습니다. */
        bool preloadClip( const hashed_string& clipName );
        /**
         * @brief 시작 때 요청한 클립(그래프가 이름 붙인 것과 시작 상태)을 아직 읽는 중이면 true 입니다. 그동안 유닛은 그리지 않고(캐릭터는 애니메이션이 준비된 뒤
         *        나타난다), 다 읽히면 그래프를 짓고 미뤄 둔 시작 재생을 합니다(씬 프레임의 도착 단계, `SkeletalMeshComponent::pollReadyToAppear`).
         * @details 시작(`onBeginPlay`)만 비동기다 — 스폰이 디스크를 기다리지 않게. 코드가 부르는 세터(`setAnimationGraphPath` · `setClipFolder`)와 `play` 의 그래프 밖 클립은
         *          같은 로드 블록을 그 자리에서 기다린다(동기 API).
         */
        bool isWaitingForAnimationAssets() const { return _bClipsPending == SW_TRUE; }
        /** @brief 시작 때 요청한 클립을 그 자리에서 기다려(같은 로드 블록, 이 스레드가 돕는다) 마무리합니다. 테스트 · 에디터 미리보기처럼 이번 프레임에 포즈가 필요할 때 부릅니다. */
        void waitForAnimationAssets();

        /** @brief 상태 기계 파라미터입니다. 게임 코드가 틱에서 씁니다. */
        AnimationParameterSet&       getParameters() { return _parameters; }
        const AnimationParameterSet& getParameters() const { return _parameters; }
        /** @brief 그래프 플레이어(현재 상태, 재생)를 반환합니다. */
        const AnimationGraphPlayer& getGraphPlayer() const { return _graphPlayer; }
        /** @brief 현재 상태 이름을 반환합니다. */
        const hashed_string& getCurrentStateName() const { return _graphPlayer.getCurrentStateName(); }

        /** @brief 레이어를 더하고 그 인덱스를 반환합니다. 클립을 못 찾으면 -1 입니다. */
        int32 addLayer( const AnimationLayerDesc& desc );
        /** @brief 레이어 가중치를 바꿉니다. */
        void setLayerWeight( uint32 layerIndex, float32 weight );
        /** @brief 레이어 클립을 처음부터 다시 재생합니다(맞음 움찔처럼 한 번 도는 가산 레이어). */
        void restartLayer( uint32 layerIndex );
        /** @brief 코드가 더한 레이어를 모두 제거합니다(그래프의 레이어는 그래프 데이터라 남습니다). */
        void clearLayers();
        /**
         * @brief 그래프 레이어(그래프 "layers")의 가중치를 바꿉니다.
         * @return 그 이름의 레이어가 없거나, 가중치를 파라미터("weight_param")가 정하는 레이어면 오류를 남기고 false 입니다(값의 출처는 하나다).
         */
        bool setGraphLayerWeight( const hashed_string& layerName, float32 weight );
        /** @brief 그래프 레이어 수입니다(읽은 그래프의 "layers"). 클립을 못 찾은 레이어가 있으면 그래프를 쓰지 않으므로 0 입니다. */
        uint32 getGraphLayerCount() const { return static_cast<uint32>( _listGraphLayer.size() ); }

        /**
         * @brief 시퀀서 덮어쓰기 슬롯입니다. 시퀀서가 클립과 시간을 정해 그래프 결과 위에 @p weight 만큼 섞습니다. 시간은 시퀀서가 진행시킵니다.
         * @param clip nullptr 이거나 weight 가 0 이면 덮어쓰기가 꺼집니다.
         */
        void setSequencerOverride( shared_ptr<const AnimationClip> clip, float32 time, float32 weight );

        /** @brief 이번 프레임에 울린 알림입니다(다음 평가까지 유효). 이름에 따른 처리기 연결은 받는 쪽(`setNotifyListener`)이 맡습니다. */
        const vector<AnimationFiredNotify>& getFiredNotifies() const { return _listFiredNotify; }
        /** @brief 알림을 받는 쪽을 등록합니다(소유하지 않으며, nullptr 이면 해제합니다). 매 평가 프레임의 게임 스레드 마무리에서 루트 모션 적용 전에 불립니다. */
        void setNotifyListener( IAnimationNotifyListener* pListener ) { _pNotifyListener = pListener; }
        /** @brief 등록된 알림 수신자를 반환합니다. */
        IAnimationNotifyListener* getNotifyListener() const { return _pNotifyListener; }
        /**
         * @brief 루트 모션을 옮기기 전에 고치는 쪽을 등록하고 그 구독을 돌려줍니다(소유하지 않는다). 등록한 순서대로 불립니다.
         * @details 고치는 쪽이 구독을 들고, 놓으면 빠집니다. 애니메이터가 먼저 사라지면 구독은 무효가 됩니다. nullptr 이면 빈 구독입니다.
         */
        [[nodiscard]] Subscription registerRootMotionModifier( IRootMotionModifier* pModifier );
        /** @brief 루트 모션을 같은 오브젝트의 캐릭터 컨트롤러로 옮길지 정합니다(컨트롤러가 있을 때만 적용되며, 벽에 막히고 턱을 오릅니다). */
        void setRootMotionThroughController( bool bThrough ) { _bRootMotionThroughController = bThrough ? SW_TRUE : SW_FALSE; }
        /** @brief 재생 속도 배율을 반환합니다. */
        float32 getPlayRate() const { return _playRate; }
        /** @brief 지금 재생 중인 항목(현재와 다음 슬롯, 레이어)을 @p outListPlayable 에 채웁니다(비우고 채웁니다). */
        void collectActivePlayables( vector<const IAnimationPlayable*>& outListPlayable ) const;
        /** @brief 이번 프레임의 커브 값입니다. 없는 커브는 0 입니다. */
        float32 getCurveValue( const hashed_string& curveName ) const;
        /** @brief 마지막 프레임의 루트 모션(부모 공간의 이동과 회전 차이)입니다. */
        const BoneTransform& getLastRootMotion() const { return _rootMotionDelta; }
        /** @brief 루트 모션을 뽑을지 정합니다. */
        void setExtractRootMotion( bool bExtract ) { _bExtractRootMotion = bExtract ? SW_TRUE : SW_FALSE; }
        /** @brief 재생 속도 배율을 정합니다. */
        void setPlayRate( float32 rate ) { _playRate = rate > 0.0f ? rate : 0.0f; }
        /** @brief 동기 그룹 이름과 가중치(리더 판정)를 정합니다. */
        void setSyncGroup( const hashed_string& groupName, float32 weight );

        /** @brief 지금 할 일이 있으면(재생 중, 레이어, 시퀀서 덮어쓰기) true 입니다. */
        bool isAnimationActive() const;
        /** @brief 이름이 일치하는 클립을 반환합니다(미리 읽어 둔 것만). 없으면 nullptr 입니다. */
        const AnimationClip* findClip( const hashed_string& name ) const;
        /**
         * @brief 이번 프레임 포즈를 군중 묶음과 나눌 수 있으면 요청을 채웁니다. 반복 클립 하나를 블렌드, 레이어, 시퀀서 덮어쓰기 없이 재생 중일 때입니다.
         * @details 시간, 상태 기계, 알림, 루트 모션, 커브는 나눠도 이 애니메이터가 계속 처리한다(시간 단계). 나누는 것은 포즈뿐이다.
         */
        bool describeSharedPose( AnimationSharedPoseRequest& outRequest ) const;
        /** @brief 유닛과 상태 기계에 노출하는 인터페이스 구현을 반환합니다. */
        SkeletalAnimatorBinding& getBinding() { return _binding; }

    private:
        /** @brief 루트 모션 수정자 등록의 출처입니다. 구독이 풀리면 그 수정자를 뺍니다. 리플렉션 클래스라 기반을 더하지 않고 멤버로 둔다. */
        class RootMotionModifierSource final : public SubscriptionSource
        {
        public:
            explicit RootMotionModifierSource( vector<IRootMotionModifier*>& listModifier )
                : _listModifier{ listModifier }
            {
            }
            [[nodiscard]] Subscription issue( IRootMotionModifier* pModifier ) { return issueSubscription( reinterpret_cast<uint64>( pModifier ) ); }

        private:
            void releaseSubscription( uint64 id ) override { VectorUtil::removeFirst( _listModifier, reinterpret_cast<IRootMotionModifier*>( id ) ); }

            vector<IRootMotionModifier*>& _listModifier;
        };

        /**
         * @brief 화면에 보인 지난 두 포즈입니다. inertialization · 이어 섞기의 출발 포즈와 출발 속도가 여기서 나옵니다.
         * @details 출발 포즈는 "지금 화면에 보이는 포즈" 여야 한다. 화면 밖 · 숨김으로 포즈를 건너뛴 프레임이 끼면(`AnimationFrameContext::_bOnScreen` 거짓) 아무도 보지 않은
         *          옛 포즈라 무효로 표시한다(`invalidate`) — 다시 보일 때 옛 포즈에서 출발해 튀지 않는다. LOD 주기로 건너뛴 화면 안 프레임은 지난 포즈가 그대로
         *          보이므로 남기고, 속도는 두 포즈 사이의 실제 시간(`getIntervalSeconds`)으로 구한다(한 프레임 시간으로 나누면 주기 배만큼 빠르다).
         */
        struct PoseHistory
        {
            /** @brief 시간 단계마다 흐른 시간을 셉니다(배율 적용). */
            void advance( float32 deltaSeconds ) { _secondsSinceLast += deltaSeconds; }
            /** @brief 이번 프레임에 만든 포즈를 기록합니다. 지난 포즈는 한 칸 뒤로 밀립니다. */
            void record( const Pose& pose )
            {
                std::swap( _previous, _last );
                _last             = pose;
                _intervalSeconds  = _secondsSinceLast;
                _secondsSinceLast = 0.0f;
                _bPreviousValid   = _bLastValid;
                _bLastValid       = SW_TRUE;
            }
            /** @brief 기록을 무효로 표시합니다(포즈가 화면에 보이지 않은 프레임이 끼었다). */
            void invalidate()
            {
                _bLastValid     = SW_FALSE;
                _bPreviousValid = SW_FALSE;
            }
            /** @brief 출발 포즈입니다. 무효면 nullptr 입니다. */
            const Pose* findLast() const { return _bLastValid == SW_TRUE ? &_last : nullptr; }
            /** @brief 출발 포즈의 한 기록 전 포즈입니다(출발 속도). 무효이거나 본 수가 다르면 nullptr 입니다. */
            const Pose* findPrevious() const
            {
                return ( _bLastValid == SW_TRUE && _bPreviousValid == SW_TRUE && _previous.getBoneCount() == _last.getBoneCount() ) ? &_previous : nullptr;
            }
            /** @brief 두 기록 사이의 시간(초)입니다. 출발 속도를 구할 때 씁니다. */
            float32 getIntervalSeconds() const { return _intervalSeconds; }

        private:
            Pose    _last{};
            Pose    _previous{};
            float32 _intervalSeconds{ 0.0f };
            float32 _secondsSinceLast{ 0.0f };
            uint8   _bLastValid{ SW_FALSE };
            uint8   _bPreviousValid{ SW_FALSE };
        };

        /** @brief 레이어 하나의 실행 상태입니다. */
        struct LayerState
        {
            AnimationLayerDesc              _desc;
            shared_ptr<const AnimationClip> _clip;
            AnimationPlayer                 _player;
            vector<float32>                 _listBoneWeight; ///< 본 마스크(본 수 크기)다. 스켈레톤이 바뀌면 다시 만든다
            const Skeleton*                 _pMaskSkeleton{ nullptr };
            const AnimationGraphLayerDef*   _pGraphLayer{ nullptr };      ///< 그래프 레이어면 그 데이터다(`_graph` 안). 코드 레이어면 nullptr 이다
            const AnimationBoneMaskDef*     _pBoneMask{ nullptr };        ///< 그래프 레이어의 본 마스크다(`_graph` 안). nullptr 이면 `_desc._maskRootBone` 을 쓴다
            Pose                            _additiveReference;           ///< 가산 기준(클립 시각 0, 본 포즈)
            PoseInertializer                _inertializer;                ///< 다시 시작할 때 끊긴 레이어 포즈에서 이어 간다(`_desc._halfLifeSeconds` 가 0 보다 클 때)
            PoseHistory                     _history;                     ///< 화면에 얹힌 지난 레이어 포즈다(inertialization 의 출발 · 속도)
            uint8                           _bRestartPending{ SW_FALSE }; ///< 다음 평가에서 inertialization 을 시작한다
        };

        /** @brief 같은 오브젝트의 유닛에 붙습니다. */
        void bindToUnit();
        /** @brief 그래프를 다시 읽고 노드의 클립을 미리 읽습니다(그 자리에서). 그래프 레이어와 블렌드 스페이스 상태도 다시 만듭니다. 시작 때 요청해 둔 로드는 버립니다. */
        void loadGraph();
        /** @brief 시작 때의 그래프 읽기입니다. 클립은 비동기로 요청하고(`requestClipLoads`), 다 읽혔으면 그 자리에서 마무리합니다. */
        void loadGraphForBeginPlay();
        /** @brief 그래프가 이름 붙인 클립(노드 · 블렌드 표본 · 레이어)과 그래프 밖 시작 상태의 클립을 비동기로 요청합니다(이미 표에 있는 것은 빼고). */
        void requestClipLoads();
        /** @brief 클립 하나를 비동기로 요청합니다. 이름이 비었거나 이미 표에 있거나 요청한 클립이면 아무것도 하지 않습니다. */
        void requestClipLoad( const hashed_string& clipName );
        /** @brief 요청한 클립이 다 읽혔으면 표에 넣고 그래프를 지은 뒤 미뤄 둔 재생을 합니다. 아직 읽는 중이면 false 입니다. */
        [[nodiscard]] bool finishClipLoadsIfComplete();
        /** @brief 시작 상태를 재생합니다(핫 리로드로 되살아났으면 그 상태 · 시각에서 잇는다). */
        void startInitialState();
        /** @brief 클립을 기다리는 동안 미룬 재생(코드의 `play`, 없으면 시작 상태)을 합니다. */
        void runDeferredPlayback();
        /**
         * @brief 그래프의 재생할 것을 다시 만듭니다(`rebuildGraphPlayables`). 이 애니메이터가 재생할 수 없는 것을 그래프가 적었으면 파일을 못 읽은 것과 같이
         *        그래프를 쓰지 않습니다 — 빠진 노드 · 레이어만 빼고 도는 그래프는 데이터가 말한 것과 다른 포즈를 낸다.
         */
        void applyGraphPlayables();
        /**
         * @brief 노드의 클립(블렌드 스페이스면 표본 클립)을 미리 읽고 블렌드 스페이스 상태와 그래프 레이어를 다시 만듭니다.
         * @return 포즈 노드(아직 평가하지 않는다), 표본 클립이 없는 블렌드 스페이스, 클립이 없는 레이어가 있으면 각각 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool rebuildGraphPlayables();
        /** @brief 그래프의 "layers" 로 그래프 레이어를 다시 만듭니다. 클립을 못 찾은 레이어가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool rebuildGraphLayers();
        /** @brief 블렌드 스페이스 상태의 가중치를 파라미터로 다시 구합니다. 현재 슬롯의 상태는 길이가 바뀌어도 위상을 지킵니다. */
        void updateBlendSpaceWeights();
        /** @brief 재생할 것이 이 애니메이터의 블렌드 스페이스 상태면 그것을, 아니면(클립) nullptr 을 반환합니다. */
        const SkeletalBlendSpaceState* findBlendSpaceState( const IAnimationPlayable* pPlayable ) const;
        /** @brief 재생할 것(클립 또는 블렌드 스페이스 상태)을 @p time 에 샘플해 @p inoutPose 에 씁니다. */
        void samplePlayableIntoPose( const IAnimationPlayable& playable, float32 time, const Skeleton& skeleton, Pose& inoutPose, const uint8* pBoneMask );
        /** @brief 재생할 것이 @p step 동안 낸 루트 모션입니다. 블렌드 스페이스면 표본의 움직임을 가중치로 섞습니다. */
        BoneTransform computePlayableRootMotion( const IAnimationPlayable& playable, const AnimationTimeStep& step ) const;
        /** @brief 재생할 것의 커브를 @p weight 를 곱해 이번 프레임 커브 값에 더합니다. */
        void accumulatePlayableCurves( const IAnimationPlayable& playable, float32 time, float32 weight );
        /** @brief 클립 하나의 커브를 @p weight 를 곱해 이번 프레임 커브 값에 더합니다. */
        void accumulateClipCurves( const AnimationClip& clip, float32 time, float32 weight );
        /** @brief 레이어 하나를 @p inoutPose 에 얹습니다. */
        void applyLayer( LayerState& layer, const Skeleton& skeleton, Pose& inoutPose, const uint8* pBoneMask );
        /** @brief 클립의 트랙에서 본으로 가는 테이블입니다(유닛 스켈레톤 기준이며, 처음 쓸 때 만듭니다). */
        const vector<int32>& getTrackMap( const AnimationClip& clip, const Skeleton& skeleton );
        /**
         * @brief 클립을 @p time 에 샘플해 @p inoutPose(레퍼런스로 시작한 본 포즈)에 씁니다.
         * @param pBoneMask 본 LOD 마스크(본 수 크기이며 0 이면 평가하지 않고 레퍼런스로 둔다). nullptr 이면 모든 본입니다.
         */
        void sampleClipIntoPose( const AnimationClip& clip, float32 time, const Skeleton& skeleton, Pose& inoutPose, const uint8* pBoneMask = nullptr );
        /** @brief 레이어의 본 마스크를 스켈레톤에 맞춥니다. */
        void refreshLayerMask( LayerState& layer, const Skeleton& skeleton );
        /** @brief 시간 단계입니다. */
        void advanceTime( const AnimationFrameContext& context );
        /** @brief 기본 포즈 단계입니다. */
        void populateBasePose( SkeletalMeshComponent& unit );
        /** @brief 이름에서 파일 경로를 만듭니다(`_clipFolder/<소문자>.animclip`). */
        string makeClipPath( const hashed_string& clipName ) const;
        /** @brief 이번 프레임의 알림을 받는 쪽에 넘깁니다(게임 스레드). */
        void dispatchNotifies();
        /** @brief 루트 모션을 오브젝트에 옮깁니다(게임 스레드). */
        void applyRootMotion( SkeletalMeshComponent& unit );

        PROPERTY( Category = "Animation", DisplayName = "Animation Graph", AssetPath, AssetType = "AnimationGraph", Tooltip = "State machine graph; nodes name clips" )
        string _animationGraphPath;
        PROPERTY( Category = "Animation", DisplayName = "Clip Folder", Tooltip = "Folder holding <clip>.animclip files (lowercase names)" )
        string _clipFolder;
        PROPERTY( Category = "Animation", DisplayName = "Initial State", Tooltip = "State (or clip) played at begin play; empty uses the graph entry node" )
        string _initialState;
        PROPERTY( Category = "Animation", DisplayName = "Current State", Tooltip = "Currently playing state (kept across hot reload)", ReadOnly )
        string _currentState;
        PROPERTY( Category = "Animation", DisplayName = "State Time", Tooltip = "Playback time of the current state in seconds (kept across hot reload)", Min = 0.0 )
        float32 _stateTime;
        PROPERTY( Category = "Playback", DisplayName = "Play Rate", Tooltip = "Playback speed multiplier", Min = 0.0, Max = 10.0 )
        float32 _playRate;
        PROPERTY( Category = "Playback", DisplayName = "Blend Seconds", Tooltip = "Default crossfade length for transitions", Min = 0.0, Max = 5.0, Units = s )
        float32 _blendSeconds;
        PROPERTY( Category = "Sync", DisplayName = "Sync Group", Tooltip = "Units in the same sync group follow the heaviest one's phase" )
        string _syncGroup;
        PROPERTY( Category = "Sync", DisplayName = "Sync Weight", Tooltip = "Leader is the member with the largest weight", Min = 0.0 )
        float32 _syncWeight;

        SkeletalAnimatorBinding _binding;
        Subscription            _bindingRegistration; ///< 유닛 단계 작업 등록 구독(놓으면 유닛에서 빠진다)
        AnimationGraphAsset     _graph;
        AnimationGraphPlayer    _graphPlayer;
        AnimationParameterSet   _parameters;
        /** @brief 시작 때 요청한 클립 하나입니다. */
        struct ClipLoad
        {
            hashed_string                    _clipName;
            SharedAssetHandle<AnimationClip> _load;
        };

        unordered_map<hashed_string, shared_ptr<const AnimationClip>> _mapClip;
        vector<ClipLoad>                                              _listClipLoad;      ///< 시작 때 요청해 아직 표에 넣지 않은 클립(`requestClipLoads`)
        hashed_string                                                 _deferredPlayState; ///< 클립을 기다리는 동안 미룬 `play` 의 상태
        float32                                                       _deferredPlayBlend; ///< 미룬 `play` 의 섞기 시간
        unordered_map<const AnimationClip*, vector<int32>>            _mapTrackToBone;
        const Skeleton*                                               _pTrackMapSkeleton;
        vector<LayerState>                                            _listLayer;
        vector<LayerState>                                            _listGraphLayer; ///< 그래프 "layers" 의 레이어다. 코드 레이어보다 먼저 얹는다
        vector<unique_ptr<SkeletalBlendSpaceState>>                   _listBlendSpace; ///< 그래프의 블렌드 스페이스 상태다(상태 기계가 포인터를 빌린다)
        vector<Pose>                                                  _listScratchSamplePose;
        vector<const Pose*>                                           _listScratchSamplePosePointer;
        vector<float32>                                               _listScratchSampleWeight;
        vector<AnimationFiredNotify>                                  _listFiredNotify;
        vector<const IAnimationPlayable*>                             _listActivePlayable; ///< 받는 쪽에 넘길 재생 중인 항목이다(재사용)
        vector<IRootMotionModifier*>                                  _listRootMotionModifier;
        RootMotionModifierSource                                      _rootMotionModifierSource; ///< `_listRootMotionModifier` 의 등록 구독을 내준다
        CurveAccumulator                                              _curves;                   ///< 이번 프레임 커브(이름 → 값)
        Pose                                                          _scratchPose;
        Pose                                                          _scratchTrackPose;
        Pose                                                          _scratchLayerPose;
        vector<uint8>                                                 _listScratchTrackMask; ///< 본 LOD 마스크를 트랙 순서로 옮긴 것(샘플마다 재사용)
        PoseHistory                                                   _baseHistory;          ///< 화면에 보인 지난 기본 포즈(레이어 전)다. 페이드가 끊기면 · inertialization 이 여기서 출발한다
        Pose                                                          _carryOverPose;        ///< 끊긴 순간의 포즈다. 새 페이드 길이 동안 현재 포즈로 섞이며 사라진다
        PoseInertializer                                              _inertializer;         ///< inertialization 전이의 남은 차이다(기본 포즈에 더한다)
        shared_ptr<const AnimationClip>                               _sequencerClip;
        BoneTransform                                                 _rootMotionDelta;
        SkeletalMeshComponent*                                        _pUnit;
        IAnimationNotifyListener*                                     _pNotifyListener;
        float32                                                       _lastDeltaSeconds; ///< 마지막 시간 단계의 델타 시간(배율 적용)
        float32                                                       _sequencerTime;
        float32                                                       _sequencerWeight;
        float32                                                       _initialTime; ///< 시작 상태의 첫 시각(초, 저장하지 않는 런타임 값)
        float32                                                       _carryOverElapsed;
        float32                                                       _carryOverDuration;
        uint32                                                        _seenInterruptCount;       ///< 플레이어의 중단 횟수를 마지막으로 확인한 값
        uint32                                                        _seenInertializationCount; ///< 플레이어의 inertialization 횟수를 마지막으로 확인한 값
        uint64                                                        _lastTimeFrameIndex;       ///< 마지막 시간 단계의 프레임 번호다. 사이가 비면(쉬었다) 지난 포즈가 낡았다
        PROPERTY( Category = "Animation", DisplayName = "Extract Root Motion", Tooltip = "Move the object by the clip's root motion track" )
        uint8 _bExtractRootMotion : 1;
        PROPERTY( Category = "Animation", DisplayName = "Play On Begin", Tooltip = "Start the initial state at begin play" )
        uint8 _bPlayOnBegin : 1;
        PROPERTY( Category = "Animation", DisplayName = "Root Motion Through Controller",
                  Tooltip = "Move by root motion through the sibling character controller (collides, climbs steps) instead of writing the transform" )
        uint8                  _bRootMotionThroughController : 1;
        uint8                  _bPoseShownThisFrame          : 1; ///< 이번 프레임에 포즈를 만들었거나 지난 포즈가 화면에 그대로 보인다(다음 시간 단계가 기록의 유효를 판정한다)
        uint8                  _bCarryingOver                : 1;
        uint8                  _bInertializationPending      : 1; ///< 새 inertialization 이 다음 포즈 계산에서 시작되기를 기다린다
        uint8                  _bClipsPending                : 1; ///< 시작 때 요청한 클립을 읽는 중이다(`isWaitingForAnimationAssets`)
        uint8                  _bStartPending                : 1; ///< 클립이 다 읽히면 시작 상태를 재생한다
        uint8                  _bPlayDeferred                : 1; ///< 클립이 다 읽히면 미룬 play 를 한다
        uint8                  _bDeferredPlayLoop            : 1; ///< 미룬 `play` 의 반복 여부
        [[maybe_unused]] uint8 _reserved                     : 6;
    };
} // namespace sw
