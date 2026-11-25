// TickIntervalTest — 형식이 선언한 틱 주기(`Component::kTickInterval`) · 서브틱 주기가 모드와 무관하게 씬의 시간과 id 의 위상으로만 차례를 정하는지,
// 같은 주기의 컴포넌트가 프레임에 흩어지는지 본다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/GameObject/Tests/TickModeTestComponents.h"
#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"
#include "Engine/Object/GameObject/Tick/TickInterval.h"
#include "Engine/Object/GameObject/Tick/TickRegistry.h"

#include <limits>

using namespace sw;

namespace
{
    constexpr float32 kFrameSeconds     = 1.0f / 64.0f; // 주기(0.125 초)가 딱 여덟 프레임이다
    constexpr uint32  kFramesPerTick    = 8;
    constexpr uint32  kFramesPerSlow    = 32;
    constexpr uint32  kObjectCount      = 64;
    constexpr uint32  kPeriodCount      = 4;
    constexpr uint32  kMaxTicksPerFrame = 10; // 64 개를 여덟 프레임에 고르게 나누면 8 — 황금비 위상의 편차는 둘을 넘지 않는다

    constexpr utf8 kIntervalBatchScopeName[] = "GT.Scene.tickBatch.TickIntervalTestComponent";

    /** @brief 주기 컴포넌트 여럿을 둔 씬입니다. 일괄 경로 · 기준 길(오브젝트 칸) · 메인 모드(같은 오브젝트에 Main 형식)로 나눠 세운다. */
    struct TickIntervalRig
    {
        TickBatchRegistry                  _batchRegistry;
        TickBatchRegistry::Entry           _intervalEntry{ makeDeclaredTickEntry<TickIntervalTestComponent, kIntervalBatchScopeName>() };
        TickBatchRegistry::Entry           _mainEntry{ &TickModeMainTestComponent::StaticType, nullptr, TickMode::Main };
        GameObjectManager                  _manager;
        vector<TickIntervalTestComponent*> _listComponent;
        uint32                             _frame = 0;

        TickIntervalRig( bool bBatchPath, bool bMainMode )
            : _manager{ makeServices( _batchRegistry ) }
        {
            SW_EXPECT_TRUE( _batchRegistry.registerEntry( _intervalEntry ) );
            SW_EXPECT_TRUE( _batchRegistry.registerEntry( _mainEntry ) );
            _manager.getTickRegistry().setBatchPathEnabled( bBatchPath );
            for ( uint32 index = 0; index < kObjectCount; ++index )
            {
                GameObject*                pObject    = _manager.createGameObject( hashed_string( "Interval" ) );
                TickIntervalTestComponent* pComponent = pObject->addComponent<TickIntervalTestComponent>();
                pComponent->_pFrame                   = &_frame;
                if ( bMainMode )
                    (void)pObject->addComponent<TickModeMainTestComponent>();
                _listComponent.push_back( pComponent );
            }
            _manager.beginPlay();
        }

        ~TickIntervalRig()
        {
            _batchRegistry.unregisterEntry( _intervalEntry );
            _batchRegistry.unregisterEntry( _mainEntry );
        }

        TickIntervalRig( const TickIntervalRig& )            = delete;
        TickIntervalRig& operator=( const TickIntervalRig& ) = delete;

        /** @brief 엔진 서비스 묶음에 이 씬의 일괄 함수 표를 끼웁니다. */
        static SceneEngineServices makeServices( TickBatchRegistry& batchRegistry )
        {
            SceneEngineServices services = SceneEngineServices::makeFromBoundEngine();
            services._pTickBatchRegistry = &batchRegistry;
            return services;
        }

        void run( uint32 frameCount, float32 deltaTime )
        {
            for ( uint32 index = 0; index < frameCount; ++index )
            {
                _manager.update( deltaTime );
                ++_frame;
            }
        }
    };

    /** @brief @p pComponent 가 돈 프레임이 id 의 위상으로 셈한 프레임과 같은지 봅니다. */
    bool hasExpectedFrames( const TickIntervalTestComponent& component, uint32 frameCount )
    {
        vector<uint32> listExpected;
        TickFrameTime  time{};
        for ( uint32 frame = 0; frame < frameCount; ++frame )
        {
            time._startSeconds = time._endSeconds;
            time._endSeconds += static_cast<float64>( kFrameSeconds );
            time._deltaTime     = kFrameSeconds;
            const TickTurn turn = TickIntervalUtil::computeTurn( TickIntervalTestComponent::kTickInterval, component.getComponentID(), time );
            for ( uint32 index = 0; index < turn._intervalCount; ++index )
            {
                listExpected.push_back( frame );
            }
        }
        return listExpected == component._listTickFrame;
    }
} // namespace

/**
 * @brief [TickIntervalTest] 주기마다 한 번 돌고 시간 합이 흐른 시간과 같다 — 일괄 · 오브젝트 칸 · 메인 모드가 모두 위상 셈과 같은 프레임에 돈다
 * @details 변이: 한 길이라도 주기를 보지 않으면 매 프레임 돌아 수가 여덟 배가 된다.
 */
SW_TEST_CASE( TickIntervalTest, EveryPathTicksOnTheSameFrames )
{
    const uint32 frameCount = kFramesPerTick * kPeriodCount;
    for ( uint32 path = 0; path < 3; ++path )
    {
        TickIntervalRig rig{ path != 1, path == 2 };
        rig.run( frameCount, kFrameSeconds );
        for ( const TickIntervalTestComponent* pComponent : rig._listComponent )
        {
            SW_EXPECT_EQUAL( kPeriodCount, pComponent->_tickCount );
            SW_EXPECT_NEAR( static_cast<float32>( frameCount ) * kFrameSeconds, pComponent->_tickSeconds, 1.0e-5f );
            SW_EXPECT_EQUAL( frameCount / kFramesPerSlow, pComponent->_slowCount );
            SW_EXPECT_TRUE( hasExpectedFrames( *pComponent, frameCount ) );
        }
    }
}

/** @brief [TickIntervalTest] 같은 주기의 컴포넌트가 한 프레임에 몰리지 않는다 — 한 주기 안의 모든 프레임에 고르게 흩어진다 */
SW_TEST_CASE( TickIntervalTest, SameIntervalSpreadsOverFrames )
{
    TickIntervalRig rig{ true, false };
    rig.run( kFramesPerTick, kFrameSeconds );
    uint32 arrCount[kFramesPerTick] = {};
    for ( const TickIntervalTestComponent* pComponent : rig._listComponent )
    {
        SW_REQUIRE_EQUAL( size_t{ 1 }, pComponent->_listTickFrame.size() );
        ++arrCount[pComponent->_listTickFrame[0]];
    }
    for ( const uint32 count : arrCount )
    {
        SW_EXPECT_TRUE( count > 0 && count <= kMaxTicksPerFrame );
    }
}

/** @brief [TickIntervalTest] 프레임이 주기보다 길면 한 번에 찬 주기 수를 넘기고, 꺼진 동안 찬 주기는 다시 켜진 뒤 몰아 돌지 않는다 */
SW_TEST_CASE( TickIntervalTest, LongFrameCountsIntervalsAndDisabledTimeIsDropped )
{
    TickIntervalRig            rig{ true, false };
    TickIntervalTestComponent& component = *rig._listComponent[0];
    rig.run( 1, TickIntervalTestComponent::kTickInterval * 3.0f );
    SW_EXPECT_EQUAL( 1u, component._tickCount );
    SW_EXPECT_EQUAL( size_t{ 3 }, component._listTickFrame.size() );
    SW_EXPECT_NEAR( TickIntervalTestComponent::kTickInterval * 3.0f, component._tickSeconds, 1.0e-5f );

    component.getOwner()->setActive( false );
    rig.run( 10, TickIntervalTestComponent::kTickInterval );
    component.getOwner()->setActive( true );
    const uint32 tickCountBefore = component._tickCount;
    rig.run( 1, TickIntervalTestComponent::kTickInterval );
    SW_EXPECT_EQUAL( tickCountBefore + 1u, component._tickCount );
    SW_EXPECT_EQUAL( size_t{ 4 }, component._listTickFrame.size() );

    rig.run( 3, 0.0f ); // 멈춘 씬은 주기도 차지 않는다
    SW_EXPECT_EQUAL( tickCountBefore + 1u, component._tickCount );
}

/** @brief [TickIntervalTest] 고정 스텝과 같은 주기는 프레임마다 정확히 한 번이다 — 위상을 흩어도 float 반올림으로 0 · 2 번이 번갈아 나오지 않는다 */
SW_TEST_CASE( TickIntervalTest, FixedStepIntervalFillsOncePerFrame )
{
    constexpr float32 kStep = 1.0f / 60.0f;
    for ( uint64 tickKey = 1; tickKey <= TickIntervalUtil::kPhaseCount; ++tickKey )
    {
        TickFrameTime time{};
        bool          bOncePerFrame = true;
        for ( uint32 frame = 0; frame < 36000; ++frame )
        {
            time._startSeconds = time._endSeconds;
            time._endSeconds += static_cast<float64>( kStep );
            time._deltaTime = kStep;
            bOncePerFrame   = bOncePerFrame && TickIntervalUtil::computeTurn( kStep, tickKey, time )._intervalCount == 1u;
        }
        SW_EXPECT_TRUE( bOncePerFrame );
    }
    SW_EXPECT_EQUAL( 90, TickIntervalUtil::secondsToCount( kStep, 1.5f, 1 ) );
    SW_EXPECT_EQUAL( 1, TickIntervalUtil::secondsToCount( kStep, 0.0f, 1 ) );
}

/** @brief [TickIntervalTest] 음수 · NaN 주기의 서브틱은 등록하지 않는다 */
SW_TEST_CASE( TickIntervalTest, InvalidSubTickIntervalIsRefused )
{
    SW_EXPECT_LOG( "Component.SubTickInterval" );
    TickIntervalRig rig{ true, false };
    SW_EXPECT_FALSE( rig._listComponent[0]->registerSubTick( TickGroup::DuringPhysics, 7, TickPhase::Normal, 0, -1.0f ).isValid() );
    SW_EXPECT_FALSE( rig._listComponent[0]->registerSubTick( TickGroup::DuringPhysics, 7, TickPhase::Normal, 0, std::numeric_limits<float32>::quiet_NaN() ).isValid() );
}
