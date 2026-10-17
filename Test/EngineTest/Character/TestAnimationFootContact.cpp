#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Notify/AnimationFootContact.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyComponent.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimationFootContactTest — 발 닿음 감지(높이 · 세로 속도 · 다시 들림 간격), 알림 표의 `<FootContact>` 읽기 검사, 감지가 내는 알림 이름의 클립 알림은 처리하지 않음.

namespace
{
    struct TestAnimationFootContactInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @class RecordingHandler @brief 불린 알림 이름을 적는 테스트 처리기입니다. */
        class RecordingHandler final : public IAnimationNotifyHandler
        {
        public:
            explicit RecordingHandler( vector<string>* pLog )
                : _pLog{ pLog }
            {
            }

            vector_reference<const AnimationNotifyParamDef> getParams() const override { return vector_reference<const AnimationNotifyParamDef>{ kArrParam }; }
            void                                            onNotify( AnimationNotifyContext& context ) const override { _pLog->push_back( string( context._fired._name.c_str() ) ); }

        private:
            static constexpr AnimationNotifyParamDef kArrParam[] = {
                { "tag", AnimationNotifyParamKind::Name, false },
            };
            vector<string>* _pLog;
        };

        static AnimationFootContactDef makeDef()
        {
            AnimationFootContactDef def{};
            def._notify           = hashed_string( "FootL" );
            def._bone             = hashed_string( "foot.l" );
            def._contactHeight    = 0.05f;
            def._releaseHeight    = 0.1f;
            def._maxVerticalSpeed = 0.6f;
            return def;
        }
    };
} // namespace

/**
 * @brief [AnimationFootContactTest] 걸음 한 주기에 닿음이 한 번 — 발이 내려와 멈추는 프레임에 내고, 땅에 선 채 시작한 첫 프레임 · 디딘 동안 · 빠르게 지나는 프레임에는 내지 않는다
 * @details 발 높이 0.15 × max(0, sin 2πt), 60 Hz 로 3 초. 땅에 닿는 시각 0.5 · 1.5 · 2.5 초 바로 뒤 프레임(세로 속도가 상한 아래로 떨어진 때)에 한 번씩이다.
 */
SW_TEST_CASE( AnimationFootContactTest, TrackerFiresOncePerStepWhenTheFootSettles )
{
    const AnimationFootContactDef def = TestAnimationFootContactInternal::makeDef();
    AnimationFootContactTracker   tracker;
    vector<float32>               listContactTime;
    for ( uint32 frameIndex = 0; frameIndex < 180; ++frameIndex )
    {
        const float32 time   = static_cast<float32>( frameIndex ) * TestAnimationFootContactInternal::kFrame;
        const float32 height = 0.15f * MathUtil::max( 0.0f, MathUtil::sin( MathUtil::kTwoPi * time ) );
        if ( tracker.update( def, height, TestAnimationFootContactInternal::kFrame ) )
            listContactTime.push_back( time );
    }
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listContactTime.size() ) );
    for ( uint32 stepIndex = 0; stepIndex < 3; ++stepIndex )
    {
        const float32 touchdown = 0.5f + static_cast<float32>( stepIndex );
        SW_EXPECT_TRUE( touchdown <= listContactTime[stepIndex] && listContactTime[stepIndex] <= touchdown + 0.05f );
    }

    // 높이 간격 안에서 떨리면(0.04 ↔ 0.08, 놓는 높이 0.1 아래) 다시 내지 않는다.
    AnimationFootContactTracker jitter;
    (void)jitter.update( def, 0.3f, TestAnimationFootContactInternal::kFrame ); // 첫 프레임은 기록만 한다
    uint32 jitterCount = 0;
    for ( uint32 frameIndex = 0; frameIndex < 20; ++frameIndex )
    {
        const float32 height = 0.3f - 0.026f * static_cast<float32>( frameIndex < 10 ? frameIndex : 10 );
        jitterCount += jitter.update( def, frameIndex < 10 ? height : ( frameIndex % 2 == 0 ? 0.04f : 0.042f ), TestAnimationFootContactInternal::kFrame ) ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 1u, jitterCount );
    SW_EXPECT_TRUE( jitter.isPlanted() );
    SW_EXPECT_FALSE( jitter.update( def, 0.0f, 0.0f ) ); // 시간이 흐르지 않으면 아무것도 하지 않는다
}

/**
 * @brief [AnimationFootContactTest] 알림 표의 `<FootContact>` — 읽고, 없는 알림 이름 · 높이 순서 · 0 이하 속도 · 빠진 필드 · 겹친 알림 · 모르는 필드는 읽기 오류다
 */
SW_TEST_CASE( AnimationFootContactTest, TableReadsAndValidatesFootContacts )
{
    vector<string>                 listLog;
    AnimationNotifyHandlerRegistry registry;
    registry.registerHandler( hashed_string( "Record" ), sw::make_unique<TestAnimationFootContactInternal::RecordingHandler>( &listLog ) );
    AnimationNotifyTable table;
    SW_ASSERT_TRUE( table.loadFromXMLText( R"(<AnimationNotifies>
            <FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.1" max_vertical_speed="0.6"/>
            <Notify name="FootL" handler="Record"/>
        </AnimationNotifies>)",
                                           "good", registry ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( table.getFootContacts().size() ) );
    const AnimationFootContactDef& def = table.getFootContacts()[0];
    SW_EXPECT_TRUE( def._bone == hashed_string( "foot.l" ) );
    SW_EXPECT_NEAR_EQUAL( 0.1f, def._releaseHeight, 1e-6f );
    SW_EXPECT_TRUE( table.isFootContactNotify( hashed_string( "FootL" ) ) );
    SW_EXPECT_FALSE( table.isFootContactNotify( hashed_string( "FootR" ) ) );

    const utf8* const arrBad[] = {
        R"(<AnimationNotifies><FootContact notify="FootR" bone="foot.r" contact_height="0.05" release_height="0.1" max_vertical_speed="0.6"/></AnimationNotifies>)",
        R"(<AnimationNotifies><Notify name="FootL" handler="Record"/><FootContact notify="FootL" bone="foot.l" contact_height="0.1" release_height="0.05" max_vertical_speed="0.6"/></AnimationNotifies>)",
        R"(<AnimationNotifies><Notify name="FootL" handler="Record"/><FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.1" max_vertical_speed="0"/></AnimationNotifies>)",
        R"(<AnimationNotifies><Notify name="FootL" handler="Record"/><FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.1"/></AnimationNotifies>)",
        R"(<AnimationNotifies><Notify name="FootL" handler="Record"/><FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.1" max_vertical_speed="0.6" speed="1"/></AnimationNotifies>)",
        R"(<AnimationNotifies><Notify name="FootL" handler="Record"/><FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.1" max_vertical_speed="0.6"/><FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.1" max_vertical_speed="0.6"/></AnimationNotifies>)",
    };
    for ( const utf8* pXML : arrBad )
    {
        AnimationNotifyTable bad;
        SW_EXPECT_FALSE( bad.loadFromXMLText( pXML, "bad", registry ) );
    }
}

/**
 * @brief [AnimationFootContactTest] 감지가 내는 알림 이름의 클립 알림은 처리하지 않는다(한 걸음에 소리 두 번 없음, 출처는 하나) — 다른 알림은 그대로 처리한다
 */
SW_TEST_CASE( AnimationFootContactTest, ClipNotifyOwnedByFootContactIsSkipped )
{
    vector<string>                 listLog;
    AnimationNotifyHandlerRegistry registry;
    registry.registerHandler( hashed_string( "Record" ), sw::make_unique<TestAnimationFootContactInternal::RecordingHandler>( &listLog ) );
    shared_ptr<AnimationNotifyTable> table = make_shared<AnimationNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXMLText( R"(<AnimationNotifies>
            <Notify name="FootL" handler="Record"/>
            <Notify name="Ping" handler="Record"/>
            <FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.1" max_vertical_speed="0.6"/>
        </AnimationNotifies>)",
                                            "owned", registry ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Walker" ) );
    SW_ASSERT_NOT_NULL( pObject );
    AnimationNotifyComponent* pNotify = pObject->addComponent<AnimationNotifyComponent>();
    SW_ASSERT_NOT_NULL( pNotify );
    pNotify->setNotifyTable( table );

    test::TestPlayable                      clip( 1.0f, true );
    const vector<const IAnimationPlayable*> listActive{ &clip };
    vector<AnimationFiredNotify>            listFired( 2 );
    listFired[0]._name    = hashed_string( "FootL" );
    listFired[0]._pSource = &clip;
    listFired[1]._name    = hashed_string( "Ping" );
    listFired[1]._pSource = &clip;
    AnimationNotifyFrame frame;
    frame._listFired          = vector_reference<const AnimationFiredNotify>{ listFired.data(), listFired.size() };
    frame._listActivePlayable = vector_reference<const IAnimationPlayable* const>{ listActive.data(), listActive.size() };
    frame._deltaSeconds       = TestAnimationFootContactInternal::kFrame;
    pNotify->processFrame( frame );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listLog.size() ) );
    SW_EXPECT_TRUE( listLog[0] == "Ping" );
}
