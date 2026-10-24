/**
 * @file TestSceneFrameAccess.cpp
 * @brief 프레임 시스템의 읽기 · 쓰기 선언(`SceneFrameAccess`) — 붙인 순서와 모순인 등록은 거절하고, 지금 붙는 시스템들은 어느 순서로 붙여도 받아들인다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Character/Hit/MeleeHitLedger.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/SceneFrameAccess.h"

#include "GameFramework/Base/Actor/Control/ControlSystem.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionSpatialIndex.h"

#include "TestFramework/TestFramework.h"

namespace
{
    constexpr const utf8* kLogIDAccessOrder = "FrameSystem.AccessOrder";

    /** @brief 지정한 접근을 선언하고, 불리면 자기 번호를 적는 테스트 시스템입니다. */
    class AccessProbeSystem final : public sw::ISceneFrameSystem
    {
    public:
        AccessProbeSystem( sw::vector<int32>& inoutListCall, int32 id, const sw::SceneFrameAccess& access )
            : _listCall{ inoutListCall }
            , _access{ access }
            , _id{ id }
        {
        }

        void runBeforeTick( sw::GameObjectManager& /*manager*/, float32 /*deltaTime*/ ) override { _listCall.push_back( _id ); }

        sw::SceneFrameAccess getFrameAccess() const override { return _access; }

    private:
        sw::vector<int32>&   _listCall;
        sw::SceneFrameAccess _access;
        int32                _id;
    };

    struct SceneFrameAccessTestInternal
    {
        static sw::unique_ptr<sw::ISceneFrameSystem> makeProbe( sw::vector<int32>& inoutListCall, int32 id, const sw::SceneDataKindSet& writes, const sw::SceneDataKindSet& reads,
                                                                const sw::SceneDataKindSet& readsPrevious )
        {
            sw::SceneFrameAccess access{};
            access._writes        = writes;
            access._reads         = reads;
            access._readsPrevious = readsPrevious;
            return sw::make_unique<AccessProbeSystem>( inoutListCall, id, access );
        }

        /** @brief 지금 매니저에 붙는 프레임 시스템 넷 가운데 @p index 번째(0 장부, 1 페어드 애니메이션, 2 조종, 3 상호작용 색인)입니다. */
        static sw::unique_ptr<sw::ISceneFrameSystem> makeRealSystem( uint32 index )
        {
            switch ( index )
            {
                case 0:
                    return sw::make_unique<sw::MeleeHitLedger>();
                case 1:
                    return sw::make_unique<sw::PairedAnimationSystem>();
                case 2:
                    return sw::make_unique<sw::ControlSystem>();
                default:
                    return sw::make_unique<sw::InteractionSpatialIndex>();
            }
        }
    };
    using Internal = SceneFrameAccessTestInternal;
} // namespace

/**
 * @brief [SceneFrameAccessTest] 쓰는 시스템을 읽는 시스템보다 먼저 붙이면 받아들이고, 그 순서로 돈다
 */
SW_TEST_CASE( SceneFrameAccessTest, WriterBeforeReaderIsAccepted )
{
    sw::GameObjectManager      manager;
    sw::vector<int32>          listCall;
    const sw::SceneDataKindSet none{};

    SW_ASSERT_TRUE( manager.addFrameSystem( sw::hashed_string( "Writer" ), Internal::makeProbe( listCall, 1, { sw::SceneDataKind::Transform }, none, none ) ) );
    SW_ASSERT_TRUE( manager.addFrameSystem( sw::hashed_string( "Reader" ), Internal::makeProbe( listCall, 2, none, { sw::SceneDataKind::Transform }, none ) ) );

    manager.update( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listCall.size() );
    SW_EXPECT_EQUAL( 1, listCall[0] );
    SW_EXPECT_EQUAL( 2, listCall[1] );
}

/**
 * @brief [SceneFrameAccessTest] 읽는 시스템 뒤에 같은 종류를 쓰는 시스템을 붙이는 등록은 오류로 거절되고, 붙어 있던 시스템은 그대로 돈다
 */
SW_TEST_CASE( SceneFrameAccessTest, WriterAfterReaderIsRejected )
{
    sw::GameObjectManager      manager;
    sw::vector<int32>          listCall;
    const sw::SceneDataKindSet none{};
    test::ScopedLogCollector   logs;
    SW_TEST_DEFENSIVE_SCOPE( "a writer attached after its reader breaks the write-then-read order" );

    SW_ASSERT_TRUE( manager.addFrameSystem( sw::hashed_string( "Reader" ), Internal::makeProbe( listCall, 1, none, { sw::SceneDataKind::Transform, sw::SceneDataKind::Navigation }, none ) ) );
    SW_EXPECT_FALSE( manager.addFrameSystem( sw::hashed_string( "Writer" ), Internal::makeProbe( listCall, 2, { sw::SceneDataKind::Navigation }, none, none ) ) );
    SW_EXPECT_EQUAL( 1u, logs.countWithID( kLogIDAccessOrder ) );
    SW_EXPECT_TRUE( manager.findFrameSystem( sw::hashed_string( "Writer" ) ) == nullptr );
    SW_EXPECT_TRUE( manager.findFrameSystem( sw::hashed_string( "Reader" ) ) != nullptr );

    manager.update( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listCall.size() );
    SW_EXPECT_EQUAL( 1, listCall[0] );

    // 읽는 쪽을 떼면 쓰는 쪽이 붙는다 — 거절은 붙은 시스템의 선언이 기준이다.
    manager.removeFrameSystem( sw::hashed_string( "Reader" ) );
    SW_EXPECT_TRUE( manager.addFrameSystem( sw::hashed_string( "Writer" ), Internal::makeProbe( listCall, 2, { sw::SceneDataKind::Navigation }, none, none ) ) );
    SW_EXPECT_EQUAL( 1u, logs.countWithID( kLogIDAccessOrder ) );
}

/**
 * @brief [SceneFrameAccessTest] 순서를 따지지 않는 접근은 거절하지 않는다 — 지난 프레임 값 읽기, 같은 종류를 쓰는 시스템 둘, 자기 쓰기를 자기가 읽기
 */
SW_TEST_CASE( SceneFrameAccessTest, UnorderedAccessIsAccepted )
{
    sw::GameObjectManager      manager;
    sw::vector<int32>          listCall;
    const sw::SceneDataKindSet none{};
    test::ScopedLogCollector   logs;

    // 지난 프레임 값을 읽는 시스템이 먼저 붙고, 그 종류를 쓰는 시스템이 뒤에 붙는다.
    SW_EXPECT_TRUE( manager.addFrameSystem( sw::hashed_string( "LateReader" ), Internal::makeProbe( listCall, 1, none, none, { sw::SceneDataKind::Transform } ) ) );
    SW_EXPECT_TRUE( manager.addFrameSystem( sw::hashed_string( "WriterA" ), Internal::makeProbe( listCall, 2, { sw::SceneDataKind::Transform }, none, none ) ) );
    // 같은 종류를 쓰는 둘째 — 붙인 순서가 의미일 뿐 거절하지 않는다.
    SW_EXPECT_TRUE( manager.addFrameSystem( sw::hashed_string( "WriterB" ), Internal::makeProbe( listCall, 3, { sw::SceneDataKind::Transform }, none, none ) ) );
    // 자기가 쓰는 종류를 자기가 읽는다.
    SW_EXPECT_TRUE( manager.addFrameSystem( sw::hashed_string( "SelfReader" ),
                                            Internal::makeProbe( listCall, 4, { sw::SceneDataKind::HitRecord }, { sw::SceneDataKind::HitRecord }, none ) ) );
    SW_EXPECT_EQUAL( 0u, logs.countWithID( kLogIDAccessOrder ) );

    manager.update( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 4 ), listCall.size() );
    for ( size_t index = 0; index < listCall.size(); ++index )
    {
        SW_EXPECT_EQUAL( static_cast<int32>( index ) + 1, listCall[index] );
    }
}

/**
 * @brief [SceneFrameAccessTest] 지금 붙는 프레임 시스템 넷(장부 · 페어드 애니메이션 · 조종 · 상호작용 색인)은 어느 순서로 붙여도 받아들인다 — 선언이 기존 동작을 바꾸지 않는다
 */
SW_TEST_CASE( SceneFrameAccessTest, EveryRealSystemAttachesInAnyOrder )
{
    constexpr uint32         kSystemCount          = 4;
    constexpr const utf8*    kArrKey[kSystemCount] = { "MeleeHitLedger", "PairedAnimation", "ControlSystem", "InteractionSpatialIndex" };
    test::ScopedLogCollector logs;

    // 붙이는 순서를 4 진수 네 자리(자리마다 시스템 번호)로 훑고, 같은 시스템이 겹친 것은 건너뛴다 — 남는 것이 순열 24 개다.
    uint32 permutationCount = 0;
    for ( uint32 code = 0; code < kSystemCount * kSystemCount * kSystemCount * kSystemCount; ++code )
    {
        uint32 arrOrder[kSystemCount] = { code % 4, ( code / 4 ) % 4, ( code / 16 ) % 4, ( code / 64 ) % 4 };
        uint32 usedMask               = 0;
        for ( uint32 slot = 0; slot < kSystemCount; ++slot )
        {
            usedMask |= 1u << arrOrder[slot];
        }
        if ( usedMask != 0xFu )
            continue;
        sw::GameObjectManager manager;
        for ( uint32 slot = 0; slot < kSystemCount; ++slot )
        {
            const uint32 system = arrOrder[slot];
            SW_EXPECT_TRUE( manager.addFrameSystem( sw::hashed_string( kArrKey[system] ), Internal::makeRealSystem( system ) ) );
        }
        ++permutationCount;
    }

    SW_EXPECT_EQUAL( 24u, permutationCount );
    SW_EXPECT_EQUAL( 0u, logs.countWithID( kLogIDAccessOrder ) );
}

/**
 * @brief [SceneFrameAccessTest] 종류 표와 집합 — 모든 종류에 이름이 있고, 집합은 든 종류만 알고, 교집합의 첫 종류를 낸다
 */
SW_TEST_CASE( SceneFrameAccessTest, KindTableAndSet )
{
    for ( uint32 index = 0; index < static_cast<uint32>( sw::SceneDataKind::Count ); ++index )
    {
        SW_EXPECT_TRUE( sw::string_view( sw::getSceneDataKindName( static_cast<sw::SceneDataKind>( index ) ) ) != "Unknown" );
    }
    SW_EXPECT_STREQ( "Unknown", sw::getSceneDataKindName( sw::SceneDataKind::Count ) );
    SW_EXPECT_STREQ( "Transform", sw::getSceneDataKindName( sw::SceneDataKind::Transform ) );

    const sw::SceneDataKindSet empty{};
    const sw::SceneDataKindSet pair{ sw::SceneDataKind::Transform, sw::SceneDataKind::Navigation };
    SW_EXPECT_TRUE( empty.isEmpty() );
    SW_EXPECT_TRUE( empty.getFirstKind() == sw::SceneDataKind::Count );
    SW_EXPECT_TRUE( pair.contains( sw::SceneDataKind::Navigation ) );
    SW_EXPECT_FALSE( pair.contains( sw::SceneDataKind::HitRecord ) );
    SW_EXPECT_TRUE( pair.intersect( sw::SceneDataKindSet{ sw::SceneDataKind::HitRecord } ).isEmpty() );
    SW_EXPECT_TRUE( pair.intersect( sw::SceneDataKindSet{ sw::SceneDataKind::Navigation, sw::SceneDataKind::HitRecord } ).getFirstKind() == sw::SceneDataKind::Navigation );
}
