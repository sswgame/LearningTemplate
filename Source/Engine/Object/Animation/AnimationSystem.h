/**
 * @file AnimationSystem.h
 * @brief 애니메이션 유닛(`SkeletalMeshComponent`)을 의존 순서 · 단계별로 평가하는 시스템입니다. `GameObjectManager` 가 소유하고 틱 뒤에 부릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Animation/AnimationCrowd.h"
#include "Engine/Object/Animation/AnimationLod.h"

namespace sw
{
    class AnimPlayer;
    class GameObjectManager;
    class SkeletalMeshComponent;
    class SkeletonBoneLod;

    /**
     * @enum AnimationPhase
     * @brief 한 프레임의 단계입니다. 단계마다 모든 유닛을 의존 순서(레벨)대로, 레벨 안에서는 병렬로 돕니다.
     */
    enum class AnimationPhase : uint8
    {
        Time = 0,    ///< 시간 · 상태 기계 · 알림 · 루트 모션 · 커브(포즈를 만들지 않는다 — LOD 로 건너뛴 유닛도 돈다)
        BasePose,    ///< 기본 포즈(클립 샘플 · 블렌드 · 레이어, 리더 포즈 따르기). 끝나면 모델 공간을 구한다
        Attachment,  ///< 부착(소켓 붙이기 — 위 유닛의 본을 읽는다)
        PostProcess, ///< 후처리 자리(IK · 제약 · 스프링 본 — PoseModifierComponent). 끝나면 모델 공간을 다시 구한다
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
        uint8   _bPoseNeeded{ SW_TRUE }; ///< 이번 프레임에 포즈를 만드는지(LOD · 화면 밖이면 거짓 — 시간만 흐른다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAnimationPhaseTask
     * @brief 유닛 하나의 단계에 끼어드는 일입니다(애니메이터 · 나중의 PoseModifier · 소켓 부착). 유닛에 등록합니다(`SkeletalMeshComponent::addAnimationPhaseTask`).
     * @details 단계 함수는 워커에서 불립니다 — 자기 유닛과, 의존으로 선언한 위 유닛만 읽습니다. 게임 스레드에서 해야 하는 일(트랜스폼 쓰기)은
     *          `finishAnimationFrame` 에 둡니다(모든 단계가 끝난 뒤 게임 스레드에서 차례로 불립니다).
     */
    class SW_API IAnimationPhaseTask
    {
    public:
        IAnimationPhaseTask()                                        = default;
        virtual ~IAnimationPhaseTask()                               = default;
        IAnimationPhaseTask( const IAnimationPhaseTask& )            = delete;
        IAnimationPhaseTask& operator=( const IAnimationPhaseTask& ) = delete;

        /** @brief 이번 프레임에 할 일이 있는지입니다. 모든 일이 거짓이고 포즈가 깨끗한 유닛은 쉬고 비용이 없습니다. */
        virtual bool isAnimationActive() const = 0;
        /** @brief 단계 하나를 합니다(워커). */
        virtual void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) = 0;
        /** @brief 동기 그룹에 들면 그룹 이름 · 가중치와 맞출 플레이어를 줍니다. 아니면 nullptr 입니다(시간 단계 뒤, 게임 스레드). */
        virtual AnimPlayer* findSyncPlayer( hashed_string& outGroupName, float32& outWeight )
        {
            (void)outGroupName;
            (void)outWeight;
            return nullptr;
        }
        /** @brief 모든 단계 뒤 게임 스레드에서 불립니다(루트 모션 적용 등). */
        virtual void finishAnimationFrame( SkeletalMeshComponent& unit ) { (void)unit; }
        /**
         * @brief 이번 프레임 포즈를 군중 묶음과 나눌 수 있으면 요청을 채우고 true 입니다(시간 단계 뒤, 게임 스레드).
         * @details 나눌 수 있는 것: 반복 클립 하나를 섞기 · 레이어 · 시퀀서 덮어쓰기 없이 재생 중. 기본은 나눌 수 없음입니다(IK 같은 후처리 일은 유닛마다다).
         */
        virtual bool describeSharedPose( AnimSharedPoseRequest& outRequest ) const
        {
            (void)outRequest;
            return false;
        }
        /** @brief 요청의 클립 소유입니다(묶음이 들고 있게). 모르는 클립이면 nullptr 입니다. */
        virtual shared_ptr<const AnimClip> findSharedPoseClip( const AnimClip* pClip ) const
        {
            (void)pClip;
            return nullptr;
        }
        /** @brief 유닛이 사라지거나 일을 뗐습니다. 들고 있던 유닛 포인터를 놓습니다. */
        virtual void onAnimationUnitDetached( SkeletalMeshComponent& unit ) { (void)unit; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAnimationLodClient
     * @brief LOD 판정을 받는 것(스켈레탈 유닛 · 스프라이트 애니메이터)입니다. `AnimationSystem::registerLodClient` 로 올립니다.
     * @details 리플렉션 컴포넌트는 기반 클래스 하나만 두므로 컴포넌트가 이것을 구현한 작은 객체를 하나 듭니다(`SkeletalAnimatorBinding` 과 같은 자리).
     *          판정은 게임 스레드에서 평가 앞에 한 번이고, 결과는 `applyAnimationLod` 로 받습니다.
     */
    class SW_API IAnimationLodClient
    {
    public:
        IAnimationLodClient()                                        = default;
        virtual ~IAnimationLodClient()                               = default;
        IAnimationLodClient( const IAnimationLodClient& )            = delete;
        IAnimationLodClient& operator=( const IAnimationLodClient& ) = delete;

        /** @brief 월드 경계 구입니다. 없으면 false — 늘 보이고 가장 중요한 것으로 봅니다. */
        virtual bool findAnimationLodBounds( float3& outCenter, float32& outRadius ) const = 0;
        /** @brief 본 LOD 표입니다(없으면 nullptr). */
        virtual const SkeletonBoneLod* findBoneLod() const { return nullptr; }
        /** @brief 이번 프레임의 판정을 받습니다(게임 스레드). */
        virtual void applyAnimationLod( const AnimationLodState& state ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationSystem
     * @brief 등록된 유닛을 의존(위 유닛 → 아래 유닛) 레벨로 나눠 단계마다 평가합니다. 언리얼 애니메이션 평가 · 유니티 Playable 그래프 평가의 자리입니다.
     * @details 프레임 순서: (0) 의존이 바뀌었으면 레벨을 다시 짓는다(위상 정렬, 고리는 오류로 알리고 남은 유닛은 마지막 레벨) → (1) 유닛마다 이번 프레임
     *          할 일과 LOD(갱신 주기 · 화면 밖)를 정한다 → (2) 단계마다 레벨 순서로 `engine::runParallel` → 시간 단계 뒤에는 동기 그룹을 맞춘다 →
     *          (3) 게임 스레드에서 `finishAnimationFrame`. 쉬는 유닛(할 일 없음 · 포즈 깨끗함)은 (1) 에서 빠져 비용이 없습니다.
     *          "상태를 가진 시스템 타입 + `engine::runParallel` + 매니저 tick 의 단계 한 줄" 모양입니다(`SceneTransformHierarchy` 와 같다).
     */
    class SW_API AnimationSystem
    {
    public:
        /** @brief 한 레벨의 유닛이 이 수 이상이면 단계를 워커에 나눕니다. 유닛 하나가 본 수십 개의 샘플 · 블렌드라 수 마이크로초입니다. */
        static constexpr uint32 kParallelUnitCount = 8;

        AnimationSystem();
        ~AnimationSystem() = default;

        AnimationSystem( const AnimationSystem& )            = delete;
        AnimationSystem& operator=( const AnimationSystem& ) = delete;

        /** @brief 의존 핸들을 풀 매니저를 겁니다(매니저가 생성자에서 부릅니다). */
        void setObjectManager( GameObjectManager* pManager ) { _pManager = pManager; }
        /** @brief 유닛을 올립니다(컴포넌트 등록). */
        void registerUnit( SkeletalMeshComponent* pUnit );
        /** @brief 유닛을 내립니다. 멱등입니다. */
        void unregisterUnit( SkeletalMeshComponent* pUnit );
        /** @brief 의존 · 리더가 바뀌었습니다. 다음 평가가 레벨을 다시 짓습니다. */
        void markOrderDirty() { _bOrderDirty = SW_TRUE; }

        /** @brief 한 프레임을 평가합니다(게임 스레드, 틱 뒤). LOD 판정 → 단계들 순서입니다. */
        void evaluate( float32 deltaSeconds );

        // --- LOD (AnimationLod.h) ---
        /** @brief LOD 판정을 받을 것을 올립니다(유닛 · 스프라이트 애니메이터). */
        void registerLodClient( IAnimationLodClient* pClient );
        /** @brief 내립니다. 멱등입니다. */
        void unregisterLodClient( IAnimationLodClient* pClient );
        /**
         * @brief 다음 평가가 쓸 뷰들입니다(주 시점 + 추가 뷰). 엔진 루프가 프레임마다 넣습니다. 한 번도 넣지 않았으면 LOD 는 꺼져 있습니다
         *        (모든 클라이언트가 보이고 매 프레임 — 시험 · 서버 · 뷰가 없는 실행).
         */
        void setLodViews( const vector<AnimationLodView>& listView );
        /** @brief 뷰를 지워 LOD 를 끕니다. */
        void clearLodViews();
        /** @brief LOD 표를 정합니다(시험 · 게임). 정하지 않으면 처음 판정 때 `AnimationLodSettings::kResourcePath` 를 읽습니다. */
        void                        setLodSettings( const AnimationLodSettings& settings );
        const AnimationLodSettings& getLodSettings() const { return _lodSettings; }
        /** @brief 지난 판정에서 포즈 하나를 만드는 데 든 평균 시간(마이크로초, 지수 이동 평균)입니다 — 예산 배분이 씁니다. */
        float32 getAverageEvaluationMicroseconds() const { return _averageEvaluationMicroseconds; }
        /** @brief 비용 평균을 정합니다(시험 — 측정 대신 정한 값으로 예산을 돌린다). 0 이면 다시 잽니다. */
        void setAverageEvaluationMicroseconds( float32 microseconds ) { _averageEvaluationMicroseconds = microseconds; }
        /** @brief 지난 판정에서 예산 배분 뒤의 예상 비용(마이크로초)입니다. */
        float32 getExpectedEvaluationMicroseconds() const { return _expectedEvaluationMicroseconds; }
        /** @brief 지난 평가에서 실제로 포즈를 만든 유닛 수입니다. */
        uint32 getPoseEvaluatedUnitCount() const { return _poseEvaluatedUnitCount; }

        // --- 군중 공유 (AnimationCrowd.h) ---
        /** @brief 군중(묶음 · 사본 풀 · VAT 캐시 · 시계)입니다. */
        AnimationCrowd&       getCrowd() { return _crowd; }
        const AnimationCrowd& getCrowd() const { return _crowd; }
        /** @brief 군중 표를 정합니다(시험 · 게임). 정하지 않으면 처음 쓸 때 `AnimationCrowdSettings::kResourcePath` 를 읽습니다. */
        void setCrowdSettings( const AnimationCrowdSettings& settings );

        /** @brief 등록된 유닛 수입니다. */
        uint32 getUnitCount() const { return static_cast<uint32>( _listUnit.size() ); }
        /** @brief 의존 레벨입니다(0 이 먼저). 시험 · 진단용입니다. */
        const vector<vector<SkeletalMeshComponent*>>& getLevels() const { return _listLevel; }
        /** @brief 마지막으로 지은 레벨에 의존 고리가 있었는지입니다. */
        bool hasDependencyCycle() const { return _bCycle == SW_TRUE; }
        /** @brief 평가한 프레임 수입니다. */
        uint64 getFrameIndex() const { return _frameIndex; }
        /** @brief 지난 프레임에 일한(쉬지 않은) 유닛 수입니다. */
        uint32 getActiveUnitCount() const { return _activeUnitCount; }

    private:
        /** @brief LOD 판정(가시성 · 화면 크기 · 주기 · 본 LOD · 예산)을 하고 클라이언트에 알립니다. */
        void updateLod();
        /** @brief 의존을 풀어 레벨을 다시 짓습니다. */
        void rebuildLevels();
        /** @brief 단계 하나를 레벨 순서로 돕니다. */
        void runPhase( AnimationPhase phase );
        /** @brief 같은 이름의 동기 그룹끼리 위상을 맞춥니다. */
        void synchronizeGroups();
        /** @brief 군중 공유를 켠 유닛의 묶음 · 사본 · VAT 를 정하고 묶음을 평가합니다(시간 단계 뒤). */
        void updateCrowd();

        vector<SkeletalMeshComponent*>         _listUnit;
        vector<vector<SkeletalMeshComponent*>> _listLevel;
        vector<SkeletalMeshComponent*>         _listActive;     ///< 이번 프레임 단계를 도는 유닛(레벨 순서). 재사용합니다
        vector<uint32>                         _listLevelStart; ///< `_listActive` 안의 레벨 시작 위치(끝 하나 더)
        vector<IAnimationLodClient*>           _listLodClient;
        vector<AnimationLodView>               _listLodView;
        vector<AnimationLodState>              _listScratchLodState;   ///< 판정 중 클라이언트마다의 상태(재사용)
        vector<AnimationBudgetItem>            _listScratchBudgetItem; ///< 예산 배분 입력(재사용)
        AnimationLodSettings                   _lodSettings;
        AnimationCrowd                         _crowd;
        GameObjectManager*                     _pManager;
        uint64                                 _frameIndex;
        float32                                _deltaSeconds;
        float32                                _averageEvaluationMicroseconds;
        float32                                _expectedEvaluationMicroseconds;
        uint32                                 _activeUnitCount;
        uint32                                 _poseEvaluatedUnitCount;
        uint8                                  _bOrderDirty;
        uint8                                  _bCycle;
        uint8                                  _bLodViewsSet;        ///< 뷰를 한 번이라도 받았다(받지 않으면 LOD 꺼짐)
        uint8                                  _bLodSettingsReady;   ///< 표를 정했거나 읽었다
        uint8                                  _bLodApplied;         ///< 지난 프레임에 판정을 넣었다(꺼질 때 한 번 되돌린다)
        uint8                                  _bCrowdSettingsReady; ///< 군중 표를 정했거나 읽었다
    };
} // namespace sw
