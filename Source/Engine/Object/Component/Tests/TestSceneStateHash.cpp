// 씬 상태 해시 — 컴포넌트가 접는 상태(회전 · 리플렉션 상태 칸), 실수 정규화(-0.0 · NaN), 순회 순서와 무관한 정렬(NaN 이 있어도).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/Tests/StateHashTestComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tests/SceneIterationTestAccess.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Scene/SceneObjectHash.h"

#include <limits>

using namespace sw;

namespace
{
    SceneComponent* addStateHashTestObject( GameObjectManager& manager, const utf8* pName, const float3& position )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        pObject->addComponent<SceneComponent>();
        SceneComponent* pRoot = pObject->getComponent<SceneComponent>();
        pRoot->setLocalPosition( position );
        return pRoot;
    }
} // namespace

/**
 * @brief [SceneStateHashTest] 실수는 -0.0 과 +0.0 을 같게, NaN 은 페이로드와 무관하게 같게 접는다
 */
SW_TEST_CASE( SceneStateHashTest, BuilderNormalizesSignedZeroAndNan )
{
    StateHashBuilder positiveZero;
    positiveZero.addFloat( 0.0f );
    StateHashBuilder negativeZero;
    negativeZero.addFloat( -0.0f );
    SW_EXPECT_EQUAL( positiveZero.getHash(), negativeZero.getHash() );

    StateHashBuilder quietNan;
    quietNan.addFloat( std::numeric_limits<float32>::quiet_NaN() );
    StateHashBuilder payloadNan;
    payloadNan.addFloat( BitUtil::bitsToFloat( 0xFFC12345u ) );
    SW_EXPECT_EQUAL( quietNan.getHash(), payloadNan.getHash() );

    StateHashBuilder one;
    one.addFloat( 1.0f );
    SW_EXPECT_TRUE( one.getHash() != positiveZero.getHash() );
}

/**
 * @brief [SceneStateHashTest] 위치만이 아니라 회전도 해시를 바꾼다 — 되돌리면 같은 값이다
 */
SW_TEST_CASE( SceneStateHashTest, RotationChangesTheHash )
{
    GameObjectManager manager;
    SceneComponent*   pRoot   = addStateHashTestObject( manager, "Turret", float3{ 1.0f, 0.0f, 2.0f } );
    const uint64      initial = SceneObjectHash::compute( SceneIterationTestAccess::getKey(), manager );
    pRoot->setLocalRotation( float3{ 0.0f, 0.5f, 0.0f } );
    SW_EXPECT_TRUE( SceneObjectHash::compute( SceneIterationTestAccess::getKey(), manager ) != initial );
    pRoot->setLocalRotation( float3{ 0.0f, 0.0f, 0.0f } );
    SW_EXPECT_EQUAL( initial, SceneObjectHash::compute( SceneIterationTestAccess::getKey(), manager ) );
    pRoot->setLocalPosition( float3{ 1.0f, -0.0f, 2.0f } );
    SW_EXPECT_EQUAL( initial, SceneObjectHash::compute( SceneIterationTestAccess::getKey(), manager ) );
}

/**
 * @brief [SceneStateHashTest] NaN 위치의 오브젝트가 있어도 만든 순서와 무관하게 같은 해시다(이름은 만들 때 유일해지므로 정렬은 이름으로 정해지고 실수 비교가 끼지 않는다)
 */
SW_TEST_CASE( SceneStateHashTest, NanPositionIsIndependentOfCreationOrder )
{
    const float32     nan = std::numeric_limits<float32>::quiet_NaN();
    GameObjectManager first;
    addStateHashTestObject( first, "CrateA", float3{ 1.0f, 0.0f, 0.0f } );
    addStateHashTestObject( first, "CrateB", float3{ nan, 0.0f, 0.0f } );
    GameObjectManager second;
    addStateHashTestObject( second, "CrateB", float3{ nan, 0.0f, 0.0f } );
    addStateHashTestObject( second, "CrateA", float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_EQUAL( SceneObjectHash::compute( SceneIterationTestAccess::getKey(), first ), SceneObjectHash::compute( SceneIterationTestAccess::getKey(), second ) );
}

/**
 * @brief [SceneStateHashTest] 실행 중에 세운 파생 오브젝트(`GameObject::setTransient`)는 상태가 아니다 — 움직여도 해시가 그대로다
 * @details 외형 조립의 부품은 워커가 끝낸 프레임에 붙어 자리가 실행마다 달랐다(game.shooter3d.slice 의 두 번 불러오기 해시). 표시를 지우면 진다.
 */
SW_TEST_CASE( SceneStateHashTest, TransientObjectsAreNotState )
{
    GameObjectManager manager;
    (void)addStateHashTestObject( manager, "Player", float3{ 0.0f, 0.0f, 0.0f } );
    SceneComponent* pPart = addStateHashTestObject( manager, "Helmet", float3{ 0.0f, 1.8f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pPart );
    pPart->getOwner()->setTransient( true );
    const uint64 before = SceneObjectHash::compute( SceneIterationTestAccess::getKey(), manager );
    pPart->setWorldPosition( float3{ 0.3f, 1.7f, 0.1f } );
    SW_EXPECT_EQUAL( SceneObjectHash::compute( SceneIterationTestAccess::getKey(), manager ), before );
    vector<SceneObjectHash> listObject;
    SceneObjectHash::collectObjects( SceneIterationTestAccess::getKey(), manager, listObject );
    SW_REQUIRE_EQUAL( listObject.size(), static_cast<size_t>( 1 ) );
    SW_EXPECT_TRUE( listObject[0]._name == "Player" );
}

namespace
{
    /**
     * @brief 리플렉션 값 하나를 다른 값으로 바꿉니다(구조체는 첫 칸, 시퀀스는 원소 하나 더). 바꿀 수 없는 형식이면 false 입니다.
     * @details 테스트가 형식을 손으로 나열하는 대신 상태 해시가 접는 형식 표와 같은 범위를 바꾼다 — 바꿀 수 없으면 실패로 드러난다.
     */
    bool changeStateValue( const TypeRegistry& registry, const PropertyInfo& prop, void* pObject, const hashed_string& typeName, void* pValue )
    {
        if ( prop._bIsBitField == SW_TRUE )
        {
            prop.setValue( pObject, prop.getValue<bool>( pObject ) == false );
            return true;
        }
        if ( prop._bIsContainer == SW_TRUE )
        {
            const ISequenceContainerWrapper* pSequence = prop._containerWrapper != nullptr ? prop._containerWrapper->asSequence() : nullptr;
            if ( pSequence == nullptr )
                return false;
            pSequence->addElementDefault( pValue );
            return true;
        }
        const string_view name = typeName.c_str();
        if ( name == "bool" )
        {
            *static_cast<bool*>( pValue ) = *static_cast<bool*>( pValue ) == false;
            return true;
        }
        const bool bFloat = name == "float32" || name == "float2" || name == "float3" || name == "float4" || name == "quaternion";
        if ( bFloat )
        {
            *static_cast<float32*>( pValue ) += 1.0f;
            return true;
        }
        if ( name == "float64" )
        {
            *static_cast<float64*>( pValue ) += 1.0;
            return true;
        }
        if ( name == "string" )
        {
            *static_cast<string*>( pValue ) += "x";
            return true;
        }
        if ( const EnumInfo* pEnum = registry.findEnum( typeName ) )
        {
            pEnum->writeValueToMemory( pValue, pEnum->readValueFromMemory( pValue ) + 1 );
            return true;
        }
        const TypeInfo* pType = registry.findType( typeName );
        if ( pType == nullptr )
            return false;
        if ( pType->isPrimitive() )
        {
            ++*static_cast<uint8*>( pValue ); // 남은 내장 스칼라는 정수다 — 낮은 바이트(리틀 엔디언)를 바꾼다
            return true;
        }
        const PropertyListView listInner = pType->getPropertiesWithBase();
        if ( listInner.begin() == listInner.end() )
            return false;
        const PropertyInfo& inner = *listInner.begin();
        return changeStateValue( registry, inner, pValue, inner._typeName, inner.getRawPtr( pValue ) );
    }

    uint64 foldComponentState( const Component& component )
    {
        StateHashBuilder builder;
        component.foldStateHash( builder );
        return builder.getHash();
    }
} // namespace

/**
 * @brief [StateHashTest] 상태 칸(`RuntimeState`)을 가진 모든 컴포넌트 타입의 모든 상태 칸이 하나씩 바뀔 때마다 해시가 바뀐다 — 새 칸이 해시에서 빠질 수 없다
 * @details 손으로 쓴 `foldStateHash` 만 있던 때는 상태 칸을 더해도 아무것도 지지 않았다. 상태가 아닌 칸(`_tuning`)은 해시를 바꾸지 않는다.
 */
SW_TEST_CASE( StateHashTest, EveryStateFieldChangesTheHash )
{
    const TypeRegistry&   registry   = engine::getSubsystem<TypeRegistry>();
    const TypeInfo*       pComponent = registry.findType( hashed_string( "sw::Component" ) );
    vector<hashed_string> listStateType;
    SW_REQUIRE_NOT_NULL( pComponent );
    registry.forEachType( [&]( const TypeInfo& type )
    {
        if ( type._bAbstract == SW_TRUE || type.isDerivedFrom( pComponent ) == false || type._addComponent == nullptr )
            return;
        for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            if ( prop._metadata->_bRuntimeState == SW_TRUE )
            {
                listStateType.push_back( type._fullyQualifiedName );
                return;
            }
        }
    } );
    SW_REQUIRE_TRUE( VectorUtil::contains( listStateType, hashed_string( "sw::StateHashTestComponent" ) ) );

    for ( const hashed_string& typeName : listStateType )
    {
        const TypeInfo& type = *registry.findType( typeName );
        for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            if ( prop._metadata->_bRuntimeState == SW_FALSE )
                continue;
            GameObjectManager manager;
            GameObject*       pObject   = manager.createGameObject( hashed_string( "StateHashProbe" ) );
            Component*        pInstance = manager.addComponentByName( pObject, typeName );
            SW_REQUIRE_TRUE_MSG( pInstance != nullptr, typeName.c_str() );
            const uint64 before   = foldComponentState( *pInstance );
            void*        pAddress = static_cast<void*>( pInstance );
            SW_REQUIRE_TRUE_MSG( changeStateValue( registry, prop, pAddress, prop._typeName, prop.getRawPtr( pAddress ) ), prop._name.c_str() );
            SW_EXPECT_TRUE_MSG( foldComponentState( *pInstance ) != before, prop._name.c_str() );
        }
    }

    // 상태가 아닌 칸은 해시를 바꾸지 않는다.
    GameObjectManager       manager;
    GameObject*             pObject = manager.createGameObject( hashed_string( "StateHashProbe" ) );
    StateHashTestComponent* pProbe  = static_cast<StateHashTestComponent*>( manager.addComponentByName( pObject, hashed_string( "sw::StateHashTestComponent" ) ) );
    SW_REQUIRE_NOT_NULL( pProbe );
    const uint64 before = foldComponentState( *pProbe );
    pProbe->_tuning += 1.0f;
    SW_EXPECT_EQUAL( foldComponentState( *pProbe ), before );
}
