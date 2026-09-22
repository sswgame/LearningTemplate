#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/Serializer.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

#include "ReflectionParser/AnnotationMeta.h"

#include "ReflectionTest/TestReflectionFixtures.h"
#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 리플렉션 데이터 모델 — TypeInfo · TypeRegistry · PropertyInfo · 메타데이터.
/**
 * @brief [ReflectionTypeRegistryTest] 등록된 클래스 조회
 */

SW_TEST_CASE( ReflectionTypeRegistryTest, FindRegisteredClass )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );

    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    SW_EXPECT_EQUAL( sw::string( "DummyActor" ), sw::string( info->_name.c_str() ) );
    SW_EXPECT_EQUAL( sizeof( sw::DummyActor ), info->_size );
}

/**
 * @brief [ReflectionTypeRegistryTest] 없는 클래스는 null
 */
SW_TEST_CASE( ReflectionTypeRegistryTest, FindNonExistentClass )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::NotExist" ) );

    SW_EXPECT_TRUE( info == nullptr );
}

/**
 * @brief ReflectBuiltins.gen.cpp 의 primitive TypeInfo (canonical 이름)
 */
SW_TEST_CASE( ReflectionTypeRegistryTest, PrimitiveBuiltins )
{
    const sw::TypeInfo* i32 = sw::engine::getTypeRegistry().findType( sw::hashed_string( "int32" ) );
    SW_ASSERT_NOT_NULL( i32 );
    SW_EXPECT_TRUE( i32->isPrimitive() );
    SW_EXPECT_EQUAL( sizeof( int32 ), i32->_size );
    SW_EXPECT_FALSE( i32->canConstruct() );

    const sw::TypeInfo* f32 = sw::engine::getTypeRegistry().findType( sw::hashed_string( "float32" ) );
    SW_ASSERT_NOT_NULL( f32 );
    SW_EXPECT_TRUE( f32->isPrimitive() );

    const sw::TypeInfo* str = sw::engine::getTypeRegistry().findType( sw::hashed_string( "string" ) );
    SW_ASSERT_NOT_NULL( str );
    SW_EXPECT_TRUE( str->isPrimitive() );
    SW_EXPECT_EQUAL( sizeof( std::string ), str->_size );

    // ReflectBuiltins 별칭 → canonical TypeInfo (직렬화 핸들러 resolve 용).
    const sw::TypeInfo* viaInt = sw::engine::getTypeRegistry().findType( sw::hashed_string( "int32" ) );
    SW_ASSERT_NOT_NULL( viaInt );
    SW_EXPECT_EQUAL( i32->_typeId, viaInt->_typeId );
    SW_EXPECT_TRUE( viaInt->_name == sw::hashed_string( "int32" ) );
}

/**
 * @brief [ReflectionTypeInfoTest] 프로퍼티 개수
 */
SW_TEST_CASE( ReflectionTypeInfoTest, PropertyCount )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( info->_listProperty.size() ) );
}

/**
 * @brief [ReflectionTypeInfoTest] 존재하는 프로퍼티 조회
 */
SW_TEST_CASE( ReflectionTypeInfoTest, FindExistingProperty )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    const sw::PropertyInfo* prop = info->findProperty( sw::hashed_string( "_hp" ) );
    SW_EXPECT_TRUE( prop != nullptr );
    if ( prop == nullptr )
        return;

    SW_EXPECT_EQUAL( sw::string( "_hp" ), sw::string( prop->_name.c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "int32" ), sw::string( prop->_typeName.c_str() ) );
    SW_EXPECT_FALSE( prop->_bIsContainer );
}

/**
 * @brief [ReflectionTypeInfoTest] 프로퍼티 메타데이터
 */
SW_TEST_CASE( ReflectionTypeInfoTest, PropertyMetadataSupport )
{
    sw::PropertyInfo prop;
#if !defined( SW_SHIPPING )
    prop._metadata._category    = "Rendering";
    prop._metadata._displayName = "Light Intensity";
    prop._metadata._tooltip     = "Controls light intensity";
#endif
    prop._metadata._minRange  = 0.0f;
    prop._metadata._maxRange  = 100.0f;
    prop._metadata._bHasRange = SW_TRUE;
    prop._metadata._bReadOnly = SW_TRUE;

#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( sw::string( "Rendering" ), prop._metadata._category );
    SW_EXPECT_EQUAL( sw::string( "Light Intensity" ), prop._metadata._displayName );
    SW_EXPECT_EQUAL( sw::string( "Controls light intensity" ), prop._metadata._tooltip );
#endif
    SW_EXPECT_NEAR_EQUAL( 0.0f, prop._metadata._minRange, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, prop._metadata._maxRange, 1e-4f );
    SW_EXPECT_TRUE( prop._metadata._bHasRange );
    SW_EXPECT_TRUE( prop._metadata._bReadOnly );
}

/**
 * @brief [ReflectionTypeInfoTest] 없는 프로퍼티는 null
 */
SW_TEST_CASE( ReflectionTypeInfoTest, FindNonExistentProperty )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    const sw::PropertyInfo* prop = info->findProperty( sw::hashed_string( "_notExist" ) );
    SW_EXPECT_TRUE( prop == nullptr );
}

/**
 * @brief [ReflectionTypeInfoTest] isA 동일 타입
 */
SW_TEST_CASE( ReflectionTypeInfoTest, IsA_SameType )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    SW_EXPECT_TRUE( info->isDerivedFrom( sw::hashed_string( "sw::DummyActor" ) ) );
}

/**
 * @brief [ReflectionTypeInfoTest] isA 부모 타입
 */
SW_TEST_CASE( ReflectionTypeInfoTest, IsA_ParentType )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    SW_EXPECT_TRUE( info->isDerivedFrom( sw::hashed_string( "sw::DummyBase" ) ) );
}

/**
 * @brief [ReflectionTypeInfoTest] isA 무관 타입
 */
SW_TEST_CASE( ReflectionTypeInfoTest, IsA_UnrelatedType )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    SW_EXPECT_FALSE( info->isDerivedFrom( sw::hashed_string( "sw::UnrelatedType" ) ) );
}

/**
 * @brief [ReflectionPropertyInfoTest] 오프셋 정확성
 */
SW_TEST_CASE( ReflectionPropertyInfoTest, OffsetCorrectness )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    const sw::PropertyInfo* hpProp    = info->findProperty( sw::hashed_string( "_hp" ) );
    const sw::PropertyInfo* nameProp  = info->findProperty( sw::hashed_string( "_name" ) );
    const sw::PropertyInfo* speedProp = info->findProperty( sw::hashed_string( "_speed" ) );

    SW_EXPECT_TRUE( hpProp != nullptr );
    SW_EXPECT_TRUE( nameProp != nullptr );
    SW_EXPECT_TRUE( speedProp != nullptr );

    if ( hpProp != nullptr )
        SW_EXPECT_EQUAL( SW_OFFSET_OF( sw::DummyActor, _hp ), hpProp->_offset );

    if ( nameProp != nullptr )
        SW_EXPECT_EQUAL( SW_OFFSET_OF( sw::DummyActor, _name ), nameProp->_offset );

    if ( speedProp != nullptr )
        SW_EXPECT_EQUAL( SW_OFFSET_OF( sw::DummyActor, _speed ), speedProp->_offset );
}

/**
 * @brief [ReflectionPropertyInfoTest] getValuePtr
 */
SW_TEST_CASE( ReflectionPropertyInfoTest, GetValuePtr )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    sw::DummyActor actor;
    actor._hp = 42;

    const sw::PropertyInfo* hpProp = info->findProperty( sw::hashed_string( "_hp" ) );
    SW_EXPECT_TRUE( hpProp != nullptr );
    if ( hpProp == nullptr )
        return;

    const int32* hpPtr = hpProp->getValuePtr<int32>( &actor );
    SW_EXPECT_TRUE( hpPtr != nullptr );
    SW_EXPECT_EQUAL( 42, *hpPtr );
}

/**
 * @brief [ReflectionPropertyInfoTest] setValue
 */
SW_TEST_CASE( ReflectionPropertyInfoTest, SetValue )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    sw::DummyActor actor;
    actor._hp = 0;

    const sw::PropertyInfo* hpProp = info->findProperty( sw::hashed_string( "_hp" ) );
    SW_EXPECT_TRUE( hpProp != nullptr );
    if ( hpProp == nullptr )
        return;

    hpProp->setValue<int32>( &actor, 999 );
    SW_EXPECT_EQUAL( 999, actor._hp );
}

/**
 * @brief [ReflectionPropertyInfoTest] setValue 중복 쓰기 없음
 */
SW_TEST_CASE( ReflectionPropertyInfoTest, SetValue_NoDuplicateWrite )
{
    const sw::TypeInfo* info =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    sw::DummyActor actor;
    actor._hp = 100;

    const sw::PropertyInfo* hpProp = info->findProperty( sw::hashed_string( "_hp" ) );
    SW_EXPECT_TRUE( hpProp != nullptr );
    if ( hpProp == nullptr )
        return;

    hpProp->setValue<int32>( &actor, 100 );
    SW_EXPECT_EQUAL( 100, actor._hp );
}

/**
 * @brief [ReflectionTypeRegistryTest] REFLECT(Alias) / ENUM(Alias) codegen 등록
 */
SW_TEST_CASE( ReflectionTypeRegistryTest, TypeAndEnumAliasLookup )
{
    const sw::TypeInfo* canonical =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::RenameCompatActor" ) );
    SW_ASSERT_NOT_NULL( canonical );

    const sw::TypeInfo* viaAlias =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::LegacyRenameActor" ) );
    SW_ASSERT_NOT_NULL( viaAlias );
    SW_EXPECT_EQUAL( canonical->_typeId, viaAlias->_typeId );
    SW_EXPECT_TRUE( viaAlias->_fullyQualifiedName == sw::hashed_string( "sw::RenameCompatActor" ) );

    const sw::EnumInfo* enumCanonical =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::SampleStatus" ) );
    SW_ASSERT_NOT_NULL( enumCanonical );
    const sw::EnumInfo* enumAlias =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::LegacySampleStatus" ) );
    SW_ASSERT_NOT_NULL( enumAlias );
    SW_EXPECT_TRUE( enumAlias->_fullyQualifiedName == sw::hashed_string( "sw::SampleStatus" ) );

    // enumerator ValueAlias: OldIdle → Idle 값
    SW_EXPECT_EQUAL( enumCanonical->stringFlagsToValue( "Idle" ), enumCanonical->stringFlagsToValue( "OldIdle" ) );
    SW_EXPECT_EQUAL( enumCanonical->stringFlagsToValue( "Moving" ),
                     enumCanonical->stringFlagsToValue( "OldMoving" ) );
}

/**
 * @brief [ReflectionTypeInfoTest] PropertyInfo 이름 매칭
 */
SW_TEST_CASE( ReflectionTypeInfoTest, PropertyInfoMatchesName )
{
    sw::PropertyInfo prop;
    prop._name      = sw::hashed_string( "_currentHp" );
    prop._listAlias = { sw::hashed_string( "hp" ), sw::hashed_string( "HitPoints" ) };

    SW_EXPECT_TRUE( prop.matchesName( sw::hashed_string( "_currentHp" ) ) );
    SW_EXPECT_TRUE( prop.matchesName( sw::hashed_string( "hp" ) ) );
    SW_EXPECT_TRUE( prop.matchesName( sw::hashed_string( "HitPoints" ) ) );
    SW_EXPECT_FALSE( prop.matchesName( sw::hashed_string( "mana" ) ) );
}

/**
 * @brief [ReflectionTypeInfoTest] 동적 메서드 호출
 */
SW_TEST_CASE( ReflectionTypeInfoTest, DynamicMethodInvoke )
{
    struct InvokableTestActor
    {
        int32 _score{ 0 };
        void  addScore( int32 delta )
        {
            _score += delta;
        }
    } actor;

    sw::FunctionInfo funcInfo;
    funcInfo._name     = "addScore";
    funcInfo._hashName = sw::hashed_string( "addScore" );
    funcInfo._invoker  = SW_DELEGATE_LAMBDA( sw::Delegate<sw::TaskValue( void*, const sw::TaskArgs& )>, []( void* pObjPtr, const sw::TaskArgs& args ) -> sw::TaskValue
     {
        static_cast<InvokableTestActor*>( pObjPtr )->addScore( args.get<int32>( 0 ) );
        return sw::TaskValue{};
    } );

    sw::TypeInfo info;
    info._name               = sw::hashed_string( "InvokableTestActor" );
    info._fullyQualifiedName = sw::hashed_string( "sw::InvokableTestActor" );
    info._size               = sizeof( InvokableTestActor );
    info._listMethod.push_back( funcInfo );

    sw::engine::getTypeRegistry().registerClass( info );

    sw::TaskArgs args;
    args.add( int32{ 50 } );
    sw::engine::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::InvokableTestActor" ), sw::hashed_string( "addScore" ), args );

    SW_EXPECT_EQUAL( 50, actor._score );
}

/**
 * @brief [ReflectionTypeInfoTest] REFLECT 생성자 placement new
 */
SW_TEST_CASE( ReflectionTypeInfoTest, ReflectCtorPlacementNew )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::CtorDemoActor" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );
    SW_EXPECT_TRUE( typeInfo->canConstruct() );

    const sw::FunctionInfo* defaultCtor = typeInfo->findMethod( sw::hashed_string( "$ctor" ) );
    const sw::FunctionInfo* valueCtor   = typeInfo->findMethod( sw::hashed_string( "$ctor(int32)" ) );
    SW_ASSERT_TRUE( defaultCtor != nullptr );
    SW_ASSERT_TRUE( valueCtor != nullptr );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( defaultCtor->_metadata._bConstructor ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( valueCtor->_metadata._bConstructor ) );

    alignas( sw::CtorDemoActor ) uint8 defaultStorage[sizeof( sw::CtorDemoActor )]{};
    sw::CtorDemoActor*                 defaultInstance = reinterpret_cast<sw::CtorDemoActor*>( defaultStorage );
    defaultCtor->_invoker( defaultInstance, sw::TaskArgs{} );
    SW_EXPECT_EQUAL( 0, defaultInstance->_value );
    defaultInstance->~CtorDemoActor();

    alignas( sw::CtorDemoActor ) uint8 valueStorage[sizeof( sw::CtorDemoActor )]{};
    sw::CtorDemoActor*                 valueInstance = reinterpret_cast<sw::CtorDemoActor*>( valueStorage );
    sw::TaskArgs                       valueArgs;
    valueArgs.add( int32{ 77 } );
    valueCtor->_invoker( valueInstance, valueArgs );
    SW_EXPECT_EQUAL( 77, valueInstance->_value );
    valueInstance->~CtorDemoActor();
}

/**
 * @brief [ReflectionTypeInfoTest] PROPERTY 어노테이션 메타 코드젠
 */
SW_TEST_CASE( ReflectionTypeInfoTest, PropertyAnnotationMetadataCodegen )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::MetadataDemoActor" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );
    const sw::PropertyInfo* hp = typeInfo->findProperty( sw::hashed_string( "_hp" ) );
    SW_ASSERT_TRUE( hp != nullptr );
#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( sw::string( "Stats" ), hp->_metadata._category );
    SW_EXPECT_EQUAL( sw::string( "Hit Points" ), hp->_metadata._displayName );
    SW_EXPECT_EQUAL( sw::string( "Current HP" ), hp->_metadata._tooltip );
#endif
    SW_EXPECT_TRUE( hp->_metadata._bReadOnly );
}

/**
 * @brief [ReflectionMetadataTest] TypeMetadata (Category, DisplayName, Tooltip, HideInMenu, CustomMeta) 검증
 */
SW_TEST_CASE( ReflectionMetadataTest, TypeMetadataQuery )
{
#if !defined( SW_SHIPPING )
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::MetaTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    SW_EXPECT_TRUE( pType->getCategory() == "Gameplay" );
    SW_EXPECT_TRUE( sw::string( pType->getDisplayName() ) == "Meta Test Actor" );
    SW_EXPECT_TRUE( pType->getTooltip() == "Actor for testing rich metadata" );
    SW_EXPECT_TRUE( pType->isHiddenInMenu() );

    const sw::string* pCustomVal = pType->findCustomMeta( sw::hashed_string( "CustomTag" ) );
    SW_ASSERT_NOT_NULL( pCustomVal );
    SW_EXPECT_TRUE( *pCustomVal == "ActorVal" );

    const sw::string* pPriority = pType->findCustomMeta( sw::hashed_string( "Priority" ) );
    SW_ASSERT_NOT_NULL( pPriority );
    SW_EXPECT_TRUE( *pPriority == "10" );
#else
    SW_TEST_SKIP( "Metadata is omitted in shipping builds" );
#endif
}

/**
 * @brief [ReflectionMetadataTest] PropertyMetadata (DisplayName, Category, Tooltip, Transient, HideInInspector, CustomMeta) 검증
 */
SW_TEST_CASE( ReflectionMetadataTest, PropertyMetadataQuery )
{
#if !defined( SW_SHIPPING )
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::MetaTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    const sw::PropertyInfo* pHealth = pType->findProperty( sw::hashed_string( "_health" ) );
    SW_ASSERT_NOT_NULL( pHealth );

    SW_EXPECT_TRUE( pHealth->_metadata._bTransient == SW_TRUE );
    SW_EXPECT_TRUE( pHealth->_metadata._bHideInInspector == SW_TRUE );
    SW_EXPECT_TRUE( pHealth->_metadata._displayName == "Health Points" );
    SW_EXPECT_TRUE( pHealth->_metadata._category == "Stats" );
    SW_EXPECT_TRUE( pHealth->_metadata._tooltip == "Current health" );

    const sw::string* pUnits = pHealth->findCustomMeta( sw::hashed_string( "Units" ) );
    SW_ASSERT_NOT_NULL( pUnits );
    SW_EXPECT_TRUE( *pUnits == "HP" );

    const sw::string* pClamp = pHealth->findCustomMeta( sw::hashed_string( "Clamp" ) );
    SW_ASSERT_NOT_NULL( pClamp );
    SW_EXPECT_TRUE( *pClamp == "True" );

    const sw::PropertyInfo* pArmor = pType->findProperty( sw::hashed_string( "_armor" ) );
    SW_ASSERT_NOT_NULL( pArmor );
    SW_EXPECT_TRUE( pArmor->_metadata._bTransient == SW_FALSE );
    SW_EXPECT_TRUE( pArmor->_metadata._bHideInInspector == SW_FALSE );
#else
    SW_TEST_SKIP( "Metadata is omitted in shipping builds" );
#endif
}

/**
 * @brief [ReflectionMetadataTest] FunctionMetadata (DisplayName, Category, Tooltip, CallInEditor, CustomMeta) 검증
 */
SW_TEST_CASE( ReflectionMetadataTest, FunctionMetadataQuery )
{
#if !defined( SW_SHIPPING )
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::MetaTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    const sw::FunctionInfo* pMethod = pType->findMethod( sw::hashed_string( "resetHealth" ) );
    SW_ASSERT_NOT_NULL( pMethod );

    SW_EXPECT_TRUE( pMethod->_metadata._bCallInEditor == SW_TRUE );
    SW_EXPECT_TRUE( pMethod->_metadata._displayName == "Reset Health" );
    SW_EXPECT_TRUE( pMethod->_metadata._category == "Actions" );
    SW_EXPECT_TRUE( pMethod->_metadata._tooltip == "Resets health to 100" );

    const sw::string* pActionType = pMethod->findCustomMeta( sw::hashed_string( "ActionType" ) );
    SW_ASSERT_NOT_NULL( pActionType );
    SW_EXPECT_TRUE( *pActionType == "Reset" );
#else
    SW_TEST_SKIP( "Metadata is omitted in shipping builds" );
#endif
}

/**
 * @brief [ReflectionMetadataTest] EnumInfo CustomMeta 검증
 */
SW_TEST_CASE( ReflectionMetadataTest, EnumMetadataQuery )
{
#if !defined( SW_SHIPPING )
    const sw::EnumInfo* pEnumInfo = sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "TestMetaEnum" ) );
    SW_ASSERT_NOT_NULL( pEnumInfo );

    const sw::string* pDoc = pEnumInfo->findCustomMeta( sw::hashed_string( "Doc" ) );
    SW_ASSERT_NOT_NULL( pDoc );
    SW_EXPECT_TRUE( *pDoc == "EnumForTesting" );

    const sw::string* pVersion = pEnumInfo->findCustomMeta( sw::hashed_string( "Version" ) );
    SW_ASSERT_NOT_NULL( pVersion );
    SW_EXPECT_TRUE( *pVersion == "2" );
#else
    SW_TEST_SKIP( "Metadata is omitted in shipping builds" );
#endif
}

/**
 * @brief [ReflectionMetadataTest] Transient 프로퍼티의 JSON 및 Binary 직렬화 제외 검증
 */
SW_TEST_CASE( ReflectionMetadataTest, TransientPropertySerialization )
{
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::MetaTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    sw::MetaTestActor sourceActor;
    sourceActor._health = 999;
    sourceActor._armor  = 77;

    // 1) JSON 직렬화 검증: _health는 제외되고 _armor만 직렬화되어야 함
    const sw::string json = sw::JsonSerializer::serialize( &sourceActor, *pType );
    SW_EXPECT_TRUE( json.find( "_armor" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "_health" ) == sw::string::npos );
    SW_EXPECT_TRUE( json.find( "Health Points" ) == sw::string::npos );

    // 2) JSON 역직렬화 검증: targetActor의 _health는 기본값을 유지해야 함
    sw::MetaTestActor targetActor;
    targetActor._health = 50;
    targetActor._armor  = 0;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &targetActor, *pType, json ) );
    SW_EXPECT_EQUAL( 77, targetActor._armor );
    SW_EXPECT_EQUAL( 50, targetActor._health );

    // 3) Binary 직렬화/역직렬화 검증
    sw::vector<uint8> listBin;
    sw::BinarySerializer::serialize( &sourceActor, *pType, listBin );
    sw::MetaTestActor binTarget;
    binTarget._health = 30;
    binTarget._armor  = 0;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &binTarget, *pType, listBin.data(), listBin.size() ) );
    SW_EXPECT_EQUAL( 77, binTarget._armor );
    SW_EXPECT_EQUAL( 30, binTarget._health );
}

/**
 * @brief [ReflectionTypeRegistryTest] 배치 등록 뒤 조회 캐시가 전부 만들어져 있는지 검증
 * @details `TypeInfo` 의 이름→프로퍼티 맵과 상속 병합 목록은 첫 조회 때 `mutable` 로, **잠금 없이**
 *          채워진다. 워커 둘이 같은 타입을 처음 조회하면 같은 맵에 동시에 삽입한다.
 *          `TypeRegistry::buildLookupCaches()` 가 등록 배치 직후 단일 스레드에서 만들어 그 창을 없앤다.
 *
 *          **등록하는 자리에서 하나씩 만들 수 없다는 것이 이 테스트의 핵심이다.** 타입 표는 밀집
 *          배열이라 커질 때 원소를 옮기고, 그때 `TypeInfo` 이동 생성자가 캐시를 비운다 — 뒤이은
 *          등록 하나가 앞서 만든 것을 전부 날린다. 그래서 "배치 뒤에 한 번" 이 유일하게 성립한다.
 */
SW_TEST_CASE( ReflectionTypeRegistryTest, LookupCachesAreBuiltAfterRegistrationBatch )
{
    const sw::TypeRegistry& registry = sw::engine::getTypeRegistry();

    // 이 테스트 바이너리의 픽스처처럼 배치 밖에서 등록된 타입도 있으므로, 여기서 한 번 돌린다 —
    // 엔진에서는 `registerPendingTypes` 가 배치 끝에서 부르는 바로 그 호출이다.
    registry.buildLookupCaches();

    uint32 checkedCount = 0;
    uint32 coldCount    = 0;
    registry.forEachType( [&checkedCount, &coldCount]( const sw::TypeInfo& typeInfo )
    {
        ++checkedCount;
        if ( typeInfo.isLookupCacheBuilt() == false )
            ++coldCount;
    } );

    SW_EXPECT_TRUE( checkedCount > 0 );
    SW_EXPECT_EQUAL( 0u, coldCount );
}

/**
 * @brief [ReflectionTypeRegistryTest] 부모 체인이 순환해도 멈추는지 검증
 * @details `_parentFQN` 은 코드젠이 적는 값이지만 `registerClass` 는 **공개 API 이고 그 값을
 *          검사하지 않는다.** 모듈이 따로따로 등록되는 핫리로드에서는 A→B→A 가 만들어질 수
 *          있는데, 부모 체인을 거는 세 곳이 전부 그것을 대비하지 않고 있었다:
 *          `isDerivedFrom`(루프 — **행**), `findPropertyInHierarchy`(재귀 — 스택 오버플로),
 *          `getPropertiesWithBase`(재귀 — 스택 오버플로).
 *
 *          같은 저장소의 `ComponentDefaults::collectTypeChain` 은 "순환 방지" 를 명시적으로
 *          하고 있었다 — 그 가드가 형제들로 옮겨지지 않은 것이다.
 */
SW_TEST_CASE( ReflectionTypeRegistryTest, ParentChainLoopDoesNotHang )
{
    SW_TEST_SUPPRESS_LOGS();

    sw::TypeRegistry& registry = sw::engine::getTypeRegistry();

    sw::TypeInfo typeA;
    typeA._name               = sw::hashed_string( "LoopA" );
    typeA._fullyQualifiedName = sw::hashed_string( "swtest::LoopA" );
    typeA._parentFQN          = sw::hashed_string( "swtest::LoopB" );
    typeA._moduleName         = sw::hashed_string( "TestParentLoop" );

    sw::TypeInfo typeB;
    typeB._name               = sw::hashed_string( "LoopB" );
    typeB._fullyQualifiedName = sw::hashed_string( "swtest::LoopB" );
    typeB._parentFQN          = sw::hashed_string( "swtest::LoopA" );
    typeB._moduleName         = sw::hashed_string( "TestParentLoop" );

    registry.registerClass( typeA );
    registry.registerClass( typeB );

    const sw::TypeInfo* pLoopA = registry.findType( sw::hashed_string( "swtest::LoopA" ) );
    SW_ASSERT_NOT_NULL( pLoopA );

    // 고치기 전에는 이 줄에서 영원히 돈다 — 없는 이름을 물으면 체인이 끝나지 않는다.
    SW_EXPECT_FALSE( pLoopA->isDerivedFrom( sw::hashed_string( "swtest::NotThere" ) ) );
    // 재귀였던 둘은 스택을 넘긴다.
    SW_EXPECT_TRUE( pLoopA->findPropertyInHierarchy( sw::hashed_string( "nope" ) ) == nullptr );
    SW_EXPECT_TRUE( pLoopA->getPropertiesWithBase().empty() );

    // 순환이어도 실제로 답이 있는 질문에는 맞게 답해야 한다.
    SW_EXPECT_TRUE( pLoopA->isDerivedFrom( sw::hashed_string( "swtest::LoopB" ) ) );

    // 포인터 걷기도 같은 순환에서 멈추고, 있는 답은 낸다.
    const sw::TypeInfo* pLoopB = registry.findType( sw::hashed_string( "swtest::LoopB" ) );
    SW_ASSERT_NOT_NULL( pLoopB );
    SW_EXPECT_TRUE( pLoopA->isDerivedFrom( pLoopB ) );
    SW_EXPECT_TRUE( pLoopB->isDerivedFrom( pLoopA ) );
    // 순환은 조상 표를 세울 수 없다 — 그래도 위 답은 걷기가 냈다.
    SW_EXPECT_FALSE( pLoopA->hasAncestorDisplay() );
    sw::TypeInfo typeStranger;
    typeStranger._fullyQualifiedName = sw::hashed_string( "swtest::LoopStranger" );
    SW_EXPECT_FALSE( pLoopA->isDerivedFrom( &typeStranger ) );

#if !defined( SW_SHIPPING )
    registry.unregisterTypesByModule( "TestParentLoop" );
#endif
}

/**
 * @brief [ReflectionTypeRegistryTest] 부모 포인터가 배치 끝에서 풀리고, 포인터 걷기가 이름 걷기와 같은 답을 내는지
 * @details `isDerivedFrom` 은 조상마다 `findType(_parentFQN)` 을 불렀다(잠금 + 해시맵). 이제 등록 배치 끝
 *          (`buildLookupCaches`)에서 부모를 포인터로 한 번 풀어 두고, 캐스트는 그 포인터만 걷는다.
 */
SW_TEST_CASE( ReflectionTypeRegistryTest, ParentTypePointerIsResolvedAfterBatch )
{
    sw::TypeRegistry& registry = sw::engine::getTypeRegistry();

    sw::TypeInfo typeRoot;
    typeRoot._name               = sw::hashed_string( "ChainRoot" );
    typeRoot._fullyQualifiedName = sw::hashed_string( "swtest::ChainRoot" );
    typeRoot._moduleName         = sw::hashed_string( "TestParentChain" );

    sw::TypeInfo typeMid;
    typeMid._name               = sw::hashed_string( "ChainMid" );
    typeMid._fullyQualifiedName = sw::hashed_string( "swtest::ChainMid" );
    typeMid._parentFQN          = sw::hashed_string( "swtest::ChainRoot" );
    typeMid._moduleName         = sw::hashed_string( "TestParentChain" );

    sw::TypeInfo typeLeaf;
    typeLeaf._name               = sw::hashed_string( "ChainLeaf" );
    typeLeaf._fullyQualifiedName = sw::hashed_string( "swtest::ChainLeaf" );
    typeLeaf._parentFQN          = sw::hashed_string( "swtest::ChainMid" );
    typeLeaf._moduleName         = sw::hashed_string( "TestParentChain" );

    registry.registerClass( typeRoot );
    registry.registerClass( typeMid );
    registry.registerClass( typeLeaf );
    registry.buildLookupCaches();

    const sw::TypeInfo* pRoot = registry.findType( sw::hashed_string( "swtest::ChainRoot" ) );
    const sw::TypeInfo* pMid  = registry.findType( sw::hashed_string( "swtest::ChainMid" ) );
    const sw::TypeInfo* pLeaf = registry.findType( sw::hashed_string( "swtest::ChainLeaf" ) );
    SW_ASSERT_NOT_NULL( pRoot );
    SW_ASSERT_NOT_NULL( pMid );
    SW_ASSERT_NOT_NULL( pLeaf );

    // 배치 끝에서 풀린 포인터는 레지스트리의 그 항목 자체다.
    SW_EXPECT_TRUE( pLeaf->getParentType() == pMid );
    SW_EXPECT_TRUE( pMid->getParentType() == pRoot );
    SW_EXPECT_NULL( pRoot->getParentType() );

    // 포인터 걷기 — 자기 자신·조상은 true, 자손·무관·nullptr 은 false.
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( pLeaf ) );
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( pMid ) );
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( pRoot ) );
    SW_EXPECT_FALSE( pRoot->isDerivedFrom( pLeaf ) );
    SW_EXPECT_FALSE( pMid->isDerivedFrom( pLeaf ) );
    SW_EXPECT_FALSE( pLeaf->isDerivedFrom( static_cast<const sw::TypeInfo*>( nullptr ) ) );

    // 이름 걷기도 같은 답 — FQN 과 짧은 이름 둘 다.
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( sw::hashed_string( "swtest::ChainRoot" ) ) );
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( sw::hashed_string( "ChainRoot" ) ) );
    SW_EXPECT_FALSE( pRoot->isDerivedFrom( sw::hashed_string( "swtest::ChainLeaf" ) ) );

#if !defined( SW_SHIPPING )
    registry.unregisterTypesByModule( "TestParentChain" );
#endif
}

/**
 * @brief [ReflectionTypeRegistryTest] 조상 표 — 배치 끝에 세워지고, 걷기와 같은 답을 내며, 사슬이 바뀌면 따라온다
 * @details 캐스트의 핫패스는 `isDerivedFrom( const TypeInfo* )` 다. 예전엔 조상마다 이름 비교 둘과 부모 포인터
 *          로드였다 — 실패 캐스트는 루트까지 약 28 ns. 이제 등록 배치 끝에 타입마다 루트부터의 `_typeId` 표를
 *          적어 두고, 검사는 로드 둘과 비교 하나다(HotSpot 의 primary supers display). 지켜야 할 것 넷:
 *          (1) 표의 답은 걷기의 답과 같다 (2) 표가 없는 쪽 — 레지스트리 밖 사본·표보다 깊은 사슬 — 은 걷기로
 *          폴백해 같은 답을 낸다 (3) 재등록으로 부모가 바뀌면 옛 표로 답하지 않는다 (4) 모듈 해제 뒤 남은
 *          타입은 표를 다시 세워 답한다.
 */
SW_TEST_CASE( ReflectionTypeRegistryTest, AncestorDisplayMatchesWalkAndFollowsRechain )
{
    SW_TEST_SUPPRESS_LOGS();

    sw::TypeRegistry&       registry = sw::engine::getTypeRegistry();
    const sw::hashed_string moduleName( "TestAncestorDisplay" );

    auto makeType = [&]( const utf8* pName, const utf8* pFqn, const utf8* pParentFqn )
    {
        sw::TypeInfo type;
        type._name               = sw::hashed_string( pName );
        type._fullyQualifiedName = sw::hashed_string( pFqn );
        if ( pParentFqn != nullptr )
            type._parentFQN = sw::hashed_string( pParentFqn );
        type._moduleName = moduleName;
        return type;
    };

    registry.registerClass( makeType( "DispRoot", "swtest::DispRoot", nullptr ) );
    registry.registerClass( makeType( "DispMid", "swtest::DispMid", "swtest::DispRoot" ) );
    registry.registerClass( makeType( "DispLeaf", "swtest::DispLeaf", "swtest::DispMid" ) );
    registry.registerClass( makeType( "DispOther", "swtest::DispOther", nullptr ) );
    // 표보다 깊은 사슬 — Deep0 … Deep9 (깊이 9).
    const utf8* arrDeepFqn[10] = { "swtest::Deep0", "swtest::Deep1", "swtest::Deep2", "swtest::Deep3", "swtest::Deep4",
                                   "swtest::Deep5", "swtest::Deep6", "swtest::Deep7", "swtest::Deep8", "swtest::Deep9" };
    for ( uint32 index = 0; index < 10; ++index )
        registry.registerClass( makeType( arrDeepFqn[index] + 8, arrDeepFqn[index], index == 0 ? nullptr : arrDeepFqn[index - 1] ) );
    registry.buildLookupCaches();

    auto find = [&]( const utf8* pFqn )
    {
        const sw::TypeInfo* pType = registry.findType( sw::hashed_string( pFqn ) );
        return pType;
    };
    const sw::TypeInfo* pRoot  = find( "swtest::DispRoot" );
    const sw::TypeInfo* pMid   = find( "swtest::DispMid" );
    const sw::TypeInfo* pLeaf  = find( "swtest::DispLeaf" );
    const sw::TypeInfo* pOther = find( "swtest::DispOther" );
    SW_ASSERT_NOT_NULL( pRoot );
    SW_ASSERT_NOT_NULL( pMid );
    SW_ASSERT_NOT_NULL( pLeaf );
    SW_ASSERT_NOT_NULL( pOther );

    // 배치 끝에 표가 서 있다.
    SW_EXPECT_TRUE( pRoot->hasAncestorDisplay() );
    SW_EXPECT_TRUE( pMid->hasAncestorDisplay() );
    SW_EXPECT_TRUE( pLeaf->hasAncestorDisplay() );

    // (1) 표의 답 — 자기·조상은 true, 자손·형제·무관·nullptr 은 false.
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( pLeaf ) );
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( pMid ) );
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( pRoot ) );
    SW_EXPECT_FALSE( pRoot->isDerivedFrom( pLeaf ) );
    SW_EXPECT_FALSE( pMid->isDerivedFrom( pLeaf ) );
    SW_EXPECT_FALSE( pLeaf->isDerivedFrom( pOther ) );
    SW_EXPECT_FALSE( pOther->isDerivedFrom( pRoot ) );
    SW_EXPECT_FALSE( pLeaf->isDerivedFrom( static_cast<const sw::TypeInfo*>( nullptr ) ) );

    // (2a) 레지스트리 밖 사본 — 손으로 만든 같은 이름의 TypeInfo(테스트 목의 `StaticType()` 이 이렇다. 자기 `_typeId`
    //      를 따로 가진다)는 **이름**으로 같은 타입이다. 표가 이름을 적는 이유. 표 쪽에서도, 걷기 쪽에서도 같은 답.
    sw::TypeInfo midCopy;
    midCopy._name               = sw::hashed_string( "DispMid" );
    midCopy._fullyQualifiedName = sw::hashed_string( "swtest::DispMid" );
    midCopy._parentFQN          = sw::hashed_string( "swtest::DispRoot" );
    midCopy._typeId             = 0xC0FFEE;
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( &midCopy ) );
    SW_EXPECT_TRUE( midCopy.isDerivedFrom( pRoot ) );
    SW_EXPECT_FALSE( midCopy.isDerivedFrom( pLeaf ) );
    SW_EXPECT_FALSE( midCopy.isDerivedFrom( pOther ) );
    SW_EXPECT_TRUE( midCopy.hasAncestorDisplay() );
    // 이름이 없는 TypeInfo 는 표를 세울 수 없다 — 아무와도 같지 않다.
    sw::TypeInfo nameless;
    SW_EXPECT_FALSE( nameless.isDerivedFrom( pRoot ) );
    SW_EXPECT_FALSE( pLeaf->isDerivedFrom( &nameless ) );
    SW_EXPECT_FALSE( nameless.hasAncestorDisplay() );
    // 레지스트리 항목의 복사본은 표가 비어 나온다 — 첫 조회가 다시 세운다.
    sw::TypeInfo leafCopy = *pLeaf;
    SW_EXPECT_FALSE( leafCopy.hasAncestorDisplay() );
    SW_EXPECT_TRUE( leafCopy.isDerivedFrom( pRoot ) );
    SW_EXPECT_TRUE( leafCopy.hasAncestorDisplay() );
    SW_EXPECT_FALSE( leafCopy.isDerivedFrom( pOther ) );

    // (2b) 표보다 깊은 사슬 — Deep7 까지는 표, Deep8 부터는 걷기. 답은 같다.
    const sw::TypeInfo* pDeep0 = find( "swtest::Deep0" );
    const sw::TypeInfo* pDeep7 = find( "swtest::Deep7" );
    const sw::TypeInfo* pDeep8 = find( "swtest::Deep8" );
    const sw::TypeInfo* pDeep9 = find( "swtest::Deep9" );
    SW_ASSERT_NOT_NULL( pDeep0 );
    SW_ASSERT_NOT_NULL( pDeep7 );
    SW_ASSERT_NOT_NULL( pDeep8 );
    SW_ASSERT_NOT_NULL( pDeep9 );
    SW_EXPECT_TRUE( pDeep7->hasAncestorDisplay() );
    SW_EXPECT_FALSE( pDeep8->hasAncestorDisplay() );
    SW_EXPECT_FALSE( pDeep9->hasAncestorDisplay() );
    SW_EXPECT_TRUE( pDeep9->isDerivedFrom( pDeep0 ) );
    SW_EXPECT_TRUE( pDeep9->isDerivedFrom( pDeep8 ) );
    SW_EXPECT_TRUE( pDeep8->isDerivedFrom( pDeep7 ) );
    SW_EXPECT_FALSE( pDeep0->isDerivedFrom( pDeep9 ) );
    SW_EXPECT_FALSE( pDeep7->isDerivedFrom( pDeep8 ) );
    SW_EXPECT_FALSE( pDeep9->isDerivedFrom( pRoot ) );

    // (3) 재등록으로 Mid 의 부모가 Root → Other 로 바뀐다. 등록이 표를 비우므로, 배치 끝을 부르기 **전**에도
    //     옛 표로 답하지 않아야 한다(첫 조회가 새 사슬로 다시 세운다). 등록으로 포인터가 옮겨질 수 있어 다시 찾는다.
    registry.registerClass( makeType( "DispMid", "swtest::DispMid", "swtest::DispOther" ) );
    pRoot  = find( "swtest::DispRoot" );
    pMid   = find( "swtest::DispMid" );
    pLeaf  = find( "swtest::DispLeaf" );
    pOther = find( "swtest::DispOther" );
    SW_ASSERT_NOT_NULL( pLeaf );
    SW_EXPECT_FALSE( pLeaf->hasAncestorDisplay() );
    SW_EXPECT_FALSE( pLeaf->isDerivedFrom( pRoot ) );
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( pOther ) );
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( pMid ) );
    registry.buildLookupCaches();
    SW_EXPECT_TRUE( pLeaf->hasAncestorDisplay() );
    SW_EXPECT_FALSE( pLeaf->isDerivedFrom( pRoot ) );
    SW_EXPECT_TRUE( pLeaf->isDerivedFrom( pOther ) );

    // (3b) 부모가 **아직 등록되지 않은** 타입 — `Component` 처럼 REFLECT 가 아닌 기반이거나 아직 안 올라온 모듈.
    //      안 풀리는 부모는 사슬의 끝이라 표는 선다(걷기가 매번 그 이름을 잠금 잡고 찾던 것이 사라진다). 부모가
    //      뒤늦게 등록되면 등록이 표를 비우고, 다음 조회가 이어진 사슬로 다시 세운다.
    registry.registerClass( makeType( "DispOrphan", "swtest::DispOrphan", "swtest::DispLateParent" ) );
    registry.buildLookupCaches();
    const sw::TypeInfo* pOrphan = find( "swtest::DispOrphan" );
    pRoot                       = find( "swtest::DispRoot" );
    SW_ASSERT_NOT_NULL( pOrphan );
    SW_ASSERT_NOT_NULL( pRoot );
    SW_EXPECT_TRUE( pOrphan->hasAncestorDisplay() );
    // 못 푸는 부모는 같은 세대 동안 다시 찾지 않는다 — 두 번 물어도 nullptr 이고, 부모가 등록되면(세대가 오르면) 풀린다.
    SW_EXPECT_NULL( pOrphan->getParentType() );
    SW_EXPECT_NULL( pOrphan->getParentType() );
    SW_EXPECT_FALSE( pOrphan->isDerivedFrom( pRoot ) );
    SW_EXPECT_TRUE( pOrphan->isDerivedFrom( pOrphan ) );
    registry.registerClass( makeType( "DispLateParent", "swtest::DispLateParent", "swtest::DispRoot" ) );
    pOrphan = find( "swtest::DispOrphan" );
    pRoot   = find( "swtest::DispRoot" );
    SW_ASSERT_NOT_NULL( pOrphan );
    SW_EXPECT_TRUE( pOrphan->getParentType() == find( "swtest::DispLateParent" ) );
    SW_EXPECT_FALSE( pOrphan->hasAncestorDisplay() );
    SW_EXPECT_TRUE( pOrphan->isDerivedFrom( pRoot ) );
    SW_EXPECT_TRUE( pOrphan->hasAncestorDisplay() );
    SW_EXPECT_TRUE( pOrphan->isDerivedFrom( find( "swtest::DispLateParent" ) ) );

#if !defined( SW_SHIPPING )
    // (4) 모듈 해제는 남은 타입의 표도 비운다 — 이 바이너리의 픽스처(DummyActor → DummyBase)가 다시 세워 답한다.
    registry.unregisterTypesByModule( "TestAncestorDisplay" );
    const sw::TypeInfo* pActor = find( "sw::DummyActor" );
    const sw::TypeInfo* pBase  = find( "sw::DummyBase" );
    SW_ASSERT_NOT_NULL( pActor );
    SW_ASSERT_NOT_NULL( pBase );
    SW_EXPECT_FALSE( pActor->hasAncestorDisplay() );
    SW_EXPECT_TRUE( pActor->isDerivedFrom( pBase ) );
    SW_EXPECT_TRUE( pActor->hasAncestorDisplay() );
    SW_EXPECT_FALSE( pBase->isDerivedFrom( pActor ) );
    SW_EXPECT_NULL( find( "swtest::DispLeaf" ) );
#endif
}

/**
 * @brief [ReflectionTypeInfoTest] 계층 프로퍼티 조회는 병합 목록과 같은 답을 낸다 — 이름 · 별칭 · 실패
 * @details `findPropertyInHierarchy` 는 이제 `getPropertiesWithBase()` 가 지어 둔 맵 하나로 답한다(단계마다 걷던 때 적중
 *          17 · 실패 34 ns). 병합 목록의 모든 항목이 이름으로 그 항목 자신을 내고, 별칭도 같은 항목을 내며, 없는 이름은
 *          nullptr 여야 한다. 부모가 없는 타입은 자기 목록으로 답한다.
 */
SW_TEST_CASE( ReflectionTypeInfoTest, FindPropertyInHierarchyMatchesMergedList )
{
    const sw::TypeRegistry& registry = sw::engine::getTypeRegistry();
    const sw::TypeInfo*     pActor   = registry.findType( sw::hashed_string( "sw::DummyActor" ) );
    const sw::TypeInfo*     pBase    = registry.findType( sw::hashed_string( "sw::DummyBase" ) );
    SW_ASSERT_NOT_NULL( pActor );
    SW_ASSERT_NOT_NULL( pBase );

    const sw::vector<sw::PropertyInfo>& listMerged = pActor->getPropertiesWithBase();
    SW_EXPECT_TRUE( listMerged.size() >= pActor->_listProperty.size() );
    uint32 checkedCount = 0;
    for ( const sw::PropertyInfo& prop : listMerged )
    {
        const sw::PropertyInfo* pFound = pActor->findPropertyInHierarchy( prop._name );
        SW_ASSERT_NOT_NULL( pFound );
        SW_EXPECT_TRUE( pFound == &prop );
        for ( const sw::hashed_string& alias : prop._listAlias )
        {
            if ( alias.empty() == false )
                SW_EXPECT_TRUE( pActor->findPropertyInHierarchy( alias ) == &prop );
        }
        ++checkedCount;
    }
    SW_EXPECT_TRUE( checkedCount > 0 );
    SW_EXPECT_NULL( pActor->findPropertyInHierarchy( sw::hashed_string( "_noSuchPropertyAnywhere" ) ) );

    // 부모가 없는 기반은 자기 목록으로 답한다 — 병합 목록이 곧 자기 목록이다.
    for ( const sw::PropertyInfo& prop : pBase->getPropertiesWithBase() )
        SW_EXPECT_TRUE( pBase->findPropertyInHierarchy( prop._name ) == pBase->findProperty( prop._name ) );
}

/**
 * @brief [ReflectionTypeInfoTest] 병합 목록이 선형 임계(4)를 넘는 타입은 **맵**으로 답한다 — 이름 · 별칭 · 파생 우선 · 실패
 * @details 위 테스트의 픽스처는 병합 목록이 작아 선형 경로만 지난다. 여기서는 손으로 만든 사슬(기반 3 · 파생 3, 파생이
 *          기반 이름 하나를 다시 적는다)로 맵 경로를 지나게 한다. 파생이 다시 적은 이름은 파생의 항목(오프셋이 다르다)이
 *          나와야 한다 — 병합 규칙과 같다.
 */
SW_TEST_CASE( ReflectionTypeInfoTest, FindPropertyInHierarchyUsesMergedMapWhenLarge )
{
    sw::TypeRegistry& registry = sw::engine::getTypeRegistry();

    sw::TypeInfo typeRoot;
    typeRoot._name               = sw::hashed_string( "MapRoot" );
    typeRoot._fullyQualifiedName = sw::hashed_string( "swtest::MapRoot" );
    typeRoot._moduleName         = sw::hashed_string( "TestHierarchyMap" );
    typeRoot._listProperty.emplace_back( sw::hashed_string( "_alpha" ), sw::hashed_string( "int32" ), 0 );
    typeRoot._listProperty.emplace_back( sw::hashed_string( "_beta" ), sw::hashed_string( "int32" ), 4 );
    typeRoot._listProperty.emplace_back( sw::hashed_string( "_gamma" ), sw::hashed_string( "int32" ), 8 );
    typeRoot._listProperty.back()._listAlias.push_back( sw::hashed_string( "_gammaOld" ) );

    sw::TypeInfo typeLeaf;
    typeLeaf._name               = sw::hashed_string( "MapLeaf" );
    typeLeaf._fullyQualifiedName = sw::hashed_string( "swtest::MapLeaf" );
    typeLeaf._parentFQN          = sw::hashed_string( "swtest::MapRoot" );
    typeLeaf._moduleName         = sw::hashed_string( "TestHierarchyMap" );
    typeLeaf._listProperty.emplace_back( sw::hashed_string( "_delta" ), sw::hashed_string( "int32" ), 12 );
    typeLeaf._listProperty.emplace_back( sw::hashed_string( "_beta" ), sw::hashed_string( "int32" ), 16 ); // 기반 이름을 다시 적는다
    typeLeaf._listProperty.emplace_back( sw::hashed_string( "_epsilon" ), sw::hashed_string( "int32" ), 20 );

    registry.registerClass( typeRoot );
    registry.registerClass( typeLeaf );
    registry.buildLookupCaches();

    const sw::TypeInfo* pLeaf = registry.findType( sw::hashed_string( "swtest::MapLeaf" ) );
    SW_ASSERT_NOT_NULL( pLeaf );
    const sw::vector<sw::PropertyInfo>& listMerged = pLeaf->getPropertiesWithBase();
    SW_EXPECT_EQUAL( size_t( 5 ), listMerged.size() ); // 3 + 3 - 겹친 이름 1
    SW_EXPECT_TRUE( listMerged.size() > sw::constants::reflection::kLinearSearchThreshold );

    // 이름마다 병합 목록의 그 항목 자신.
    for ( const sw::PropertyInfo& prop : listMerged )
        SW_EXPECT_TRUE( pLeaf->findPropertyInHierarchy( prop._name ) == &prop );
    // 파생이 다시 적은 이름은 파생의 오프셋.
    const sw::PropertyInfo* pBeta = pLeaf->findPropertyInHierarchy( sw::hashed_string( "_beta" ) );
    SW_ASSERT_NOT_NULL( pBeta );
    SW_EXPECT_EQUAL( size_t( 16 ), pBeta->_offset );
    // 기반의 별칭도 같은 항목.
    const sw::PropertyInfo* pGamma = pLeaf->findPropertyInHierarchy( sw::hashed_string( "_gammaOld" ) );
    SW_ASSERT_NOT_NULL( pGamma );
    SW_EXPECT_TRUE( pGamma->_name == sw::hashed_string( "_gamma" ) );
    SW_EXPECT_EQUAL( size_t( 8 ), pGamma->_offset );
    SW_EXPECT_NULL( pLeaf->findPropertyInHierarchy( sw::hashed_string( "_zeta" ) ) );

#if !defined( SW_SHIPPING )
    registry.unregisterTypesByModule( "TestHierarchyMap" );
#endif
}

/**
 * @brief [ReflectionTypeRegistryTest] `TypeLookupCache` 가 레지스트리 세대를 따라 답을 갱신하는지
 * @details 코드젠의 `StaticType()` 과 `Component::getTypeInfo()` 가 이 칸을 쓴다. 세대가 같으면 지난 답,
 *          등록·해제로 세대가 오르면 다시 찾는다 — 미등록 → 등록 → 해제 세 단계에서 답이 따라와야 한다.
 */
SW_TEST_CASE( ReflectionTypeRegistryTest, TypeLookupCacheFollowsRegistryGeneration )
{
    sw::TypeRegistry&       registry = sw::engine::getTypeRegistry();
    const sw::hashed_string fqn( "swtest::CacheProbe" );
    sw::TypeLookupCache     cache;

    const uint32 generationBefore = registry.getGeneration();
    SW_EXPECT_NULL( cache.find( fqn ) );
    // 같은 세대의 두 번째 조회는 캐시에서 온다(값은 같아야 한다).
    SW_EXPECT_NULL( cache.find( fqn ) );

    sw::TypeInfo typeProbe;
    typeProbe._name               = sw::hashed_string( "CacheProbe" );
    typeProbe._fullyQualifiedName = fqn;
    typeProbe._moduleName         = sw::hashed_string( "TestLookupCache" );
    registry.registerClass( typeProbe );

    SW_EXPECT_TRUE( registry.getGeneration() != generationBefore );
    const sw::TypeInfo* pProbe = cache.find( fqn );
    SW_ASSERT_NOT_NULL( pProbe );
    SW_EXPECT_TRUE( pProbe == registry.findType( fqn ) );
    SW_EXPECT_TRUE( cache.find( fqn ) == pProbe );

#if !defined( SW_SHIPPING )
    registry.unregisterTypesByModule( "TestLookupCache" );
    SW_EXPECT_NULL( cache.find( fqn ) );
    SW_EXPECT_NULL( registry.findType( fqn ) );
    // 주소 고정 · 묘비: 같은 FQN 을 다시 등록하면 **같은 객체**가 되살아나고, 칸은 다시 찾지 않고 그것을 낸다.
    registry.registerClass( typeProbe );
    SW_EXPECT_TRUE( registry.findType( fqn ) == pProbe );
    SW_EXPECT_TRUE( cache.find( fqn ) == pProbe );
    SW_EXPECT_TRUE( pProbe->isAlive() );
    registry.unregisterTypesByModule( "TestLookupCache" );
    SW_EXPECT_NULL( cache.find( fqn ) );
    SW_EXPECT_FALSE( pProbe->isAlive() );
#endif
}

/**
 * @brief [ReflectionTypeRegistryTest] `TypeInfo` 의 주소는 등록이 아무리 이어져도 고정이다
 * @details 예전엔 타입 표가 밀집 배열이라 커질 때 원소를 옮겼고, `findType` 이 내준 포인터는 다음 등록에서 무효였다 —
 *          그래서 `TypeLookupCache` 가 세대를 견줬고 `rebindAllCachedTypeInfo` 가 있었다. 이제 `unique_ptr` 로 든다.
 *          200 개를 더 등록해 표를 몇 번 키운 뒤에도 처음 포인터가 그 타입이어야 하고, 내용이 온전해야 한다.
 */
SW_TEST_CASE( ReflectionTypeRegistryTest, TypeInfoAddressIsStableAcrossRegistrations )
{
    sw::TypeRegistry& registry = sw::engine::getTypeRegistry();

    sw::TypeInfo typeFirst;
    typeFirst._name               = sw::hashed_string( "StableFirst" );
    typeFirst._fullyQualifiedName = sw::hashed_string( "swtest::StableFirst" );
    typeFirst._moduleName         = sw::hashed_string( "TestStableAddress" );
    typeFirst._size               = 48;
    registry.registerClass( typeFirst );
    const sw::TypeInfo* pFirst = registry.findType( sw::hashed_string( "swtest::StableFirst" ) );
    SW_ASSERT_NOT_NULL( pFirst );

    for ( uint32 index = 0; index < 200; ++index )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer64> name;
        name.append( "swtest::StableFiller" ).append( index );
        sw::TypeInfo typeFiller;
        typeFiller._fullyQualifiedName = sw::hashed_string( name.c_str() );
        typeFiller._name               = typeFiller._fullyQualifiedName;
        typeFiller._moduleName         = sw::hashed_string( "TestStableAddress" );
        registry.registerClass( typeFiller );
    }
    registry.buildLookupCaches();

    SW_EXPECT_TRUE( registry.findType( sw::hashed_string( "swtest::StableFirst" ) ) == pFirst );
    SW_EXPECT_TRUE( pFirst->_fullyQualifiedName == sw::hashed_string( "swtest::StableFirst" ) );
    SW_EXPECT_EQUAL( size_t( 48 ), pFirst->_size );
    SW_EXPECT_TRUE( pFirst->isAlive() );

#if !defined( SW_SHIPPING )
    registry.unregisterTypesByModule( "TestStableAddress" );
    SW_EXPECT_NULL( registry.findType( sw::hashed_string( "swtest::StableFirst" ) ) );
    SW_EXPECT_FALSE( pFirst->isAlive() );
    uint32 aliveFillerCount = 0;
    registry.forEachType( [&aliveFillerCount]( const sw::TypeInfo& typeInfo )
    {
        if ( typeInfo._moduleName == sw::hashed_string( "TestStableAddress" ) )
            ++aliveFillerCount;
    } );
    SW_EXPECT_EQUAL( 0u, aliveFillerCount );
#endif
}
