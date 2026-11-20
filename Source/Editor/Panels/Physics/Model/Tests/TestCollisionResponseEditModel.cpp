// CollisionResponseEditModelTest — 충돌 응답 격자 편집: 칸을 고치면 원문(주석 · 자산 줄 보존)에 기본과 다른 줄만 남고, 다시 읽은 표가 그 응답을 내며, 원문 단계로 되돌아간다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Physics/Model/CollisionResponseEditModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TestCollisionResponseEditModelInternal
    {
        static const utf8* getText()
        {
            return R"(<?asset guid="00000000000000000000000000000001"?>
<!-- 시험 표 -->
<PhysicsSettings _maxBodyCount="64" _maxBodyPairCount="64" _maxContactCount="64" _tempAllocatorBytes="65536" _maxDebrisBodyCount="8">
	<_listChannel>
		<CollisionChannelDef _name="Solid" _kind="Object" _defaultResponse="Block" />
		<CollisionChannelDef _name="Look" _kind="Trace" _defaultResponse="Ignore" />
	</_listChannel>
	<_listProfile>
		<CollisionProfileDef _name="Wall" _objectChannel="Solid" />
		<CollisionProfileDef _name="Glass" _objectChannel="Solid">
			<_listResponse>
				<CollisionResponseDef _channel="Look" _response="Block" />
			</_listResponse>
		</CollisionProfileDef>
	</_listProfile>
	<_listMaterial>
		<PhysicsMaterialDef _name="Default" />
	</_listMaterial>
	<_listGroundQueryLayer>
		<item>Wall</item>
	</_listGroundQueryLayer>
</PhysicsSettings>
)";
        }
    };
} // namespace

/** @brief [CollisionResponseEditModelTest] 줄 없는 프로필의 칸을 고치면 응답 목록과 줄을 더하고, 기본으로 돌리면 줄을 빼며, 주석 · 자산 줄은 그대로다 */
SW_TEST_CASE( CollisionResponseEditModelTest, CellEditWritesOnlyNonDefaultLines )
{
    CollisionResponseEditModel model;
    SW_REQUIRE_TRUE( model.loadText( TestCollisionResponseEditModelInternal::getText() ) );
    SW_EXPECT_TRUE( model.getResponse( 0, 1 ) == CollisionResponse::Ignore ); // Wall · Look — 채널 기본
    SW_EXPECT_TRUE( model.isDefaultResponse( 0, 1 ) );
    SW_EXPECT_TRUE( model.getResponse( 1, 1 ) == CollisionResponse::Block ); // Glass · Look — 적힌 줄

    SW_REQUIRE_TRUE( model.setResponse( 0, 0, CollisionResponse::Overlap ) ); // Wall · Solid
    SW_EXPECT_TRUE( model.getResponse( 0, 0 ) == CollisionResponse::Overlap );
    SW_EXPECT_FALSE( model.isDefaultResponse( 0, 0 ) );
    SW_EXPECT_TRUE( model.getSettings().makeCollisionLayers().getResponse( 0, 1 ) == CollisionResponse::Overlap ); // Wall 이 Solid 에 겹침 — Glass 와의 쌍도 약한 쪽
    SW_EXPECT_TRUE( model.getText().find( "<!-- 시험 표 -->" ) != string::npos );
    SW_EXPECT_TRUE( model.getText().find( "<?asset guid=" ) == 0 );

    SW_EXPECT_FALSE( model.setResponse( 0, 0, CollisionResponse::Overlap ) ); // 같은 값
    SW_REQUIRE_TRUE( model.setResponse( 1, 1, CollisionResponse::Ignore ) );  // 기본으로 — 줄을 뺀다
    SW_EXPECT_TRUE( model.isDefaultResponse( 1, 1 ) );
    SW_REQUIRE_TRUE( model.cycleResponse( 0, 0 ) ); // 겹침 → 무시
    SW_EXPECT_TRUE( model.getResponse( 0, 0 ) == CollisionResponse::Ignore );
    SW_REQUIRE_TRUE( model.cycleResponse( 0, 0 ) ); // 무시 → 막음(기본) — 줄을 뺀다
    SW_EXPECT_TRUE( model.isDefaultResponse( 0, 0 ) );
}

/** @brief [CollisionResponseEditModelTest] 휠클릭 되돌리기(R10): 적힌 칸은 채널 기본으로 돌아가 줄이 빠지고, 이미 기본인 칸 · 범위 밖은 아무 일도 하지 않는다 */
SW_TEST_CASE( CollisionResponseEditModelTest, ResetReturnsCellToChannelDefault )
{
    CollisionResponseEditModel model;
    SW_REQUIRE_TRUE( model.loadText( TestCollisionResponseEditModelInternal::getText() ) );
    SW_REQUIRE_TRUE( model.resetResponse( 1, 1 ) ); // Glass · Look — 적힌 막음 → 채널 기본 무시
    SW_EXPECT_TRUE( model.isDefaultResponse( 1, 1 ) );
    SW_EXPECT_TRUE( model.getResponse( 1, 1 ) == CollisionResponse::Ignore );
    SW_EXPECT_FALSE( model.resetResponse( 1, 1 ) );
    SW_EXPECT_FALSE( model.resetResponse( 0, 0 ) );
    SW_EXPECT_FALSE( model.resetResponse( 9, 9 ) );
}

/** @brief [CollisionResponseEditModelTest] 되돌리기 원문 단계: 고치기 전 원문을 적용하면 표가 처음 응답으로 돌아간다 */
SW_TEST_CASE( CollisionResponseEditModelTest, UndoTextRestoresTheTable )
{
    CollisionResponseEditModel model;
    SW_REQUIRE_TRUE( model.loadText( TestCollisionResponseEditModelInternal::getText() ) );
    const string before = model.captureText();
    SW_REQUIRE_TRUE( model.setResponse( 1, 0, CollisionResponse::Ignore ) );
    SW_EXPECT_TRUE( model.getResponse( 1, 0 ) == CollisionResponse::Ignore );
    model.applyText( before );
    SW_EXPECT_TRUE( model.getResponse( 1, 0 ) == CollisionResponse::Block );
    SW_EXPECT_EQUAL( before, model.captureText() );
}
