#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"

// StateArchiveUtilTest — 상태 구간: 기대한 표 · 버전의 구간만 읽히고(다른 버전은 거절), 표로 갈라 읽는 쪽은 아무 구간이나 받는다(nogpu).

namespace
{
    constexpr uint32 kTag = 0x54534554u; // "TEST"

    sw::Archive makeSection( uint32 version, int32 value )
    {
        sw::Archive body;
        body << value;
        sw::Archive outer;
        sw::StateArchiveUtil::writeSection( outer, kTag, version, body );
        return outer;
    }
} // namespace

/**
 * @brief [StateArchiveUtilTest] readExpectedSection 은 표와 버전이 모두 같을 때만 본문을 주고, 버전 · 표가 다르면 거절한다(옛 판을 새 판으로 읽지 않는다)
 */
SW_TEST_CASE( StateArchiveUtilTest, ExpectedSectionRejectsOtherVersionOrTag )
{
    const sw::Archive written = makeSection( 2, 42 );

    sw::Archive same( written.getData(), written.getSize() );
    sw::Archive body;
    SW_REQUIRE_TRUE( sw::StateArchiveUtil::readExpectedSection( same, kTag, 2, body ) );
    int32 value = 0;
    body >> value;
    SW_EXPECT_EQUAL( 42, value );
    SW_EXPECT_EQUAL( size_t{ 0 }, same.getRemainingBytes() );

    sw::Archive oldVersion( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( sw::StateArchiveUtil::readExpectedSection( oldVersion, kTag, 1, body ) );
    sw::Archive otherTag( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( sw::StateArchiveUtil::readExpectedSection( otherTag, kTag + 1, 2, body ) );

    sw::Archive any( written.getData(), written.getSize() );
    uint32      tag     = 0;
    uint32      version = 0;
    SW_REQUIRE_TRUE( sw::StateArchiveUtil::readSection( any, tag, version, body ) );
    SW_EXPECT_EQUAL( kTag, tag );
    SW_EXPECT_EQUAL( 2u, version );
}
