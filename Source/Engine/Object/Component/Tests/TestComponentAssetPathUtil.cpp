// 에셋 경로 순회(`ComponentAssetPathUtil::forEachAssetPath`) — 중첩 구조체 · 구조체 시퀀스 · 참조 시퀀스 안까지 내려가고 그 자리를 바꾼다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Tests/AssetRefTestTypes.h"
#include "Engine/Object/Component/ComponentAssetPathUtil.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/TypeInfo.h"

using namespace sw;

/**
 * @brief [ComponentAssetPathUtilTest] 에셋 경로 순회가 구조체 칸 · 구조체 시퀀스 원소 · 참조 시퀀스 원소 안의 경로까지 내려가고, 맨 위 칸을 알려 주며, 그 자리를 바꿀 수 있다.
 * @details 예전에는 맨 위 스칼라 칸만 읽어 핫 리로드 · 프리로드 · 옮김 반영이 중첩 안의 경로를 놓쳤다(감사 P0-4). 테스트는 순회 도우미와 같은 층에 둔다(에셋 층 테스트는 컴포넌트 층을 못 본다).
 */
SW_TEST_CASE( ComponentAssetPathUtilTest, AssetPathWalkReachesNestedValues )
{
    AssetRefSampleHolder holder;
    SW_REQUIRE_TRUE( holder._inner._reference.assign( "game/a/inner.reftarget.xml" ) );
    holder._listInner.resize( 1 );
    SW_REQUIRE_TRUE( holder._listInner[0]._reference.assign( "game/a/listed.reftarget.xml" ) );
    holder._listInner[0]._listReference.resize( 1 );
    SW_REQUIRE_TRUE( holder._listInner[0]._listReference[0].assign( "game/a/deep.reftarget.xml" ) );

    vector<string> listPath;
    vector<string> listTop;
    ComponentAssetPathUtil::forEachAssetPath( *AssetRefSampleHolder::StaticType(), &holder, AssetPathVisitor::create( [&listPath, &listTop]( const AssetPathValue& value )
    {
        if ( value.getPath().empty() )
            return;
        listPath.emplace_back( value.getPath() );
        listTop.emplace_back( value._pTopProperty->_name.c_str() );
        if ( value.getPath() == "game/a/deep.reftarget.xml" )
            SW_EXPECT_TRUE( value.assign( "game/b/deep.reftarget.xml" ) );
    } ) );
    SW_EXPECT_TRUE( ( listPath == vector<string>{ "game/a/inner.reftarget.xml", "game/a/listed.reftarget.xml", "game/a/deep.reftarget.xml" } ) );
    SW_EXPECT_TRUE( ( listTop == vector<string>{ "_inner", "_listInner", "_listInner" } ) );
    SW_EXPECT_TRUE( holder._listInner[0]._listReference[0] == "game/b/deep.reftarget.xml" );
}
