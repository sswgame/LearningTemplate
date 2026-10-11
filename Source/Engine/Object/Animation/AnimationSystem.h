/**
 * @file AnimationSystem.h
 * @brief 애니메이션 유닛(`SkeletalMeshComponent`)을 의존 순서와 단계별로 평가하는 시스템입니다. `GameObjectManager` 가 소유하고 틱 뒤에 부릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Object/Animation/AnimationCrowd.h"
#include "Engine/Object/Animation/AnimationDebugState.h"
#include "Engine/Object/Animation/AnimationLOD.h"
#include "Engine/Object/Animation/AnimationRewind.h"

namespace sw
{
    class AnimPlayer;
    class Component;
    class GameObjectManager;
    class SkeletalMeshComponent;
    class SkeletonBoneLOD;

    /**
     * @enum AnimationPhase
     * @brief 한 프레임의 단계입니다. 단계마다 모든 유닛을 의존 순서(레벨)대로 처리하고, 레벨 안에서는 병렬로 처리합니다.
     */
    enum class AnimationPhase : uint8
    {
        Time = 0,    ///< 시간, 상태 기계, 알림, 루트 모션, 커브를 처리한다. 포즈는 만들지 않으며, LOD 로 건너뛴 유닛도 처리한다
        BasePose,    ///< 기본 포즈 단계다. 클립 샘플, 블렌드, 레이어, 리더 포즈 따르기를 처리하고, 끝나면 모델 공간을 구한다
        Attachment,  ///< 부착 단계다. 소켓을 붙이며, 위 유닛의 본을 읽는다
        PostProcess, ///< 후처리 단계다. IK, 제약, 스프링 본(PoseModifierComponent)을 처리하고, 끝나면 모델 공간을 다시 구한다
        SkinPalette, ///< 스킨 팔레트(역 바인드 × 모델 공간)
        Count,
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationFrameContext
     * @brief 단계 하나에 넘기는 프레임 값입니다.
     */
    struct AnimationFrameContext
    {
        float32 _deltaSeconds{ 0.0f };
        uint64  _frameIndex{ 0 };
        uint8   _bPoseNeeded{ SW_TRUE }; ///< 이번 프레임에 포즈를 만드는지 나타낸다. LOD 나 화면 밖이면 거짓이고, 시간만 흐른다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAnimationPhaseTask
     * @brief 유닛 하나의 단계에 끼어드는 작업입니다(애니메이터, 이후의 PoseModifier, 소켓 부착). 유닛에 등록합니다(`SkeletalMeshComponent::addAnimationPhaseTask`).
     * @details 단계 함수는 워커에서 불린다. 자기 유닛과, 의존으로 선언한 위 유닛만 읽는다. 게임 스레드에서 해야 하는 일(트랜스폼 쓰기)은
     *          `finishAnimationFrame` 에 둔다(모든 단계가 끝난 뒤 게임 스레드에서 차례로 불린다).
     */
    class SW_API IAnimationPhaseTask
    {
    public:
        IAnimationPhaseTask()                                        = default;
        virtual ~IAnimationPhaseTask()                               = default;
        IAnimationPhaseTask( const IAnimationPhaseTask& )            = delete;
        IAnimationPhaseTask& operator=( const IAnimationPhaseTask& ) = delete;

        /** @brief 이번 프레임에 할 일이 있으면 true 입니다. 모든 작업이 거짓이고 포즈가 깨끗한 유닛은 쉬며 비용이 없습니다. */
        virtual bool isAnimationActive() const = 0;
        /**
         * @brief 단계들 앞의 게임 스레드 준비 작업입니다(일하는 유닛만, LOD 로 포즈를 건너뛰는 프레임도 부릅니다). 다른 오브젝트의 월드 변환이나 물리 질의처럼
         *        워커에서 읽을 수 없는 것을 여기서 스냅샷으로 저장해 둡니다.
         */
        virtual void prepareAnimationFrame( SkeletalMeshComponent& unit, const AnimationFrameContext& context )
        {
            (void)unit;
            (void)context;
        }
        /** @brief 단계 하나를 처리합니다(워커). */
        virtual void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) = 0;
        /** @brief 동기 그룹에 속하면 그룹 이름과 가중치, 맞출 플레이어를 반환합니다. 아니면 nullptr 입니다(시간 단계 뒤, 게임 스레드). */
        virtual AnimPlayer* findSyncPlayer( hashed_string& outGroupName, float32& outWeight )
        {
            (void)outGroupName;
            (void)outWeight;
            return nullptr;
        }
        /** @brief 모든 단계 뒤 게임 스레드에서 불립니다(루트 모션 적용 등). */
        virtual void finishAnimationFrame( SkeletalMeshComponent& unit ) { (void)unit; }
        /**
         * @brief 이번 프레임 포즈를 군중 묶음과 나눌 수 있으면 요청을 채우고 true 를 반환합니다(시간 단계 뒤, 게임 스레드).
         * @details 나눌 수 있는 경우는 반복 클립 하나를 블렌드나 레이어, 시퀀서 덮어쓰기 없이 재생 중일 때다. 기본은 나눌 수 없음이다(IK 같은 후처리 작업은 유닛마다 다르다).
         */
        virtual bool describeSharedPose( AnimSharedPoseRequest& outRequest ) const
        {
            (void)outRequest;
            return false;
        }
        /** @brief 요청한 클립의 소유권입니다(묶음이 보관하게). 모르는 클립이면 nullptr 입니다. */
        virtual shared_ptr<const AnimClip> findSharedPoseClip( const AnimClip* pClip ) const
        {
            (void)pClip;
            return nullptr;
        }
        /** @brief 유닛이 사라지거나 작업이 해제됐을 때 부릅니다. 보관하던 유닛 포인터를 해제합니다. */
        virtual void onAnimationUnitDetached( SkeletalMeshComponent& unit ) { (void)unit; }
        /** @brief 진단 상태(그래프 상태, 알림, 커브, 루트 모션)를 채웁니다. 되감기 기록기와 패널이 읽습니다(게임 스레드, 평가 뒤). */
        virtual void collectDebugState( AnimationDebugState& inoutState ) const { (void)inoutState; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAnimationLODClient
     * @brief LOD 판정을 받는 대상(스켈레탈 유닛, 스프라이트 애니메이터)입니다. `AnimationSystem::registerLODClient` 로 등록합니다.
     * @details 리플렉션 컴포넌트는 기반 클래스 하나만 두므로 컴포넌트가 이 인터페이스를 구현한 작은 객체를 하나 가진다(`SkeletalAnimatorBinding` 과 같은 방식이다).
     *          판정은 평가 앞에 게임 스레드에서 한 번 하고, 결과는 `applyAnimationLOD` 로 받는다.
     */
    class SW_API IAnimationLODClient
    {
    public:
        IAnimationLODClient()                                        = default;
        virtual ~IAnimationLODClient()                               = default;
        IAnimationLODClient( const IAnimationLODClient& )            = delete;
        IAnimationLODClient& operator=( const IAnimationLODClient& ) = delete;

        /** @brief 월드 경계 구를 반환합니다. 없으면 false 이며, 이 경우 항상 보이고 가장 중요한 것으로 취급합니다. */
        virtual bool findAnimationLODBounds( float3& outCenter, float32& outRadius ) const = 0;
        /** @brief 본 LOD 테이블을 반환합니다(없으면 nullptr). */
        virtual const SkeletonBoneLOD* findBoneLOD() const { return nullptr; }
        /** @brief 이번 프레임의 판정을 받습니다(게임 스레드). */
        virtual void applyAnimationLOD( const AnimationLODState& state ) = 0;
        /**
         * @brief 포즈 없이 상태만 되감기에 남기는 클라이언트(스프라이트 애니메이터)이면 그 컴포넌트를 반환합니다. 기본은 nullptr 입니다. 스켈레탈 유닛은 포즈와 함께
         *        따로 기록합니다.
         */
        virtual const Component* findRewindTarget() const { return nullptr; }
        /** @brief 되감기에 남길 상태를 채웁니다(게임 스레드, 평가 뒤). */
        virtual void collectDebugState( AnimationDebugState& inoutState ) const { (void)inoutState; }
        /** @brief 되감는 동안 기록된 상태를 적용합니다(게임 스레드). */
        virtual void applyRewindState( const AnimationDebugState& state ) { (void)state; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationSystem
     * @brief 등록된 유닛을 의존(위 유닛 → 아래 유닛) 레벨로 나눠 단계마다 평가합니다. 언리얼의 애니메이션 평가, 유니티의 Playable 그래프 평가에 해당합니다.
     * @details 프레임 순서는 다음과 같다. (0) 의존이 바뀌었으면 레벨을 다시 만든다(위상 정렬이며, 고리는 오류로 알리고 남은 유닛은 마지막 레벨에 둔다). (1) 유닛마다 이번 프레임
     *          할 일과 LOD(갱신 주기, 화면 밖 여부)를 정한다. (2) 단계마다 레벨 순서로 `engine::runParallel` 을 돌리고, 시간 단계 뒤에는 동기 그룹을 맞춘다.
     *          (3) 게임 스레드에서 `finishAnimationFrame` 을 부른다. 쉬는 유닛(할 일 없음, 포즈 깨끗함)은 (1) 에서 빠지므로 비용이 없다.
     *          "상태를 가진 시스템 타입, `engine::runParallel`, 매니저 tick 의 단계 한 줄"이라는 구조를 따른다(`SceneTransformHierarchy` 와 같다).
     */
    class SW_API AnimationSystem
    {
    public:
        /** @brief 한 레벨의 유닛이 이 수 이상이면 단계를 워커에 나눕니다. 유닛 하나가 본 수십 개의 샘플과 블렌드라 수 마이크로초입니다. */
        static constexpr uint32 kParallelUnitCount = 8;

        AnimationSystem();
        ~AnimationSystem() = default;

        AnimationSystem( const AnimationSystem& )            = delete;
        AnimationSystem& operator=( const AnimationSystem& ) = delete;

        /** @brief 의존 핸들을 해석할 매니저를 지정합니다(매니저가 생성자에서 부릅니다). */
        void setObjectManager( GameObjectManager* pManager ) { _pManager = pManager; }
        /** @brief 유닛을 등록합니다(컴포넌트 등록 때 부릅니다). */
        void registerUnit( SkeletalMeshComponent* pUnit );
        /** @brief 유닛을 해제합니다. 여러 번 불러도 안전합니다. */
        void unregisterUnit( SkeletalMeshComponent* pUnit );
        /** @brief 의존이나 리더가 바뀌었음을 알립니다. 다음 평가가 레벨을 다시 만듭니다. */
        void markOrderDirty() { _bOrderDirty = SW_TRUE; }

        /** @brief 한 프레임을 평가합니다(게임 스레드, 틱 뒤). LOD 판정 다음에 단계들을 순서대로 처리합니다. */
        void evaluate( float32 deltaSeconds );

        // --- LOD (AnimationLOD.h) ---
        /** @brief LOD 판정을 받을 대상을 등록합니다(유닛, 스프라이트 애니메이터). */
        void registerLODClient( IAnimationLODClient* pClient );
        /** @brief 등록을 해제합니다. 여러 번 불러도 안전합니다. */
        void unregisterLODClient( IAnimationLODClient* pClient );
        /**
         * @brief 다음 평가가 쓸 뷰 목록입니다(주 시점과 추가 뷰). 엔진 루프가 프레임마다 넣습니다. 한 번도 넣지 않았으면 LOD 는 꺼져 있습니다
         *        (모든 클라이언트가 보이고 매 프레임 평가합니다. 테스트, 서버, 뷰가 없는 실행이 그렇습니다).
         */
        void setLODViews( const vector<AnimationLODView>& listView );
        /** @brief 뷰를 지워 LOD 를 끕니다. */
        void clearLODViews();
        /** @brief LOD 테이블을 정합니다(테스트, 게임). 정하지 않으면 처음 판정할 때 `AnimationLODSettings::kResourcePath` 를 읽습니다. */
        void                        setLODSettings( const AnimationLODSettings& settings );
        const AnimationLODSettings& getLODSettings() const { return _lodSettings; }
        /** @brief 지난 판정에서 포즈 하나를 만드는 데 든 평균 시간(마이크로초, 지수 이동 평균)입니다. 예산 배분이 씁니다. */
        float32 getAverageEvaluationMicroseconds() const { return _averageEvaluationMicroseconds; }
        /** @brief 평균 비용을 정합니다(테스트에서 측정 대신 정한 값으로 예산을 돌릴 때 씁니다). 0 이면 다시 측정합니다. */
        void setAverageEvaluationMicroseconds( float32 microseconds ) { _averageEvaluationMicroseconds = microseconds; }
        /** @brief 지난 판정에서 예산 배분 뒤의 예상 비용(마이크로초)입니다. */
        float32 getExpectedEvaluationMicroseconds() const { return _expectedEvaluationMicroseconds; }
        /** @brief 지난 평가에서 실제로 포즈를 만든 유닛 수입니다. */
        uint32 getPoseEvaluatedUnitCount() const { return _poseEvaluatedUnitCount; }

        // --- 군중 공유 (AnimationCrowd.h) ---
        /** @brief 군중(묶음, 사본 풀, VAT 캐시, 시계)을 반환합니다. */
        AnimationCrowd&       getCrowd() { return _crowd; }
        const AnimationCrowd& getCrowd() const { return _crowd; }
        /** @brief 군중 테이블을 정합니다(테스트, 게임). 정하지 않으면 처음 쓸 때 `AnimationCrowdSettings::kResourcePath` 를 읽습니다. */
        void setCrowdSettings( const AnimationCrowdSettings& settings );

        /** @brief 등록된 유닛 수입니다. */
        uint32 getUnitCount() const { return static_cast<uint32>( _listUnit.size() ); }
        /** @brief 등록된 유닛 전부입니다(평가 순서는 아닙니다). 레벨은 유닛이 빠지는 순간 비워지므로 "지금 있는 유닛"은 이 함수로 봅니다. */
        const vector<SkeletalMeshComponent*>& getUnits() const { return _listUnit; }
        /** @brief 의존 레벨입니다(0 이 먼저). 테스트와 진단용입니다. */
        const vector<vector<SkeletalMeshComponent*>>& getLevels() const { return _listLevel; }
        /** @brief 마지막으로 만든 레벨에 의존 고리가 있었으면 true 입니다. */
        bool hasDependencyCycle() const { return _bCycle == SW_TRUE; }
        /** @brief 평가한 프레임 수입니다. */
        uint64 getFrameIndex() const { return _frameIndex; }
        /** @brief 지난 프레임에 일한(쉬지 않은) 유닛 수를 반환합니다. */
        uint32 getActiveUnitCount() const { return _activeUnitCount; }
#if SW_ANIMATION_REWIND_ENABLED
        // --- 되감기 (AnimationRewind.h, Shipping 에는 없다) ---
        /** @brief 되감기 기록기입니다. 켜져 있으면 평가 뒤 일한 유닛을 기록하고, 되감는 동안에는 평가 대신 기록된 포즈를 적용합니다. */
        AnimationRewindRecorder&       getRewind() { return _rewind; }
        const AnimationRewindRecorder& getRewind() const { return _rewind; }
#endif
        /** @brief 거리 LOD 의 기준점(보통 카메라 월드 위치)을 정합니다. 카메라나 뷰를 가진 쪽이 프레임마다 넣습니다. */
        void setLODViewPosition( const float3& position );
        /** @brief 거리 LOD 기준점을 지웁니다(거리 LOD 가 꺼집니다). */
        void clearLODViewPosition() { _bHasLODViewPosition = SW_FALSE; }
        /** @brief 거리 LOD 기준점을 찾습니다. 정하지 않았으면 false 입니다. */
        bool findLODViewPosition( float3& outPosition ) const;

    private:
        /** @brief LOD 판정(가시성, 화면 크기, 주기, 본 LOD, 예산)을 하고 클라이언트에 알립니다. */
        void updateLOD();
        /** @brief 의존을 해석해 레벨을 다시 만듭니다. */
        void rebuildLevels();
        /** @brief 단계 하나를 레벨 순서로 처리합니다. */
        void runPhase( AnimationPhase phase );
        /** @brief 같은 이름의 동기 그룹끼리 위상을 맞춥니다. */
        void synchronizeGroups();
        /** @brief 군중 공유를 켠 유닛의 묶음, 사본, VAT 를 정하고 묶음을 평가합니다(시간 단계 뒤). */
        void updateCrowd();
#if SW_ANIMATION_REWIND_ENABLED
        /** @brief 되감는 동안 기록된 포즈를 유닛마다, 기록된 상태를 스프라이트 애니메이터마다 적용합니다(평가하지 않습니다). */
        void applyRewindScrub();
        /** @brief 이번 프레임을 기록합니다. 일한 유닛(포즈와 상태)과, 상태만 남기는 LOD 클라이언트를 기록합니다. */
        void recordRewindFrame();
#endif

        vector<SkeletalMeshComponent*>         _listUnit;
        vector<vector<SkeletalMeshComponent*>> _listLevel;
        vector<SkeletalMeshComponent*>         _listActive;     ///< 이번 프레임에 단계를 처리하는 유닛(레벨 순서)이다. 재사용한다
        vector<uint32>                         _listLevelStart; ///< `_listActive` 안의 레벨 시작 위치다. 끝 위치를 하나 더 둔다
        vector<IAnimationLODClient*>           _listLODClient;
        vector<AnimationLODView>               _listLODView;
        vector<AnimationLODState>              _listScratchLODState;   ///< 판정 중 클라이언트별 상태다(재사용)
        vector<AnimationBudgetItem>            _listScratchBudgetItem; ///< 예산 배분 입력(재사용)
        AnimationLODSettings                   _lodSettings;
        AnimationCrowd                         _crowd;
#if SW_ANIMATION_REWIND_ENABLED
        AnimationRewindRecorder _rewind;
        Pose                    _rewindScratchPose; ///< 되감기 포즈를 해석할 임시 포즈다(재사용)
        uint8                   _bRewindApplied;    ///< 지난 프레임에 되감기 포즈를 적용했다는 표시다. 되감기가 끝나면 다시 평가하게 한다
#endif
        GameObjectManager* _pManager;
        float3             _lodViewPosition;
        uint64             _frameIndex;
        float32            _deltaSeconds;
        float32            _averageEvaluationMicroseconds;
        float32            _expectedEvaluationMicroseconds;
        uint32             _activeUnitCount;
        uint32             _poseEvaluatedUnitCount;
        uint8              _bOrderDirty;
        uint8              _bCycle;
        uint8              _bLODViewsSet;        ///< 뷰를 한 번이라도 받았다(받지 않으면 LOD 꺼짐)
        uint8              _bLODSettingsReady;   ///< 테이블을 정했거나 읽었다
        uint8              _bLODApplied;         ///< 지난 프레임에 판정을 적용했다. 꺼질 때 한 번 되돌린다
        uint8              _bCrowdSettingsReady; ///< 군중 테이블을 정했거나 읽었다
        uint8              _bHasLODViewPosition; ///< 거리 LOD 기준점을 정했다
    };
} // namespace sw
