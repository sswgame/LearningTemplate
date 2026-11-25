#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Math/Radians.h"
#include "Core/Math/VectorMath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/PropertyRoleUtil.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Reflection/TextAddress.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

// ReflectionPropertyRoleTest — 역할 플래그. 네트워크(Replicated · RepNotify) · 세이브(SaveGame) · 시퀀서(Interp)가 읽는 프로퍼티 메타.

namespace
{
    const sw::PropertyInfo* findRoleProperty( const sw::hashed_string& name )
    {
        const sw::TypeInfo* pType = sw::PropertyRoleActor::StaticType();
        return pType != nullptr ? pType->findPropertyInHierarchy( name ) : nullptr;
    }

    sw::SerializeContext makeSaveContext()
    {
        return sw::SerializeContext::deriveFromDefault( sw::SerializePurpose::SaveGame );
    }
} // namespace

/**
 * @brief [ReflectionPropertyRoleTest] 역할 애노테이션이 프로퍼티 메타에 닿는다 — RepNotify 는 Replicated 를 켠다
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, AnnotationsReachPropertyMetadata )
{
    const sw::TypeInfo*     pType         = sw::PropertyRoleActor::StaticType();
    const sw::PropertyInfo* pHealth       = findRoleProperty( "_health" );
    const sw::PropertyInfo* pGold         = findRoleProperty( "_gold" );
    const sw::PropertyInfo* pSessionScore = findRoleProperty( "_sessionScore" );
    const sw::PropertyInfo* pOpacity      = findRoleProperty( "_opacity" );
    SW_REQUIRE_NOT_NULL( pType );
    SW_REQUIRE_NOT_NULL( pHealth );
    SW_REQUIRE_NOT_NULL( pGold );
    SW_REQUIRE_NOT_NULL( pSessionScore );
    SW_REQUIRE_NOT_NULL( pOpacity );

    SW_EXPECT_TRUE( pHealth->_metadata->_bReplicated == SW_TRUE );
    SW_EXPECT_TRUE( pHealth->_metadata->_repNotify == sw::hashed_string( "onHealthReplicated" ) );
    SW_EXPECT_NOT_NULL( pHealth->_pRepNotify );

    SW_EXPECT_TRUE( pGold->_metadata->_bSaveGame == SW_TRUE );
    SW_EXPECT_TRUE( pSessionScore->_metadata->_bSaveGame == SW_FALSE );
    SW_EXPECT_TRUE( pOpacity->_metadata->_bInterp == SW_TRUE );

    sw::vector<const sw::PropertyInfo*> listReplicated;
    sw::PropertyRoleUtil::collectReplicatedProperties( *pType, listReplicated );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), listReplicated.size() );
    SW_EXPECT_TRUE( listReplicated[0]->_name == sw::hashed_string( "_health" ) );
    SW_EXPECT_TRUE( listReplicated[1]->_name == sw::hashed_string( "_team" ) );
}

/**
 * @brief [ReflectionPropertyRoleTest] RepNotify 는 선언한 모양대로 불린다 — 이전 값을 받는 함수에는 이전 값이, 안 받는 함수에는 아무것도
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, RepNotifyCallsTheDeclaredFunction )
{
    const sw::PropertyInfo* pHealth = findRoleProperty( "_health" );
    const sw::PropertyInfo* pTeam   = findRoleProperty( "_team" );
    const sw::PropertyInfo* pGold   = findRoleProperty( "_gold" );
    SW_REQUIRE_NOT_NULL( pHealth );
    SW_REQUIRE_NOT_NULL( pTeam );
    SW_REQUIRE_NOT_NULL( pGold );

    sw::PropertyRoleActor actor;
    const int32           oldHealth = actor._health;
    actor._health                   = 40; // 네트워크 계층이 받은 값을 쓴 뒤
    SW_EXPECT_TRUE( sw::PropertyRoleUtil::callRepNotify( *pHealth, sw::MutableObjectAddress{ &actor }, sw::ValueAddress{ &oldHealth } ) );
    SW_EXPECT_EQUAL( 100, actor._oldHealthSeen );

    SW_EXPECT_TRUE( sw::PropertyRoleUtil::callRepNotify( *pTeam, sw::MutableObjectAddress{ &actor }, sw::ValueAddress{ nullptr } ) );
    SW_EXPECT_EQUAL( 1, actor._teamNotifyCount );

    // RepNotify 가 없는 프로퍼티는 부르지 않는다
    SW_EXPECT_FALSE( sw::PropertyRoleUtil::callRepNotify( *pGold, sw::MutableObjectAddress{ &actor }, sw::ValueAddress{ nullptr } ) );
}

/**
 * @brief [ReflectionPropertyRoleTest] SaveGame 이 있는 타입은 세이브 직렬화에서 그것만 쓰고 읽는다 — 나머지는 지금 값 그대로(기본값으로도 되돌리지 않는다)
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, SaveGameOptInWritesOnlyFlaggedProperties )
{
    const sw::TypeInfo* pType = sw::PropertyRoleActor::StaticType();
    SW_REQUIRE_NOT_NULL( pType );
    SW_EXPECT_TRUE( pType->hasSaveGameProperty() );

    sw::PropertyRoleActor saved;
    saved._gold         = 50;
    saved._heroName     = "Aria";
    saved._health       = 10;
    saved._sessionScore = 99;

    sw::Archive writer;
    SW_REQUIRE_TRUE( writer.serializeObject( saved, makeSaveContext() ) );

    sw::PropertyRoleActor loaded;
    loaded._sessionScore = 5;
    sw::Archive reader( writer.getData(), writer.getSize() );
    SW_REQUIRE_TRUE( reader.deserializeObject( loaded, makeSaveContext() ) );
    SW_EXPECT_EQUAL( 50, loaded._gold );
    SW_EXPECT_EQUAL( sw::string( "Aria" ), loaded._heroName );
    SW_EXPECT_EQUAL( 100, loaded._health );     // 쓰지 않았다
    SW_EXPECT_EQUAL( 5, loaded._sessionScore ); // 덮지도, Default="7" 로 되돌리지도 않는다

    // 스냅샷 문맥은 전부 쓴다
    const sw::SerializeContext& snapshotContext = sw::SerializeContext::getDefault( sw::SerializePurpose::Snapshot );
    sw::Archive                 fullWriter;
    SW_REQUIRE_TRUE( fullWriter.serializeObject( saved, snapshotContext ) );
    sw::PropertyRoleActor fullLoaded;
    sw::Archive           fullReader( fullWriter.getData(), fullWriter.getSize() );
    SW_REQUIRE_TRUE( fullReader.deserializeObject( fullLoaded, snapshotContext ) );
    SW_EXPECT_EQUAL( 10, fullLoaded._health );
    SW_EXPECT_EQUAL( 99, fullLoaded._sessionScore );
    SW_EXPECT_TRUE( fullWriter.getSize() > writer.getSize() );
}

/**
 * @brief [ReflectionPropertyRoleTest] 판정 비트(세이브 · POD · 검증)는 등록이 미리 구한다 — 레지스트리 밖 사본은 구하기 전에는 내려가 있고, 명시 계산이 같은 답을 낸다
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, DerivedFlagsAreBuiltByRegistrationNotByFirstQuery )
{
    const sw::TypeInfo* pType = sw::PropertyRoleActor::StaticType();
    SW_REQUIRE_NOT_NULL( pType );
    SW_EXPECT_TRUE( pType->hasDerivedFlags() );

    sw::TypeInfo copy = *pType; // 사본은 파생 캐시를 비운다(대입 연산자)
    SW_EXPECT_FALSE( copy.hasDerivedFlags() );
    copy.rebuildDerivedCaches();
    copy.computeDerivedFlags();
    SW_EXPECT_EQUAL( pType->hasSaveGameProperty(), copy.hasSaveGameProperty() );
    SW_EXPECT_EQUAL( pType->usesPODCopyFastPath(), copy.usesPODCopyFastPath() );
    SW_EXPECT_EQUAL( pType->hasValidator(), copy.hasValidator() );
}

/**
 * @brief [ReflectionPropertyRoleTest] 대입은 지어 둔 파생 캐시(이름 맵 · 판정 · 상속 판)를 한 번에 비운다(`TypeDerivedCache`, `_derived = {}`). 다시 지으면 같은 답이다
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, AssignmentClearsEveryDerivedCache )
{
    const sw::TypeInfo* pType = sw::PropertyRoleActor::StaticType();
    SW_REQUIRE_NOT_NULL( pType );

    sw::TypeInfo copy = *pType;
    copy.rebuildDerivedCaches();
    copy.computeDerivedFlags();
    SW_REQUIRE_TRUE( copy.isLookupCacheBuilt() );
    SW_REQUIRE_TRUE( copy.hasDerivedFlags() );

    copy = *pType; // 지어 둔 사본에 다시 대입한다
    SW_EXPECT_FALSE( copy.isLookupCacheBuilt() );
    SW_EXPECT_FALSE( copy.hasDerivedFlags() );

    copy.rebuildDerivedCaches();
    copy.computeDerivedFlags();
    SW_EXPECT_TRUE( copy.isLookupCacheBuilt() );
    SW_EXPECT_EQUAL( pType->getPropertiesWithBase().size(), copy.getPropertiesWithBase().size() );
    SW_EXPECT_EQUAL( pType->hasSaveGameProperty(), copy.hasSaveGameProperty() );
}

/**
 * @brief [ReflectionPropertyRoleTest] SaveGame 이 하나도 없는 타입은 세이브 문맥에서도 전부 쓴다(옵트인이 아닌 타입은 지금 동작 그대로)
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, TypeWithoutSaveGameSavesEverything )
{
    const sw::TypeInfo* pType = sw::SampleTestActor::StaticType();
    SW_REQUIRE_NOT_NULL( pType );
    SW_EXPECT_FALSE( pType->hasSaveGameProperty() );

    sw::SampleTestActor saved;
    saved._hp   = 7;
    saved._name = "Saved";
    sw::Archive writer;
    SW_REQUIRE_TRUE( writer.serializeObject( saved, makeSaveContext() ) );
    sw::SampleTestActor loaded;
    sw::Archive         reader( writer.getData(), writer.getSize() );
    SW_REQUIRE_TRUE( reader.deserializeObject( loaded, makeSaveContext() ) );
    SW_EXPECT_EQUAL( 7, loaded._hp );
    SW_EXPECT_EQUAL( sw::string( "Saved" ), loaded._name );
}

/**
 * @brief [ReflectionPropertyRoleTest] 값 트랙이 Interp 프로퍼티를 섞는다 — 실수 선형 · 정수 반올림 · 벡터 · 회전(slerp), 섞을 수 없는 타입은 거절
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, InterpBlendsNumbersVectorsAndRotations )
{
    const sw::TypeInfo*     pType     = sw::PropertyRoleActor::StaticType();
    const sw::PropertyInfo* pOpacity  = findRoleProperty( "_opacity" );
    const sw::PropertyInfo* pStep     = findRoleProperty( "_step" );
    const sw::PropertyInfo* pOffset   = findRoleProperty( "_offset" );
    const sw::PropertyInfo* pRotation = findRoleProperty( "_rotation" );
    SW_REQUIRE_NOT_NULL( pType );
    SW_REQUIRE_NOT_NULL( pOpacity );
    SW_REQUIRE_NOT_NULL( pStep );
    SW_REQUIRE_NOT_NULL( pOffset );
    SW_REQUIRE_NOT_NULL( pRotation );

    sw::PropertyRoleActor actor;
    const float32         opacityFrom = 0.0f;
    const float32         opacityTo   = 1.0f;
    SW_REQUIRE_TRUE( sw::PropertyRoleUtil::applyInterpolated( *pOpacity, sw::MutableObjectAddress{ &actor }, sw::ValueAddress{ &opacityFrom }, sw::ValueAddress{ &opacityTo }, 0.25f ) );
    SW_EXPECT_NEAR( 0.25f, actor._opacity, 1e-5f );

    const int32 stepFrom = 0;
    const int32 stepTo   = 10;
    SW_REQUIRE_TRUE( sw::PropertyRoleUtil::applyInterpolated( *pStep, sw::MutableObjectAddress{ &actor }, sw::ValueAddress{ &stepFrom }, sw::ValueAddress{ &stepTo }, 0.46f ) );
    SW_EXPECT_EQUAL( 5, actor._step );

    const sw::float3 offsetFrom( 0.0f, 0.0f, 0.0f );
    const sw::float3 offsetTo( 2.0f, 4.0f, -8.0f );
    SW_REQUIRE_TRUE( sw::PropertyRoleUtil::applyInterpolated( *pOffset, sw::MutableObjectAddress{ &actor }, sw::ValueAddress{ &offsetFrom }, sw::ValueAddress{ &offsetTo }, 0.5f ) );
    SW_EXPECT_NEAR( 1.0f, actor._offset._x, 1e-5f );
    SW_EXPECT_NEAR( -4.0f, actor._offset._z, 1e-5f );

    const sw::quaternion rotationFrom = sw::quaternion::Identity;
    const sw::quaternion rotationTo   = sw::quaternion::makeFromYawPitchRoll( sw::float3( 1.0f, 0.0f, 0.0f ) );
    SW_REQUIRE_TRUE( sw::PropertyRoleUtil::applyInterpolated( *pRotation, sw::MutableObjectAddress{ &actor }, sw::ValueAddress{ &rotationFrom }, sw::ValueAddress{ &rotationTo }, 1.0f ) );
    SW_EXPECT_NEAR( rotationTo._w, actor._rotation._w, 1e-4f );

    // 글은 섞지 않는다
    const sw::TypeInfo* pSample = sw::SampleTestActor::StaticType();
    SW_REQUIRE_NOT_NULL( pSample );
    const sw::PropertyInfo* pName = pSample->findProperty( "_name" );
    SW_REQUIRE_NOT_NULL( pName );
    SW_EXPECT_FALSE( sw::PropertyRoleUtil::isInterpolatable( *pName ) );

    sw::vector<const sw::PropertyInfo*> listInterp;
    sw::PropertyRoleUtil::collectInterpProperties( *pType, listInterp );
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), listInterp.size() );
}

/**
 * @brief [ReflectionPropertyRoleTest] 각(`Radians`) 칸도 섞는다 — 라디안 값을 감지 않고 직선으로 잇는다(float32 각 칸이던 때와 같은 값)
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, InterpBlendsRadians )
{
    const sw::PropertyInfo angleProp( sw::hashed_string( "_angle" ), sw::hashed_string( "Radians" ), 0 ); // 오프셋 0 — 인스턴스 주소가 곧 값 주소다
    SW_EXPECT_TRUE( sw::PropertyRoleUtil::isInterpolatable( angleProp ) );

    const sw::Radians from = sw::Radians::fromDegrees( 40.0f );
    const sw::Radians to   = sw::Radians::fromDegrees( 80.0f );
    sw::Radians       out{};
    SW_REQUIRE_TRUE( sw::PropertyRoleUtil::applyInterpolated( angleProp, sw::MutableObjectAddress{ &out }, sw::ValueAddress{ &from }, sw::ValueAddress{ &to }, 0.5f ) );
    SW_EXPECT_NEAR( sw::Radians::fromDegrees( 60.0f ).radians(), out.radians(), 1e-6f );
}

/**
 * @brief [ReflectionPropertyRoleTest] 등록된 모든 타입에서 Interp 는 섞을 수 있는 타입이고 RepNotify 는 부를 함수가 있다(파서 검사가 빠지면 여기서 걸린다)
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, EveryRegisteredRolePropertyIsUsable )
{
    sw::string failures;
    sw::engine::getSubsystem<sw::TypeRegistry>().forEachType( [&failures]( const sw::TypeInfo& type )
    {
        for ( const sw::PropertyInfo& prop : type._listProperty )
        {
            if ( prop._metadata->_bInterp == SW_TRUE && sw::PropertyRoleUtil::isInterpolatable( prop ) == false )
            {
                sw::StringBuilder<sw::constant::buffer::kMax256> failure;
                failure.appendFormat( "%#::%# (Interp) ", type._fullyQualifiedName.c_str(), prop._name.c_str() );
                failures += failure.view();
            }
            if ( prop._metadata->_repNotify.empty() == false && prop._pRepNotify == nullptr )
            {
                sw::StringBuilder<sw::constant::buffer::kMax256> failure;
                failure.appendFormat( "%#::%# (RepNotify) ", type._fullyQualifiedName.c_str(), prop._name.c_str() );
                failures += failure.view();
            }
        }
    } );
    SW_EXPECT_TRUE_MSG( failures.empty(), failures.c_str() );
}

/**
 * @brief [ReflectionPropertyRoleTest] 내장 표(`ReflectBuiltins.xxx`)의 Interp 역할 줄마다 그 타입의 칸이 섞이고, Interp 가 아닌 줄은 섞이지 않는다
 * @details 시퀀서 · 파서 · UI 애니메이션이 같은 칸을 읽는다 — 손 목록이 셋이던 때는 하나만 늘고 둘이 남았다.
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, InterpRoleRowsAreInterpolatable )
{
    for ( size_t index = 0; index < static_cast<size_t>( sw::ReflectBuiltinIndex::kCount ); ++index )
    {
        const sw::ReflectBuiltinIndex  builtinIndex = static_cast<sw::ReflectBuiltinIndex>( index );
        const sw::ReflectBuiltinTraits traits       = sw::ReflectBuiltinTraitTable::get( builtinIndex );
        const sw::hashed_string&       typeName     = sw::ReflectedValueUtil::getBuiltinTypeName( builtinIndex );
        const sw::PropertyInfo         prop( sw::hashed_string( "_value" ), typeName, 0 );
        SW_EXPECT_TRUE_MSG( sw::PropertyRoleUtil::isInterpolatable( prop ) == traits.hasRole( sw::ReflectBuiltinRole::Interp ), typeName.c_str() );
    }
}

/**
 * @brief [ReflectionPropertyRoleTest] 내장 표의 Layout 이 성분 수와 크기를 정한다 — f32 · i32 는 멤버 수 × 4 바이트가 등록된 크기이고, num · angle 은 성분 하나, none 은 0 이다
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, BuiltinLayoutGivesComponentCountAndSize )
{
    const sw::TypeRegistry& registry = sw::engine::getSubsystem<sw::TypeRegistry>();
    for ( size_t index = 0; index < static_cast<size_t>( sw::ReflectBuiltinIndex::kCount ); ++index )
    {
        const sw::ReflectBuiltinIndex  builtinIndex = static_cast<sw::ReflectBuiltinIndex>( index );
        const sw::ReflectBuiltinTraits traits       = sw::ReflectBuiltinTraitTable::get( builtinIndex );
        const sw::hashed_string&       typeName     = sw::ReflectedValueUtil::getBuiltinTypeName( builtinIndex );
        const uint8                    count        = sw::ReflectedValueUtil::getBuiltinComponentCount( builtinIndex );
        switch ( traits._layout )
        {
            case sw::ReflectBuiltinLayout::Float32:
            case sw::ReflectBuiltinLayout::Int32:
            {
                const sw::TypeInfo* pType = registry.findType( typeName );
                SW_REQUIRE_TRUE_MSG( pType != nullptr, typeName.c_str() );
                SW_EXPECT_TRUE_MSG( count > 0 && pType->_size == static_cast<size_t>( count ) * sizeof( float32 ), typeName.c_str() );
                break;
            }
            case sw::ReflectBuiltinLayout::Number:
            case sw::ReflectBuiltinLayout::Angle:
                SW_EXPECT_TRUE_MSG( count == 1, typeName.c_str() );
                break;
            case sw::ReflectBuiltinLayout::None:
                SW_EXPECT_TRUE_MSG( count == 0, typeName.c_str() );
                break;
        }
    }
    SW_EXPECT_EQUAL( static_cast<uint8>( 2 ), sw::ReflectedValueUtil::getBuiltinComponentCount( sw::ReflectBuiltinIndex::kint2 ) );
    SW_EXPECT_TRUE( sw::ReflectBuiltinTraitTable::get( sw::ReflectBuiltinIndex::kint2 )._layout == sw::ReflectBuiltinLayout::Int32 );
    SW_EXPECT_EQUAL( static_cast<uint8>( 16 ), sw::ReflectedValueUtil::getBuiltinComponentCount( sw::ReflectBuiltinIndex::kfloat4x4 ) );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0 ), sw::ReflectedValueUtil::getBuiltinComponentCount( sw::ReflectBuiltinIndex::kNone ) );
}

namespace
{
    // 래퍼 형식이 정하는 모양 — 표(`ReflectBuiltins.xxx` CONTAINER 줄)의 종류 · State · Fixed 와 생성 코드가 이 값으로 대조한다.
    using VectorWrapperT = sw::VectorWrapper<sw::vector<int32>>;
    using SetWrapperT    = sw::SetWrapper<sw::set<int32>>;
    using ArrayWrapperT  = sw::ArrayWrapper<int32[4]>;
    using MapWrapperT    = sw::MapWrapper<sw::map<int32, int32>>;
    static_assert( sw::ContainerWrapperOf<VectorWrapperT>::matchesTable( sw::ContainerKind::Sequence, true, false ) );
    static_assert( sw::ContainerWrapperOf<SetWrapperT>::matchesTable( sw::ContainerKind::Sequence, false, false ) );
    static_assert( sw::ContainerWrapperOf<ArrayWrapperT>::matchesTable( sw::ContainerKind::Sequence, true, true ) );
    static_assert( sw::ContainerWrapperOf<MapWrapperT>::matchesTable( sw::ContainerKind::Map, false, false ) );
    static_assert( sw::ContainerWrapperOf<SetWrapperT>::matchesTable( sw::ContainerKind::Sequence, true, false ) == false, "집합에 State 를 달면 컴파일이 멈춰야 한다" );
    static_assert( sw::ContainerWrapperOf<VectorWrapperT>::matchesTable( sw::ContainerKind::Map, true, false ) == false, "시퀀스 래퍼를 Map 이라 적으면 컴파일이 멈춰야 한다" );
} // namespace

/**
 * @brief [ReflectionPropertyRoleTest] 상태 직렬화 · 해시가 싣는 시퀀스는 차례가 정해진 평평한 시퀀스뿐이다 — 집합 · 맵은 nullptr(`PropertyInfo::findStateSequence`)
 * @details 래퍼 가상 함수(`allowsInPlaceElementWrite` · `isFixedSize`)가 같은 형식의 정적 값과 같아 표 · 생성 코드 · 런타임 판정이 한 값을 본다.
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, StateSequenceIsTheOrderedFlatSequence )
{
    const auto makeContainer = []( const sw::ContainerKind kind, const sw::IContainerWrapper* pWrapper )
    { return sw::PropertyInfo( sw::hashed_string( "_list" ), sw::hashed_string( "container" ), 0, true, kind, sw::hashed_string( "int32" ), {}, pWrapper ); };
    const sw::PropertyInfo vectorProp = makeContainer( sw::ContainerKind::Sequence, &sw::ContainerWrapperOf<VectorWrapperT>::kInstance );
    const sw::PropertyInfo arrayProp  = makeContainer( sw::ContainerKind::Sequence, &sw::ContainerWrapperOf<ArrayWrapperT>::kInstance );
    const sw::PropertyInfo setProp    = makeContainer( sw::ContainerKind::Sequence, &sw::ContainerWrapperOf<SetWrapperT>::kInstance );
    const sw::PropertyInfo mapProp    = makeContainer( sw::ContainerKind::Map, &sw::ContainerWrapperOf<MapWrapperT>::kInstance );
    const sw::PropertyInfo scalarProp( sw::hashed_string( "_value" ), sw::hashed_string( "int32" ), 0 );

    SW_EXPECT_TRUE( vectorProp.findStateSequence() != nullptr );
    SW_EXPECT_TRUE( arrayProp.findStateSequence() != nullptr );
    SW_EXPECT_TRUE( setProp.findStateSequence() == nullptr );
    SW_EXPECT_TRUE( mapProp.findStateSequence() == nullptr );
    SW_EXPECT_TRUE( scalarProp.findStateSequence() == nullptr );
    SW_EXPECT_TRUE( sw::ContainerWrapperOf<ArrayWrapperT>::kInstance.isFixedSize() );
    SW_EXPECT_FALSE( sw::ContainerWrapperOf<SetWrapperT>::kInstance.allowsInPlaceElementWrite() );
}

/**
 * @brief [ReflectionPropertyRoleTest] State 줄은 모두 실릴 길이 있다 — 손 처리기 목록(`hasStateHandHandler`)의 줄은 State 이고 Layout 이 none 이며, 각(angle)은 State 가 아니다
 * @details 손 처리기 목록은 하나다. 상태 직렬화 · 상태 해시 · 테스트 값 바꾸기가 같은 목록을 보고, `ReflectedValue.cpp` 가 줄마다 `isStateCarried` 를 컴파일 때 본다.
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, StateRowsHaveOneHandHandlerList )
{
    for ( size_t index = 0; index < static_cast<size_t>( sw::ReflectBuiltinIndex::kCount ); ++index )
    {
        const sw::ReflectBuiltinIndex  builtinIndex = static_cast<sw::ReflectBuiltinIndex>( index );
        const sw::ReflectBuiltinTraits traits       = sw::ReflectBuiltinTraitTable::get( builtinIndex );
        const sw::hashed_string&       typeName     = sw::ReflectedValueUtil::getBuiltinTypeName( builtinIndex );
        SW_EXPECT_TRUE_MSG( sw::ReflectBuiltinTraitTable::isStateCarried( builtinIndex ), typeName.c_str() );
        if ( sw::ReflectBuiltinTraitTable::hasStateHandHandler( builtinIndex ) )
            SW_EXPECT_TRUE_MSG( traits.hasRole( sw::ReflectBuiltinRole::State ) && traits._layout == sw::ReflectBuiltinLayout::None, typeName.c_str() );
        if ( traits._layout == sw::ReflectBuiltinLayout::Angle )
            SW_EXPECT_FALSE_MSG( traits.hasRole( sw::ReflectBuiltinRole::State ), typeName.c_str() );
    }
}

/**
 * @brief [ReflectionPropertyRoleTest] 핸들 칸도 세이브가 쓴다 — 대상의 고정 지역 id · 키 해시로 적혀 다른 실행에서도 같은 대상을 찾는다(감사 A-2, ObjectStateSerializerTest.HandleToMissingComponentSurvivesBinarySaveRoundTrip)
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, HandlesAreSavedByStableKey )
{
    const sw::PropertyInfo handleProp( sw::hashed_string( "_target" ), sw::hashed_string( "GameObjectHandle" ), 0 );
    const sw::PropertyInfo handleListProp( sw::hashed_string( "_listTarget" ), sw::hashed_string( "vector" ), 0, true, sw::ContainerKind::Sequence,
                                           sw::hashed_string( "ComponentHandle" ) );
    SW_EXPECT_TRUE( sw::ReflectBuiltinTraitTable::get( sw::ReflectBuiltinIndex::kGameObjectHandle ).hasRole( sw::ReflectBuiltinRole::State ) );

    const sw::TypeInfo* pOwner = sw::AnyValueActor::StaticType(); // SaveGame 칸이 없는 타입 — 세이브가 모든 칸을 쓴다
    SW_REQUIRE_NOT_NULL( pOwner );
    const sw::SerializeContext saveContext = makeSaveContext();
    SW_EXPECT_TRUE( sw::SerializerUtil::shouldSerializeProperty( *pOwner, handleProp, saveContext ) );
    SW_EXPECT_TRUE( sw::SerializerUtil::shouldSerializeProperty( *pOwner, handleListProp, saveContext ) );
}
