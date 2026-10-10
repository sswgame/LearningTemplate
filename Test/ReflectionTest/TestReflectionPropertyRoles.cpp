#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/PropertyRoleUtil.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Format/Archive.h"

#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 역할 플래그 — 네트워크(Replicated · RepNotify) · 세이브(SaveGame) · 시퀀서(Interp) · 설정(Config)이 읽는 프로퍼티 메타.

namespace
{
    const sw::TypeInfo& getRoleType()
    {
        const sw::TypeInfo* pType = sw::PropertyRoleActor::StaticType();
        SW_ASSERT( pType != nullptr );
        return *pType;
    }

    const sw::PropertyInfo& getRoleProperty( const sw::hashed_string& name )
    {
        const sw::PropertyInfo* pProp = getRoleType().findPropertyInHierarchy( name );
        SW_ASSERT( pProp != nullptr );
        return *pProp;
    }

    sw::SerializeContext makeSaveContext()
    {
        sw::SerializeContext context = sw::SerializeContext::deriveFromDefault();
        context.setSaveGameOnly( true );
        return context;
    }
} // namespace

/**
 * @brief [ReflectionPropertyRoleTest] 역할 애노테이션이 프로퍼티 메타에 닿는다 — RepNotify 는 Replicated 를 켠다
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, AnnotationsReachPropertyMetadata )
{
    const sw::PropertyInfo& health = getRoleProperty( "_health" );
    SW_EXPECT_TRUE( health._metadata._bReplicated == SW_TRUE );
    SW_EXPECT_TRUE( health._metadata._repNotify == sw::hashed_string( "onHealthReplicated" ) );
    SW_EXPECT_NOT_NULL( health._pRepNotify );

    SW_EXPECT_TRUE( getRoleProperty( "_gold" )._metadata._bSaveGame == SW_TRUE );
    SW_EXPECT_TRUE( getRoleProperty( "_sessionScore" )._metadata._bSaveGame == SW_FALSE );
    SW_EXPECT_TRUE( getRoleProperty( "_opacity" )._metadata._bInterp == SW_TRUE );

    sw::vector<const sw::PropertyInfo*> listReplicated;
    sw::PropertyRoleUtil::collectReplicatedProperties( getRoleType(), listReplicated );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listReplicated.size() );
    SW_EXPECT_TRUE( listReplicated[0]->_name == sw::hashed_string( "_health" ) );
    SW_EXPECT_TRUE( listReplicated[1]->_name == sw::hashed_string( "_team" ) );
}

/**
 * @brief [ReflectionPropertyRoleTest] RepNotify 는 선언한 모양대로 불린다 — 이전 값을 받는 함수에는 이전 값이, 안 받는 함수에는 아무것도
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, RepNotifyCallsTheDeclaredFunction )
{
    sw::PropertyRoleActor actor;
    const int32           oldHealth = actor._health;
    actor._health                   = 40; // 네트워크 계층이 받은 값을 쓴 뒤
    SW_EXPECT_TRUE( sw::PropertyRoleUtil::callRepNotify( getRoleProperty( "_health" ), &actor, &oldHealth ) );
    SW_EXPECT_EQUAL( 100, actor._oldHealthSeen );

    SW_EXPECT_TRUE( sw::PropertyRoleUtil::callRepNotify( getRoleProperty( "_team" ), &actor, nullptr ) );
    SW_EXPECT_EQUAL( 1, actor._teamNotifyCount );

    // RepNotify 가 없는 프로퍼티는 부르지 않는다
    SW_EXPECT_FALSE( sw::PropertyRoleUtil::callRepNotify( getRoleProperty( "_gold" ), &actor, nullptr ) );
}

/**
 * @brief [ReflectionPropertyRoleTest] SaveGame 이 있는 타입은 세이브 직렬화에서 그것만 쓰고 읽는다 — 나머지는 지금 값 그대로(기본값으로도 되돌리지 않는다)
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, SaveGameOptInWritesOnlyFlaggedProperties )
{
    SW_EXPECT_TRUE( getRoleType().hasSaveGameProperty() );

    sw::PropertyRoleActor saved;
    saved._gold         = 50;
    saved._heroName     = "Aria";
    saved._health       = 10;
    saved._sessionScore = 99;

    sw::Archive writer;
    SW_ASSERT_TRUE( writer.serializeObject( saved, makeSaveContext() ) );

    sw::PropertyRoleActor loaded;
    loaded._sessionScore = 5;
    sw::Archive reader( writer.getData(), writer.getSize() );
    SW_ASSERT_TRUE( reader.deserializeObject( loaded, makeSaveContext() ) );
    SW_EXPECT_EQUAL( 50, loaded._gold );
    SW_EXPECT_EQUAL( sw::string( "Aria" ), loaded._heroName );
    SW_EXPECT_EQUAL( 100, loaded._health );     // 쓰지 않았다
    SW_EXPECT_EQUAL( 5, loaded._sessionScore ); // 덮지도, Default="7" 로 되돌리지도 않는다

    // 세이브 문맥이 아니면 전부 쓴다(지금 동작 그대로)
    sw::Archive fullWriter;
    SW_ASSERT_TRUE( fullWriter.serializeObject( saved ) );
    sw::PropertyRoleActor fullLoaded;
    sw::Archive           fullReader( fullWriter.getData(), fullWriter.getSize() );
    SW_ASSERT_TRUE( fullReader.deserializeObject( fullLoaded ) );
    SW_EXPECT_EQUAL( 10, fullLoaded._health );
    SW_EXPECT_EQUAL( 99, fullLoaded._sessionScore );
    SW_EXPECT_TRUE( fullWriter.getSize() > writer.getSize() );
}

/**
 * @brief [ReflectionPropertyRoleTest] SaveGame 이 하나도 없는 타입은 세이브 문맥에서도 전부 쓴다(옵트인이 아닌 타입은 지금 동작 그대로)
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, TypeWithoutSaveGameSavesEverything )
{
    const sw::TypeInfo* pType = sw::SampleTestActor::StaticType();
    SW_ASSERT_NOT_NULL( pType );
    SW_EXPECT_FALSE( pType->hasSaveGameProperty() );

    sw::SampleTestActor saved;
    saved._hp   = 7;
    saved._name = "Saved";
    sw::Archive writer;
    SW_ASSERT_TRUE( writer.serializeObject( saved, makeSaveContext() ) );
    sw::SampleTestActor loaded;
    sw::Archive         reader( writer.getData(), writer.getSize() );
    SW_ASSERT_TRUE( reader.deserializeObject( loaded, makeSaveContext() ) );
    SW_EXPECT_EQUAL( 7, loaded._hp );
    SW_EXPECT_EQUAL( sw::string( "Saved" ), loaded._name );
}

/**
 * @brief [ReflectionPropertyRoleTest] 값 트랙이 Interp 프로퍼티를 섞는다 — 실수 선형 · 정수 반올림 · 벡터 · 회전(slerp), 섞을 수 없는 타입은 거절
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, InterpBlendsNumbersVectorsAndRotations )
{
    sw::PropertyRoleActor actor;

    const float32 opacityFrom = 0.0f;
    const float32 opacityTo   = 1.0f;
    SW_ASSERT_TRUE( sw::PropertyRoleUtil::applyInterpolated( getRoleProperty( "_opacity" ), &actor, &opacityFrom, &opacityTo, 0.25f ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, actor._opacity, 1e-5f );

    const int32 stepFrom = 0;
    const int32 stepTo   = 10;
    SW_ASSERT_TRUE( sw::PropertyRoleUtil::applyInterpolated( getRoleProperty( "_step" ), &actor, &stepFrom, &stepTo, 0.46f ) );
    SW_EXPECT_EQUAL( 5, actor._step );

    const sw::float3 offsetFrom( 0.0f, 0.0f, 0.0f );
    const sw::float3 offsetTo( 2.0f, 4.0f, -8.0f );
    SW_ASSERT_TRUE( sw::PropertyRoleUtil::applyInterpolated( getRoleProperty( "_offset" ), &actor, &offsetFrom, &offsetTo, 0.5f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, actor._offset._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( -4.0f, actor._offset._z, 1e-5f );

    const sw::quaternion rotationFrom = sw::quaternion::Identity;
    const sw::quaternion rotationTo   = sw::quaternion::makeFromYawPitchRoll( sw::float3( 1.0f, 0.0f, 0.0f ) );
    SW_ASSERT_TRUE( sw::PropertyRoleUtil::applyInterpolated( getRoleProperty( "_rotation" ), &actor, &rotationFrom, &rotationTo, 1.0f ) );
    SW_EXPECT_NEAR_EQUAL( rotationTo._w, actor._rotation._w, 1e-4f );

    // 글 · 비트필드는 섞지 않는다
    const sw::TypeInfo* pSample = sw::SampleTestActor::StaticType();
    SW_ASSERT_NOT_NULL( pSample );
    SW_EXPECT_FALSE( sw::PropertyRoleUtil::isInterpolatable( *pSample->findProperty( "_name" ) ) );

    sw::vector<const sw::PropertyInfo*> listInterp;
    sw::PropertyRoleUtil::collectInterpProperties( getRoleType(), listInterp );
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), listInterp.size() );
}

/**
 * @brief [ReflectionPropertyRoleTest] 등록된 모든 타입에서 Interp 는 섞을 수 있는 타입이고 RepNotify 는 부를 함수가 있다(파서 검사가 빠지면 여기서 걸린다)
 */
SW_TEST_CASE( ReflectionPropertyRoleTest, EveryRegisteredRolePropertyIsUsable )
{
    sw::string failures;
    sw::engine::getTypeRegistry().forEachType( [&failures]( const sw::TypeInfo& type )
    {
        for ( const sw::PropertyInfo& prop : type._listProperty )
        {
            if ( prop._metadata._bInterp == SW_TRUE && sw::PropertyRoleUtil::isInterpolatable( prop ) == false )
                failures += sw::string( type._fullyQualifiedName.c_str() ) + "::" + prop._name.c_str() + " (Interp) ";
            if ( prop._metadata._repNotify.empty() == false && prop._pRepNotify == nullptr )
                failures += sw::string( type._fullyQualifiedName.c_str() ) + "::" + prop._name.c_str() + " (RepNotify) ";
        }
    } );
    SW_EXPECT_TRUE_MSG( failures.empty(), failures.c_str() );
}
