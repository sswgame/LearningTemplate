#include "pch.h"

#include "Core/Delegate/Delegate.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskArgs.h"
#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Cook/CookReflectionDigest.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/FunctionInfo.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/Tests/ScopedTestModule.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <memory>
#include <new>

// CookReflectionDigestTest — 쿠킹 키의 리플렉션 지문: 저작 칸의 배치(역할 · 형식 · 기반)와 쿠킹 결과를 정하는 메타데이터가 바뀌면 다르고, 쿠킹본이 싣지 않는 역할(SaveGame)은 같다(nogpu).

using namespace sw;

namespace
{
    struct DigestSample
    {
        int32 _value;
    };

    struct DigestTestUtil
    {
        static PropertyInfo makeProperty( const utf8* pName, const utf8* pTypeName ) { return PropertyInfo{ hashed_string( pName ), hashed_string( pTypeName ), 0 }; }

        static TypeInfo makeType( const utf8* pName, const utf8* pParentName )
        {
            StringBuilder<constant::buffer::kMax128> qualified;
            qualified.appendFormat( "swtest::%#", pName );
            TypeInfo info;
            info._name               = hashed_string( pName );
            info._fullyQualifiedName = hashed_string( qualified.view() );
            info._moduleName         = hashed_string( "TestCookDigest" );
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
            return CookReflectionDigest::compute( registry );
        }
    };
} // namespace

/** @brief [CookReflectionDigestTest] 저장 역할 · 원소 형식 · 선언 기본값 · 에셋 경로 표시가 바뀌면 지문이 다르고, SaveGame 만 바꾸면 같다 */
SW_TEST_CASE( CookReflectionDigestTest, RolesAndMetadataChangeTheDigest )
{
    const test::ScopedTestModule scopedModule( "TestCookDigest" );
    TypeInfo                     type = DigestTestUtil::makeType( "DigestRole", nullptr );
    type._listProperty                = { DigestTestUtil::makeProperty( "_value", "int32" ) };
    const uint64 plain                = DigestTestUtil::computeWith( type );
    SW_EXPECT_EQUAL( plain, DigestTestUtil::computeWith( type ) );

    const auto computeEdited = [&type]( void ( *pEdit )( PropertyMetadata& ) )
    {
        TypeInfo edited = type;
        pEdit( edited._listProperty[0]._metadata.edit() );
        return DigestTestUtil::computeWith( edited );
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
    SW_EXPECT_TRUE( plain != DigestTestUtil::computeWith( type ) );
}

/** @brief [CookReflectionDigestTest] 생성자가 주는 기본값만 다른 타입은 지문이 다르고, 만들 때마다 다른 값은 지문에 들지 않는다 */
SW_TEST_CASE( CookReflectionDigestTest, ConstructorDefaultsChangeTheDigest )
{
    const test::ScopedTestModule scopedModule( "TestCookDigest" );
    TypeInfo                     type = DigestTestUtil::makeType( "DigestDefault", nullptr );
    type._listProperty                = { DigestTestUtil::makeProperty( "_value", "int32" ) };
    type._size                        = sizeof( DigestSample );
    type._destroyInstance             = []( void* pInstance )
    { std::destroy_at( static_cast<DigestSample*>( pInstance ) ); };

    const auto computeConstructed = [&type]( Delegate<TaskValue( void*, const TaskArgs& )> constructor )
    {
        FunctionInfo method;
        method._name            = "$ctor";
        method._hashName        = hashed_string( "$ctor" );
        method._invoker         = std::move( constructor );
        TypeInfo constructed    = type;
        constructed._listMethod = { method };
        return DigestTestUtil::computeWith( constructed );
    };
    const auto makeConstructor = []( const int32 value )
    {
        return SW_DELEGATE_LAMBDA( Delegate<TaskValue( void*, const TaskArgs& )>, [value]( void* pMemory, const TaskArgs& ) -> TaskValue
        {
            new ( pMemory ) DigestSample{ value };
            return TaskValue{};
        } );
    };
    const uint64 withOne = computeConstructed( makeConstructor( 1 ) );
    SW_EXPECT_EQUAL( withOne, computeConstructed( makeConstructor( 1 ) ) );
    SW_EXPECT_TRUE( withOne != computeConstructed( makeConstructor( 2 ) ) );

    // 만들 때마다 다른 값(GUID · 런타임 id)은 기본값이 아니다 — 쿠킹마다 키가 달라지지 않는다.
    int32      nextValue       = 0;
    const auto makeEachTimeNew = [&nextValue]()
    {
        return SW_DELEGATE_LAMBDA( Delegate<TaskValue( void*, const TaskArgs& )>, [&nextValue]( void* pMemory, const TaskArgs& ) -> TaskValue
        {
            new ( pMemory ) DigestSample{ ++nextValue };
            return TaskValue{};
        } );
    };
    const uint64 eachTimeNew = computeConstructed( makeEachTimeNew() );
    SW_EXPECT_EQUAL( eachTimeNew, computeConstructed( makeEachTimeNew() ) );
}

/** @brief [CookReflectionDigestTest] 자기 칸이 같고 기반만 다른 타입은 지문이 다르다 */
SW_TEST_CASE( CookReflectionDigestTest, BaseTypeChangesTheDigest )
{
    const test::ScopedTestModule scopedModule( "TestCookDigest" );
    TypeInfo                     baseA = DigestTestUtil::makeType( "DigestBaseA", nullptr );
    baseA._listProperty                = { DigestTestUtil::makeProperty( "_a", "int32" ) };
    TypeInfo baseB                     = DigestTestUtil::makeType( "DigestBaseB", nullptr );
    baseB._listProperty                = { DigestTestUtil::makeProperty( "_b", "int32" ) };
    (void)DigestTestUtil::computeWith( baseA );
    (void)DigestTestUtil::computeWith( baseB );

    TypeInfo derived      = DigestTestUtil::makeType( "DigestDerived", "DigestBaseA" );
    derived._listProperty = { DigestTestUtil::makeProperty( "_own", "int32" ) };
    const uint64 fromA    = DigestTestUtil::computeWith( derived );
    derived._parentFQN    = baseB._fullyQualifiedName;
    SW_EXPECT_TRUE( fromA != DigestTestUtil::computeWith( derived ) );
}
