#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/YawUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Codec/Raw/RawAnimationCodec.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyComponent.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/PairedAnimation/PairedAnimationComponent.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSession.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSnapshot.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSystem.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Object/SceneAnimation/LocomotionWarpingComponent.h"
#include "Engine/Object/SceneAnimation/MotionWarpingComponent.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// PairedAnimationTest — 페어드 애니메이션 세션의 한 시계 · 정렬(snap · warp) · 공유 알림 · 끝 · 중단 · 시작 거절. 에셋 읽기는 TestPairedAnimationDef.cpp.

namespace
{
    struct TestPairedAnimationInternal
    {
        /** @brief 이진으로 정확한 프레임 길이입니다 — 시각 비교가 반올림에 흔들리지 않는다. */
        static constexpr float32 kStep = 0.125f;

        /**
         * @brief 사슬 스켈레톤(3 뼈) 클립 하나를 @p folder 에 씁니다. 루트는 +Z 로 1 m/s 입니다. 길이는 (표본 - 1) / 30 초입니다.
         * @param authoredSpeed 0 보다 크면 보폭 보정이 읽는 `Speed` 커브(m/s)를 답니다.
         */
        [[nodiscard]] static bool writeClip( const string& folder, const utf8* pName, uint32 sampleCount, bool bLoop, float32 authoredSpeed )
        {
            const Skeleton         skeleton = test::makeChainSkeleton( 3 );
            const AnimationRawClip raw      = test::makeChainRawClip( skeleton, sampleCount, 30.0f, 0.0f );
            AnimationClip          clip;
            clip.setName( hashed_string( pName ) );
            clip.setLooping( bLoop );
            clip.setRootMotionTrack( 0 );
            if ( authoredSpeed > 0.0f )
            {
                AnimationClipCurve curve{};
                curve._name    = hashed_string( "Speed" );
                curve._listKey = {
                    AnimationClipCurveKey{0.0f, authoredSpeed},
                    AnimationClipCurveKey{1.0f, authoredSpeed}
                };
                clip.addCurve( curve );
            }
            if ( clip.compressFrom( raw, RawAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) == false )
                return false;
            return clip.saveToFile( FileUtil::joinPath( folder, StringUtil::toLower( pName ) + string( AnimationClip::kExtension ) ) );
        }

        /** @brief 세션 테스트 클립 폴더입니다 — `Lead` · `Follow` (1 초), `Late` (0.5 초), 반복 `Idle` (1 초). @p leadSpeed 가 0 보다 크면 `Lead` 에 `Speed` 커브를 답니다. */
        static string writeClips( const utf8* pFolderName, float32 leadSpeed )
        {
            const string folder   = test::makeTempPath( pFolderName );
            const bool   bWritten = writeClip( folder, "Lead", 31, false, leadSpeed ) && writeClip( folder, "Follow", 31, false, 0.0f ) && writeClip( folder, "Late", 16, false, 0.0f ) &&
                                  writeClip( folder, "Idle", 31, true, 0.0f );
            return bWritten ? folder : string{};
        }

        /** @brief 앵커 `Leader` 와 `snap` 으로 맞추는 `Follower` 두 역할 바인딩입니다. */
        static vector<PairedAnimationBinding> makeBindings( SkeletalAnimatorComponent* pLeader, SkeletalAnimatorComponent* pFollower )
        {
            return {
                PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pLeader )},
                PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pFollower )},
            };
        }

        /** @brief 유닛 · 애니메이터를 단 참가자 오브젝트를 만들고 시작합니다(처음엔 `Idle` 반복). */
        static SkeletalAnimatorComponent* addParticipant( GameObjectManager& manager, const utf8* pName, const string& folder, bool bRootMotion )
        {
            GameObject*                pObject   = manager.createGameObject( hashed_string( pName ) );
            SkeletalMeshComponent*     pUnit     = pObject != nullptr ? pObject->addComponent<SkeletalMeshComponent>() : nullptr;
            SkeletalAnimatorComponent* pAnimator = pObject != nullptr ? pObject->addComponent<SkeletalAnimatorComponent>() : nullptr;
            if ( pUnit == nullptr || pAnimator == nullptr )
                return nullptr;
            pUnit->setSkeleton( make_shared<Skeleton>( test::makeChainSkeleton( 3 ) ) );
            pAnimator->setClipFolder( folder );
            pAnimator->setInitialState( "Idle" );
            pAnimator->setExtractRootMotion( bRootMotion );
            pAnimator->dispatchBeginPlay();
            return pAnimator;
        }

        /** @brief 같은 오브젝트에 알림 표(`Release` → `GameplayEvent`)를 단 알림 컴포넌트를 붙입니다. */
        static AnimationNotifyComponent* addReleaseNotify( SkeletalAnimatorComponent& animator )
        {
            AnimationNotifyComponent*        pNotify = animator.getOwner()->addComponent<AnimationNotifyComponent>();
            shared_ptr<AnimationNotifyTable> table   = make_shared<AnimationNotifyTable>();
            const bool                       bLoaded = table->loadFromXMLText( R"(<AnimationNotifies><Notify name="Release" handler="GameplayEvent" event="Fire"/></AnimationNotifies>)", "release",
                                                                               AnimationNotifyHandlerRegistry::getDefault() );
            if ( pNotify == nullptr || bLoaded == false )
                return nullptr;
            pNotify->setNotifyTable( table );
            pNotify->dispatchBeginPlay();
            return pNotify;
        }

        static shared_ptr<const PairedAnimationDef> makeDef( const utf8* pXML )
        {
            shared_ptr<PairedAnimationDef> def = make_shared<PairedAnimationDef>();
            return def->loadFromXMLText( pXML, "test.pairedanim.xml" ) ? def : nullptr;
        }

        /** @brief 세션 갱신(프레임 시스템 자리) → 애니메이션 평가 한 프레임입니다. `GameObjectManager::update` 의 그 두 단계와 같은 순서입니다. */
        static void step( GameObjectManager& manager, PairedAnimationSystem& system )
        {
            system.runBeforeTick( manager, kStep );
            manager.getSubsystem<AnimationSystem>().evaluate( kStep );
        }

        /** @brief 애니메이터의 현재 슬롯 시각입니다. */
        static float32 getCurrentTime( const SkeletalAnimatorComponent& animator ) { return animator.getGraphPlayer().getPlayer().getCurrentTime(); }

        /** @brief 이번 처리 기록에서 `Release` 알림 수입니다. */
        static uint32 countRelease( const AnimationNotifyComponent& notify )
        {
            uint32 releaseCount = 0;
            for ( const AnimationNotifyAction& action : notify.getActions() )
            {
                if ( action._notify == hashed_string( "Release" ) )
                    ++releaseCount;
            }
            return releaseCount;
        }

        /** @brief 두 역할(앵커 `Leader` · `snap` 으로 앞 1 m 에서 마주 보는 `Follower`), 0.3 초에 `Follower` 로 `Release` 알림. */
        static constexpr const utf8* kSnapXML = R"(<PairedAnimation duration="1">
            <Role name="Leader" anchor="true" required="true" clip="Lead" enterBlend="0" exitState="Idle" exitBlend="0.1"/>
            <Role name="Follower" required="true" clip="Follow" align="snap" offset="0 0 1" yaw="180" maxAlignDistance="10" enterBlend="0" exitState="Idle" exitBlend="0.1"/>
            <Role name="Prop" required="false" clip="Late" start="0.5" align="none" enterBlend="0" exitState="Idle" exitBlend="0"/>
            <Notify name="Release" time="0.3" role="Follower"/>
        </PairedAnimation>)";
    };
} // namespace

/**
 * @brief [PairedAnimationTest] 세션 — 시작에서 `snap` 역할이 앵커 앞 1 m 에서 마주 보고, 두 역할이 한 시계로 돌며, 공유 알림이 한 번 울리고, 끝에서 종료 상태로 간다
 * @details 앵커는 (1, 0, 2) 에서 요 90° — 오프셋 (0, 0, 1) 은 월드 +X 로 돌아 (2, 0, 2), 요는 90° + 180° = -90°.
 *          알림 0.3 초는 앵커 시각 0.25 를 읽은 갱신(예측 0.375)에서 울린다 — 그 프레임 평가가 0.3 을 지난다.
 */
SW_TEST_CASE( PairedAnimationTest, SessionSnapsAndPlaysOnOneClock )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedsnap", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, false );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr );
    AnimationNotifyComponent* pNotify = TestPairedAnimationInternal::addReleaseNotify( *pFollower );
    SW_ASSERT_NOT_NULL( pNotify );
    SceneComponent* pLeaderRoot   = pLeader->getOwner()->getPrimarySceneComponent();
    SceneComponent* pFollowerRoot = pFollower->getOwner()->getPrimarySceneComponent();
    SW_ASSERT_TRUE( pLeaderRoot != nullptr && pFollowerRoot != nullptr );
    pLeaderRoot->setWorldTransform( float4x4::makeTrs( float3{ 1.0f, 0.0f, 2.0f }, quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, MathUtil::kHalfPi ), float3{ 1.0f, 1.0f, 1.0f } ) );
    pFollowerRoot->setWorldPosition( float3{ 3.0f, 0.0f, 0.0f } );
    pLeader->setPlayRate( 0.5f ); // 세션 동안 1, 끝나면 되돌린다

    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );
    PairedAnimationSystem&               system      = PairedAnimationSystem::getOrCreate( manager );
    uint32                               sessionID   = 0;
    const vector<PairedAnimationBinding> listBinding = {
        PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pLeader )},
        PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pFollower )},
    };
    SW_ASSERT_TRUE( system.startSession( manager, def, listBinding, sessionID ) == PairedAnimationStartResult::Started );
    SW_EXPECT_TRUE( sessionID != 0 );
    SW_EXPECT_TRUE( system.isParticipant( TComponentHandle<SkeletalAnimatorComponent>( pFollower ) ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pLeader->getPlayRate(), 1e-6f );

    // 진입 정렬(snap).
    SW_EXPECT_NEAR_EQUAL( 2.0f, pFollowerRoot->getWorldPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pFollowerRoot->getWorldPosition()._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -MathUtil::kHalfPi, YawUtil::fromWorld( pFollowerRoot->getWorldMatrix() ), 1e-3f );

    // 한 시계 — 같은 프레임 · 같은 속도로 흘러 두 역할 시각이 같다. 알림은 셋째 갱신에서 한 번.
    for ( uint32 frameIndex = 0; frameIndex < 4; ++frameIndex )
    {
        system.runBeforeTick( manager, TestPairedAnimationInternal::kStep );
        SW_EXPECT_EQUAL( frameIndex == 2 ? 1u : 0u, TestPairedAnimationInternal::countRelease( *pNotify ) );
        manager.getSubsystem<AnimationSystem>().evaluate( TestPairedAnimationInternal::kStep );
        SW_EXPECT_NEAR_EQUAL( TestPairedAnimationInternal::getCurrentTime( *pLeader ), TestPairedAnimationInternal::getCurrentTime( *pFollower ), 1e-5f );
    }
    const PairedAnimationSession* pSession = system.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_TRUE( pSession->isPlaying() );
    SW_EXPECT_NEAR_EQUAL( 0.375f, pSession->getTime(), 1e-5f );
    SW_EXPECT_FALSE( pSession->isRolePlaying( hashed_string( "Prop" ) ) ); // 선택 역할은 묶지 않았다

    // 끝 — 앵커가 1 초에 닿은 다음 갱신에서 끝나고 두 역할 모두 종료 상태(`Idle`)로 간다.
    for ( uint32 frameIndex = 0; frameIndex < 4; ++frameIndex )
    {
        TestPairedAnimationInternal::step( manager, system );
    }
    system.runBeforeTick( manager, TestPairedAnimationInternal::kStep );
    pSession = system.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_TRUE( pSession->getState() == PairedAnimationSessionState::Finished );
    SW_EXPECT_TRUE( pLeader->getCurrentStateName() == hashed_string( "Idle" ) );
    SW_EXPECT_TRUE( pFollower->getCurrentStateName() == hashed_string( "Idle" ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pLeader->getPlayRate(), 1e-6f );
    SW_EXPECT_FALSE( system.isParticipant( TComponentHandle<SkeletalAnimatorComponent>( pFollower ) ) );
    // 끝난 세션은 다음 갱신 앞에서 빠진다.
    system.runBeforeTick( manager, TestPairedAnimationInternal::kStep );
    SW_EXPECT_NULL( system.findSession( sessionID ) );
}

/**
 * @brief [PairedAnimationTest] 세션 — `warp` 역할은 `MotionWarpingComponent` 창으로 루트 모션이 휘어 `warpEnd` 에 앵커 기준 목표(자리 · 요)에 닿는다
 * @details 앵커는 원점 · 요 0, 목표는 (0, 0, 2) · 요 180°. 역할은 (0.5, 0, 0) 에서 +Z 1 m/s 루트 모션 클립 — 창 끝 0.5 초(넷째 프레임)에 목표와 1 mm 안.
 */
SW_TEST_CASE( PairedAnimationTest, SessionWarpsRoleToAnchorOffset )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedwarp", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, true );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr );
    MotionWarpingComponent* pWarping = pFollower->getOwner()->addComponent<MotionWarpingComponent>();
    SW_ASSERT_NOT_NULL( pWarping );
    pWarping->dispatchBeginPlay();
    SceneComponent* pFollowerRoot = pFollower->getOwner()->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pFollowerRoot );
    pFollowerRoot->setWorldPosition( float3{ 0.5f, 0.0f, 0.0f } );

    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( R"(<PairedAnimation duration="1">
        <Role name="Leader" anchor="true" required="true" clip="Lead" enterBlend="0" exitState="Idle" exitBlend="0"/>
        <Role name="Follower" required="true" clip="Follow" align="warp" offset="0 0 2" yaw="180" warpEnd="0.5" maxAlignDistance="5" enterBlend="0" exitState="Idle" exitBlend="0"/>
    </PairedAnimation>)" );
    SW_ASSERT_NOT_NULL( def.get() );
    PairedAnimationSystem&               system      = PairedAnimationSystem::getOrCreate( manager );
    uint32                               sessionID   = 0;
    const vector<PairedAnimationBinding> listBinding = {
        PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pLeader )},
        PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pFollower )},
    };
    SW_ASSERT_TRUE( system.startSession( manager, def, listBinding, sessionID ) == PairedAnimationStartResult::Started );
    SW_EXPECT_EQUAL( 1u, pWarping->getOpenWindowCount() );

    for ( uint32 frameIndex = 0; frameIndex < 4; ++frameIndex )
    {
        TestPairedAnimationInternal::step( manager, system );
    }
    const float3 position = pFollowerRoot->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 0.0f, position._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, position._z, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::kPi, MathUtil::abs( YawUtil::fromWorld( pFollowerRoot->getWorldMatrix() ) ), 1e-3f );
    // 다음 갱신이 창을 닫는다(목표에 닿았으니 오류 없음).
    TestPairedAnimationInternal::step( manager, system );
    SW_EXPECT_EQUAL( 0u, pWarping->getOpenWindowCount() );
    SW_EXPECT_NULL( pWarping->findWarpTarget( hashed_string( "PairedAnimation" ) ) );
}

/**
 * @brief [PairedAnimationTest] 세션 — 필수 역할이 다른 것을 재생하면(게임플레이가 끊음) 다음 갱신에서 중단되고 남은 앵커는 종료 상태로 간다. 끊은 쪽의 재생은 덮지 않는다
 */
SW_TEST_CASE( PairedAnimationTest, SessionAbortsWhenRequiredRoleIsInterrupted )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedabort", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, false );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr );
    SW_ASSERT_NOT_NULL( TestPairedAnimationInternal::addReleaseNotify( *pFollower ) );
    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );
    PairedAnimationSystem&               system      = PairedAnimationSystem::getOrCreate( manager );
    uint32                               sessionID   = 0;
    const vector<PairedAnimationBinding> listBinding = {
        PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pLeader )},
        PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pFollower )},
    };
    SW_ASSERT_TRUE( system.startSession( manager, def, listBinding, sessionID ) == PairedAnimationStartResult::Started );
    TestPairedAnimationInternal::step( manager, system );
    TestPairedAnimationInternal::step( manager, system );

    SW_ASSERT_TRUE( pFollower->play( hashed_string( "Late" ), false, 0.0f ) ); // 세션 밖에서 끊는다
    system.runBeforeTick( manager, TestPairedAnimationInternal::kStep );
    const PairedAnimationSession* pSession = system.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_TRUE( pSession->getState() == PairedAnimationSessionState::Aborted );
    SW_EXPECT_TRUE( pLeader->getCurrentStateName() == hashed_string( "Idle" ) );
    SW_EXPECT_TRUE( pFollower->getCurrentStateName() == hashed_string( "Late" ) );
}

/**
 * @brief [PairedAnimationTest] 세션 — 늦게 들어오는 선택 역할은 앵커 시각이 `start` 를 지난 갱신에 들어와 그만큼 뒤에서 같은 속도로 흐르고, 끊겨도 세션은 이어 간다
 */
SW_TEST_CASE( PairedAnimationTest, LateOptionalRoleEntersAndLeavesWithoutAborting )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedlate", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, false );
    SkeletalAnimatorComponent* pProp     = TestPairedAnimationInternal::addParticipant( manager, "Prop", folder, false );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr && pProp != nullptr );
    SW_ASSERT_NOT_NULL( TestPairedAnimationInternal::addReleaseNotify( *pFollower ) );
    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );
    PairedAnimationSystem&               system      = PairedAnimationSystem::getOrCreate( manager );
    uint32                               sessionID   = 0;
    const vector<PairedAnimationBinding> listBinding = {
        PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pLeader )},
        PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pFollower )},
        PairedAnimationBinding{    hashed_string( "Prop" ),     TComponentHandle<SkeletalAnimatorComponent>( pProp )},
    };
    SW_ASSERT_TRUE( system.startSession( manager, def, listBinding, sessionID ) == PairedAnimationStartResult::Started );
    const PairedAnimationSession* pSession = system.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_FALSE( pSession->isRolePlaying( hashed_string( "Prop" ) ) );

    // 앵커 0.5 초를 읽은 갱신(다섯째)에서 들어온다 — 그 프레임 평가 뒤 프롭 0.125, 앵커 0.625.
    for ( uint32 frameIndex = 0; frameIndex < 5; ++frameIndex )
    {
        TestPairedAnimationInternal::step( manager, system );
    }
    SW_EXPECT_TRUE( pSession->isRolePlaying( hashed_string( "Prop" ) ) );
    SW_EXPECT_NEAR_EQUAL( 0.125f, TestPairedAnimationInternal::getCurrentTime( *pProp ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.625f, TestPairedAnimationInternal::getCurrentTime( *pLeader ), 1e-5f );

    // 선택 역할을 끊으면 그 역할만 빠진다.
    SW_ASSERT_TRUE( pProp->play( hashed_string( "Idle" ), true, 0.0f ) );
    TestPairedAnimationInternal::step( manager, system );
    SW_EXPECT_TRUE( pSession->isPlaying() );
    SW_EXPECT_FALSE( pSession->isRolePlaying( hashed_string( "Prop" ) ) );
    SW_EXPECT_EQUAL( 2u, pSession->getPlayingRoleCount() );
}

/**
 * @brief [PairedAnimationTest] 세션 시작 거절 — 빠진 필수 역할 · 모르는 역할 · 없는 클립 · 길이가 다른 앵커 클립 · 먼 정렬 · 다른 세션의 참가자. 거절은 아무것도 바꾸지 않는다
 */
SW_TEST_CASE( PairedAnimationTest, StartRefusesWithoutSideEffects )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedrefuse", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, false );
    SkeletalAnimatorComponent* pOther    = TestPairedAnimationInternal::addParticipant( manager, "Other", folder, false );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr && pOther != nullptr );
    SW_ASSERT_NOT_NULL( TestPairedAnimationInternal::addReleaseNotify( *pFollower ) );
    PairedAnimationSystem&                     system = PairedAnimationSystem::getOrCreate( manager );
    const shared_ptr<const PairedAnimationDef> def    = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );
    const PairedAnimationBinding leader{ hashed_string( "Leader" ), TComponentHandle<SkeletalAnimatorComponent>( pLeader ) };
    const PairedAnimationBinding follower{ hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pFollower ) };
    uint32                       sessionID = 0;

    SW_EXPECT_TRUE( system.startSession( manager, def, { leader }, sessionID ) == PairedAnimationStartResult::MissingRequiredRole );
    SW_EXPECT_TRUE( system.startSession( manager, def, {
                                                           leader, follower, PairedAnimationBinding{ hashed_string( "Ghost" ), TComponentHandle<SkeletalAnimatorComponent>( pOther ) }
    },
                                         sessionID ) == PairedAnimationStartResult::InvalidBinding );
    // 알림 표가 없는 오브젝트가 알림을 받는 역할이면 거절한다.
    SW_EXPECT_TRUE( system.startSession( manager, def, {
                                                           leader, PairedAnimationBinding{ hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pOther ) }
    },
                                         sessionID ) == PairedAnimationStartResult::NotifyTargetMissing );

    const shared_ptr<const PairedAnimationDef> missingClip = TestPairedAnimationInternal::makeDef(
        R"(<PairedAnimation duration="1"><Role name="Leader" anchor="true" required="true" clip="NoSuchClip" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)" );
    SW_ASSERT_NOT_NULL( missingClip.get() );
    SW_EXPECT_TRUE( system.startSession( manager, missingClip, { leader }, sessionID ) == PairedAnimationStartResult::ClipNotFound );

    const shared_ptr<const PairedAnimationDef> longTimeline = TestPairedAnimationInternal::makeDef(
        R"(<PairedAnimation duration="2"><Role name="Leader" anchor="true" required="true" clip="Lead" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)" );
    SW_ASSERT_NOT_NULL( longTimeline.get() );
    SW_EXPECT_TRUE( system.startSession( manager, longTimeline, { leader }, sessionID ) == PairedAnimationStartResult::ClipLengthMismatch );

    pFollower->getOwner()->getPrimarySceneComponent()->setWorldPosition( float3{ 50.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( system.startSession( manager, def, { leader, follower }, sessionID ) == PairedAnimationStartResult::TooFarToAlign );
    SW_EXPECT_EQUAL( 0u, sessionID );
    SW_EXPECT_EQUAL( 0u, system.getSessionCount() );
    // 거절은 재생을 바꾸지 않았다.
    SW_EXPECT_TRUE( pLeader->getCurrentStateName() == hashed_string( "Idle" ) );
    SW_EXPECT_TRUE( pFollower->getCurrentStateName() == hashed_string( "Idle" ) );

    // 다른 세션의 참가자는 묶을 수 없다.
    pFollower->getOwner()->getPrimarySceneComponent()->setWorldPosition( float3{ 0.0f, 0.0f, 0.0f } );
    SW_ASSERT_TRUE( system.startSession( manager, def, { leader, follower }, sessionID ) == PairedAnimationStartResult::Started );
    uint32 secondID = 0;
    SW_EXPECT_TRUE( system.startSession( manager, longTimeline, { leader }, secondID ) == PairedAnimationStartResult::ParticipantBusy );
    SW_EXPECT_EQUAL( 0u, secondID );
}

/**
 * @brief [PairedAnimationTest] 세션 — 같은 오브젝트의 보폭 보정은 역할에 있는 동안 재생 속도를 쓰지 않고(쥠), 세션이 끝나면 놓는다
 * @details 앵커 클립 `Lead` 에 `Speed` 2 m/s 커브가 있고 오브젝트는 서 있다 — 쥐지 않으면 보폭 보정이 속도를 0.6(최소)으로 써서 세션이 시각 어긋남으로 중단한다.
 */
SW_TEST_CASE( PairedAnimationTest, SessionHoldsLocomotionPlayRate )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedstride", 2.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, false );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr );
    SW_ASSERT_NOT_NULL( TestPairedAnimationInternal::addReleaseNotify( *pFollower ) );
    LocomotionWarpingComponent* pLocomotion = pLeader->getOwner()->addComponent<LocomotionWarpingComponent>();
    SW_ASSERT_NOT_NULL( pLocomotion );
    pLocomotion->dispatchBeginPlay();

    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );
    PairedAnimationSystem& system    = PairedAnimationSystem::getOrCreate( manager );
    uint32                 sessionID = 0;
    SW_ASSERT_TRUE( system.startSession( manager, def, TestPairedAnimationInternal::makeBindings( pLeader, pFollower ), sessionID ) == PairedAnimationStartResult::Started );
    SW_EXPECT_TRUE( pLocomotion->isPlayRateHeld() );

    for ( uint32 frameIndex = 0; frameIndex < 6; ++frameIndex )
    {
        TestPairedAnimationInternal::step( manager, system );
    }
    const PairedAnimationSession* pSession = system.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_TRUE( pSession->isPlaying() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pLeader->getPlayRate(), 1e-6f );

    for ( uint32 frameIndex = 0; frameIndex < 2; ++frameIndex )
    {
        TestPairedAnimationInternal::step( manager, system );
    }
    system.runBeforeTick( manager, TestPairedAnimationInternal::kStep );
    pSession = system.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_TRUE( pSession->getState() == PairedAnimationSessionState::Finished );
    SW_EXPECT_FALSE( pLocomotion->isPlayRateHeld() );
}

/**
 * @brief [PairedAnimationTest] 세트 고르기 — 역할이 앵커 앞에서 보이는 자리에 맞는 첫 세트로 시작한다. 맞는 세트가 없으면 `NoMatchingSet` 이고 아무것도 바꾸지 않는다
 * @details 앵커는 원점 · 앞 +Z. 앞 세트(방위 ±45°)는 앞 1 m, 뒤 세트(135° ~ -135°)는 뒤 1 m 로 맞춘다. 역할이 (0, 0, -2) 면 뒤 세트를 고른다.
 */
SW_TEST_CASE( PairedAnimationTest, MatchingSessionPicksSetByRelativePlacement )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedselect", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, false );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr );
    SceneComponent* pFollowerRoot = pFollower->getOwner()->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pFollowerRoot );

    const shared_ptr<const PairedAnimationDef> front = TestPairedAnimationInternal::makeDef( R"(<PairedAnimation duration="1">
        <Role name="Leader" anchor="true" required="true" clip="Lead" enterBlend="0" exitState="Idle" exitBlend="0"/>
        <Role name="Follower" required="true" clip="Follow" align="snap" offset="0 0 1" yaw="180" maxAlignDistance="10" enterBlend="0" exitState="Idle" exitBlend="0"/>
        <Select role="Follower" minAngle="-45" maxAngle="45" maxDistance="3"/>
    </PairedAnimation>)" );
    const shared_ptr<const PairedAnimationDef> back  = TestPairedAnimationInternal::makeDef( R"(<PairedAnimation duration="1">
        <Role name="Leader" anchor="true" required="true" clip="Lead" enterBlend="0" exitState="Idle" exitBlend="0"/>
        <Role name="Follower" required="true" clip="Follow" align="snap" offset="0 0 -1" maxAlignDistance="10" enterBlend="0" exitState="Idle" exitBlend="0"/>
        <Select role="Follower" minAngle="135" maxAngle="-135" maxDistance="3"/>
    </PairedAnimation>)" );
    SW_ASSERT_TRUE( front != nullptr && back != nullptr );
    const vector<shared_ptr<const PairedAnimationDef>> listSet   = { front, back };
    PairedAnimationSystem&                             system    = PairedAnimationSystem::getOrCreate( manager );
    uint32                                             sessionID = 0;
    uint32                                             setIndex  = 0;

    // 옆 5 m — 어느 세트에도 맞지 않는다.
    pFollowerRoot->setWorldPosition( float3{ 5.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( system.startMatchingSession( manager, listSet, TestPairedAnimationInternal::makeBindings( pLeader, pFollower ), sessionID, setIndex ) ==
                    PairedAnimationStartResult::NoMatchingSet );
    SW_EXPECT_EQUAL( invalid_index::kUint32, setIndex );
    SW_EXPECT_EQUAL( 0u, system.getSessionCount() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pFollowerRoot->getWorldPosition()._x, 1e-5f );

    // 뒤 2 m — 뒤 세트로 시작하고 뒤 1 m 로 맞춘다.
    pFollowerRoot->setWorldPosition( float3{ 0.0f, 0.0f, -2.0f } );
    SW_ASSERT_TRUE( system.startMatchingSession( manager, listSet, TestPairedAnimationInternal::makeBindings( pLeader, pFollower ), sessionID, setIndex ) ==
                    PairedAnimationStartResult::Started );
    SW_EXPECT_EQUAL( 1u, setIndex );
    const PairedAnimationSession* pSession = system.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_TRUE( &pSession->getDef() == back.get() );
    SW_EXPECT_NEAR_EQUAL( -1.0f, pFollowerRoot->getWorldPosition()._z, 1e-4f );
}

/**
 * @brief [PairedAnimationTest] 에셋 캐시 — 파일을 고쳐 캐시가 다시 읽으면 컴포넌트의 다음 시작이 새 에셋을 쓰고, 돌고 있던 세션은 옛 에셋을 끝까지 쥔다
 */
SW_TEST_CASE( PairedAnimationTest, ComponentStartsWithEditedAssetAfterCacheReload )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedreload", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );
    const string path = FileUtil::joinPath( test::makeTempPath( "pairedreloadasset" ), "edited.pairedanim.xml" );
    FileUtil::ensureParentDirectoryExists( path );
    SW_ASSERT_TRUE( FileUtil::writeTextFile(
        path, R"(<PairedAnimation duration="1"><Role name="Leader" anchor="true" required="true" clip="Lead" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)" ) );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SW_ASSERT_NOT_NULL( pLeader );
    PairedAnimationComponent* pComponent = pLeader->getOwner()->addComponent<PairedAnimationComponent>();
    SW_ASSERT_NOT_NULL( pComponent );
    pComponent->setRole( "Leader" );
    pComponent->setPairedAnimationPath( path );
    pComponent->dispatchBeginPlay();

    SW_ASSERT_TRUE( pComponent->startSession( {} ) == PairedAnimationStartResult::Started );
    PairedAnimationSystem* pSystem = PairedAnimationSystem::find( manager );
    SW_ASSERT_NOT_NULL( pSystem );
    const PairedAnimationSession* pFirst = pSystem->findSession( pComponent->getSessionID() );
    SW_ASSERT_NOT_NULL( pFirst );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), pFirst->getDef().getRoles().size() );

    // 선택 역할을 더한 파일 — 캐시가 다시 읽는다(에디터 핫 리로드가 부르는 그 자리).
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, R"(<PairedAnimation duration="1">
        <Role name="Leader" anchor="true" required="true" clip="Lead" enterBlend="0" exitState="Idle" exitBlend="0"/>
        <Role name="Prop" required="false" clip="Late" align="none" enterBlend="0" exitState="Idle" exitBlend="0"/>
    </PairedAnimation>)" ) );
    PairedAnimationDefCache cache;
    SW_EXPECT_TRUE( cache.isCached( path ) );
    const uint32 reloadCount = PairedAnimationDefCache::getReloadCount();
    cache.reload( path, nullptr );
    SW_EXPECT_EQUAL( reloadCount + 1, PairedAnimationDefCache::getReloadCount() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), pFirst->getDef().getRoles().size() ); // 돌던 세션은 옛 에셋

    pComponent->abortSession();
    pSystem->runBeforeTick( manager, TestPairedAnimationInternal::kStep );
    SW_ASSERT_TRUE( pComponent->startSession( {} ) == PairedAnimationStartResult::Started );
    const PairedAnimationSession* pSecond = pSystem->findSession( pComponent->getSessionID() );
    SW_ASSERT_NOT_NULL( pSecond );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), pSecond->getDef().getRoles().size() );
    cache.clear(); // 임시 경로가 회차마다 같다 — `--test_repeat` 다음 회차가 고친 에셋을 읽지 않게
}

/**
 * @brief [PairedAnimationTest] 스냅샷 — 이상적인 상태(`makeAtElapsedTime`)가 시각에 맞게 역할 단계와 알림 커서를 정하고, 바이트로 썼다 읽어도 같으며, 깨진 바이트는 거절한다
 * @details 역할은 순서대로 Leader(앵커) · Follower · Prop(0.5 초에 들어옴), 알림 `Release` 는 0.3 초다.
 */
SW_TEST_CASE( PairedAnimationTest, SnapshotMakesElapsedStateAndSurvivesBytes )
{
    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );

    const PairedAnimationSessionSnapshot early = PairedAnimationSessionSnapshot::makeAtElapsedTime( *def, 7, 0.25f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), early._listRole.size() );
    SW_EXPECT_EQUAL( 7u, early._sessionID );
    SW_EXPECT_NEAR_EQUAL( 0.25f, early._anchorTime, 1e-6f );
    SW_EXPECT_EQUAL( 0u, early._nextNotifyIndex ); // 0.3 초 알림은 아직 안 지났다
    SW_EXPECT_TRUE( early._listRole[0]._stage == PairedAnimationRoleStage::Playing );
    SW_EXPECT_TRUE( early._listRole[1]._stage == PairedAnimationRoleStage::Playing );
    SW_EXPECT_TRUE( early._listRole[2]._stage == PairedAnimationRoleStage::Waiting );

    const PairedAnimationSessionSnapshot late = PairedAnimationSessionSnapshot::makeAtElapsedTime( *def, 7, 0.625f );
    SW_EXPECT_EQUAL( 1u, late._nextNotifyIndex ); // 지난 알림은 넘긴다
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), late._listRole.size() );
    SW_EXPECT_TRUE( late._listRole[2]._stage == PairedAnimationRoleStage::Playing );
    SW_EXPECT_NEAR_EQUAL( 0.5f, late._listRole[2]._entryAnchorTime, 1e-6f );

    vector<uint8> bytes;
    late.writeBytes( bytes );
    PairedAnimationSessionSnapshot parsed{};
    SW_ASSERT_TRUE( PairedAnimationSessionSnapshot::readBytes( bytes, parsed ) == PairedAnimationSessionSnapshot::ReadResult::Success );
    SW_EXPECT_EQUAL( late._sessionID, parsed._sessionID );
    SW_EXPECT_NEAR_EQUAL( late._anchorTime, parsed._anchorTime, 1e-7f );
    SW_EXPECT_EQUAL( late._nextNotifyIndex, parsed._nextNotifyIndex );
    SW_ASSERT_EQUAL( late._listRole.size(), parsed._listRole.size() );
    for ( size_t roleIndex = 0; roleIndex < late._listRole.size(); ++roleIndex )
    {
        SW_EXPECT_TRUE( late._listRole[roleIndex]._stage == parsed._listRole[roleIndex]._stage );
        SW_EXPECT_NEAR_EQUAL( late._listRole[roleIndex]._entryAnchorTime, parsed._listRole[roleIndex]._entryAnchorTime, 1e-7f );
    }

    // 깨진 바이트 — 매직이 다르거나, 잘렸거나, 역할 수가 남은 바이트보다 크다. 거절하면 출력은 비어 있다.
    vector<uint8> wrongMagic = bytes;
    wrongMagic[0]            = static_cast<uint8>( wrongMagic[0] ^ 0xFFu );
    SW_EXPECT_TRUE( PairedAnimationSessionSnapshot::readBytes( wrongMagic, parsed ) == PairedAnimationSessionSnapshot::ReadResult::WrongMagic );
    SW_EXPECT_TRUE( parsed._listRole.empty() );
    const vector<uint8> truncated( bytes.begin(), bytes.begin() + 8 );
    SW_EXPECT_TRUE( PairedAnimationSessionSnapshot::readBytes( truncated, parsed ) == PairedAnimationSessionSnapshot::ReadResult::Truncated );
    // 머리(매직 · id · 앵커 시각 · 알림 커서 · 역할 수 = 11 바이트)만 남으면 역할 수 3 이 남은 비트보다 크다.
    const vector<uint8> headerOnly( bytes.begin(), bytes.begin() + 11 );
    SW_EXPECT_TRUE( PairedAnimationSessionSnapshot::readBytes( headerOnly, parsed ) == PairedAnimationSessionSnapshot::ReadResult::Truncated );
    SW_EXPECT_TRUE( parsed._listRole.empty() );
    SW_EXPECT_TRUE( PairedAnimationSessionSnapshot::readBytes( {}, parsed ) == PairedAnimationSessionSnapshot::ReadResult::WrongMagic );
}

/**
 * @brief [PairedAnimationTest] 복원 — 시작하고 0.625 초가 지난 상태로 이어 받으면 두 역할과 늦은 역할이 그 시각에서 이어 돌고, 지난 알림은 다시 울리지 않으며, 세션이 끝까지 간다
 * @details 앵커 0.625 초, Prop 은 0.5 초에 들어왔으므로 0.125 초. 복원은 진입 블렌드 없이 시각을 옮기고, 이어 받은 시스템의 다음 세션 id 는 복원한 id 를 넘는다.
 */
SW_TEST_CASE( PairedAnimationTest, RestoreContinuesFromElapsedState )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedrestore", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, false );
    SkeletalAnimatorComponent* pProp     = TestPairedAnimationInternal::addParticipant( manager, "Prop", folder, false );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr && pProp != nullptr );
    AnimationNotifyComponent* pNotify = TestPairedAnimationInternal::addReleaseNotify( *pFollower );
    SW_ASSERT_NOT_NULL( pNotify );
    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );
    PairedAnimationSystem&               system      = PairedAnimationSystem::getOrCreate( manager );
    const vector<PairedAnimationBinding> listBinding = {
        PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pLeader )},
        PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pFollower )},
        PairedAnimationBinding{    hashed_string( "Prop" ),     TComponentHandle<SkeletalAnimatorComponent>( pProp )},
    };

    const PairedAnimationSessionSnapshot snapshot = PairedAnimationSessionSnapshot::makeAtElapsedTime( *def, 7, 0.625f );
    SW_ASSERT_TRUE( system.restoreSession( manager, def, listBinding, snapshot ) == PairedAnimationStartResult::Started );
    const PairedAnimationSession* pSession = system.findSession( 7 );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_EQUAL( 3u, pSession->getPlayingRoleCount() );
    SW_EXPECT_NEAR_EQUAL( 0.625f, TestPairedAnimationInternal::getCurrentTime( *pLeader ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.625f, TestPairedAnimationInternal::getCurrentTime( *pFollower ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.125f, TestPairedAnimationInternal::getCurrentTime( *pProp ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pLeader->getPlayRate(), 1e-6f );

    // 한 시계로 끝까지 — 어긋남으로 중단하지 않고, 0.3 초 알림은 울리지 않는다.
    for ( uint32 frameIndex = 0; frameIndex < 3; ++frameIndex )
    {
        TestPairedAnimationInternal::step( manager, system );
        SW_EXPECT_TRUE( pSession->isPlaying() );
    }
    SW_EXPECT_NEAR_EQUAL( TestPairedAnimationInternal::getCurrentTime( *pLeader ), TestPairedAnimationInternal::getCurrentTime( *pFollower ), 1e-5f );
    system.runBeforeTick( manager, TestPairedAnimationInternal::kStep );
    SW_EXPECT_TRUE( pSession->getState() == PairedAnimationSessionState::Finished );
    SW_EXPECT_EQUAL( 0u, TestPairedAnimationInternal::countRelease( *pNotify ) );

    // 이어 받은 id 보다 새 세션의 id 가 크다.
    SkeletalAnimatorComponent* pLeader2   = TestPairedAnimationInternal::addParticipant( manager, "Leader2", folder, false );
    SkeletalAnimatorComponent* pFollower2 = TestPairedAnimationInternal::addParticipant( manager, "Follower2", folder, false );
    SW_ASSERT_TRUE( pLeader2 != nullptr && pFollower2 != nullptr );
    SW_ASSERT_NOT_NULL( TestPairedAnimationInternal::addReleaseNotify( *pFollower2 ) );
    uint32 newID = 0;
    SW_ASSERT_TRUE( system.startSession( manager, def, TestPairedAnimationInternal::makeBindings( pLeader2, pFollower2 ), newID ) == PairedAnimationStartResult::Started );
    SW_EXPECT_TRUE( newID > 7u );
}

/**
 * @brief [PairedAnimationTest] 복원 — 이른 시각(0.25 초)에서 이어 받으면 아직 안 지난 알림은 울리고 늦은 역할은 기다리다 `start` 에 들어온다. 이미 클립이 끝난 역할은 종료 상태로 간다
 */
SW_TEST_CASE( PairedAnimationTest, RestoreKeepsPendingNotifyAndExitsFinishedRole )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedrestoreearly", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, false );
    SkeletalAnimatorComponent* pProp     = TestPairedAnimationInternal::addParticipant( manager, "Prop", folder, false );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr && pProp != nullptr );
    AnimationNotifyComponent* pNotify = TestPairedAnimationInternal::addReleaseNotify( *pFollower );
    SW_ASSERT_NOT_NULL( pNotify );
    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );
    PairedAnimationSystem&               system      = PairedAnimationSystem::getOrCreate( manager );
    const vector<PairedAnimationBinding> listBinding = {
        PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pLeader )},
        PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pFollower )},
        PairedAnimationBinding{    hashed_string( "Prop" ),     TComponentHandle<SkeletalAnimatorComponent>( pProp )},
    };
    SW_ASSERT_TRUE( system.restoreSession( manager, def, listBinding, PairedAnimationSessionSnapshot::makeAtElapsedTime( *def, 3, 0.25f ) ) == PairedAnimationStartResult::Started );
    const PairedAnimationSession* pSession = system.findSession( 3 );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_FALSE( pSession->isRolePlaying( hashed_string( "Prop" ) ) ); // 기다린다
    SW_EXPECT_TRUE( pProp->getCurrentStateName() == hashed_string( "Idle" ) );

    // 읽은 앵커 0.25 초의 갱신(예측 0.375)이 0.3 초 알림을 낸다. 앵커 0.5 초를 읽는 갱신에서 Prop 이 들어온다.
    system.runBeforeTick( manager, TestPairedAnimationInternal::kStep );
    SW_EXPECT_EQUAL( 1u, TestPairedAnimationInternal::countRelease( *pNotify ) );
    manager.getSubsystem<AnimationSystem>().evaluate( TestPairedAnimationInternal::kStep );
    for ( uint32 frameIndex = 0; frameIndex < 3; ++frameIndex )
    {
        TestPairedAnimationInternal::step( manager, system );
    }
    SW_EXPECT_TRUE( pSession->isRolePlaying( hashed_string( "Prop" ) ) );
    SW_EXPECT_TRUE( pSession->isPlaying() );
    SW_EXPECT_EQUAL( 1u, TestPairedAnimationInternal::countRelease( *pNotify ) );

    // 클립이 이미 끝난 역할 — Prop 이 0.2 초에 들어와 0.5 초 클립을 도는데 0.8 초에서 이어 받는다.
    const shared_ptr<const PairedAnimationDef> finishedDef = TestPairedAnimationInternal::makeDef( R"(<PairedAnimation duration="1">
        <Role name="Leader" anchor="true" required="true" clip="Lead" enterBlend="0" exitState="Idle" exitBlend="0"/>
        <Role name="Prop" required="false" clip="Late" start="0.2" align="none" enterBlend="0" exitState="Idle" exitBlend="0"/>
    </PairedAnimation>)" );
    SW_ASSERT_NOT_NULL( finishedDef.get() );
    SkeletalAnimatorComponent* pLeader2 = TestPairedAnimationInternal::addParticipant( manager, "Leader2", folder, false );
    SkeletalAnimatorComponent* pProp2   = TestPairedAnimationInternal::addParticipant( manager, "Prop2", folder, false );
    SW_ASSERT_TRUE( pLeader2 != nullptr && pProp2 != nullptr );
    SW_ASSERT_TRUE( pProp2->play( hashed_string( "Late" ), false, 0.0f ) ); // 종료 상태(Idle)로 가는지 보려고 다른 것을 재생해 둔다
    const vector<PairedAnimationBinding> listFinished = {
        PairedAnimationBinding{hashed_string( "Leader" ), TComponentHandle<SkeletalAnimatorComponent>( pLeader2 )},
        PairedAnimationBinding{  hashed_string( "Prop" ),   TComponentHandle<SkeletalAnimatorComponent>( pProp2 )},
    };
    SW_ASSERT_TRUE( system.restoreSession( manager, finishedDef, listFinished, PairedAnimationSessionSnapshot::makeAtElapsedTime( *finishedDef, 4, 0.8f ) ) ==
                    PairedAnimationStartResult::Started );
    const PairedAnimationSession* pFinished = system.findSession( 4 );
    SW_ASSERT_NOT_NULL( pFinished );
    SW_EXPECT_FALSE( pFinished->isRolePlaying( hashed_string( "Prop" ) ) );
    SW_EXPECT_EQUAL( 1u, pFinished->getPlayingRoleCount() );
    SW_EXPECT_TRUE( pProp2->getCurrentStateName() == hashed_string( "Idle" ) );
    SW_EXPECT_NEAR_EQUAL( 0.8f, TestPairedAnimationInternal::getCurrentTime( *pLeader2 ), 1e-6f );
}

/**
 * @brief [PairedAnimationTest] 복원 거절 — 에셋과 맞지 않는 스냅샷(역할 수 · 시각 범위 · 앵커 단계 · 세션 id 0 · 묶인 역할이 Unbound)과 재생 중인 같은 id 는 아무것도 바꾸지 않고 거절한다
 */
SW_TEST_CASE( PairedAnimationTest, RestoreRefusesMismatchedSnapshotWithoutSideEffects )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedrestorerefuse", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationInternal::addParticipant( manager, "Leader", folder, false );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationInternal::addParticipant( manager, "Follower", folder, false );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr );
    SW_ASSERT_NOT_NULL( TestPairedAnimationInternal::addReleaseNotify( *pFollower ) );
    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );
    PairedAnimationSystem&               system      = PairedAnimationSystem::getOrCreate( manager );
    const vector<PairedAnimationBinding> listBinding = TestPairedAnimationInternal::makeBindings( pLeader, pFollower );
    const PairedAnimationSessionSnapshot good        = PairedAnimationSessionSnapshot::makeAtElapsedTime( *def, 5, 0.5f );

    PairedAnimationSessionSnapshot wrongRoleCount = good;
    wrongRoleCount._listRole.pop_back();
    SW_EXPECT_TRUE( system.restoreSession( manager, def, listBinding, wrongRoleCount ) == PairedAnimationStartResult::InvalidSnapshot );
    PairedAnimationSessionSnapshot pastEnd = good;
    pastEnd._anchorTime                    = 1.0f;
    SW_EXPECT_TRUE( system.restoreSession( manager, def, listBinding, pastEnd ) == PairedAnimationStartResult::InvalidSnapshot );
    PairedAnimationSessionSnapshot anchorWaiting = good;
    anchorWaiting._listRole[0]._stage            = PairedAnimationRoleStage::Waiting;
    SW_EXPECT_TRUE( system.restoreSession( manager, def, listBinding, anchorWaiting ) == PairedAnimationStartResult::InvalidSnapshot );
    PairedAnimationSessionSnapshot noID = good;
    noID._sessionID                     = 0;
    SW_EXPECT_TRUE( system.restoreSession( manager, def, listBinding, noID ) == PairedAnimationStartResult::InvalidSnapshot );
    PairedAnimationSessionSnapshot boundButUnbound = good;
    boundButUnbound._listRole[1]._stage            = PairedAnimationRoleStage::Unbound;
    SW_EXPECT_TRUE( system.restoreSession( manager, def, listBinding, boundButUnbound ) == PairedAnimationStartResult::InvalidSnapshot );
    PairedAnimationSessionSnapshot badEntry = good;
    badEntry._listRole[1]._entryAnchorTime  = 0.75f; // 앵커 시각(0.5)보다 뒤
    SW_EXPECT_TRUE( system.restoreSession( manager, def, listBinding, badEntry ) == PairedAnimationStartResult::InvalidSnapshot );

    // 거절은 재생을 바꾸지 않았고 세션을 남기지 않았다.
    SW_EXPECT_EQUAL( 0u, system.getSessionCount() );
    SW_EXPECT_TRUE( pLeader->getCurrentStateName() == hashed_string( "Idle" ) );
    SW_EXPECT_TRUE( pFollower->getCurrentStateName() == hashed_string( "Idle" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pLeader->getPlayRate(), 1e-6f );

    // 맞는 스냅샷은 들어가고, 같은 id 는 재생 중에 다시 받지 않는다.
    SW_ASSERT_TRUE( system.restoreSession( manager, def, listBinding, good ) == PairedAnimationStartResult::Started );
    SkeletalAnimatorComponent* pOther = TestPairedAnimationInternal::addParticipant( manager, "Other", folder, false );
    SW_ASSERT_NOT_NULL( pOther );
    SW_EXPECT_TRUE( system.restoreSession( manager, def, {
                                                             PairedAnimationBinding{ hashed_string( "Leader" ), TComponentHandle<SkeletalAnimatorComponent>( pOther ) }
    },
                                           good ) == PairedAnimationStartResult::SessionIDInUse );
    SW_EXPECT_EQUAL( 1u, system.getSessionCount() );
}

/**
 * @brief [PairedAnimationTest] 스냅샷 복사 — 돌고 있는 세션의 상태를 복사해 바이트를 거쳐 다른 씬에 복원하면, 한 프레임 뒤 두 씬의 앵커 · 늦은 역할 시각이 같고 지난 알림은 다시 울리지 않는다
 * @details 첫 씬은 다섯 번 갱신한다(앵커 0.5 초를 읽은 갱신에서 Prop 이 들어오고 알림은 이미 울렸다). 복사한 상태는 그 갱신 때의 값(앵커 0.5, Prop 진입 0.5)이다.
 */
SW_TEST_CASE( PairedAnimationTest, CapturedSnapshotRestoresIntoAnotherSceneInStep )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const string folder = TestPairedAnimationInternal::writeClips( "pairedcapture", 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager          serverManager;
    SkeletalAnimatorComponent* pServerLeader   = TestPairedAnimationInternal::addParticipant( serverManager, "Leader", folder, false );
    SkeletalAnimatorComponent* pServerFollower = TestPairedAnimationInternal::addParticipant( serverManager, "Follower", folder, false );
    SkeletalAnimatorComponent* pServerProp     = TestPairedAnimationInternal::addParticipant( serverManager, "Prop", folder, false );
    SW_ASSERT_TRUE( pServerLeader != nullptr && pServerFollower != nullptr && pServerProp != nullptr );
    SW_ASSERT_NOT_NULL( TestPairedAnimationInternal::addReleaseNotify( *pServerFollower ) );
    const shared_ptr<const PairedAnimationDef> def = TestPairedAnimationInternal::makeDef( TestPairedAnimationInternal::kSnapXML );
    SW_ASSERT_NOT_NULL( def.get() );
    PairedAnimationSystem&               serverSystem  = PairedAnimationSystem::getOrCreate( serverManager );
    uint32                               sessionID     = 0;
    const vector<PairedAnimationBinding> serverBinding = {
        PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pServerLeader )},
        PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pServerFollower )},
        PairedAnimationBinding{    hashed_string( "Prop" ),     TComponentHandle<SkeletalAnimatorComponent>( pServerProp )},
    };
    SW_ASSERT_TRUE( serverSystem.startSession( serverManager, def, serverBinding, sessionID ) == PairedAnimationStartResult::Started );
    for ( uint32 frameIndex = 0; frameIndex < 5; ++frameIndex )
    {
        TestPairedAnimationInternal::step( serverManager, serverSystem );
    }
    const PairedAnimationSession* pServerSession = serverSystem.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pServerSession );
    PairedAnimationSessionSnapshot captured{};
    SW_ASSERT_TRUE( pServerSession->captureSnapshot( captured ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, captured._anchorTime, 1e-6f );
    SW_EXPECT_EQUAL( 1u, captured._nextNotifyIndex );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), captured._listRole.size() );
    SW_EXPECT_TRUE( captured._listRole[2]._stage == PairedAnimationRoleStage::Playing );
    SW_EXPECT_NEAR_EQUAL( 0.5f, captured._listRole[2]._entryAnchorTime, 1e-6f );

    vector<uint8> bytes;
    captured.writeBytes( bytes );
    PairedAnimationSessionSnapshot received{};
    SW_ASSERT_TRUE( PairedAnimationSessionSnapshot::readBytes( bytes, received ) == PairedAnimationSessionSnapshot::ReadResult::Success );

    GameObjectManager          clientManager;
    SkeletalAnimatorComponent* pClientLeader   = TestPairedAnimationInternal::addParticipant( clientManager, "Leader", folder, false );
    SkeletalAnimatorComponent* pClientFollower = TestPairedAnimationInternal::addParticipant( clientManager, "Follower", folder, false );
    SkeletalAnimatorComponent* pClientProp     = TestPairedAnimationInternal::addParticipant( clientManager, "Prop", folder, false );
    SW_ASSERT_TRUE( pClientLeader != nullptr && pClientFollower != nullptr && pClientProp != nullptr );
    AnimationNotifyComponent* pClientNotify = TestPairedAnimationInternal::addReleaseNotify( *pClientFollower );
    SW_ASSERT_NOT_NULL( pClientNotify );
    PairedAnimationSystem&               clientSystem  = PairedAnimationSystem::getOrCreate( clientManager );
    const vector<PairedAnimationBinding> clientBinding = {
        PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pClientLeader )},
        PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pClientFollower )},
        PairedAnimationBinding{    hashed_string( "Prop" ),     TComponentHandle<SkeletalAnimatorComponent>( pClientProp )},
    };
    SW_ASSERT_TRUE( clientSystem.restoreSession( clientManager, def, clientBinding, received ) == PairedAnimationStartResult::Started );
    SW_EXPECT_TRUE( clientSystem.findSession( sessionID ) != nullptr );

    // 클라이언트가 한 프레임 가면 서버가 방금 평가한 시각에 닿는다.
    TestPairedAnimationInternal::step( clientManager, clientSystem );
    SW_EXPECT_NEAR_EQUAL( TestPairedAnimationInternal::getCurrentTime( *pServerLeader ), TestPairedAnimationInternal::getCurrentTime( *pClientLeader ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( TestPairedAnimationInternal::getCurrentTime( *pServerProp ), TestPairedAnimationInternal::getCurrentTime( *pClientProp ), 1e-5f );
    SW_EXPECT_EQUAL( 0u, TestPairedAnimationInternal::countRelease( *pClientNotify ) );

    // 캡처는 재생 중에만 된다.
    serverSystem.abortSession( serverManager, sessionID );
    PairedAnimationSessionSnapshot afterAbort{};
    SW_EXPECT_FALSE( pServerSession->captureSnapshot( afterAbort ) );
}
