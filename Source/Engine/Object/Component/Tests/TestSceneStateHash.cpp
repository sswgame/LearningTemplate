// 씬 상태 해시 — 컴포넌트가 접는 상태(회전 · 리플렉션 상태 칸), 실수 정규화(-0.0 · NaN), 순회 순서와 무관한 정렬(NaN 이 있어도).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/Tests/StateHashTestComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tests/SceneIterationTestAccess.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
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
#if defined( SW_DO_CHECKS )
    SW_EXPECT_LOG( "SceneComponent.NonFiniteTransform" ); // NaN 위치를 넣는 것이 이 테스트다 — 트랜스폼 입구의 NaN 진단이 이름("CrateB")과 함께 알린다
#endif
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
 * @details 외형 조립의 부품은 워커가 끝낸 프레임에 붙어 자리가 실행마다 달랐다(game.playground.shooter3d_slice 의 두 번 불러오기 해시). 표시를 지우면 진다.
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
     * @details 내장 타입은 표의 State 역할 · Layout 이 정하는 길로 바꾼다(상태 해시가 접는 길과 같은 칸) — 바꿀 수 없으면 실패로 드러난다.
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
        // 내장 타입은 표(`ReflectBuiltins.xxx`)의 State 역할 · Layout 으로 바꾼다 — 상태 해시가 접는 길과 같은 칸이라 형식 이름을 따로 나열하지 않는다.
        const ReflectBuiltinIndex builtinIndex = ReflectedValueUtil::findBuiltinIndex( typeName );
        if ( builtinIndex != ReflectBuiltinIndex::kNone )
        {
            const ReflectBuiltinTraits traits = ReflectBuiltinTraitTable::get( builtinIndex );
            if ( traits.hasRole( ReflectBuiltinRole::State ) == false )
                return false;
            switch ( traits._layout )
            {
                case ReflectBuiltinLayout::Number:
                {
                    float64 number = 0.0;
                    return ReflectedValueUtil::readNumber( builtinIndex, pValue, number ) && ReflectedValueUtil::writeNumber( builtinIndex, pValue, number + 1.0 );
                }
                case ReflectBuiltinLayout::Float32:
                    *static_cast<float32*>( pValue ) += 1.0f; // 첫 성분
                    return true;
                case ReflectBuiltinLayout::Int32:
                    *static_cast<int32*>( pValue ) += 1; // 첫 성분
                    return true;
                case ReflectBuiltinLayout::Angle: // State 가 아니다
                    return false;
                case ReflectBuiltinLayout::None:
                    break;
            }
            switch ( builtinIndex ) // 손 처리기 — 목록은 `ReflectBuiltinTraitTable::hasStateHandHandler` 하나다
            {
                case ReflectBuiltinIndex::kbool:
                    *static_cast<bool*>( pValue ) = *static_cast<bool*>( pValue ) == false;
                    return true;
                case ReflectBuiltinIndex::kstring:
                    *static_cast<string*>( pValue ) += "x";
                    return true;
                case ReflectBuiltinIndex::khashed_string:
                {
                    hashed_string&                          label = *static_cast<hashed_string*>( pValue );
                    fixed_string<constant::buffer::kMax256> text;
                    formatstring( text.data(), text.capacity(), "%#x", label.c_str() );
                    label = hashed_string( text.c_str() );
                    return true;
                }
                case ReflectBuiltinIndex::kGameObjectHandle:
                {
                    GameObjectHandle& handle = *static_cast<GameObjectHandle*>( pValue );
                    handle                   = GameObjectHandle::make( handle.objectID() + 1, 0 );
                    return true;
                }
                default:
                    return false;
            }
        }
        if ( const EnumInfo* pEnum = registry.findEnum( typeName ) )
        {
            pEnum->writeValueToMemory( pValue, pEnum->readValueFromMemory( pValue ) + 1 );
            return true;
        }
        const TypeInfo* pType = registry.findType( typeName );
        if ( pType == nullptr || pType->isPrimitive() )
            return false;
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

/**
 * @brief [StateHashTest] 내장 표의 Layout 으로 접는 칸(int2 성분)과 손 처리기 칸(hashed_string · GameObjectHandle)이 FATAL 없이 접히고, 값이 같으면 같은 해시다
 * @details hashed_string 상태 칸은 전에는 해시가 접지 못해 FATAL 이었다(상태 직렬화는 실었다) — 두 쪽이 같은 State 역할을 읽는다.
 */
SW_TEST_CASE( StateHashTest, LayoutAndHandHandlerPropertiesFold )
{
    GameObjectManager       manager;
    GameObject*             pObject = manager.createGameObject( hashed_string( "StateHashProbe" ) );
    StateHashTestComponent* pProbe  = static_cast<StateHashTestComponent*>( manager.addComponentByName( pObject, hashed_string( "sw::StateHashTestComponent" ) ) );
    SW_REQUIRE_NOT_NULL( pProbe );
    pProbe->_cell         = int2{ 3, -4 };
    pProbe->_label        = hashed_string( "Gate" );
    pProbe->_targetObject = GameObjectHandle::make( 9, 0 );
    const uint64 first    = foldComponentState( *pProbe );
    SW_EXPECT_EQUAL( foldComponentState( *pProbe ), first );

    pProbe->_cell = int2{ -4, 3 }; // 성분 순서가 해시에 든다
    SW_EXPECT_TRUE( foldComponentState( *pProbe ) != first );
    pProbe->_cell = int2{ 3, -4 };
    SW_EXPECT_EQUAL( foldComponentState( *pProbe ), first );
}

namespace
{
    /** @brief State 역할 줄이면 그 형식의 기본값 하나를 `changeStateValue` 로 바꿔 봅니다. State 가 아닌 줄은 형식을 건드리지 않는다(압축 타입은 이름만 선언돼 있다). */
    template <typename T, bool bState>
    bool canChangeStateRow( const TypeRegistry& registry, const hashed_string& typeName )
    {
        if constexpr ( bState )
        {
            T                  value{};
            const PropertyInfo prop( hashed_string( "_value" ), typeName, 0 );
            return changeStateValue( registry, prop, &value, typeName, &value );
        }
        else
        {
            (void)registry;
            (void)typeName;
            return true;
        }
    }
} // namespace

/**
 * @brief [StateHashTest] 표의 State 역할 줄마다 값을 바꾸는 길이 있다 — 상태 칸에 쓸 수 있는 내장 형식이 늘면 테스트의 값 바꾸기도 표에서 따라온다
 */
SW_TEST_CASE( StateHashTest, EveryStateRowCanBeChanged )
{
    const TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, ... ) SW_EXPECT_TRUE_MSG( ( canChangeStateRow<CppType, ReflectBuiltinTraitTable::get( ReflectBuiltinIndex::k##Canon ).hasRole( ReflectBuiltinRole::State )>( registry, ReflectedValueUtil::getBuiltinTypeName( ReflectBuiltinIndex::k##Canon ) ) ), #Canon );
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
}
