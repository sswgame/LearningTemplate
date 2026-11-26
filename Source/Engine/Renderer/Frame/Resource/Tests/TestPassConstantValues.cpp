/**
 * @file TestPassConstantValues.cpp
 * @brief 패스 상수 값 저장소(`PassConstantValues`)의 이름 표입니다 — 늘리고 지워도 모든 이름이 제 값을 찾는가.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Frame/Resource/PassConstantValues.h"

using namespace sw;

namespace
{
    struct PassConstantTestUtil
    {
        static hashed_string makeName( uint32 index )
        {
            array<utf8, 32> text{};
            formatstring( text.data(), text.size(), "g_TestConstant%#", index );
            return hashed_string( text.data() );
        }

        /** @brief 이름의 uint 값입니다(없으면 무효). */
        static uint32 read( const PassConstantValues& values, uint32 index )
        {
            uint32       size{ 0 };
            const uint8* pBytes = values.find( makeName( index ), size );
            if ( pBytes == nullptr || size != sizeof( uint32 ) )
                return constant::invalid_index::kUint32;
            uint32 value{ 0 };
            Memory::copy( &value, pBytes, sizeof( value ) );
            return value;
        }
    };
} // namespace

/**
 * @brief [PassConstantValuesTest] 표가 자라는 수(300)를 넣어도 모두 찾고, 가운데를 지우면 그 이름만 없고 뒤 이름은 제 값이다
 * @details 변이: 늘릴 때 옛 항목을 다시 넣지 않으면 첫 단언이, 지운 뒤 번호를 당기지 않으면 뒤 이름 단언이 진다.
 */
SW_TEST_CASE( PassConstantValuesTest, FindsEveryNameAcrossGrowthAndRemoval )
{
    constexpr uint32   kCount = 300;
    PassConstantValues values;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        values.setUint( PassConstantTestUtil::makeName( index ), index * 3u );
    }
    bool bAllFound = true;
    for ( uint32 index = 0; index < kCount; ++index )
    {
        bAllFound = bAllFound && PassConstantTestUtil::read( values, index ) == index * 3u;
    }
    SW_EXPECT_TRUE( bAllFound );

    const uint32 versionBefore = values.getVersion();
    values.setUint( PassConstantTestUtil::makeName( 7 ), 21u ); // 같은 값
    SW_EXPECT_EQUAL( versionBefore, values.getVersion() );

    values.remove( PassConstantTestUtil::makeName( 100 ) );
    SW_EXPECT_EQUAL( constant::invalid_index::kUint32, PassConstantTestUtil::read( values, 100 ) );
    bool bRestFound = true;
    for ( uint32 index = 101; index < kCount; ++index )
    {
        bRestFound = bRestFound && PassConstantTestUtil::read( values, index ) == index * 3u;
    }
    SW_EXPECT_TRUE( bRestFound );
    values.clear();
    SW_EXPECT_EQUAL( constant::invalid_index::kUint32, PassConstantTestUtil::read( values, 1 ) );
}
