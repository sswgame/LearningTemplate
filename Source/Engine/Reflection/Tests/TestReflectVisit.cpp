#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Reflection/TypeInfo.h"

// ReflectVisitTest — 리플렉션 칸을 도는 한 곳(`ReflectPropertyVisitor`). 순서(기반부터) · 구조체 값과 시퀀스 원소로 내려가기 · 멈춤.

/**
 * @brief [ReflectVisitTest] 기반 칸이 먼저이고, 구조체 칸 다음에 그 안쪽 칸, 구조체 시퀀스면 원소마다 안쪽 칸이 깊이 하나 아래로 온다
 */
SW_TEST_CASE( ReflectVisitTest, VisitsBaseFirstAndNestedValues )
{
    sw::ValidatedChildActor actor;
    actor._listPart.resize( 2 );
    actor._listPart[1]._weight = 3.0f;

    const sw::TypeRegistry& registry = sw::engine::getSubsystem<sw::TypeRegistry>();
    const sw::TypeInfo*     pType    = registry.findType( sw::hashed_string( "sw::ValidatedChildActor" ) );
    SW_REQUIRE_TRUE( pType != nullptr );

    sw::ReflectVisitOptions options;
    options._bIntoValues = SW_TRUE;
    sw::vector<sw::string>           listVisited;
    sw::vector<uint32>               listDepth;
    float32                          lastWeight = 0.0f;
    const sw::ReflectPropertyVisitor visitor{ registry, options };
    const bool                       bFinished = visitor.visitProperties( *pType, static_cast<const void*>( &actor ), [&]( const sw::ReflectVisitConstField& field )
                          {
        listVisited.push_back( field._property._name.c_str() );
        listDepth.push_back( field._depth );
        if ( field._depth == 1 && field._property._name == sw::hashed_string( "_weight" ) )
            lastWeight = *static_cast<const float32*>( field._pValue );
        return sw::ReflectVisitFlow::Continue;
    } );

    SW_EXPECT_TRUE( bFinished );
    // _name, _min, _max, _part, (_weight), _listPart, (_weight), (_weight), _extra
    const sw::vector<sw::string> listExpected{ "_name", "_min", "_max", "_part", "_weight", "_listPart", "_weight", "_weight", "_extra" };
    const sw::vector<uint32>     listExpectedDepth{ 0, 0, 0, 0, 1, 0, 1, 1, 0 };
    SW_EXPECT_TRUE( listVisited == listExpected );
    SW_EXPECT_TRUE( listDepth == listExpectedDepth );
    SW_EXPECT_EQUAL( lastWeight, 3.0f );
}

/**
 * @brief [ReflectVisitTest] Stop 이면 그 자리에서 걷기가 끝나고 false 이고, SkipChildren 이면 그 칸 안으로 내려가지 않는다
 */
SW_TEST_CASE( ReflectVisitTest, StopEndsTheWalk )
{
    sw::ValidatedChildActor actor;
    actor._listPart.resize( 3 );
    const sw::TypeRegistry& registry = sw::engine::getSubsystem<sw::TypeRegistry>();
    const sw::TypeInfo*     pType    = registry.findType( sw::hashed_string( "sw::ValidatedChildActor" ) );
    SW_REQUIRE_TRUE( pType != nullptr );

    sw::ReflectVisitOptions options;
    options._bIntoValues                        = SW_TRUE;
    uint32                           visitCount = 0;
    const sw::ReflectPropertyVisitor visitor{ registry, options };
    const bool                       bFinished = visitor.visitProperties( *pType, static_cast<void*>( &actor ), [&visitCount]( const sw::ReflectVisitField& field )
                          {
        ++visitCount;
        return field._property._name == sw::hashed_string( "_max" ) ? sw::ReflectVisitFlow::Stop : sw::ReflectVisitFlow::Continue;
    } );
    SW_EXPECT_FALSE( bFinished );
    SW_EXPECT_EQUAL( visitCount, 3u ); // _name, _min, _max

    uint32 nestedCount = 0;
    SW_EXPECT_TRUE( visitor.visitProperties( *pType, static_cast<void*>( &actor ), [&nestedCount]( const sw::ReflectVisitField& field )
    {
        if ( field._depth > 0 )
            ++nestedCount;
        return sw::ReflectVisitFlow::SkipChildren;
    } ) );
    SW_EXPECT_EQUAL( nestedCount, 0u );
}

/**
 * @brief [ReflectVisitTest] 객체 주소가 nullptr 이면 스키마만 걷고(값 주소 없음, 내려가지 않음), `_bWithBase` 를 끄면 최상위 타입이 선언한 칸만 온다
 */
SW_TEST_CASE( ReflectVisitTest, SchemaWalkAndDeclaredPropertiesOnly )
{
    const sw::TypeRegistry& registry = sw::engine::getSubsystem<sw::TypeRegistry>();
    const sw::TypeInfo*     pType    = registry.findType( sw::hashed_string( "sw::ValidatedChildActor" ) );
    SW_REQUIRE_TRUE( pType != nullptr );

    sw::ReflectVisitOptions options;
    options._bIntoValues = SW_TRUE;
    options._bWithBase   = SW_FALSE;
    sw::vector<sw::string>           listVisited;
    bool                             bAnyValue = false;
    const sw::ReflectPropertyVisitor visitor{ registry, options };
    SW_EXPECT_TRUE( visitor.visitProperties( *pType, static_cast<const void*>( nullptr ), [&]( const sw::ReflectVisitConstField& field )
    {
        listVisited.push_back( field._property._name.c_str() );
        bAnyValue = bAnyValue || field._pValue != nullptr;
        return sw::ReflectVisitFlow::Continue;
    } ) );
    SW_EXPECT_TRUE( listVisited == sw::vector<sw::string>{ "_extra" } );
    SW_EXPECT_FALSE( bAnyValue );
}
