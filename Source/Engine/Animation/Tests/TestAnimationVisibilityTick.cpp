// AnimationVisibilityTickTest — 화면 밖 애니메이션 최적화(LOD)가 게임 판정을 바꾸지 않는지: 같은 입력 · 같은 시간에 보이는 유닛과 화면 밖 유닛의
// 루트 이동 · 알림 순서 · 판정 시점의 소켓 위치를 견준다(nogpu, 에셋 없음). 가시성은 가짜 뷰 판정(`AnimationLODState::makeState` 의 bVisible)으로만 바꾼다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

namespace AnimationVisibilityTickTestInternal
{
    /** @brief 판정 기록 한 줄 — 알림이 울린 프레임 · 이름과, 그 순간(디스패치 시점)의 칼끝 소켓 월드 위치입니다. */
    struct HitRecord
    {
        uint64        _frameIndex{ 0 };
        hashed_string _notify{};
        float3        _socketWorld{};
    };

    /**
     * @brief 애니메이터를 흉내 내는 단계 작업입니다 — 시간 단계에서 시각 · 알림 · 루트 모션을 내고, 기본 포즈 단계에서 팔을 시각의 함수로 돌립니다.
     * @details 실제 애니메이터와 같은 자리(알림 · 루트 모션 적용은 `finishAnimationFrame`, 모든 단계 뒤)에서 판정을 기록한다 — 판정이 읽는 소켓은 그때의 모델 공간이다.
     */
    class ScriptedSwingTask final : public IAnimationPhaseTask
    {
    public:
        static constexpr uint32 kTipBone = 2;

        bool                isAnimationActive() const override { return true; }
        AnimationPoseAccess getPoseAccess() const override { return AnimationPoseAccess::Writes; }
        void                runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& skeletalMesh, const AnimationFrameContext& context ) override
        {
            if ( phase == AnimationPhase::Time )
            {
                // 1 초 반복 클립: 0.1 FootL, 0.3 Impact, 0.6 FootR. 루트는 +Z 로 1.5 m/s(포즈와 상관없는 클립 데이터).
                const float32 previous = _time;
                _time += context._deltaSeconds;
                _rootDelta = 1.5f * context._deltaSeconds;
                _listPendingNotify.clear();
                const struct
                {
                    const utf8* _pName;
                    float32     _time;
                } arrEvent[] = {
                    { "FootL", 0.1f},
                    {"Impact", 0.3f},
                    { "FootR", 0.6f}
                };
                for ( const auto& event : arrEvent )
                {
                    const float32 loopStart = MathUtil::floor( previous );
                    for ( float32 loop = loopStart; loop <= _time; loop += 1.0f )
                    {
                        const float32 eventTime = loop + event._time;
                        if ( previous < eventTime && eventTime <= _time )
                            _listPendingNotify.push_back( hashed_string( event._pName ) );
                    }
                }
                _frameIndex = context._frameIndex;
            }
            else if ( phase == AnimationPhase::BasePose )
            {
                BoneTransform arm = skeletalMesh.getSkeleton().getBone( 1 )._referencePose;
                arm._rotation     = quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, 1.2f * MathUtil::sin( _time * 6.0f ) );
                skeletalMesh.getLocalPose().setBoneTransform( 1, arm );
            }
        }
        void finishAnimationFrame( SkeletalMeshComponent& skeletalMesh ) override
        {
            _rootTravel += _rootDelta;
            for ( const hashed_string& notify : _listPendingNotify )
            {
                const float3 tipModel = skeletalMesh.getModelSpaceTransforms()[kTipBone].getTranslation();
                _listRecord.push_back( HitRecord{
                    _frameIndex, notify, float3{ tipModel._x, tipModel._y, tipModel._z + _rootTravel }
                } );
            }
        }

        float32                  getRootTravel() const { return _rootTravel; }
        const vector<HitRecord>& getRecords() const { return _listRecord; }

    private:
        vector<hashed_string> _listPendingNotify;
        vector<HitRecord>     _listRecord;
        uint64                _frameIndex{ 0 };
        float32               _time{ 0.0f };
        float32               _rootDelta{ 0.0f };
        float32               _rootTravel{ 0.0f };
    };

    /** @brief 엔진 데이터(`animationlod.xml`)와 같은 모양의 표 — 가까우면 매 프레임, 멀수록 2 · 4 · 8 프레임, 화면 밖 판정 캐릭터는 1. */
    AnimationLODSettings makeSettings()
    {
        AnimationLODSettings settings;
        const struct
        {
            float32 _minScreenSize;
            uint32  _divisor;
            bool    _bInterpolate;
        } arrLevel[] = {
            {0.25f, 1, false},
            {0.10f, 2,  true},
            {0.04f, 4,  true},
            { 0.0f, 8, false}
        };
        for ( const auto& row : arrLevel )
        {
            AnimationLODRateLevel level;
            level._minScreenSize     = row._minScreenSize;
            level._updateRateDivisor = row._divisor;
            level._bInterpolate      = row._bInterpolate;
            settings._listRateLevel.push_back( level );
        }
        settings._offscreenUpdateRateDivisor = 1;
        return settings;
    }

    shared_ptr<Skeleton> makeArmSkeleton()
    {
        auto          skeleton = make_shared<Skeleton>();
        BoneTransform up{};
        up._translation = float3{ 0.0f, 1.0f, 0.0f };
        (void)skeleton->addBone( hashed_string( "root" ), -1, BoneTransform{} );
        (void)skeleton->addBone( hashed_string( "arm" ), 0, up );
        (void)skeleton->addBone( hashed_string( "SwordTip" ), 1, up );
        return skeleton;
    }

    /** @brief 유닛 하나를 프레임 하나 돌립니다(`AnimationSystem::evaluate` 가 유닛마다 하는 순서 — 시작 · 다섯 단계 · 끝). */
    void runFrame( SkeletalMeshComponent& mesh, float32 deltaSeconds, uint64 frameIndex )
    {
        if ( mesh.beginAnimationFrame( deltaSeconds, frameIndex ) == false )
            return;
        for ( uint32 phase = 0; phase < static_cast<uint32>( AnimationPhase::Count ); ++phase )
            mesh.runAnimationPhase( static_cast<AnimationPhase>( phase ) );
        mesh.finishAnimationFrame();
    }

    /**
     * @brief 보이는 유닛과 화면 밖 유닛을 같은 시간으로 돌리고 판정 기록을 견줍니다.
     * @return 소켓 위치가 모든 판정에서 같았는지입니다(루트 이동 · 알림 순서는 늘 같아야 하므로 안에서 기대한다).
     */
    bool runVisibleAgainstOffscreen( VisibilityBasedAnimationTickOption offscreenOption )
    {
        const AnimationLODSettings settings = makeSettings();
        const shared_ptr<Skeleton> skeleton = makeArmSkeleton();
        GameObjectManager          manager;
        GameObject*                pVisible       = manager.createGameObject( hashed_string( "Visible" ) );
        GameObject*                pOffscreen     = manager.createGameObject( hashed_string( "Offscreen" ) );
        SkeletalMeshComponent*     pVisibleMesh   = pVisible != nullptr ? pVisible->addComponent<SkeletalMeshComponent>() : nullptr;
        SkeletalMeshComponent*     pOffscreenMesh = pOffscreen != nullptr ? pOffscreen->addComponent<SkeletalMeshComponent>() : nullptr;
        // 도우미는 bool 을 돌려주므로 SW_REQUIRE_*(케이스 본문 전용)를 쓰지 않는다.
        SW_EXPECT_NOT_NULL( pVisibleMesh );
        SW_EXPECT_NOT_NULL( pOffscreenMesh );
        if ( pVisibleMesh == nullptr || pOffscreenMesh == nullptr )
            return false;
        pVisibleMesh->setSkeleton( skeleton );
        pOffscreenMesh->setSkeleton( skeleton );
        pOffscreenMesh->setVisibilityBasedTickOption( offscreenOption );
        ScriptedSwingTask  visibleTask;
        ScriptedSwingTask  offscreenTask;
        const Subscription visibleRegistration   = pVisibleMesh->registerAnimationPhaseTask( &visibleTask );
        const Subscription offscreenRegistration = pOffscreenMesh->registerAnimationPhaseTask( &offscreenTask );

        // 두 유닛이 같은 화면 크기에서 시작해 한 프레임 보인 뒤(두 포즈가 같다), 하나만 뷰 밖으로 나간다 — 판정은 그 뒤에 모두 일어난다.
        const AnimationLODState nearState      = AnimationLODState::makeState( settings, 0.5f, true, nullptr );
        const AnimationLODState offscreenState = AnimationLODState::makeState( settings, 0.0f, false, nullptr );
        const float32           deltaSeconds   = 1.0f / 60.0f;
        pVisibleMesh->applyAnimationLOD( nearState );
        pOffscreenMesh->applyAnimationLOD( nearState );
        runFrame( *pVisibleMesh, deltaSeconds, 1 );
        runFrame( *pOffscreenMesh, deltaSeconds, 1 );
        pOffscreenMesh->applyAnimationLOD( offscreenState );
        for ( uint64 frameIndex = 2; frameIndex <= 150; ++frameIndex )
        {
            runFrame( *pVisibleMesh, deltaSeconds, frameIndex );
            runFrame( *pOffscreenMesh, deltaSeconds, frameIndex );
        }

        // (a) 루트 모션 — 다가오는 거리 · 시간이 같다.
        SW_EXPECT_NEAR( visibleTask.getRootTravel(), offscreenTask.getRootTravel(), 1e-5f );
        // (c) 알림 — 같은 프레임에 같은 순서로, 빠짐 · 겹침 없이(2.5 초에 FootL · Impact · FootR 가 세 번씩 — 마지막 FootR 은 2.6 초라 두 번).
        const vector<HitRecord>& listVisible   = visibleTask.getRecords();
        const vector<HitRecord>& listOffscreen = offscreenTask.getRecords();
        SW_EXPECT_EQUAL( size_t( 8 ), listVisible.size() );
        SW_EXPECT_EQUAL( listVisible.size(), listOffscreen.size() );
        if ( listVisible.size() != listOffscreen.size() )
            return false;
        bool bSocketsMatch = true;
        for ( size_t recordIndex = 0; recordIndex < listVisible.size(); ++recordIndex )
        {
            SW_EXPECT_EQUAL( listVisible[recordIndex]._frameIndex, listOffscreen[recordIndex]._frameIndex );
            SW_EXPECT_TRUE( listVisible[recordIndex]._notify == listOffscreen[recordIndex]._notify );
            // (b) 판정 시점의 소켓 — 디스패치 순간의 칼끝 월드 위치.
            bSocketsMatch = bSocketsMatch && ( listVisible[recordIndex]._socketWorld - listOffscreen[recordIndex]._socketWorld ).getLength() < 1e-4f;
        }
        return bSocketsMatch;
    }
} // namespace AnimationVisibilityTickTestInternal

/** @brief [AnimationVisibilityTickTest] 화면 밖 상태는 표의 화면 밖 주기 · 가장 가까운 등급 · 모든 본 · 보간 없음이다 — 판정 캐릭터의 소켓이 끝 본 · 꺼진 노드로 틀리지 않는다 */
SW_TEST_CASE( AnimationVisibilityTickTest, OffscreenStateIsNearestGradeAtTheOffscreenRate )
{
    AnimationLODSettings settings        = AnimationVisibilityTickTestInternal::makeSettings();
    settings._offscreenUpdateRateDivisor = 3;
    const AnimationLODState state        = AnimationLODState::makeState( settings, 0.0f, false, nullptr );
    SW_EXPECT_FALSE( state._bVisible == SW_TRUE );
    SW_EXPECT_EQUAL( 3u, state._updateRateDivisor );
    SW_EXPECT_EQUAL( uint8( 0 ), state._lodGrade );
    SW_EXPECT_EQUAL( uint8( 0 ), state._rateLevel );
    SW_EXPECT_EQUAL( uint8( 0 ), state._boneLODLevel );
    SW_EXPECT_FALSE( state._bInterpolate == SW_TRUE );
    SW_EXPECT_TRUE( state.isNodeOn( 0 ) ); // max_lod 0 인 노드(가장 가까울 때만)도 화면 밖에서 켜진 채다 — 블렌드 스페이스가 접히지 않아 루트 모션 · 알림이 같다
}

/** @brief [AnimationVisibilityTickTest] 본을 갱신하는 화면 밖 유닛(AlwaysTickPoseAndRefreshBones)은 루트 이동 · 알림 순서 · 판정 시점 소켓이 보이는 유닛과 같다 */
SW_TEST_CASE( AnimationVisibilityTickTest, RefreshBonesOffscreenMatchesVisibleJudgment )
{
    SW_EXPECT_TRUE( AnimationVisibilityTickTestInternal::runVisibleAgainstOffscreen( VisibilityBasedAnimationTickOption::AlwaysTickPoseAndRefreshBones ) );
}

/**
 * @brief [AnimationVisibilityTickTest] 본을 갱신하지 않는 화면 밖 유닛(AlwaysTickPose)도 루트 이동 · 알림 순서는 같지만 소켓은 마지막으로 보이던 포즈에 굳는다
 * @details 그래서 소켓 · 본으로 판정하는 컴포넌트는 `requireBonesWhenNotRendered` 로 AlwaysTickPoseAndRefreshBones 를 요구한다(아래 케이스 · 처리기 쪽은 AnimationNotifyTableTest.OnlyHitWindowUsesBonesForHitDetection).
 */
SW_TEST_CASE( AnimationVisibilityTickTest, TickPoseOnlyOffscreenKeepsTimeButFreezesSockets )
{
    SW_EXPECT_FALSE( AnimationVisibilityTickTestInternal::runVisibleAgainstOffscreen( VisibilityBasedAnimationTickOption::AlwaysTickPose ) );
}

/** @brief [AnimationVisibilityTickTest] 본으로 판정하는 사용처는 화면 밖에서 본을 갱신하지 않는 유닛에 묶이면 로드 오류다 */
SW_TEST_CASE( AnimationVisibilityTickTest, HitDetectionRequiresBonesWhenNotRendered )
{
    SW_EXPECT_LOG( "SkeletalMeshComponent.HitDetectionNeedsBonesWhenNotRendered" );
    GameObjectManager manager;
    GameObject*       pActor = manager.createGameObject( hashed_string( "Enemy" ) );
    SW_REQUIRE_NOT_NULL( pActor );
    SkeletalMeshComponent* pMesh = pActor->addComponent<SkeletalMeshComponent>();
    SW_REQUIRE_NOT_NULL( pMesh );
    SW_EXPECT_TRUE( pMesh->getVisibilityBasedTickOption() == VisibilityBasedAnimationTickOption::AlwaysTickPose ); // 기본은 싼 쪽이다
    SW_EXPECT_FALSE( pMesh->requireBonesWhenNotRendered( "RagdollComponent" ) );
    pMesh->setVisibilityBasedTickOption( VisibilityBasedAnimationTickOption::AlwaysTickPoseAndRefreshBones );
    SW_EXPECT_TRUE( pMesh->requireBonesWhenNotRendered( "RagdollComponent" ) );
}
