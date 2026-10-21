#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Codec/Raw/RawAnimationCodec.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyComponent.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyHandlers.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/AnimationTestUtil.h"
#include "EngineTest/AssetArrivalTestUtil.h"
#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimationNotifyTest — 구간 알림(시작 · 끝)의 트랙 의미, 알림 표 읽기 검사, 디스패치(한 번씩 · 틱 · 끊김), 내장 처리기(발소리 재질 · 칼 궤적 판정 · 이벤트 · 흔들림).

namespace sw
{
    /** @brief 맞음 · 알림 이벤트를 적는 컴포넌트입니다. */
    class MockHitListenerComponent : public Component
    {
    public:
        REFLECT_BODY();

        vector<HitInfo>             _listHit;
        vector<AnimationNotifyInfo> _listNotify;

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        void            onHitReceived( const HitInfo& hit ) override { _listHit.push_back( hit ); }
        void            onAnimationNotify( const AnimationNotifyInfo& notify ) override { _listNotify.push_back( notify ); }
    };

    inline const TypeInfo* MockHitListenerComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockHitListenerComponent>, hashed_string( "MockHitListenerComponent" ),
                                          hashed_string( "sw::MockHitListenerComponent" ), sizeof( MockHitListenerComponent ) );
    }
} // namespace sw

namespace
{
    struct TestAnimationNotifyInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @class RecordingHandler @brief 불린 순서를 "단계:알림" 으로 적는 테스트 처리기입니다. */
        class RecordingHandler final : public IAnimationNotifyHandler
        {
        public:
            explicit RecordingHandler( vector<string>* pLog )
                : _pLog{ pLog }
            {
            }

            vector_reference<const AnimationNotifyParamDef> getParams() const override { return vector_reference<const AnimationNotifyParamDef>{ kArrParam }; }
            bool                                            supportsState() const override { return true; }
            void                                            onNotify( AnimationNotifyContext& context ) const override { _pLog->push_back( string( "Notify:" ) + context._fired._name.c_str() ); }
            void                                            onNotifyBegin( AnimationNotifyContext& context ) const override { _pLog->push_back( string( "Begin:" ) + context._fired._name.c_str() ); }
            void                                            onNotifyTick( AnimationNotifyContext& context ) const override { _pLog->push_back( string( "Tick:" ) + context._fired._name.c_str() ); }
            void                                            onNotifyEnd( AnimationNotifyContext& context ) const override { _pLog->push_back( string( "End:" ) + context._fired._name.c_str() ); }

        private:
            static constexpr AnimationNotifyParamDef kArrParam[] = {
                { "tag", AnimationNotifyParamKind::Name, false },
            };
            vector<string>* _pLog;
        };

        static uint32 countPhase( const vector<AnimationFiredNotify>& listFired, const utf8* pName, AnimationNotifyPhase phase )
        {
            uint32 count = 0;
            for ( const AnimationFiredNotify& fired : listFired )
            {
                count += ( fired._name == hashed_string( pName ) && fired._phase == phase ) ? 1u : 0u;
            }
            return count;
        }

        static uint32 countLog( const vector<string>& listLog, const utf8* pEntry )
        {
            uint32 count = 0;
            for ( const string& entry : listLog )
            {
                count += entry == pEntry ? 1u : 0u;
            }
            return count;
        }

        static AnimationFiredNotify makeFired( const utf8* pName, const IAnimationPlayable* pSource, uint32 eventIndex, AnimationNotifyPhase phase )
        {
            AnimationFiredNotify fired;
            fired._name       = hashed_string( pName );
            fired._pSource    = pSource;
            fired._eventIndex = eventIndex;
            fired._phase      = phase;
            fired._duration   = phase == AnimationNotifyPhase::Instant ? 0.0f : 0.2f;
            return fired;
        }

        static AnimationNotifyFrame makeFrame( const vector<AnimationFiredNotify>& listFired, const vector<const IAnimationPlayable*>& listActive )
        {
            AnimationNotifyFrame frame;
            frame._listFired          = vector_reference<const AnimationFiredNotify>{ listFired.data(), listFired.size() };
            frame._listActivePlayable = vector_reference<const IAnimationPlayable* const>{ listActive.data(), listActive.size() };
            frame._deltaSeconds       = kFrame;
            return frame;
        }

        /** @brief 정적 · 키네마틱 상자 강체 오브젝트입니다. */
        static RigidBodyComponent* spawnBox( GameObjectManager& manager, const utf8* pName, const float3& position, const float3& halfExtents, PhysicsBodyType type,
                                             const utf8* pMaterial )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( type );
            pBody->setMaterial( hashed_string( pMaterial ) );
            pBody->setLocalPosition( position );
            return pBody;
        }
    };
} // namespace

/**
 * @brief [AnimationNotifyTest] 구간 알림은 시작 시각을 지날 때 Begin, 끝 시각을 지날 때 End — 반복 경계를 넘어도 한 바퀴에 한 번씩, 같은 시각이면 End 가 먼저
 * @details 길이 1 초 반복, 구간 [0.2, 0.5] 와 [0.5, 0.8], 순간 0.9. 0.1 씩 열 걸음이면 Begin · End 가 각 한 번, 0.5 에서는 앞 구간 End 가 뒤 구간 Begin 보다 앞.
 */
SW_TEST_CASE( AnimationNotifyTest, StateNotifyBeginsAndEndsOncePerCrossing )
{
    test::TestPlayable   clip( 1.0f, true );
    AnimationNotifyTrack track;
    track.addEvent( AnimationNotifyEvent{ hashed_string( "Swing" ), 0.2f, 0.3f } );
    track.addEvent( AnimationNotifyEvent{ hashed_string( "Recover" ), 0.5f, 0.3f } );
    track.addEvent( AnimationNotifyEvent{ hashed_string( "Step" ), 0.9f, 0.0f } );

    AnimationClipCursor          cursor;
    vector<AnimationFiredNotify> listFired;
    for ( uint32 stepIndex = 0; stepIndex < 10; ++stepIndex )
    {
        track.collectFired( cursor.advance( 0.1f, 1.0f, true ), 1.0f, 1.0f, &clip, listFired );
    }

    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countPhase( listFired, "Swing", AnimationNotifyPhase::Begin ) );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countPhase( listFired, "Swing", AnimationNotifyPhase::End ) );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countPhase( listFired, "Recover", AnimationNotifyPhase::Begin ) );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countPhase( listFired, "Recover", AnimationNotifyPhase::End ) );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countPhase( listFired, "Step", AnimationNotifyPhase::Instant ) );
    // 0.5 초: Swing 이 닫힌 뒤 Recover 가 열린다.
    size_t swingEnd     = listFired.size();
    size_t recoverBegin = listFired.size();
    for ( size_t firedIndex = 0; firedIndex < listFired.size(); ++firedIndex )
    {
        if ( listFired[firedIndex]._name == hashed_string( "Swing" ) && listFired[firedIndex]._phase == AnimationNotifyPhase::End )
            swingEnd = firedIndex;
        if ( listFired[firedIndex]._name == hashed_string( "Recover" ) && listFired[firedIndex]._phase == AnimationNotifyPhase::Begin )
            recoverBegin = firedIndex;
        SW_EXPECT_TRUE( listFired[firedIndex]._pSource == &clip );
    }
    SW_EXPECT_TRUE( swingEnd < recoverBegin );

    // 한 걸음에 반복 경계를 넘어 0.75 → 1.35(감겨 0.35): Recover 끝(0.8) · Step(0.9) · Swing 시작(0.2) 이 그 순서로 한 번씩.
    AnimationClipCursor loopCursor;
    loopCursor.reset( 0.75f );
    (void)loopCursor.advance( 0.0f, 1.0f, true );
    listFired.clear();
    track.collectFired( loopCursor.advance( 0.6f, 1.0f, true ), 1.0f, 1.0f, &clip, listFired );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listFired.size() ) );
    SW_EXPECT_TRUE( listFired[0]._name == hashed_string( "Recover" ) && listFired[0]._phase == AnimationNotifyPhase::End );
    SW_EXPECT_TRUE( listFired[1]._name == hashed_string( "Step" ) && listFired[1]._phase == AnimationNotifyPhase::Instant );
    SW_EXPECT_TRUE( listFired[2]._name == hashed_string( "Swing" ) && listFired[2]._phase == AnimationNotifyPhase::Begin );
}

/**
 * @brief [AnimationNotifyTest] 알림 표는 모르는 처리기 · 모르는 인자 · 빠진 필수 인자 · 숫자가 아닌 숫자 · 겹친 알림을 읽기 오류로 막는다
 */
SW_TEST_CASE( AnimationNotifyTest, TableRejectsUnknownHandlerAndArguments )
{
    const AnimationNotifyHandlerRegistry& registry = AnimationNotifyHandlerRegistry::getDefault();
    AnimationNotifyTable                  table;
    SW_ASSERT_TRUE( table.loadFromXMLText( R"(<AnimationNotifies>
            <Notify name="FootL" handler="Footstep" socket="foot.l" distance="0.4" sound="common/audio/step_{surface}.wav"/>
            <Notify name="Swing" handler="HitWindow" socketA="hand.r" socketB="SwordTip" radius="0.05" damage="10"/>
            <Notify name="Shake" handler="CameraShake" amplitude="0.1"/>
            <Notify name="Taunt" handler="GameplayEvent" event="Taunted"/>
        </AnimationNotifies>)",
                                           "good", registry ) );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( table.getEntries().size() ) );
    const AnimationNotifyEntry* pSwing = table.findEntry( hashed_string( "Swing" ) );
    SW_ASSERT_NOT_NULL( pSwing );
    SW_EXPECT_TRUE( pSwing->_pHandler == registry.findHandler( hashed_string( "HitWindow" ) ) );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pSwing->getFloatParam( hashed_string( "damage" ) ), 1e-6f );
    SW_EXPECT_TRUE( pSwing->getNameParam( hashed_string( "socketB" ) ) == hashed_string( "SwordTip" ) );
    // 적지 않은 선택 인자는 읽을 때 선언의 "적지 않음" 값으로 채워진다 — 처리기가 대신할 값을 따로 들지 않는다.
    SW_ASSERT_NOT_NULL( pSwing->findParam( hashed_string( "samples" ) ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pSwing->getFloatParam( hashed_string( "samples" ) ), 1e-6f );

    AnimationNotifyTable bad;
    SW_EXPECT_FALSE( bad.loadFromXMLText( R"(<AnimationNotifies><Notify name="A" handler="NoSuchHandler"/></AnimationNotifies>)", "unknownHandler", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXMLText( R"(<AnimationNotifies><Notify name="A" handler="CameraShake" amplitde="0.1"/></AnimationNotifies>)", "typo", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXMLText( R"(<AnimationNotifies><Notify name="A" handler="Footstep"/></AnimationNotifies>)", "missingRequired", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXMLText( R"(<AnimationNotifies><Notify name="A" handler="CameraShake" amplitude="big"/></AnimationNotifies>)", "notNumber", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXMLText(
        R"(<AnimationNotifies><Notify name="A" handler="CameraShake"/><Notify name="A" handler="CameraShake"/></AnimationNotifies>)", "duplicate", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXMLText( R"(<AnimationNotifies><Shake name="A" handler="CameraShake"/></AnimationNotifies>)", "unknownElement", registry ) );
}

/**
 * @brief [AnimationNotifyTest] 디스패치 — 구간은 시작 → 프레임마다 틱 → 끝이 한 번씩, 클립이 재생에서 빠지면 끝을 대신 내고, 표에 없는 알림은 건너뛴다
 */
SW_TEST_CASE( AnimationNotifyTest, DispatchRunsStateHandlersOnceAndClosesInterruptedStates )
{
    vector<string>                 listLog;
    AnimationNotifyHandlerRegistry registry;
    registry.registerHandler( hashed_string( "Record" ), sw::make_unique<TestAnimationNotifyInternal::RecordingHandler>( &listLog ) );
    // 같은 이름으로 다시 올리면 그 자리에서 교체한다(이름은 하나로 남는다).
    const IAnimationNotifyHandler* pFirstHandler = registry.findHandler( hashed_string( "Record" ) );
    registry.registerHandler( hashed_string( "Record" ), sw::make_unique<TestAnimationNotifyInternal::RecordingHandler>( &listLog ) );
    SW_EXPECT_TRUE( registry.findHandler( hashed_string( "Record" ) ) != pFirstHandler );
    shared_ptr<AnimationNotifyTable> table = make_shared<AnimationNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXMLText( R"(<AnimationNotifies><Notify name="Swing" handler="Record"/><Notify name="Ping" handler="Record" tag="x"/></AnimationNotifies>)",
                                            "record", registry ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Dispatcher" ) );
    SW_ASSERT_NOT_NULL( pObject );
    AnimationNotifyComponent* pNotify = pObject->addComponent<AnimationNotifyComponent>();
    SW_ASSERT_NOT_NULL( pNotify );
    pNotify->setNotifyTable( table );

    test::TestPlayable                      clip( 1.0f, true );
    test::TestPlayable                      otherClip( 1.0f, true );
    const vector<const IAnimationPlayable*> listActive{ &clip };
    vector<AnimationFiredNotify>            listFired{ TestAnimationNotifyInternal::makeFired( "Swing", &clip, 0, AnimationNotifyPhase::Begin ),
                                            TestAnimationNotifyInternal::makeFired( "Ping", &clip, 1, AnimationNotifyPhase::Instant ),
                                            TestAnimationNotifyInternal::makeFired( "Unlisted", &clip, 2, AnimationNotifyPhase::Instant ) };
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );
    SW_EXPECT_EQUAL( 1u, pNotify->getActiveStateCount() );
    listFired.clear();
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );
    listFired.push_back( TestAnimationNotifyInternal::makeFired( "Swing", &clip, 0, AnimationNotifyPhase::End ) );
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );
    SW_EXPECT_EQUAL( 0u, pNotify->getActiveStateCount() );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countLog( listLog, "Begin:Swing" ) );
    SW_EXPECT_EQUAL( 3u, TestAnimationNotifyInternal::countLog( listLog, "Tick:Swing" ) );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countLog( listLog, "End:Swing" ) );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countLog( listLog, "Notify:Ping" ) );
    SW_EXPECT_EQUAL( 0u, TestAnimationNotifyInternal::countLog( listLog, "Notify:Unlisted" ) );

    // 끊김 — 구간이 열린 채 클립이 재생에서 빠지면(상태 전이) 다음 프레임에 End 를 대신 낸다. 그 뒤 끝 알림이 와도 두 번 닫지 않는다.
    listLog.clear();
    listFired.assign( 1, TestAnimationNotifyInternal::makeFired( "Swing", &clip, 0, AnimationNotifyPhase::Begin ) );
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );
    listFired.assign( 1, TestAnimationNotifyInternal::makeFired( "Swing", &clip, 0, AnimationNotifyPhase::End ) );
    const vector<const IAnimationPlayable*> listOther{ &otherClip };
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listOther ) );
    SW_EXPECT_EQUAL( 0u, pNotify->getActiveStateCount() );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countLog( listLog, "End:Swing" ) );
    SW_EXPECT_EQUAL( 0u, TestAnimationNotifyInternal::countLog( listLog, "Tick:Swing" ) );

    // 컴포넌트가 끝나면 열린 구간도 닫는다.
    listLog.clear();
    listFired.assign( 1, TestAnimationNotifyInternal::makeFired( "Swing", &clip, 0, AnimationNotifyPhase::Begin ) );
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );
    pNotify->closeAllStates();
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countLog( listLog, "End:Swing" ) );
}

/**
 * @brief [AnimationNotifyTest] 애니메이터 경로 — 클립의 구간 알림이 시스템 평가마다 받는 쪽으로 가서 시작 · 틱 · 끝이 한 번씩, 재생을 멈추면 끝을 대신 낸다
 */
SW_TEST_CASE( AnimationNotifyTest, AnimatorDeliversClipNotifiesToDispatcher )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const Skeleton         skeleton = test::makeChainSkeleton( 2 );
    const AnimationRawClip raw      = test::makeChainRawClip( skeleton, 31, 30.0f, 0.2f );
    AnimationClip          clip;
    clip.setName( hashed_string( "Attack" ) );
    clip.setLooping( true );
    clip.addNotify( AnimationNotifyEvent{ hashed_string( "Swing" ), 0.2f, 0.3f } );
    clip.addNotify( AnimationNotifyEvent{ hashed_string( "Ping" ), 0.1f, 0.0f } );
    SW_ASSERT_TRUE( clip.compressFrom( raw, RawAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) );
    const string folder = test::makeTempPath( "notifyclips" );
    SW_ASSERT_TRUE( clip.saveToFile( FileUtil::joinPath( folder, "attack.animclip" ) ) );

    vector<string>                 listLog;
    AnimationNotifyHandlerRegistry registry;
    registry.registerHandler( hashed_string( "Record" ), sw::make_unique<TestAnimationNotifyInternal::RecordingHandler>( &listLog ) );
    shared_ptr<AnimationNotifyTable> table = make_shared<AnimationNotifyTable>();
    SW_ASSERT_TRUE(
        table->loadFromXMLText( R"(<AnimationNotifies><Notify name="Swing" handler="Record"/><Notify name="Ping" handler="Record"/></AnimationNotifies>)", "record", registry ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Fighter" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
    AnimationNotifyComponent*  pNotify   = pObject->addComponent<AnimationNotifyComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr && pNotify != nullptr );
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    pAnimator->setClipFolder( ::test::makeTempResourceID( folder ) );
    pAnimator->setInitialState( "Attack" );
    pAnimator->dispatchBeginPlay();
    pNotify->dispatchBeginPlay();
    SW_ASSERT_TRUE( ::test::finishAssetArrivals( manager ) );
    pNotify->setNotifyTable( table );
    SW_EXPECT_TRUE( pAnimator->getNotifyListener() != nullptr );

    // 0.05 초씩 열 번(0.5 초) — Ping(0.1) 한 번, Swing 은 0.2 에 열리고 0.5 에 닫힌다.
    for ( uint32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        manager.getSubsystem<AnimationSystem>().evaluate( 0.05f );
    }
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countLog( listLog, "Notify:Ping" ) );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countLog( listLog, "Begin:Swing" ) );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countLog( listLog, "End:Swing" ) );
    SW_EXPECT_TRUE( TestAnimationNotifyInternal::countLog( listLog, "Tick:Swing" ) >= 4u );

    // 다음 바퀴의 구간 안(0.3 초)에서 멈추면 끝을 대신 낸다.
    listLog.clear();
    for ( uint32 frameIndex = 0; frameIndex < 16; ++frameIndex )
    {
        manager.getSubsystem<AnimationSystem>().evaluate( 0.05f );
    }
    SW_EXPECT_EQUAL( 1u, pNotify->getActiveStateCount() );
    pAnimator->stop();
    manager.getSubsystem<AnimationSystem>().evaluate( 0.05f );
    SW_EXPECT_EQUAL( 0u, pNotify->getActiveStateCount() );
    SW_EXPECT_EQUAL( 1u, TestAnimationNotifyInternal::countLog( listLog, "End:Swing" ) );

    // 컴포넌트가 끝나면 받는 쪽을 뗀다.
    pNotify->dispatchEndPlay();
    SW_EXPECT_TRUE( pAnimator->getNotifyListener() == nullptr );
}

/**
 * @brief [AnimationNotifyTest] 내장 처리기 — 발소리는 발 아래 물리 재질, 칼 궤적은 지난 → 지금 칼날을 쓸어 상대를 한 번만(자기 바디는 건너뜀) 히트 존과 함께, 이벤트 · 흔들림
 * @details 공격자: 사슬 스켈레톤(뼈 1 · 2 가 1 m · 2 m 위 — 칼날)과 자기 둘레의 키네마틱 상자. 표적: 칼날 높이의 정적 상자(히트 존 Head ×2). 공격자를 -2 m → +2 m 로
 *          옮기면 칼날이 표적을 지난다. 바닥은 Stone 재질.
 */
SW_TEST_CASE( AnimationNotifyTest, BuiltInHandlersFindSurfaceHitTargetsOnceAndRaiseEvents )
{
    GameObjectManager   manager;
    RigidBodyComponent* pFloor  = TestAnimationNotifyInternal::spawnBox( manager, "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 10.0f, 0.5f, 10.0f },
                                                                         PhysicsBodyType::Static, "Stone" );
    RigidBodyComponent* pTarget = TestAnimationNotifyInternal::spawnBox( manager, "Target", float3{ 0.0f, 1.5f, 3.0f }, float3{ 0.3f, 0.3f, 0.3f },
                                                                         PhysicsBodyType::Static, "Flesh" );
    SW_ASSERT_TRUE( pFloor != nullptr && pTarget != nullptr );
    PhysicsHitZoneDef head;
    head._name             = hashed_string( "Head" );
    head._damageMultiplier = 2.0f;
    pTarget->setHitZone( head );
    MockHitListenerComponent* pTargetListener = pTarget->getOwner()->addComponent<MockHitListenerComponent>();
    SW_ASSERT_NOT_NULL( pTargetListener );

    GameObject* pAttacker = manager.createGameObject( hashed_string( "Attacker" ) );
    SW_ASSERT_NOT_NULL( pAttacker );
    RigidBodyComponent* pSelfBody = pAttacker->addComponent<RigidBodyComponent>();
    SW_ASSERT_NOT_NULL( pSelfBody );
    PhysicsShapeDesc3D selfBox;
    selfBox._halfExtents   = float3{ 0.4f, 1.2f, 0.4f };
    selfBox._localPosition = float3{ 0.0f, 1.2f, 0.0f };
    pSelfBody->setShape( selfBox );
    pSelfBody->setBodyType( PhysicsBodyType::Kinematic );
    SkeletalMeshComponent* pUnit = pAttacker->addComponent<SkeletalMeshComponent>();
    SW_ASSERT_NOT_NULL( pUnit );
    pUnit->setSkeleton( make_shared<Skeleton>( test::makeChainSkeleton( 3 ) ) );
    MockHitListenerComponent* pSelfListener = pAttacker->addComponent<MockHitListenerComponent>();
    AnimationNotifyComponent* pNotify       = pAttacker->addComponent<AnimationNotifyComponent>();
    SW_ASSERT_TRUE( pSelfListener != nullptr && pNotify != nullptr );
    pSelfBody->setLocalPosition( float3{ -2.0f, 0.0f, 3.0f } );

    shared_ptr<AnimationNotifyTable> table = make_shared<AnimationNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXMLText( R"(<AnimationNotifies>
            <Notify name="Step" handler="Footstep" socket="bone0" distance="0.6"/>
            <Notify name="Swing" handler="HitWindow" socketA="bone1" socketB="bone2" damage="10" impulse="5"/>
            <Notify name="Taunt" handler="GameplayEvent" event="Taunted"/>
            <Notify name="Impact" handler="CameraShake" amplitude="0.2" duration="0.4" socket="bone2"/>
        </AnimationNotifies>)",
                                            "builtins", AnimationNotifyHandlerRegistry::getDefault() ) );
    pNotify->setNotifyTable( table );

    manager.beginPlay();
    manager.update( TestAnimationNotifyInternal::kFrame ); // 바디를 만든다

    vector<CameraShakeRequest> listShake;
    const DelegateHandle       shakeHandle =
        AnimationNotifyHandlerUtil::getCameraShakeRequested().add( SW_DELEGATE_LAMBDA( CameraShakeRequestCallback, [&listShake]( const CameraShakeRequest& request )
    { listShake.push_back( request ); } ) );

    test::TestPlayable                      clip( 1.0f, true );
    const vector<const IAnimationPlayable*> listActive{ &clip };
    vector<AnimationFiredNotify>            listFired{ TestAnimationNotifyInternal::makeFired( "Step", &clip, 0, AnimationNotifyPhase::Instant ),
                                            TestAnimationNotifyInternal::makeFired( "Swing", &clip, 1, AnimationNotifyPhase::Begin ),
                                            TestAnimationNotifyInternal::makeFired( "Taunt", &clip, 2, AnimationNotifyPhase::Instant ),
                                            TestAnimationNotifyInternal::makeFired( "Impact", &clip, 3, AnimationNotifyPhase::Instant ) };
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );

    // 발소리: 발(뼈 0 = 오브젝트 원점) 아래 바닥 재질. 자기 상자(원점을 감싼다)는 건너뛴다.
    bool bFoundStep = false;
    for ( const AnimationNotifyAction& action : pNotify->getActions() )
    {
        if ( action._notify == hashed_string( "Step" ) )
        {
            bFoundStep = true;
            SW_EXPECT_TRUE( action._detail == hashed_string( "Stone" ) );
            SW_EXPECT_EQUAL( pFloor->getOwner()->getObjectID(), action._targetObjectID );
        }
    }
    SW_EXPECT_TRUE( bFoundStep );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pSelfListener->_listNotify.size() ) );
    SW_EXPECT_TRUE( pSelfListener->_listNotify[0]._event == hashed_string( "Taunted" ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listShake.size() ) );
    SW_EXPECT_NEAR_EQUAL( 0.2f, listShake[0]._amplitude, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, listShake[0]._origin._y, 1e-4f );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pTargetListener->_listHit.size() ) ); // 아직 칼날이 표적 앞이다

    // 칼날을 -2 m → +2 m 로 — 지난 자리 → 지금 자리 쓸기가 표적을 맞힌다. 같은 구간에서 두 번 맞지 않는다.
    listFired.clear();
    pAttacker->getPrimarySceneComponent()->setWorldPosition( float3{ 2.0f, 0.0f, 3.0f } );
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );
    pAttacker->getPrimarySceneComponent()->setWorldPosition( float3{ -2.0f, 0.0f, 3.0f } );
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );
    listFired.assign( 1, TestAnimationNotifyInternal::makeFired( "Swing", &clip, 1, AnimationNotifyPhase::End ) );
    pNotify->processFrame( TestAnimationNotifyInternal::makeFrame( listFired, listActive ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pTargetListener->_listHit.size() ) );
    const HitInfo& hit = pTargetListener->_listHit[0];
    SW_EXPECT_TRUE( hit._zone == hashed_string( "Head" ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, hit._damageMultiplier, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, hit._damage, 1e-6f );
    SW_EXPECT_TRUE( hit._pInstigator == pAttacker );
    SW_EXPECT_TRUE( hit._kind == hashed_string( "Swing" ) );
    SW_EXPECT_TRUE( hit._direction._x > 0.9f );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pSelfListener->_listHit.size() ) ); // 자기 바디는 칼날 안에 있어도 맞지 않는다

    AnimationNotifyHandlerUtil::getCameraShakeRequested().remove( shakeHandle );
    manager.endPlay();
}
