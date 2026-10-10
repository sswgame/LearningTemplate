#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/AnimPlayer.h"
#include "Engine/Animation/Codec/Raw/RawAnimCodec.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/AnimNotify/AnimNotifyComponent.h"
#include "Engine/Character/AnimNotify/AnimNotifyHandlers.h"
#include "Engine/Character/AnimNotify/AnimNotifyTable.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"
#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimNotifyTest — 구간 알림(시작 · 끝)의 트랙 의미, 알림 표 읽기 검사, 디스패치(한 번씩 · 틱 · 끊김), 내장 처리기(발소리 재질 · 칼 궤적 판정 · 이벤트 · 흔들림).

namespace sw
{
    /** @brief 맞음 · 알림 이벤트를 적는 컴포넌트입니다. */
    class MockHitListenerComponent : public Component
    {
    public:
        REFLECT_BODY();

        vector<HitInfo>        _listHit;
        vector<AnimNotifyInfo> _listNotify;

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        void            onHitReceived( const HitInfo& hit ) override { _listHit.push_back( hit ); }
        void            onAnimNotify( const AnimNotifyInfo& notify ) override { _listNotify.push_back( notify ); }
    };

    inline const TypeInfo* MockHitListenerComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockHitListenerComponent>, hashed_string( "MockHitListenerComponent" ),
                                          hashed_string( "sw::MockHitListenerComponent" ), sizeof( MockHitListenerComponent ) );
    }
} // namespace sw

namespace
{
    struct TestAnimNotifyInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @class RecordingHandler @brief 불린 순서를 "단계:알림" 으로 적는 시험 처리기입니다. */
        class RecordingHandler final : public IAnimNotifyHandler
        {
        public:
            explicit RecordingHandler( vector<string>* pLog )
                : _pLog{ pLog }
            {
            }

            vector_reference<const AnimNotifyParamDef> getParams() const override { return vector_reference<const AnimNotifyParamDef>{ kArrParam }; }
            bool                                       supportsState() const override { return true; }
            void                                       onNotify( AnimNotifyContext& context ) const override { _pLog->push_back( string( "Notify:" ) + context._fired._name.c_str() ); }
            void                                       onNotifyBegin( AnimNotifyContext& context ) const override { _pLog->push_back( string( "Begin:" ) + context._fired._name.c_str() ); }
            void                                       onNotifyTick( AnimNotifyContext& context ) const override { _pLog->push_back( string( "Tick:" ) + context._fired._name.c_str() ); }
            void                                       onNotifyEnd( AnimNotifyContext& context ) const override { _pLog->push_back( string( "End:" ) + context._fired._name.c_str() ); }

        private:
            static constexpr AnimNotifyParamDef kArrParam[] = {
                { "tag", AnimNotifyParamKind::Name, false },
            };
            vector<string>* _pLog;
        };

        static uint32 countPhase( const vector<AnimFiredNotify>& listFired, const utf8* pName, AnimNotifyPhase phase )
        {
            uint32 count = 0;
            for ( const AnimFiredNotify& fired : listFired )
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

        static AnimFiredNotify makeFired( const utf8* pName, const IAnimPlayable* pSource, uint32 eventIndex, AnimNotifyPhase phase )
        {
            AnimFiredNotify fired;
            fired._name       = hashed_string( pName );
            fired._pSource    = pSource;
            fired._eventIndex = eventIndex;
            fired._phase      = phase;
            fired._duration   = phase == AnimNotifyPhase::Instant ? 0.0f : 0.2f;
            return fired;
        }

        static AnimNotifyFrame makeFrame( const vector<AnimFiredNotify>& listFired, const vector<const IAnimPlayable*>& listActive )
        {
            AnimNotifyFrame frame;
            frame._listFired          = vector_reference<const AnimFiredNotify>{ listFired.data(), listFired.size() };
            frame._listActivePlayable = vector_reference<const IAnimPlayable* const>{ listActive.data(), listActive.size() };
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
 * @brief [AnimNotifyTest] 구간 알림은 시작 시각을 지날 때 Begin, 끝 시각을 지날 때 End — 반복 경계를 넘어도 한 바퀴에 한 번씩, 같은 시각이면 End 가 먼저
 * @details 길이 1 초 반복, 구간 [0.2, 0.5] 와 [0.5, 0.8], 순간 0.9. 0.1 씩 열 걸음이면 Begin · End 가 각 한 번, 0.5 에서는 앞 구간 End 가 뒤 구간 Begin 보다 앞.
 */
SW_TEST_CASE( AnimNotifyTest, StateNotifyBeginsAndEndsOncePerCrossing )
{
    test::TestPlayable clip( 1.0f, true );
    AnimNotifyTrack    track;
    track.addEvent( AnimNotifyEvent{ hashed_string( "Swing" ), 0.2f, 0.3f } );
    track.addEvent( AnimNotifyEvent{ hashed_string( "Recover" ), 0.5f, 0.3f } );
    track.addEvent( AnimNotifyEvent{ hashed_string( "Step" ), 0.9f, 0.0f } );

    AnimClipCursor          cursor;
    vector<AnimFiredNotify> listFired;
    for ( uint32 stepIndex = 0; stepIndex < 10; ++stepIndex )
    {
        track.collectFired( cursor.advance( 0.1f, 1.0f, true ), 1.0f, 1.0f, &clip, listFired );
    }

    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countPhase( listFired, "Swing", AnimNotifyPhase::Begin ) );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countPhase( listFired, "Swing", AnimNotifyPhase::End ) );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countPhase( listFired, "Recover", AnimNotifyPhase::Begin ) );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countPhase( listFired, "Recover", AnimNotifyPhase::End ) );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countPhase( listFired, "Step", AnimNotifyPhase::Instant ) );
    // 0.5 초: Swing 이 닫힌 뒤 Recover 가 열린다.
    size_t swingEnd     = listFired.size();
    size_t recoverBegin = listFired.size();
    for ( size_t firedIndex = 0; firedIndex < listFired.size(); ++firedIndex )
    {
        if ( listFired[firedIndex]._name == hashed_string( "Swing" ) && listFired[firedIndex]._phase == AnimNotifyPhase::End )
            swingEnd = firedIndex;
        if ( listFired[firedIndex]._name == hashed_string( "Recover" ) && listFired[firedIndex]._phase == AnimNotifyPhase::Begin )
            recoverBegin = firedIndex;
        SW_EXPECT_TRUE( listFired[firedIndex]._pSource == &clip );
    }
    SW_EXPECT_TRUE( swingEnd < recoverBegin );

    // 한 걸음에 반복 경계를 넘어 0.75 → 1.35(감겨 0.35): Recover 끝(0.8) · Step(0.9) · Swing 시작(0.2) 이 그 순서로 한 번씩.
    AnimClipCursor loopCursor;
    loopCursor.reset( 0.75f );
    (void)loopCursor.advance( 0.0f, 1.0f, true );
    listFired.clear();
    track.collectFired( loopCursor.advance( 0.6f, 1.0f, true ), 1.0f, 1.0f, &clip, listFired );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listFired.size() ) );
    SW_EXPECT_TRUE( listFired[0]._name == hashed_string( "Recover" ) && listFired[0]._phase == AnimNotifyPhase::End );
    SW_EXPECT_TRUE( listFired[1]._name == hashed_string( "Step" ) && listFired[1]._phase == AnimNotifyPhase::Instant );
    SW_EXPECT_TRUE( listFired[2]._name == hashed_string( "Swing" ) && listFired[2]._phase == AnimNotifyPhase::Begin );
}

/**
 * @brief [AnimNotifyTest] 알림 표는 모르는 처리기 · 모르는 인자 · 빠진 필수 인자 · 숫자가 아닌 숫자 · 겹친 알림을 읽기 오류로 막는다
 */
SW_TEST_CASE( AnimNotifyTest, TableRejectsUnknownHandlerAndArguments )
{
    const AnimNotifyHandlerRegistry& registry = AnimNotifyHandlerRegistry::getDefault();
    AnimNotifyTable                  table;
    SW_ASSERT_TRUE( table.loadFromXmlText( R"(<AnimNotifies>
            <Notify name="FootL" handler="Footstep" socket="foot.l" distance="0.4" sound="common/audio/step_{surface}.wav"/>
            <Notify name="Swing" handler="HitWindow" socketA="hand.r" socketB="SwordTip" radius="0.05" damage="10"/>
            <Notify name="Shake" handler="CameraShake" amplitude="0.1"/>
            <Notify name="Taunt" handler="GameplayEvent" event="Taunted"/>
        </AnimNotifies>)",
                                           "good", registry ) );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( table.getEntries().size() ) );
    const AnimNotifyEntry* pSwing = table.findEntry( hashed_string( "Swing" ) );
    SW_ASSERT_NOT_NULL( pSwing );
    SW_EXPECT_TRUE( pSwing->_pHandler == registry.findHandler( hashed_string( "HitWindow" ) ) );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pSwing->getFloatParam( hashed_string( "damage" ), 0.0f ), 1e-6f );
    SW_EXPECT_TRUE( pSwing->getNameParam( hashed_string( "socketB" ) ) == hashed_string( "SwordTip" ) );

    AnimNotifyTable bad;
    SW_EXPECT_FALSE( bad.loadFromXmlText( R"(<AnimNotifies><Notify name="A" handler="NoSuchHandler"/></AnimNotifies>)", "unknownHandler", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXmlText( R"(<AnimNotifies><Notify name="A" handler="CameraShake" amplitde="0.1"/></AnimNotifies>)", "typo", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXmlText( R"(<AnimNotifies><Notify name="A" handler="Footstep"/></AnimNotifies>)", "missingRequired", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXmlText( R"(<AnimNotifies><Notify name="A" handler="CameraShake" amplitude="big"/></AnimNotifies>)", "notNumber", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXmlText(
        R"(<AnimNotifies><Notify name="A" handler="CameraShake"/><Notify name="A" handler="CameraShake"/></AnimNotifies>)", "duplicate", registry ) );
    SW_EXPECT_FALSE( bad.loadFromXmlText( R"(<AnimNotifies><Shake name="A" handler="CameraShake"/></AnimNotifies>)", "unknownElement", registry ) );
}

/**
 * @brief [AnimNotifyTest] 디스패치 — 구간은 시작 → 프레임마다 틱 → 끝이 한 번씩, 클립이 재생에서 빠지면 끝을 대신 내고, 표에 없는 알림은 건너뛴다
 */
SW_TEST_CASE( AnimNotifyTest, DispatchRunsStateHandlersOnceAndClosesInterruptedStates )
{
    vector<string>            listLog;
    AnimNotifyHandlerRegistry registry;
    registry.registerHandler( hashed_string( "Record" ), sw::make_unique<TestAnimNotifyInternal::RecordingHandler>( &listLog ) );
    shared_ptr<AnimNotifyTable> table = make_shared<AnimNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXmlText( R"(<AnimNotifies><Notify name="Swing" handler="Record"/><Notify name="Ping" handler="Record" tag="x"/></AnimNotifies>)",
                                            "record", registry ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Dispatcher" ) );
    SW_ASSERT_NOT_NULL( pObject );
    AnimNotifyComponent* pNotify = pObject->addComponent<AnimNotifyComponent>();
    SW_ASSERT_NOT_NULL( pNotify );
    pNotify->setNotifyTable( table );

    test::TestPlayable                 clip( 1.0f, true );
    test::TestPlayable                 otherClip( 1.0f, true );
    const vector<const IAnimPlayable*> listActive{ &clip };
    vector<AnimFiredNotify>            listFired{ TestAnimNotifyInternal::makeFired( "Swing", &clip, 0, AnimNotifyPhase::Begin ),
                                       TestAnimNotifyInternal::makeFired( "Ping", &clip, 1, AnimNotifyPhase::Instant ),
                                       TestAnimNotifyInternal::makeFired( "Unlisted", &clip, 2, AnimNotifyPhase::Instant ) };
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );
    SW_EXPECT_EQUAL( 1u, pNotify->getActiveStateCount() );
    listFired.clear();
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );
    listFired.push_back( TestAnimNotifyInternal::makeFired( "Swing", &clip, 0, AnimNotifyPhase::End ) );
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );
    SW_EXPECT_EQUAL( 0u, pNotify->getActiveStateCount() );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countLog( listLog, "Begin:Swing" ) );
    SW_EXPECT_EQUAL( 3u, TestAnimNotifyInternal::countLog( listLog, "Tick:Swing" ) );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countLog( listLog, "End:Swing" ) );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countLog( listLog, "Notify:Ping" ) );
    SW_EXPECT_EQUAL( 0u, TestAnimNotifyInternal::countLog( listLog, "Notify:Unlisted" ) );

    // 끊김 — 구간이 열린 채 클립이 재생에서 빠지면(상태 전이) 다음 프레임에 End 를 대신 낸다. 그 뒤 끝 알림이 와도 두 번 닫지 않는다.
    listLog.clear();
    listFired.assign( 1, TestAnimNotifyInternal::makeFired( "Swing", &clip, 0, AnimNotifyPhase::Begin ) );
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );
    listFired.assign( 1, TestAnimNotifyInternal::makeFired( "Swing", &clip, 0, AnimNotifyPhase::End ) );
    const vector<const IAnimPlayable*> listOther{ &otherClip };
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listOther ) );
    SW_EXPECT_EQUAL( 0u, pNotify->getActiveStateCount() );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countLog( listLog, "End:Swing" ) );
    SW_EXPECT_EQUAL( 0u, TestAnimNotifyInternal::countLog( listLog, "Tick:Swing" ) );

    // 컴포넌트가 끝나면 열린 구간도 닫는다.
    listLog.clear();
    listFired.assign( 1, TestAnimNotifyInternal::makeFired( "Swing", &clip, 0, AnimNotifyPhase::Begin ) );
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );
    pNotify->closeAllStates();
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countLog( listLog, "End:Swing" ) );
}

/**
 * @brief [AnimNotifyTest] 애니메이터 경로 — 클립의 구간 알림이 시스템 평가마다 받는 쪽으로 가서 시작 · 틱 · 끝이 한 번씩, 재생을 멈추면 끝을 대신 낸다
 */
SW_TEST_CASE( AnimNotifyTest, AnimatorDeliversClipNotifiesToDispatcher )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const Skeleton    skeleton = test::makeChainSkeleton( 2 );
    const AnimRawClip raw      = test::makeChainRawClip( skeleton, 31, 30.0f, 0.2f );
    AnimClip          clip;
    clip.setName( hashed_string( "Attack" ) );
    clip.setLooping( true );
    clip.addNotify( AnimNotifyEvent{ hashed_string( "Swing" ), 0.2f, 0.3f } );
    clip.addNotify( AnimNotifyEvent{ hashed_string( "Ping" ), 0.1f, 0.0f } );
    SW_ASSERT_TRUE( clip.compressFrom( raw, RawAnimCodec::getInstance(), AnimCodecSettings{}, nullptr ) );
    const string folder = test::makeTempPath( "notifyclips" );
    SW_ASSERT_TRUE( clip.saveToFile( FileUtil::joinPath( folder, "attack.animclip" ) ) );

    vector<string>            listLog;
    AnimNotifyHandlerRegistry registry;
    registry.registerHandler( hashed_string( "Record" ), sw::make_unique<TestAnimNotifyInternal::RecordingHandler>( &listLog ) );
    shared_ptr<AnimNotifyTable> table = make_shared<AnimNotifyTable>();
    SW_ASSERT_TRUE(
        table->loadFromXmlText( R"(<AnimNotifies><Notify name="Swing" handler="Record"/><Notify name="Ping" handler="Record"/></AnimNotifies>)", "record", registry ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Fighter" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
    AnimNotifyComponent*       pNotify   = pObject->addComponent<AnimNotifyComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr && pNotify != nullptr );
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    pAnimator->setClipFolder( folder );
    pAnimator->setInitialState( "Attack" );
    pAnimator->dispatchBeginPlay();
    pNotify->dispatchBeginPlay();
    pNotify->setNotifyTable( table );
    SW_EXPECT_TRUE( pAnimator->getNotifyListener() != nullptr );

    // 0.05 초씩 열 번(0.5 초) — Ping(0.1) 한 번, Swing 은 0.2 에 열리고 0.5 에 닫힌다.
    for ( uint32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        manager.getAnimationSystem().evaluate( 0.05f );
    }
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countLog( listLog, "Notify:Ping" ) );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countLog( listLog, "Begin:Swing" ) );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countLog( listLog, "End:Swing" ) );
    SW_EXPECT_TRUE( TestAnimNotifyInternal::countLog( listLog, "Tick:Swing" ) >= 4u );

    // 다음 바퀴의 구간 안(0.3 초)에서 멈추면 끝을 대신 낸다.
    listLog.clear();
    for ( uint32 frameIndex = 0; frameIndex < 16; ++frameIndex )
    {
        manager.getAnimationSystem().evaluate( 0.05f );
    }
    SW_EXPECT_EQUAL( 1u, pNotify->getActiveStateCount() );
    pAnimator->stop();
    manager.getAnimationSystem().evaluate( 0.05f );
    SW_EXPECT_EQUAL( 0u, pNotify->getActiveStateCount() );
    SW_EXPECT_EQUAL( 1u, TestAnimNotifyInternal::countLog( listLog, "End:Swing" ) );

    // 컴포넌트가 끝나면 받는 쪽을 뗀다.
    pNotify->dispatchEndPlay();
    SW_EXPECT_TRUE( pAnimator->getNotifyListener() == nullptr );
}

/**
 * @brief [AnimNotifyTest] 내장 처리기 — 발소리는 발 아래 물리 재질, 칼 궤적은 지난 → 지금 칼날을 쓸어 상대를 한 번만(자기 바디는 건너뜀) 히트 존과 함께, 이벤트 · 흔들림
 * @details 공격자: 사슬 스켈레톤(뼈 1 · 2 가 1 m · 2 m 위 — 칼날)과 자기 둘레의 키네마틱 상자. 표적: 칼날 높이의 정적 상자(히트 존 Head ×2). 공격자를 -2 m → +2 m 로
 *          옮기면 칼날이 표적을 지난다. 바닥은 Stone 재질.
 */
SW_TEST_CASE( AnimNotifyTest, BuiltInHandlersFindSurfaceHitTargetsOnceAndRaiseEvents )
{
    GameObjectManager   manager;
    RigidBodyComponent* pFloor  = TestAnimNotifyInternal::spawnBox( manager, "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 10.0f, 0.5f, 10.0f },
                                                                    PhysicsBodyType::Static, "Stone" );
    RigidBodyComponent* pTarget = TestAnimNotifyInternal::spawnBox( manager, "Target", float3{ 0.0f, 1.5f, 3.0f }, float3{ 0.3f, 0.3f, 0.3f },
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
    AnimNotifyComponent*      pNotify       = pAttacker->addComponent<AnimNotifyComponent>();
    SW_ASSERT_TRUE( pSelfListener != nullptr && pNotify != nullptr );
    pSelfBody->setLocalPosition( float3{ -2.0f, 0.0f, 3.0f } );

    shared_ptr<AnimNotifyTable> table = make_shared<AnimNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXmlText( R"(<AnimNotifies>
            <Notify name="Step" handler="Footstep" socket="bone0" distance="0.6"/>
            <Notify name="Swing" handler="HitWindow" socketA="bone1" socketB="bone2" damage="10" impulse="5"/>
            <Notify name="Taunt" handler="GameplayEvent" event="Taunted"/>
            <Notify name="Impact" handler="CameraShake" amplitude="0.2" duration="0.4" socket="bone2"/>
        </AnimNotifies>)",
                                            "builtins", AnimNotifyHandlerRegistry::getDefault() ) );
    pNotify->setNotifyTable( table );

    manager.beginPlay();
    manager.tick( TestAnimNotifyInternal::kFrame ); // 바디를 만든다

    vector<CameraShakeRequest> listShake;
    const DelegateHandle       shakeHandle =
        AnimNotifyHandlerUtil::getCameraShakeRequested().add( SW_DELEGATE_LAMBDA( CameraShakeRequestCallback, [&listShake]( const CameraShakeRequest& request )
    { listShake.push_back( request ); } ) );

    test::TestPlayable                 clip( 1.0f, true );
    const vector<const IAnimPlayable*> listActive{ &clip };
    vector<AnimFiredNotify>            listFired{ TestAnimNotifyInternal::makeFired( "Step", &clip, 0, AnimNotifyPhase::Instant ),
                                       TestAnimNotifyInternal::makeFired( "Swing", &clip, 1, AnimNotifyPhase::Begin ),
                                       TestAnimNotifyInternal::makeFired( "Taunt", &clip, 2, AnimNotifyPhase::Instant ),
                                       TestAnimNotifyInternal::makeFired( "Impact", &clip, 3, AnimNotifyPhase::Instant ) };
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );

    // 발소리: 발(뼈 0 = 오브젝트 원점) 아래 바닥 재질. 자기 상자(원점을 감싼다)는 건너뛴다.
    bool bFoundStep = false;
    for ( const AnimNotifyAction& action : pNotify->getActions() )
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
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );
    pAttacker->getPrimarySceneComponent()->setWorldPosition( float3{ -2.0f, 0.0f, 3.0f } );
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );
    listFired.assign( 1, TestAnimNotifyInternal::makeFired( "Swing", &clip, 1, AnimNotifyPhase::End ) );
    pNotify->processFrame( TestAnimNotifyInternal::makeFrame( listFired, listActive ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pTargetListener->_listHit.size() ) );
    const HitInfo& hit = pTargetListener->_listHit[0];
    SW_EXPECT_TRUE( hit._zone == hashed_string( "Head" ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, hit._damageMultiplier, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, hit._damage, 1e-6f );
    SW_EXPECT_TRUE( hit._pInstigator == pAttacker );
    SW_EXPECT_TRUE( hit._kind == hashed_string( "Swing" ) );
    SW_EXPECT_TRUE( hit._direction._x > 0.9f );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pSelfListener->_listHit.size() ) ); // 자기 바디는 칼날 안에 있어도 맞지 않는다

    AnimNotifyHandlerUtil::getCameraShakeRequested().remove( shakeHandle );
    manager.endPlay();
}
