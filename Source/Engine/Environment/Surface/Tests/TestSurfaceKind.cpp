// SurfaceKindTable — 엔진 팩의 표면 종류 표가 모든 물리 재질을 덮고, 틀린 표(빈 표 · 겹친 이름 · 표에 없는 물 종류 · 수명 없는 발자국)는 로드 때 거절한다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

using namespace sw;

/**
 * @brief [SurfaceKindTest] 엔진 팩의 표는 물리 설정의 모든 재질에 종류를 가진다 — 재질 이름이 곧 종류 이름이라 하나라도 빠지면 그 바닥의 발소리 · 발자국이 사라진다
 */
SW_TEST_CASE( SurfaceKindTest, ShippedTableCoversPhysicsMaterials )
{
    const shared_ptr<const SurfaceKindTable> pTable = SurfaceKindTable::findDefault();
    SW_REQUIRE_NOT_NULL( pTable );
    PhysicsSettings settings;
    SW_REQUIRE_TRUE( settings.loadFromAsset( AssetID{ PhysicsSystem::kSettingsPath } ) );
    for ( const PhysicsMaterialDef& material : settings._listMaterial )
    {
        SW_EXPECT_TRUE_MSG( pTable->findKind( material._name ) != nullptr, material._name.c_str() );
    }
    SW_EXPECT_TRUE( pTable->getWaterKind()._name == pTable->_waterKind );
    // 발자국을 남기는 종류가 적어도 하나 있다(눈) — 시연 씬과 시나리오가 그 종류를 밟는다.
    const SurfaceKindDef* pSnow = pTable->findKind( "Snow" );
    SW_REQUIRE_NOT_NULL( pSnow );
    SW_EXPECT_TRUE( pSnow->hasFootprints() );
    SW_EXPECT_TRUE( pSnow->_footprintLifetime > 0.0f );
    SW_EXPECT_TRUE( pSnow->_moveScale < 1.0f );
}

namespace
{
    struct BrokenSurfaceKindTable
    {
        const utf8* _pXML;
        const utf8* _pExpectedID;
    };

    const BrokenSurfaceKindTable kArrBrokenSurfaceKindTable[] = {
        {                                                                                                                                                                    R"(<SurfaceKindTable _waterKind="Water" _footprintCapacity="8"><_listKind></_listKind></SurfaceKindTable>)","Surface.KindTableEmpty"                                                                                                                                                                                                                                                                                         },
        {                                                                      R"(<SurfaceKindTable _waterKind="Water" _footprintCapacity="8"><_listKind><SurfaceKindDef _name="Water" _footstep="water"/><SurfaceKindDef _name="water" _footstep="w"/></_listKind></SurfaceKindTable>)",
         "Surface.KindDuplicate"                                                                                                                                                                                                                                                                                    },
        {                                                                                                                    R"(<SurfaceKindTable _waterKind="Lake" _footprintCapacity="8"><_listKind><SurfaceKindDef _name="Water" _footstep="water"/></_listKind></SurfaceKindTable>)", "Surface.WaterKindUnknown"},
        {R"(<SurfaceKindTable _waterKind="Water" _footprintCapacity="8"><_listKind><SurfaceKindDef _name="Water" _footstep="water"/><SurfaceKindDef _name="Snow" _footstep="snow" _footprintPrefab="engine/prefabs/surface/footprint_snow.prefab.xml"/></_listKind></SurfaceKindTable>)",
         "Surface.FootprintLifetimeMissing"                                                                                                                                                                                                                                                                         },
        {                                                                                                    R"(<SurfaceKindTable _waterKind="Water" _footprintCapacity="8"><_listKind><SurfaceKindDef _name="Water" _footstep="water" _moveScale="5"/></_listKind></SurfaceKindTable>)",
         "Serialization.ValueOutOfRange"                                                                                                                                                                                                                                                                            },
    };
} // namespace

/**
 * @brief [SurfaceKindTest] 틀린 표는 로드 시점에 오류다 — 빈 표, 겹친 이름(대소문자 무시), 표에 없는 물 종류, 수명 없는 발자국, 범위 밖 이동 계수. 실패하면 표가 비어 코드 폴백이 없다
 */
SW_TEST_CASE_EACH( SurfaceKindTest, BrokenTableIsRejectedAtLoad, kArrBrokenSurfaceKindTable )
{
    const string path = test::makeScratchPath( "broken.surfacekinds.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, param._pXML ) );
    SW_EXPECT_LOG( param._pExpectedID );
    SurfaceKindTable table;
    SW_EXPECT_FALSE_MSG( table.loadFromDiskFile( path ), param._pXML );
    SW_EXPECT_TRUE( table._listKind.empty() );
}
