#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// PairedAnimationDefTest — 페어드 애니메이션 에셋(`*.pairedanim.xml`) 읽기와 검사(필수 값 · 유한한 수 · 앵커 · 정렬 · 알림 규칙).

/**
 * @brief [PairedAnimationDefTest] 에셋 — 역할 · 정렬 · 알림을 읽고 알림은 시각 순서로 둔다
 */
SW_TEST_CASE( PairedAnimationDefTest, DefLoadsRolesAlignmentAndNotifies )
{
    PairedAnimationDef def;
    SW_ASSERT_TRUE( def.loadFromXMLText( R"(<PairedAnimation duration="1.5">
        <Role name="Attacker" anchor="true" required="true" clip="Grab_A" enterBlend="0.1" exitState="Idle" exitBlend="0.2"/>
        <Role name="Victim" required="true" clip="Grab_V" align="warp" offset="0 0 1.2" yaw="180" warpEnd="0.4" maxAlignDistance="2" enterBlend="0.1" exitState="Idle" exitBlend="0.2" skeleton="characters/humanoid.skeleton.json"/>
        <Notify name="Impact" time="0.9" role="Victim"/>
        <Notify name="Grip" time="0.2" role="Attacker"/>
    </PairedAnimation>)",
                                         "grab" ) );
    SW_EXPECT_NEAR_EQUAL( 1.5f, def.getDuration(), 1e-6f );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( def.getRoles().size() ) );
    SW_EXPECT_EQUAL( 0u, def.getAnchorRoleIndex() );
    const PairedAnimationRoleDef& victim = def.getRoles()[def.findRoleIndex( hashed_string( "Victim" ) )];
    SW_EXPECT_TRUE( victim._align == PairedAnimationAlign::Warp );
    SW_EXPECT_NEAR_EQUAL( 1.2f, victim._offset._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::kPi, victim._yawRadians, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, victim._warpEndSeconds, 1e-6f );
    SW_EXPECT_TRUE( victim._skeletonPath == "characters/humanoid.skeleton.json" );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( def.getNotifies().size() ) );
    SW_EXPECT_TRUE( def.getNotifies()[0]._name == hashed_string( "Grip" ) );
    SW_EXPECT_TRUE( def.getNotifies()[1]._name == hashed_string( "Impact" ) );
    SW_EXPECT_EQUAL( invalid_index::kUint32, def.findRoleIndex( hashed_string( "Nobody" ) ) );
}

/**
 * @brief [PairedAnimationDefTest] 에셋 — 필수 값 없음 · 유한하지 않은 수 · 규칙 위반은 읽기 오류다(조용히 기본값이 되지 않는다)
 */
SW_TEST_CASE( PairedAnimationDefTest, DefRejectsMissingAndInvalidValues )
{
    const utf8* const kArrBadXML[] = {
        // 앵커가 없다
        R"(<PairedAnimation duration="1"><Role name="A" required="true" clip="X" align="none" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // 앵커가 둘이다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/><Role name="B" anchor="true" required="true" clip="Y" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // 클립이 없다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // 오프셋이 NaN 이다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/><Role name="B" required="true" clip="Y" align="snap" offset="0 nan 1" maxAlignDistance="1" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // 길이가 무한이다
        R"(<PairedAnimation duration="inf"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // 모르는 속성(오타)
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlnd="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // 앵커에 정렬을 적었다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" align="none" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // 앵커 밖 역할이 정렬을 적지 않았다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/><Role name="B" required="true" clip="Y" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // warp 에 창 끝이 없다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/><Role name="B" required="true" clip="Y" align="warp" offset="0 0 1" maxAlignDistance="1" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // warp 창이 들어오는 블렌드 안에서 끝난다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/><Role name="B" required="true" clip="Y" align="warp" offset="0 0 1" warpEnd="0.1" maxAlignDistance="1" enterBlend="0.2" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // none 인데 오프셋을 적었다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/><Role name="B" required="true" clip="Y" align="none" offset="0 0 1" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // 앵커가 선택 역할이다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="false" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
        // 알림이 모르는 역할로 간다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/><Notify name="N" time="0.5" role="Z"/></PairedAnimation>)",
        // 알림이 타임라인 밖이다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/><Notify name="N" time="1.5" role="A"/></PairedAnimation>)",
        // 역할 이름이 겹친다
        R"(<PairedAnimation duration="1"><Role name="A" anchor="true" required="true" clip="X" enterBlend="0" exitState="Idle" exitBlend="0"/><Role name="A" required="false" clip="Y" align="none" enterBlend="0" exitState="Idle" exitBlend="0"/></PairedAnimation>)",
    };
    for ( const utf8* pXML : kArrBadXML )
    {
        PairedAnimationDef def;
        SW_EXPECT_FALSE( def.loadFromXMLText( pXML, "bad.pairedanim.xml" ) );
        SW_EXPECT_TRUE( def.getRoles().empty() ); // 실패하면 반쯤 읽은 내용을 남기지 않는다
    }
}
