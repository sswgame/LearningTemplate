#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Physics/Asset/PhysicsRagdoll.h"

using namespace sw;

// 래그돌 빌더(PhysicsRagdoll) — 바디 역할(`IPhysicsBodyScene3D`)만 받는 함수를 가짜 씬으로 본다. 물리 백엔드 없음.
// `create` · `driveToPoseWithMotors` 는 씬 전체(`IPhysicsScene3D`)를 받아 실제 씬이 있어야 하므로 여기서 다루지 않는다.

namespace
{
    /** @brief 바디 자세와 호출 수만 기억하는 바디 씬입니다. 빌더가 부르지 않는 함수는 아무것도 하지 않습니다. */
    class PhysicsRagdollTestBodyScene final : public IPhysicsBodyScene3D
    {
    public:
        struct BodyState
        {
            float3          _position;
            quaternion      _rotation;
            PhysicsBodyType _type;
            uint32          _moveCount;
            uint32          _teleportCount;
            float32         _lastDeltaTime;
            uint8           _bEnabled  : 1;
            uint8           _bReadable : 1;

            BodyState()
                : _position{}
                , _rotation{ quaternion::Identity }
                , _type{ PhysicsBodyType::Dynamic }
                , _moveCount{ 0 }
                , _teleportCount{ 0 }
                , _lastDeltaTime{ 0.0f }
                , _bEnabled{ SW_TRUE }
                , _bReadable{ SW_TRUE }
            {
            }
        };

        vector<BodyState>         _listState{};
        vector<PhysicsBodyHandle> _listDestroyed{};

        /** @brief 바디 하나를 더하고 핸들을 돌려줍니다. 슬롯 번호는 `_listState` 의 번호입니다. */
        PhysicsBodyHandle addBody( const float3& position, PhysicsBodyType type )
        {
            BodyState state;
            state._position = position;
            state._type     = type;
            _listState.push_back( state );
            return PhysicsBodyHandle::fromSlot( SlotHandle::make( static_cast<uint32>( _listState.size() - 1 ), 1 ) );
        }

        BodyState& getState( PhysicsBodyHandle body ) { return _listState[body.getSlot().index()]; }

        // --- 빌더가 부르는 함수 -----------------------------------------------------------------------------------------

        bool isBodyEnabled( PhysicsBodyHandle body ) const override { return _listState[body.getSlot().index()]._bEnabled == SW_TRUE; }
        bool getBodyTransform( PhysicsBodyHandle body, float3& outPosition, quaternion& outRotation ) const override
        {
            const BodyState& state = _listState[body.getSlot().index()];
            if ( state._bReadable == SW_FALSE )
                return false;
            outPosition = state._position;
            outRotation = state._rotation;
            return true;
        }
        void setBodyTransform( PhysicsBodyHandle body, const float3& position, const quaternion& rotation ) override
        {
            BodyState& state = getState( body );
            state._position  = position;
            state._rotation  = rotation;
            ++state._teleportCount;
        }
        void moveKinematic( PhysicsBodyHandle body, const float3& targetPosition, const quaternion& targetRotation, float32 deltaTime ) override
        {
            BodyState& state     = getState( body );
            state._position      = targetPosition;
            state._rotation      = targetRotation;
            state._lastDeltaTime = deltaTime;
            ++state._moveCount;
        }
        void            setBodyType( PhysicsBodyHandle body, PhysicsBodyType type ) override { getState( body )._type = type; }
        PhysicsBodyType getBodyType( PhysicsBodyHandle body ) const override { return _listState[body.getSlot().index()]._type; }
        void            destroyBodies( span<const PhysicsBodyHandle> listBody ) override
        {
            for ( const PhysicsBodyHandle& body : listBody )
            {
                _listDestroyed.push_back( body );
            }
        }

        // --- 빌더가 부르지 않는 함수 ----------------------------------------------------------------------------------------

        PhysicsShapeHandle               createShape( span<const ShapeDesc>, const hashed_string& ) override { return PhysicsShapeHandle{}; }
        PhysicsShapeHandle               createCompoundShape( span<const PhysicsShapeHandle> ) override { return PhysicsShapeHandle{}; }
        void                             destroyShape( PhysicsShapeHandle ) override {}
        PhysicsBodyHandle                createBody( const BodyDesc& ) override { return PhysicsBodyHandle{}; }
        void                             createBodies( span<const BodyDesc>, vector<PhysicsBodyHandle>& ) override {}
        void                             destroyBody( PhysicsBodyHandle ) override {}
        bool                             setBodyShape( PhysicsBodyHandle, span<const ShapeDesc>, const hashed_string&, float32 ) override { return true; }
        bool                             isBodyValid( PhysicsBodyHandle ) const override { return true; }
        uint32                           getBodyCount() const override { return static_cast<uint32>( _listState.size() ); }
        void                             setBodyEnabled( PhysicsBodyHandle, bool ) override {}
        const vector<PhysicsBodyHandle>& getMovedBodies() const override { return _listDestroyed; }
        void                             readBodyStates( span<const PhysicsBodyHandle>, span<PhysicsBodyState3D> ) const override {}
        float3                           getLinearVelocity( PhysicsBodyHandle ) const override { return float3{}; }
        void                             setLinearVelocity( PhysicsBodyHandle, const float3& ) override {}
        float3                           getAngularVelocity( PhysicsBodyHandle ) const override { return float3{}; }
        void                             setAngularVelocity( PhysicsBodyHandle, const float3& ) override {}
        void                             addForce( PhysicsBodyHandle, const float3& ) override {}
        void                             addImpulse( PhysicsBodyHandle, const float3& ) override {}
        void                             addImpulseAtPoint( PhysicsBodyHandle, const float3&, const float3& ) override {}
        void                             addTorque( PhysicsBodyHandle, const float3& ) override {}
        void                             setBodyLayer( PhysicsBodyHandle, uint8 ) override {}
        uint8                            getBodyLayer( PhysicsBodyHandle ) const override { return 0; }
        void                             setGravityFactor( PhysicsBodyHandle, float32 ) override {}
        float32                          getBodyMass( PhysicsBodyHandle ) const override { return 0.0f; }
        bool                             isBodySleeping( PhysicsBodyHandle ) const override { return false; }
        void                             wakeBody( PhysicsBodyHandle ) override {}
        uint64                           getBodyUserData( PhysicsBodyHandle ) const override { return 0; }
        bool                             isBodyTrigger( PhysicsBodyHandle ) const override { return false; }
        void                             setPairCollision( PhysicsBodyHandle, PhysicsBodyHandle, bool ) override {}
        bool                             drawBodyDebug( PhysicsBodyHandle, IPhysicsDebugRenderer& ) const override { return false; }
    };

    /**
     * @brief 뼈 넷의 스켈레톤과 바디 둘의 래그돌입니다. 뼈는 root(0, 바디 0) ← spine(1, 바디 없음) ← hand(2, 바디 1) ← finger(3, 바디 없음)이고 y 로 늘어섭니다.
     * @details 손은 배율 2 입니다. 월드 행렬은 x 로 10 옮깁니다.
     */
    struct PhysicsRagdollTestFixture
    {
        vector<hashed_string>       _listName{};
        vector<int32>               _listParent{};
        vector<float4x4>            _listModel{};
        float4x4                    _worldFromModel{};
        PhysicsRagdollTestBodyScene _scene{};
        PhysicsRagdoll              _ragdoll{};

        explicit PhysicsRagdollTestFixture( PhysicsBodyType type )
        {
            _listName       = { hashed_string( "root" ), hashed_string( "spine" ), hashed_string( "hand" ), hashed_string( "finger" ) };
            _listParent     = { -1, 0, 1, 2 };
            _worldFromModel = float4x4::makeTranslation( 10.0f, 0.0f, 0.0f );
            _listModel.push_back( float4x4::makeTranslation( 0.0f, 0.0f, 0.0f ) );
            _listModel.push_back( float4x4::makeTranslation( 0.0f, 1.0f, 0.0f ) );
            _listModel.push_back( float4x4::makeTrs( float3{ 0.0f, 2.0f, 0.0f }, quaternion::Identity, float3{ 2.0f, 2.0f, 2.0f } ) );
            _listModel.push_back( float4x4::makeTranslation( 0.0f, 2.5f, 0.0f ) );
            _ragdoll._listBody.push_back( _scene.addBody( float3{}, type ) );
            _ragdoll._listBody.push_back( _scene.addBody( float3{}, type ) );
            _ragdoll._listBoneIndex  = { 0, 2 };
            _ragdoll._listParentBody = { sw::nullopt, 0u };
            _ragdoll._listBodyOfBone = { 0u, sw::nullopt, 1u, sw::nullopt };
        }

        PhysicsSkeletonView makeView() const
        {
            PhysicsSkeletonView view;
            view._listBoneName       = span<const hashed_string>{ _listName.data(), _listName.size() };
            view._listParentIndex    = span<const int32>{ _listParent.data(), _listParent.size() };
            view._listModelSpaceBone = span<const float4x4>{ _listModel.data(), _listModel.size() };
            return view;
        }
    };

    struct PhysicsRagdollTestUtil
    {
        static constexpr float32 kTolerance = 1.0e-4f;

        static void expectNear( const float3& expected, const float3& actual )
        {
            SW_EXPECT_NEAR( expected._x, actual._x, kTolerance );
            SW_EXPECT_NEAR( expected._y, actual._y, kTolerance );
            SW_EXPECT_NEAR( expected._z, actual._z, kTolerance );
        }
    };
} // namespace

/**
 * @brief [PhysicsRagdollTest] 바디 핸들에서 에셋 바디 번호를 찾고, 이 래그돌의 바디가 아니면 -1 이다
 */
SW_TEST_CASE( PhysicsRagdollTest, FindBodyIndexAndIsEmpty )
{
    PhysicsRagdollTestFixture rig( PhysicsBodyType::Dynamic );
    SW_EXPECT_FALSE( rig._ragdoll.isEmpty() );
    SW_EXPECT_TRUE( rig._ragdoll.findBodyIndex( rig._ragdoll._listBody[0] ) == 0u );
    SW_EXPECT_TRUE( rig._ragdoll.findBodyIndex( rig._ragdoll._listBody[1] ) == 1u );
    SW_EXPECT_FALSE( rig._ragdoll.findBodyIndex( PhysicsBodyHandle{} ).has_value() );
    SW_EXPECT_FALSE( rig._ragdoll.findBodyIndex( rig._scene.addBody( float3{}, PhysicsBodyType::Dynamic ) ).has_value() );
    SW_EXPECT_TRUE( PhysicsRagdoll{}.isEmpty() );
}

/**
 * @brief [PhysicsRagdollTest] 지우기는 유효한 핸들만 씬에 넘기고 래그돌을 비운다
 */
SW_TEST_CASE( PhysicsRagdollTest, DestroySkipsInvalidHandlesAndEmpties )
{
    PhysicsRagdollTestFixture rig( PhysicsBodyType::Dynamic );
    const PhysicsBodyHandle   first = rig._ragdoll._listBody[0];
    rig._ragdoll._listBody[1]       = PhysicsBodyHandle{};
    PhysicsRagdoll::destroy( rig._scene, rig._ragdoll );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( rig._scene._listDestroyed.size() ) );
    SW_EXPECT_TRUE( rig._scene._listDestroyed[0] == first );
    SW_EXPECT_TRUE( rig._ragdoll.isEmpty() );
    SW_EXPECT_TRUE( rig._ragdoll._listBodyOfBone.empty() );
}

/**
 * @brief [PhysicsRagdollTest] 종류 바꾸기는 모든 바디에 건다(히트박스 Kinematic ↔ 래그돌 Dynamic)
 */
SW_TEST_CASE( PhysicsRagdollTest, SetBodyTypeAppliesToEveryBody )
{
    PhysicsRagdollTestFixture rig( PhysicsBodyType::Kinematic );
    PhysicsRagdoll::setBodyType( rig._scene, rig._ragdoll, PhysicsBodyType::Dynamic );
    for ( const PhysicsRagdollTestBodyScene::BodyState& state : rig._scene._listState )
    {
        SW_EXPECT_TRUE( state._type == PhysicsBodyType::Dynamic );
    }
}

/**
 * @brief [PhysicsRagdollTest] Kinematic 바디는 시간 뒤에 닿도록 움직이고, 뼈의 월드 자세로 간다(배율은 버린다)
 */
SW_TEST_CASE( PhysicsRagdollTest, DriveToPoseMovesKinematicBodies )
{
    using Util = PhysicsRagdollTestUtil;
    PhysicsRagdollTestFixture rig( PhysicsBodyType::Kinematic );
    PhysicsRagdoll::driveToPose( rig._scene, rig._ragdoll, rig.makeView(), rig._worldFromModel, 0.25f );

    const PhysicsRagdollTestBodyScene::BodyState& root = rig._scene._listState[0];
    const PhysicsRagdollTestBodyScene::BodyState& hand = rig._scene._listState[1];
    SW_EXPECT_EQUAL( 1u, root._moveCount );
    SW_EXPECT_EQUAL( 0u, root._teleportCount );
    SW_EXPECT_NEAR( 0.25f, root._lastDeltaTime, 1.0e-6f );
    Util::expectNear( float3{ 10.0f, 0.0f, 0.0f }, root._position );
    Util::expectNear( float3{ 10.0f, 2.0f, 0.0f }, hand._position );
}

/**
 * @brief [PhysicsRagdollTest] 시간이 0 이면 Kinematic 도 순간이동하고, Dynamic 은 시간과 무관하게 순간이동한다
 */
SW_TEST_CASE( PhysicsRagdollTest, DriveToPoseTeleportsOtherwise )
{
    {
        PhysicsRagdollTestFixture rig( PhysicsBodyType::Kinematic );
        PhysicsRagdoll::driveToPose( rig._scene, rig._ragdoll, rig.makeView(), rig._worldFromModel, 0.0f );
        SW_EXPECT_EQUAL( 0u, rig._scene._listState[0]._moveCount );
        SW_EXPECT_EQUAL( 1u, rig._scene._listState[0]._teleportCount );
    }
    {
        PhysicsRagdollTestFixture rig( PhysicsBodyType::Dynamic );
        PhysicsRagdoll::driveToPose( rig._scene, rig._ragdoll, rig.makeView(), rig._worldFromModel, 0.25f );
        SW_EXPECT_EQUAL( 0u, rig._scene._listState[1]._moveCount );
        SW_EXPECT_EQUAL( 1u, rig._scene._listState[1]._teleportCount );
    }
}

/**
 * @brief [PhysicsRagdollTest] 시뮬레이션에서 뺀 바디(잘려 나간 영역)와 스켈레톤 밖 뼈의 바디는 움직이지 않는다
 */
SW_TEST_CASE( PhysicsRagdollTest, DriveToPoseSkipsDisabledAndOutOfRange )
{
    PhysicsRagdollTestFixture rig( PhysicsBodyType::Kinematic );
    rig._scene._listState[0]._bEnabled = SW_FALSE;
    rig._ragdoll._listBoneIndex[1]     = 99;
    PhysicsRagdoll::driveToPose( rig._scene, rig._ragdoll, rig.makeView(), rig._worldFromModel, 0.25f );
    for ( const PhysicsRagdollTestBodyScene::BodyState& state : rig._scene._listState )
    {
        SW_EXPECT_EQUAL( 0u, state._moveCount );
        SW_EXPECT_EQUAL( 0u, state._teleportCount );
    }
}

/**
 * @brief [PhysicsRagdollTest] 바디 있는 뼈는 바디의 월드 자세를 모델 공간으로 돌려 쓰되 배율은 입력 포즈의 것이고, 바디 없는 뼈는 입력의 부모 상대 변환을 새 부모에 붙인다
 */
SW_TEST_CASE( PhysicsRagdollTest, ReadBoneTransformsFollowsBodiesAndParents )
{
    using Util = PhysicsRagdollTestUtil;
    PhysicsRagdollTestFixture rig( PhysicsBodyType::Dynamic );
    const quaternion          handRotation = quaternion::makeFromAxisAngle( float3::UnitZ, 1.5707964f );
    rig._scene._listState[0]._position     = float3{ 10.0f, 0.0f, 1.0f }; // 모델 (0, 0, 1)
    rig._scene._listState[1]._position     = float3{ 12.0f, 5.0f, 0.0f }; // 모델 (2, 5, 0)
    rig._scene._listState[1]._rotation     = handRotation;

    vector<float4x4> listOut( rig._listModel.size(), float4x4::Identity );
    PhysicsRagdoll::readBoneTransforms( rig._scene, rig._ragdoll, rig.makeView(), rig._worldFromModel, span<float4x4>{ listOut.data(), listOut.size() } );

    Util::expectNear( float3{ 0.0f, 0.0f, 1.0f }, listOut[0].getTranslation() );
    Util::expectNear( float3{ 0.0f, 1.0f, 1.0f }, listOut[1].getTranslation() ); // 뿌리에서 y 로 1
    Util::expectNear( float3{ 2.0f, 5.0f, 0.0f }, listOut[2].getTranslation() );

    float3     handScale{};
    quaternion handOutRotation{};
    float3     handPosition{};
    SW_REQUIRE_TRUE( listOut[2].decompose( handScale, handOutRotation, handPosition ) );
    Util::expectNear( float3{ 2.0f, 2.0f, 2.0f }, handScale );
    Util::expectNear( float3::transform( float3::UnitX, handRotation ), float3::transform( float3::UnitX, handOutRotation ) );

    // 손가락은 입력에서 손 위 0.5 m 이다(손이 돌면 함께 돈다). 입력의 부모 배율 2 와 새 부모 배율 2 가 상쇄된다.
    Util::expectNear( float3{ 2.0f, 5.0f, 0.0f } + float3::transform( float3{ 0.0f, 0.5f, 0.0f }, handRotation ), listOut[3].getTranslation() );
}

/**
 * @brief [PhysicsRagdollTest] 자세를 읽지 못한 바디의 뼈는 바디 없는 뼈처럼 부모를 따른다. 뿌리면 입력 포즈 그대로다
 */
SW_TEST_CASE( PhysicsRagdollTest, ReadBoneTransformsTreatsUnreadableBodyAsBoneless )
{
    using Util = PhysicsRagdollTestUtil;
    PhysicsRagdollTestFixture rig( PhysicsBodyType::Dynamic );
    rig._scene._listState[0]._bReadable = SW_FALSE;
    rig._scene._listState[1]._bReadable = SW_FALSE;
    vector<float4x4> listOut( rig._listModel.size(), float4x4::Identity );
    PhysicsRagdoll::readBoneTransforms( rig._scene, rig._ragdoll, rig.makeView(), rig._worldFromModel, span<float4x4>{ listOut.data(), listOut.size() } );
    for ( size_t bone = 0; bone < listOut.size(); ++bone )
    {
        Util::expectNear( rig._listModel[bone].getTranslation(), listOut[bone].getTranslation() );
    }
}

/**
 * @brief [PhysicsRagdollTest] 출력 · 스켈레톤 · 래그돌의 뼈 수가 다르면 오류를 남기고 출력을 건드리지 않는다
 */
SW_TEST_CASE( PhysicsRagdollTest, ReadBoneTransformsRejectsBoneCountMismatch )
{
    SW_EXPECT_LOG( "PhysicsRagdoll.ReadBoneTransformsOutputSkeletonRagdoll" );
    using Util = PhysicsRagdollTestUtil;
    PhysicsRagdollTestFixture rig( PhysicsBodyType::Dynamic );
    rig._scene._listState[0]._position = float3{ 50.0f, 0.0f, 0.0f };

    vector<float4x4> listShort( 2, float4x4::Identity );
    PhysicsRagdoll::readBoneTransforms( rig._scene, rig._ragdoll, rig.makeView(), rig._worldFromModel, span<float4x4>{ listShort.data(), listShort.size() } );
    Util::expectNear( float3{}, listShort[0].getTranslation() );

    rig._ragdoll._listBodyOfBone.pop_back();
    vector<float4x4> listOut( rig._listModel.size(), float4x4::Identity );
    PhysicsRagdoll::readBoneTransforms( rig._scene, rig._ragdoll, rig.makeView(), rig._worldFromModel, span<float4x4>{ listOut.data(), listOut.size() } );
    Util::expectNear( float3{}, listOut[0].getTranslation() );
}
