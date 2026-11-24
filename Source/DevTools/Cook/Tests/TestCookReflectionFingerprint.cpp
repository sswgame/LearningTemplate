#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Cook/CookReflectionFingerprint.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/Tests/ScopedTestModule.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

// CookReflectionFingerprintTest — 쿠킹 키의 리플렉션 지문: 저작 칸의 배치(역할 · 형식 · 기반)와 쿠킹 결과를 정하는 메타데이터가 바뀌면 다르고, 쿠킹본이 싣지 않는 역할(SaveGame)은 같다(nogpu).

using namespace sw;

namespace
{
    struct FingerprintTestUtil
    {
        static PropertyInfo makeProperty( const utf8* pName, const utf8* pTypeName ) { return PropertyInfo{ hashed_string( pName ), hashed_string( pTypeName ), 0 }; }

        static TypeInfo makeType( const utf8* pName, const utf8* pParentName )
        {
            StringBuilder<constant::buffer::kMax128> qualified;
            qualified.appendFormat( "swtest::%#", pName );
            TypeInfo info;
            info._name               = hashed_string( pName );
            info._fullyQualifiedName = hashed_string( qualified.view() );
            info._moduleName         = hashed_string( "TestCookFingerprint" );
            info._size               = 8;
            if ( pParentName != nullptr )
            {
                StringBuilder<constant::buffer::kMax128> parent;
                parent.appendFormat( "swtest::%#", pParentName );
                info._parentFQN = hashed_string( parent.view() );
            }
            return info;
        }

        /** @brief @p type 을 등록(다시 등록)한 뒤의 지문입니다. */
        static uint64 computeWith( const TypeInfo& type )
        {
            TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
            registry.registerClass( type );
            return CookReflectionFingerprint::compute( registry );
        }
    };
} // namespace

/** @brief [CookReflectionFingerprintTest] 저장 역할 · 원소 형식 · 선언 기본값 · 에셋 경로 표시가 바뀌면 지문이 다르고, SaveGame 만 바꾸면 같다 */
SW_TEST_CASE( CookReflectionFingerprintTest, RolesAndMetadataChangeTheFingerprint )
{
    const test::ScopedTestModule scopedModule( "TestCookFingerprint" );
    TypeInfo                     type = FingerprintTestUtil::makeType( "FingerprintRole", nullptr );
    type._listProperty                = { FingerprintTestUtil::makeProperty( "_value", "int32" ) };
    const uint64 plain                = FingerprintTestUtil::computeWith( type );
    SW_EXPECT_EQUAL( plain, FingerprintTestUtil::computeWith( type ) );

    const auto computeEdited = [&type]( void ( *pEdit )( PropertyMetadata& ) )
    {
        TypeInfo edited = type;
        pEdit( edited._listProperty[0]._metadata.edit() );
        return FingerprintTestUtil::computeWith( edited );
    };
    SW_EXPECT_EQUAL( plain, computeEdited( []( PropertyMetadata& metadata )
    { metadata._bSaveGame = SW_TRUE; } ) );
    SW_EXPECT_TRUE( plain != computeEdited( []( PropertyMetadata& metadata )
    { metadata._bTransient = SW_TRUE; } ) );
    SW_EXPECT_TRUE( plain != computeEdited( []( PropertyMetadata& metadata )
    { metadata._bRuntimeState = SW_TRUE; } ) );
    SW_EXPECT_TRUE( plain != computeEdited( []( PropertyMetadata& metadata )
    { metadata._bRequired = SW_TRUE; } ) );
    SW_EXPECT_TRUE( plain != computeEdited( []( PropertyMetadata& metadata )
    { metadata._bAssetPath = SW_TRUE; } ) );
    SW_EXPECT_TRUE( plain != computeEdited( []( PropertyMetadata& metadata )
    { metadata._defaultValue = "7"; } ) );

    type._listProperty[0]._elementTypeName = hashed_string( "float32" );
    SW_EXPECT_TRUE( plain != FingerprintTestUtil::computeWith( type ) );
}

/** @brief [CookReflectionFingerprintTest] 자기 칸이 같고 기반만 다른 타입은 지문이 다르다 */
SW_TEST_CASE( CookReflectionFingerprintTest, BaseTypeChangesTheFingerprint )
{
    const test::ScopedTestModule scopedModule( "TestCookFingerprint" );
    TypeInfo                     baseA = FingerprintTestUtil::makeType( "FingerprintBaseA", nullptr );
    baseA._listProperty                = { FingerprintTestUtil::makeProperty( "_a", "int32" ) };
    TypeInfo baseB                     = FingerprintTestUtil::makeType( "FingerprintBaseB", nullptr );
    baseB._listProperty                = { FingerprintTestUtil::makeProperty( "_b", "int32" ) };
    (void)FingerprintTestUtil::computeWith( baseA );
    (void)FingerprintTestUtil::computeWith( baseB );

    TypeInfo derived      = FingerprintTestUtil::makeType( "FingerprintDerived", "FingerprintBaseA" );
    derived._listProperty = { FingerprintTestUtil::makeProperty( "_own", "int32" ) };
    const uint64 fromA    = FingerprintTestUtil::computeWith( derived );
    derived._parentFQN    = baseB._fullyQualifiedName;
    SW_EXPECT_TRUE( fromA != FingerprintTestUtil::computeWith( derived ) );
}
