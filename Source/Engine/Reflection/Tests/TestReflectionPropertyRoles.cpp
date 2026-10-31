#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/PropertyRoleUtil.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Reflection/TextAddress.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

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
                failures += sw::string( type._fullyQualifiedName.c_str() ) + "::" + prop._name.c_str() + " (Interp) ";
            if ( prop._metadata->_repNotify.empty() == false && prop._pRepNotify == nullptr )
                failures += sw::string( type._fullyQualifiedName.c_str() ) + "::" + prop._name.c_str() + " (RepNotify) ";
        }
    } );
    SW_EXPECT_TRUE_MSG( failures.empty(), failures.c_str() );
}
