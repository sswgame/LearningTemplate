/**
 * @file SkeletalAnimatorComponent.h
 * @brief 스켈레탈 애니메이터 — 그래프(상태 기계) 인스턴스 · 파라미터 · 레이어 · 동기 그룹 · 루트 모션 · 시퀀서 덮어쓰기 칸입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimGraphAsset.h"
#include "Engine/Animation/AnimGraphPlayer.h"
#include "Engine/Animation/Pose.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class AnimClip;
    class IAnimNotifyListener;
    class IRootMotionModifier;
    class SkeletalAnimatorComponent;

    /**
     * @enum AnimLayerBlend
     * @brief 레이어가 아래 포즈에 얹히는 방법입니다.
     */
    enum class AnimLayerBlend : uint8
    {
        Override = 0, ///< 마스크 안의 본을 가중치만큼 레이어 포즈로 섞습니다(상체만 조준).
        Additive,     ///< 레이어 클립의 시각 0 포즈 기준 차이를 가중치만큼 더합니다(숨쉬기 · 맞음 움찔).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimLayerDesc
     * @brief 레이어 하나의 서술입니다. 마스크는 본 하나와 그 자손입니다(비었으면 모든 본).
     */
    struct AnimLayerDesc
    {
        hashed_string  _clipName;
        hashed_string  _maskRootBone;
        float32        _weight{ 1.0f };
        AnimLayerBlend _blend{ AnimLayerBlend::Override };
        uint8          _bLoop{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletalAnimatorBinding
     * @brief 애니메이터가 유닛 · 상태 기계에 보이는 얼굴입니다(단계 일 · 클립 풀이). 컴포넌트가 하나 들고 그대로 넘깁니다.
     * @details 리플렉션 컴포넌트는 기반 클래스 하나만 둡니다(타입 정보 · `castTo` 가 단일 상속을 가정). 그래서 인터페이스는 이 작은 객체가 구현합니다.
     */
    class SW_API SkeletalAnimatorBinding final : public IAnimationPhaseTask, public IAnimPlayableSource
    {
    public:
        explicit SkeletalAnimatorBinding( SkeletalAnimatorComponent& owner );

        bool                       isAnimationActive() const override;
        void                       runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        AnimPlayer*                findSyncPlayer( hashed_string& outGroupName, float32& outWeight ) override;
        void                       finishAnimationFrame( SkeletalMeshComponent& unit ) override;
        void                       onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;
        bool                       describeSharedPose( AnimSharedPoseRequest& outRequest ) const override;
        shared_ptr<const AnimClip> findSharedPoseClip( const AnimClip* pClip ) const override;
        const IAnimPlayable*       findPlayable( const hashed_string& name ) const override;
        void                       collectDebugState( AnimationDebugState& inoutState ) const override;

    private:
        SkeletalAnimatorComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletalAnimatorComponent
     * @brief 같은 오브젝트의 `SkeletalMeshComponent`(유닛)에 단계 일로 붙어 포즈를 냅니다. 자기 onTick 에서 평가하지 않습니다.
     * @details - **클립**: 상태 이름 = 클립 이름이고, 파일은 `_clipFolder/<소문자 이름>.animclip` 입니다(임포트가 그렇게 씁니다).
     *          - **그래프**: `_animGraphPath` 의 노드가 상태, 링크가 전이입니다(`AnimGraphPlayer`). 그래프가 없으면 `play( 이름 )` 이 바로 재생합니다.
     *          - **단계**: 시간(상태 기계 · 알림 · 루트 모션 · 커브 — 매 프레임) → 기본 포즈(샘플 · 크로스페이드 · 레이어 · 시퀀서 덮어쓰기 — LOD 가
     *            허락한 프레임) → 게임 스레드에서 루트 모션을 오브젝트 트랜스폼에 씁니다.
     *          - **핫 리로드**: 지금 상태 · 시각을 PROPERTY(`_currentState` · `_stateTime`)로 들고 있어 모듈이 바뀌어도 그 자리에서 잇습니다.
     *          - **루트 모션**: `_bExtractRootMotion` 이면 클립의 루트 모션 트랙 움직임을 오브젝트에 옮기고, 그 본은 클립 시작 자리에 묶습니다.
     *            옮기기 전에 고치는 쪽(`IRootMotionModifier` — 모션 워핑)이 월드 이동 · 회전을 바꾸고, 같은 오브젝트에 캐릭터 컨트롤러가 있으면
     *            이동은 컨트롤러가 질의로 움직입니다(`_bRootMotionThroughController` — 중력은 컨트롤러가 더한다), 회전은 트랜스폼에 씁니다.
     */
    REFLECT( Category = "Animation 3D", DisplayName = "Skeletal Animator Component", Tooltip = "Plays skeletal clips and an animation graph on the sibling skeletal mesh" )
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

        /** @brief 상태(또는 그래프 밖 클립)를 재생합니다. @p blendSeconds 가 음수면 `_blendSeconds` 입니다. 클립을 찾았으면 true 입니다. */
        bool play( const hashed_string& stateName, bool bLoop = true, float32 blendSeconds = -1.0f );
        FUNCTION( Category = "Playback", DisplayName = "Stop", CallInEditor )
        void stop();
        /** @brief 클립 폴더를 바꿉니다(읽어 둔 클립은 비웁니다). */
        void          setClipFolder( string_view folder );
        const string& getClipFolder() const { return _clipFolder; }
        /** @brief 시작 상태를 바꿉니다(다음 시작부터). */
        void setInitialState( string_view stateName ) { _initialState = string{ stateName }; }
        /** @brief 시작 상태를 이 시각(초)부터 재생합니다(다음 시작부터 — 군중이 모두 같은 프레임으로 시작하지 않게). */
        void setInitialTime( float32 seconds ) { _initialTime = seconds > 0.0f ? seconds : 0.0f; }
        /** @brief 그래프 경로를 바꾸고 다시 읽습니다. */
        void setAnimGraphPath( string_view path );
        /** @brief 클립을 미리 읽어 둡니다(게임 스레드). 워커의 전이는 읽어 둔 클립만 찾습니다. */
        bool preloadClip( const hashed_string& clipName );

        /** @brief 상태 기계 파라미터입니다. 게임 코드가 틱에서 씁니다. */
        AnimParameterSet&       getParameters() { return _parameters; }
        const AnimParameterSet& getParameters() const { return _parameters; }
        /** @brief 그래프 플레이어(지금 상태 · 재생)입니다. */
        const AnimGraphPlayer& getGraphPlayer() const { return _graphPlayer; }
        /** @brief 지금 상태 이름입니다. */
        const hashed_string& getCurrentStateName() const { return _graphPlayer.getCurrentStateName(); }

        /** @brief 레이어를 더하고 그 인덱스를 반환합니다. 클립을 못 찾으면 -1 입니다. */
        int32 addLayer( const AnimLayerDesc& desc );
        /** @brief 레이어 가중치를 바꿉니다. */
        void setLayerWeight( uint32 layerIndex, float32 weight );
        /** @brief 레이어 클립을 처음부터 다시 재생합니다(맞음 움찔처럼 한 번 도는 가산 레이어). */
        void restartLayer( uint32 layerIndex );
        /** @brief 레이어를 모두 뗍니다. */
        void clearLayers();

        /**
         * @brief 시퀀서 덮어쓰기 칸 — 시퀀서가 클립과 시각을 정해 그래프 결과 위에 @p weight 만큼 섞습니다. 시각은 시퀀서가 흘립니다.
         * @param clip nullptr 이거나 weight 가 0 이면 덮어쓰기가 꺼집니다.
         */
        void setSequencerOverride( shared_ptr<const AnimClip> clip, float32 time, float32 weight );

        /** @brief 이번 프레임에 울린 알림입니다(다음 평가까지 유효). 디스패치(이름 → 처리기)는 받는 쪽(`setNotifyListener`)으로 받습니다. */
        const vector<AnimFiredNotify>& getFiredNotifies() const { return _listFiredNotify; }
        /** @brief 알림을 받는 쪽을 겁니다(빌립니다, nullptr 이면 뗍니다). 매 평가 프레임의 게임 스레드 마무리에서 루트 모션 적용 전에 불립니다. */
        void setNotifyListener( IAnimNotifyListener* pListener ) { _pNotifyListener = pListener; }
        /** @brief 걸린 받는 쪽입니다. */
        IAnimNotifyListener* getNotifyListener() const { return _pNotifyListener; }
        /** @brief 루트 모션을 옮기기 전에 고치는 쪽을 겁니다(빌립니다 — 사라지기 전에 떼야 합니다). 건 순서대로 불립니다. */
        void addRootMotionModifier( IRootMotionModifier* pModifier );
        /** @brief 고치는 쪽을 뗍니다. */
        void removeRootMotionModifier( IRootMotionModifier* pModifier );
        /** @brief 루트 모션을 같은 오브젝트의 캐릭터 컨트롤러로 옮길지입니다(있을 때만 — 벽에 막히고 턱을 오른다). */
        void setRootMotionThroughController( bool bThrough ) { _bRootMotionThroughController = bThrough ? SW_TRUE : SW_FALSE; }
        /** @brief 재생 속도 배율입니다. */
        float32 getPlayRate() const { return _playRate; }
        /** @brief 지금 재생 중인 재생할 것(지금 · 다음 칸, 레이어)을 @p outListPlayable 에 채웁니다(비우고 채움). */
        void collectActivePlayables( vector<const IAnimPlayable*>& outListPlayable ) const;
        /** @brief 이번 프레임의 커브 값입니다. 없는 커브는 0 입니다. */
        float32 getCurveValue( const hashed_string& curveName ) const;
        /** @brief 마지막 프레임의 루트 모션(부모 공간 이동 · 회전 차이)입니다. */
        const BoneTransform& getLastRootMotion() const { return _rootMotionDelta; }
        /** @brief 루트 모션을 뽑을지 정합니다. */
        void setExtractRootMotion( bool bExtract ) { _bExtractRootMotion = bExtract ? SW_TRUE : SW_FALSE; }
        /** @brief 재생 속도 배율입니다. */
        void setPlayRate( float32 rate ) { _playRate = rate > 0.0f ? rate : 0.0f; }
        /** @brief 동기 그룹 이름 · 가중치(리더 판정)입니다. */
        void setSyncGroup( const hashed_string& groupName, float32 weight );

        /** @brief 지금 할 일이 있는지입니다(재생 중 · 레이어 · 시퀀서 덮어쓰기). */
        bool isAnimationActive() const;
        /** @brief 이름의 클립입니다(미리 읽어 둔 것만). 없으면 nullptr 입니다. */
        const AnimClip* findClip( const hashed_string& name ) const;
        /**
         * @brief 이번 프레임 포즈를 군중 묶음과 나눌 수 있으면 요청을 채웁니다 — 반복 클립 하나를 섞기 · 레이어 · 시퀀서 덮어쓰기 없이 재생 중일 때입니다.
         * @details 시간 · 상태 기계 · 알림 · 루트 모션 · 커브는 나눠도 이 애니메이터가 계속 돌립니다(시간 단계). 나누는 것은 포즈뿐입니다.
         */
        bool describeSharedPose( AnimSharedPoseRequest& outRequest ) const;
        /** @brief 유닛 · 상태 기계에 보이는 얼굴입니다. */
        SkeletalAnimatorBinding& getBinding() { return _binding; }

    private:
        /** @brief 레이어 하나의 실행 상태입니다. */
        struct LayerState
        {
            AnimLayerDesc              _desc;
            shared_ptr<const AnimClip> _clip;
            AnimPlayer                 _player;
            vector<float32>            _listBoneWeight; ///< 본 마스크(본 수). 스켈레톤이 바뀌면 다시 짓습니다
            const Skeleton*            _pMaskSkeleton{ nullptr };
            Pose                       _additiveReference; ///< 가산 기준(클립 시각 0, 본 포즈)
        };

        /** @brief 같은 오브젝트의 유닛에 붙습니다. */
        void bindToUnit();
        /** @brief 그래프를 다시 읽고 노드의 클립을 미리 읽습니다. */
        void loadGraph();
        /** @brief 클립의 트랙 → 본 표입니다(유닛 스켈레톤 기준, 처음 쓸 때 짓습니다). */
        const vector<int32>& getTrackMap( const AnimClip& clip, const Skeleton& skeleton );
        /**
         * @brief 클립을 @p time 에 샘플해 @p inoutPose(레퍼런스로 시작한 본 포즈)에 씁니다.
         * @param pBoneMask 본 LOD 마스크(본 수, 0 = 풀지 않고 레퍼런스로 둔다). nullptr 이면 모든 본입니다.
         */
        void sampleClipIntoPose( const AnimClip& clip, float32 time, const Skeleton& skeleton, Pose& inoutPose, const uint8* pBoneMask = nullptr );
        /** @brief 레이어의 본 마스크를 스켈레톤에 맞춥니다. */
        void refreshLayerMask( LayerState& layer, const Skeleton& skeleton );
        /** @brief 시간 단계입니다. */
        void advanceTime( const AnimationFrameContext& context );
        /** @brief 기본 포즈 단계입니다. */
        void buildBasePose( SkeletalMeshComponent& unit );
        /** @brief 이름 → 파일 경로입니다(`_clipFolder/<소문자>.animclip`). */
        string makeClipPath( const hashed_string& clipName ) const;
        /** @brief 이번 프레임의 알림을 받는 쪽에 넘깁니다(게임 스레드). */
        void dispatchNotifies();
        /** @brief 루트 모션을 오브젝트에 옮깁니다(게임 스레드). */
        void applyRootMotion( SkeletalMeshComponent& unit );

        PROPERTY( Category = "Animation", DisplayName = "Animation Graph", AssetPath, AssetType = "AnimGraph", Tooltip = "State machine graph; nodes name clips" )
        string _animGraphPath;
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

        SkeletalAnimatorBinding                                  _binding;
        AnimGraphAsset                                           _graph;
        AnimGraphPlayer                                          _graphPlayer;
        AnimParameterSet                                         _parameters;
        unordered_map<hashed_string, shared_ptr<const AnimClip>> _mapClip;
        unordered_map<const AnimClip*, vector<int32>>            _mapTrackToBone;
        const Skeleton*                                          _pTrackMapSkeleton;
        vector<LayerState>                                       _listLayer;
        vector<AnimFiredNotify>                                  _listFiredNotify;
        vector<const IAnimPlayable*>                             _listActivePlayable; ///< 받는 쪽에 넘길 재생 중인 것(재사용)
        vector<IRootMotionModifier*>                             _listRootMotionModifier;
        vector<hashed_string>                                    _listCurveName;  ///< 이번 프레임 커브 이름
        vector<float32>                                          _listCurveValue; ///< `_listCurveName` 과 나란한 값
        Pose                                                     _scratchPose;
        Pose                                                     _scratchTrackPose;
        Pose                                                     _scratchLayerPose;
        vector<uint8>                                            _listScratchTrackMask; ///< 본 LOD 마스크를 트랙 순서로 옮긴 것(샘플마다 재사용)
        Pose                                                     _lastBasePose;         ///< 지난 기본 포즈(레이어 전) — 페이드가 끊기면 여기서 이어 섞는다
        Pose                                                     _carryOverPose;        ///< 끊긴 순간의 포즈 — 새 페이드 길이 동안 지금 포즈로 섞여 사라진다
        shared_ptr<const AnimClip>                               _sequencerClip;
        BoneTransform                                            _rootMotionDelta;
        SkeletalMeshComponent*                                   _pUnit;
        IAnimNotifyListener*                                     _pNotifyListener;
        float32                                                  _lastDeltaSeconds; ///< 마지막 시간 단계의 걸음(배율 적용)
        float32                                                  _sequencerTime;
        float32                                                  _sequencerWeight;
        float32                                                  _initialTime; ///< 시작 상태의 첫 시각(초, 저장하지 않는 런타임 값)
        float32                                                  _carryOverElapsed;
        float32                                                  _carryOverDuration;
        uint32                                                   _seenInterruptCount; ///< 플레이어의 끊긴 수를 마지막으로 본 값
        PROPERTY( Category = "Animation", DisplayName = "Extract Root Motion", Tooltip = "Move the object by the clip's root motion track" )
        uint8 _bExtractRootMotion : 1;
        PROPERTY( Category = "Animation", DisplayName = "Play On Begin", Tooltip = "Start the initial state at begin play" )
        uint8 _bPlayOnBegin : 1;
        PROPERTY( Category = "Animation", DisplayName = "Root Motion Through Controller",
                  Tooltip = "Move by root motion through the sibling character controller (collides, climbs steps) instead of writing the transform" )
        uint8                  _bRootMotionThroughController : 1;
        uint8                  _bLastBasePoseValid           : 1;
        uint8                  _bCarryingOver                : 1;
        [[maybe_unused]] uint8 _reserved                     : 3;
    };
} // namespace sw
