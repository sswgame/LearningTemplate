#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Animation/AnimationLOD.h"
#include "Engine/Object/Animation/AnimationRewind.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// SpriteAnimationLODTest — 2D 스프라이트 애니메이터가 애니메이션 LOD(가시성 · 주기)를 따르고 되감기에 상태(프레임)를 남기는 것.

namespace
{
    struct TestSpriteAnimationLODInternal
    {
        /** @brief 프레임 넷(각 100 ms)을 도는 반복 구간 `all` 의 클립을 쓰고 경로를 돌려줍니다. */
        static string writeClip()
        {
            const string clipPath = test::makeTempPath( "walker.sprite.json" );
            const bool   bWritten = FileUtil::writeTextFile( clipPath, R"({
  "atlas": "engine/textures/test/quadrants.dds",
  "frames": [
    { "u": 0.0, "v": 0.0, "w": 0.5, "h": 0.5, "durationMs": 100 },
    { "u": 0.5, "v": 0.0, "w": 0.5, "h": 0.5, "durationMs": 100 },
    { "u": 0.0, "v": 0.5, "w": 0.5, "h": 0.5, "durationMs": 100 },
    { "u": 0.5, "v": 0.5, "w": 0.5, "h": 0.5, "durationMs": 100 }
  ],
  "transformKeys": [],
  "animations": [ { "name": "all", "start": 0, "count": 4, "loop": true } ]
})" );
            return bWritten ? clipPath : string{};
        }

        /** @brief 원점을 앞(+Z 쪽 원점) 또는 뒤로 보는 뷰입니다. */
        static AnimationLODView makeView( bool bTowardOrigin )
        {
            const float3   eye{ 0.0f, 0.0f, -5.0f };
            const float3   target = bTowardOrigin ? float3{ 0.0f, 0.0f, 0.0f } : float3{ 0.0f, 0.0f, -10.0f };
            const float4x4 view   = float4x4::makeLookAt( eye, target, float3{ 0.0f, 1.0f, 0.0f } );
            return AnimationLODView::make( view * float4x4::makePerspectiveFieldOfView( MathUtil::kPi * 0.5f, 1.0f, 0.1f, 100.0f ), eye );
        }

        struct Walker
        {
            SpriteComponent*         _pSprite{ nullptr };
            SpriteAnimatorComponent* _pAnimator{ nullptr };
        };

        static Walker createWalker( GameObjectManager& manager, const string& clipPath )
        {
            Walker      walker{};
            GameObject* pObject = manager.createGameObject( hashed_string( "Walker" ) );
            if ( pObject == nullptr )
                return walker;
            walker._pSprite = pObject->addComponent<SpriteComponent>();
            if ( walker._pSprite == nullptr )
                return walker;
            walker._pSprite->setClipPath( clipPath );
            walker._pAnimator = pObject->addComponent<SpriteAnimatorComponent>();
            if ( walker._pAnimator != nullptr )
                walker._pAnimator->play( "all", true );
            return walker;
        }
    };
} // namespace

/**
 * @brief [SpriteAnimationLODTest] 어느 뷰에도 안 보이면 시간은 흐르되 스프라이트 프레임은 넘기지 않고, 보이게 된 틱에 곧바로 지금 프레임으로 맞춘다
 */
SW_TEST_CASE( SpriteAnimationLODTest, OffscreenSpriteKeepsTimeButSkipsFrames )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string clipPath = TestSpriteAnimationLODInternal::writeClip();
    SW_ASSERT_FALSE( clipPath.empty() );
    GameObjectManager                            manager;
    const TestSpriteAnimationLODInternal::Walker walker = TestSpriteAnimationLODInternal::createWalker( manager, clipPath );
    SW_ASSERT_NOT_NULL( walker._pAnimator );
    AnimationSystem& system = manager.getAnimationSystem();
    system.setLODSettings( AnimationLODSettings::makeDefault() );
    SW_EXPECT_EQUAL( 0, walker._pSprite->getClipFrame() );

    system.setLODViews( { TestSpriteAnimationLODInternal::makeView( false ) } );
    system.evaluate( 0.0f );
    walker._pAnimator->onTick( 0.15f );
    SW_EXPECT_EQUAL( 1, walker._pAnimator->getCurrentFrame() ); // 시간 · 상태는 흘렀다
    SW_EXPECT_EQUAL( 0, walker._pSprite->getClipFrame() );      // 스프라이트는 넘기지 않았다
    walker._pAnimator->onTick( 0.1f );
    SW_EXPECT_EQUAL( 2, walker._pAnimator->getCurrentFrame() );
    SW_EXPECT_EQUAL( 0, walker._pSprite->getClipFrame() );

    system.setLODViews( { TestSpriteAnimationLODInternal::makeView( true ) } );
    system.evaluate( 0.0f );
    walker._pAnimator->onTick( 0.0f ); // 프레임이 그대로여도 밀린 프레임을 맞춘다
    SW_EXPECT_EQUAL( 2, walker._pSprite->getClipFrame() );
}

/**
 * @brief [SpriteAnimationLODTest] LOD 주기가 3 이면 스프라이트 프레임은 그 주기의 틱에만 넘어간다(6 틱에 2 번) — 위상은 핸들에서 오고 시간은 매 틱 흐른다
 */
SW_TEST_CASE( SpriteAnimationLODTest, UpdateRateDivisorThrottlesFramePushes )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string clipPath = TestSpriteAnimationLODInternal::writeClip();
    SW_ASSERT_FALSE( clipPath.empty() );
    GameObjectManager                            manager;
    const TestSpriteAnimationLODInternal::Walker walker = TestSpriteAnimationLODInternal::createWalker( manager, clipPath );
    SW_ASSERT_NOT_NULL( walker._pAnimator );
    AnimationSystem&     system = manager.getAnimationSystem();
    AnimationLODSettings settings{};
    settings._listRateLevel.push_back( AnimationLODRateLevel{ 0.0f, 3u, SW_FALSE } );
    system.setLODSettings( settings );
    system.setLODViews( { TestSpriteAnimationLODInternal::makeView( true ) } );

    uint32 pushedCount = 0;
    for ( uint32 tick = 0; tick < 6; ++tick )
    {
        system.evaluate( 0.0f ); // 프레임 번호를 하나 올리고 판정을 넣는다
        walker._pAnimator->onTick( 0.1f );
        if ( walker._pSprite->getClipFrame() == walker._pAnimator->getCurrentFrame() )
            ++pushedCount;
    }
    SW_EXPECT_EQUAL( 2u, pushedCount );
}

#if SW_ANIMATION_REWIND_ENABLED // 되감기 기록기는 Shipping 에 없다
/**
 * @brief [SpriteAnimationLODTest] 되감기는 스프라이트 애니메이터의 상태(구간 이름 · 시각 · 프레임)를 남기고, 되감는 동안 기록된 프레임을 건 채 흐르지 않는다
 */
SW_TEST_CASE( SpriteAnimationLODTest, RewindRecordsAndRestoresSpriteFrames )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string clipPath = TestSpriteAnimationLODInternal::writeClip();
    SW_ASSERT_FALSE( clipPath.empty() );
    GameObjectManager                            manager;
    const TestSpriteAnimationLODInternal::Walker walker = TestSpriteAnimationLODInternal::createWalker( manager, clipPath );
    SW_ASSERT_NOT_NULL( walker._pAnimator );
    AnimationSystem& system = manager.getAnimationSystem();

    const bool    bWasOn          = AnimationRewindRecorder::isRecordingRequested();
    const float32 previousSeconds = AnimationRewindRecorder::getRequestedWindowSeconds();
    AnimationRewindRecorder::setRecordingRequested( true );
    AnimationRewindRecorder::setRequestedWindowSeconds( 5.0f );

    float64 timeAtFrameOne = -1.0;
    for ( uint32 tick = 0; tick < 6; ++tick )
    {
        walker._pAnimator->onTick( 0.1f );
        system.evaluate( 0.1f );
        const AnimationRewindTrack* pTrack = system.getRewind().findTrack( walker._pAnimator->getHandle() );
        SW_ASSERT_NOT_NULL( pTrack );
        const AnimationRewindFrame& latest = pTrack->getFrame( pTrack->_count - 1 );
        SW_EXPECT_EQUAL( walker._pAnimator->getCurrentFrame(), latest._state._spriteFrame );
        if ( latest._state._spriteFrame == 1 && timeAtFrameOne < 0.0 )
            timeAtFrameOne = latest._time;
    }
    const AnimationRewindTrack* pTrack = system.getRewind().findTrack( walker._pAnimator->getHandle() );
    SW_ASSERT_NOT_NULL( pTrack );
    SW_EXPECT_TRUE( pTrack->_kind == AnimationRewindKind::Sprite );
    SW_EXPECT_EQUAL( 0u, pTrack->getFrame( 0 )._boneCount );
    SW_EXPECT_TRUE( pTrack->getFrame( 0 )._state._stateName == hashed_string( "all" ) );
    SW_ASSERT_TRUE( timeAtFrameOne >= 0.0 );
    SW_ASSERT_TRUE( walker._pAnimator->getCurrentFrame() != 1 );

    system.getRewind().setScrubTime( timeAtFrameOne );
    system.evaluate( 0.1f );
    SW_EXPECT_EQUAL( 1, walker._pSprite->getClipFrame() );
    const int32 frozenFrame = walker._pAnimator->getCurrentFrame();
    walker._pAnimator->onTick( 0.1f ); // 되감는 동안에는 흐르지 않는다
    SW_EXPECT_EQUAL( frozenFrame, walker._pAnimator->getCurrentFrame() );
    SW_EXPECT_EQUAL( 1, walker._pSprite->getClipFrame() );

    // 풀면 다음 틱에 지금 프레임으로 다시 맞춘다.
    system.getRewind().clearScrub();
    walker._pAnimator->onTick( 0.0f );
    SW_EXPECT_EQUAL( walker._pAnimator->getCurrentFrame(), walker._pSprite->getClipFrame() );

    AnimationRewindRecorder::setRecordingRequested( bWasOn );
    AnimationRewindRecorder::setRequestedWindowSeconds( previousSeconds );
}
#endif // SW_ANIMATION_REWIND_ENABLED
