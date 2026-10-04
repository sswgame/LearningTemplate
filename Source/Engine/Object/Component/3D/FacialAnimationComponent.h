/**
 * @file FacialAnimationComponent.h
 * @brief 얼굴 애니메이션 — 표정 커브 · 립싱크 · 눈 깜빡임 · 시선(사카드)을 같은 오브젝트 유닛(`SkeletalMeshComponent`)의 모프 가중치 · 눈 본에 겁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class FacialAnimationComponent;
    class Mesh;
    class Skeleton;

    /**
     * @class FacialAnimationBinding
     * @brief 얼굴이 유닛에 보이는 단계 일입니다(리플렉션 컴포넌트는 기반 클래스 하나라 이 작은 객체가 인터페이스를 구현한다).
     */
    class SW_API FacialAnimationBinding final : public IAnimationPhaseTask
    {
    public:
        explicit FacialAnimationBinding( FacialAnimationComponent& owner );

        bool isAnimationActive() const override;
        void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        void finishAnimationFrame( SkeletalMeshComponent& unit ) override;
        void onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;

    private:
        FacialAnimationComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class FacialAnimationComponent
     * @brief 얼굴 리그(`_facialRigPath`, `.facial.json`)대로 얼굴을 움직입니다. 자기 onTick 이 아니라 유닛의 단계 일로 돕니다.
     * @details - **표정**: `setExpressionWeight` 로 정한 값 + 같은 오브젝트 애니메이터의 같은 이름 커브(표정 커브 — 언리얼 MetaHuman 커브 · Live Link Face
     *            의 자리)를 더해, 리그의 타깃 가중치에 곱해 유닛의 모프 가중치에 더합니다(GPU 모프 풀이 스키닝 앞에 적용).
     *          - **립싱크**: `speak( 음성 )` — 임포트가 만든 비즘 트랙(`<음성>.visemes.json`)이 있으면 그 곡선을, 없으면 PCM 의 진폭을 런타임에 재서
     *            리그의 대체 비즘(립싱크 표의 `fallback_viseme`)을 엽니다. 비즘 → 타깃은 리그 데이터입니다.
     *          - **깜빡임**: 간격(최소 ~ 최대, 결정적 난수) · 길이로 눈꺼풀 타깃을 올렸다 내립니다.
     *          - **시선**: `setLookAtTarget( 월드 위치 )` — 눈 본을 목표 쪽으로 돌립니다(최대 각으로 자름). 사카드는 간격마다 작은 각으로 튑니다.
     *          - 단계: 시간(타이머 · 말하기 시각, 워커) → 후처리(가중치 · 눈 본, 워커 — 기본 포즈 뒤라 애니메이터 커브가 이미 있다) → 게임 스레드(목표를 모델
     *            공간으로 옮겨 둔다 — 한 프레임 늦다).
     */
    REFLECT( Category = "Animation 3D", DisplayName = "Facial Animation Component", Tooltip = "Expression curves, lip sync, blink and gaze on the sibling skeletal mesh" )
    class SW_API FacialAnimationComponent : public Component
    {
        friend class FacialAnimationBinding;

    public:
        REFLECT_BODY();

        FacialAnimationComponent();
        virtual ~FacialAnimationComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 얼굴 리그 경로를 바꾸고 읽습니다. */
        void          setFacialRigPath( string_view path );
        const string& getFacialRigPath() const { return _facialRigPath; }
        /** @brief 리그를 런타임에 정합니다(시험 · 절차 생성). */
        void             setFacialRig( const FacialRig& rig );
        const FacialRig& getFacialRig() const { return _rig; }

        /** @brief 표정 가중치를 정합니다(리그에 없는 이름이면 false). 애니메이터의 같은 이름 커브와 더해집니다. */
        bool setExpressionWeight( const hashed_string& expressionName, float32 weight );
        /** @brief 시선 목표(월드)를 정합니다. */
        void setLookAtTarget( const float3& worldPosition );
        /** @brief 시선을 놓습니다(눈은 애니메이션 그대로). */
        void clearLookAtTarget();
        /**
         * @brief 음성을 말합니다 — 비즘 트랙이 있으면 그것, 없으면 PCM 진폭으로 입을 엽니다. 오디오가 있으면 소리도 냅니다.
         * @return 트랙도 PCM 도 읽지 못하면 false 입니다.
         */
        bool speak( string_view audioPath );
        /** @brief 비즘 트랙을 직접 말합니다(시험 · 절차 생성 — 소리는 내지 않는다). */
        void speakTrack( const VisemeTrack& track );
        /** @brief 진폭 표본(모노 float)을 직접 말합니다(시험 — 소리는 내지 않는다). */
        void speakSamples( vector<float32> listSample, uint32 sampleRate );
        /** @brief 말하기를 멈춥니다. */
        void stopSpeaking();
        /** @brief 말하는 중인지입니다(트랙 · 표본 끝까지). */
        bool isSpeaking() const;
        /** @brief 립싱크 표를 정합니다(시험). 정하지 않으면 `LipSyncSettings::kResourcePath` 를 읽습니다. */
        void setLipSyncSettings( const LipSyncSettings& settings );

        /** @brief 저절로 깜빡일지입니다. */
        void setAutoBlink( bool bBlink ) { _bBlink = bBlink ? SW_TRUE : SW_FALSE; }
        /** @brief 시선에 사카드를 더할지입니다. */
        void setSaccades( bool bSaccades ) { _bSaccades = bSaccades ? SW_TRUE : SW_FALSE; }

        /** @brief 지금 깜빡임 정도(0 = 뜸, 1 = 감음)입니다(진단 · 시험). */
        float32 getBlinkAmount() const { return _blinkAmount; }
        /** @brief 지금 사카드 오프셋(요 · 피치 라디안)입니다(진단 · 시험). */
        const float2& getSaccadeOffset() const { return _saccadeOffset; }
        /** @brief 얼굴이 유닛에 보이는 얼굴입니다. */
        FacialAnimationBinding& getBinding() { return _binding; }

    private:
        /** @brief 같은 오브젝트의 유닛에 붙습니다. */
        void bindToUnit();
        /** @brief 리그를 경로에서 다시 읽습니다. */
        void loadRig();
        /** @brief 유닛의 메시 · 스켈레톤에 맞춰 타깃 번호 · 눈 본 번호를 다시 짓습니다(바뀌었을 때만). */
        void refreshBindings( const SkeletalMeshComponent& unit );
        /** @brief 0 ~ 1 결정적 난수입니다(xorshift — 컴포넌트 id 로 씨앗). */
        float32 nextRandom();
        /** @brief 시간 단계 — 깜빡임 · 사카드 · 말하기 시각을 흘립니다. */
        void advanceTime( float32 deltaSeconds );
        /** @brief 후처리 단계 — 모프 가중치 · 눈 본을 겁니다. */
        void applyFace( SkeletalMeshComponent& unit );
        /** @brief 포즈 하나(표정 · 비즘)를 가중치만큼 유닛에 더합니다. */
        void addPose( SkeletalMeshComponent& unit, const vector<int32>& listTargetIndex, const FacialPose& pose, float32 weight ) const;
        /** @brief 눈 본을 목표 쪽으로 돌립니다. */
        void applyGaze( SkeletalMeshComponent& unit );

        PROPERTY( Category = "Facial", DisplayName = "Facial Rig", AssetPath, AssetType = "FacialRig", Tooltip = "Facial rig (.facial.json): expressions, visemes, blink, gaze" )
        string _facialRigPath;

        FacialAnimationBinding _binding;
        FacialRig              _rig;
        LipSyncSettings        _lipSync;
        VisemeTrack            _speechTrack;
        vector<float32>        _listSpeechSample;     ///< 트랙이 없을 때의 진폭 립싱크 표본(모노)
        vector<float32>        _listExpressionWeight; ///< 리그 표정 순서의 명시 가중치
        vector<vector<int32>>  _listExpressionTarget; ///< 표정마다 타깃 → 메시 타깃 번호
        vector<vector<int32>>  _listVisemeTarget;     ///< 비즘마다 타깃 → 메시 타깃 번호
        vector<int32>          _listBlinkTarget;      ///< 깜빡임 타깃 → 메시 타깃 번호
        vector<int32>          _listEyeBone;          ///< 눈 본 번호
        vector<int32>          _listTrackToRigViseme; ///< 립싱크 비즘 순서 → 리그 비즘 번호
        vector<float32>        _listScratchVisemeWeight;
        ComponentHandle        _animator; ///< 같은 오브젝트의 애니메이터(표정 커브 — 게임 스레드가 프레임마다 찾아 둔다)
        SkeletalMeshComponent* _pUnit;
        const Mesh*            _pBoundMesh;     ///< 번호를 지은 메시(정체성만)
        const Skeleton*        _pBoundSkeleton; ///< 번호를 지은 스켈레톤(정체성만)
        float3                 _lookAtWorld;
        float3                 _lookAtModel; ///< 게임 스레드가 옮겨 둔 모델 공간 목표
        float2                 _saccadeOffset;
        float32                _speechTime;
        uint32                 _speechSampleRate;
        float32                _blinkTimer;   ///< 다음 깜빡임까지 남은 시간
        float32                _blinkElapsed; ///< 깜빡이는 중 지난 시간(음수 = 깜빡이지 않음)
        float32                _blinkAmount;
        float32                _saccadeTimer;
        uint32                 _randomState;
        PROPERTY( Category = "Facial", DisplayName = "Blink", Tooltip = "Blink automatically with the rig's interval and duration" )
        uint8 _bBlink : 1;
        PROPERTY( Category = "Facial", DisplayName = "Saccades", Tooltip = "Add small random eye jumps while looking at a target" )
        uint8                  _bSaccades             : 1;
        uint8                  _bLookAt               : 1;
        uint8                  _bLookAtModelReady     : 1;
        uint8                  _bSpeaking             : 1;
        uint8                  _bLipSyncSettingsReady : 1;
        [[maybe_unused]] uint8 _reserved              : 2;
    };
} // namespace sw
