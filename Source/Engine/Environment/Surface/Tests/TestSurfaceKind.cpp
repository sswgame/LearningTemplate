// SurfaceKindTable — 엔진 팩의 표면 종류 표가 모든 물리 재질을 덮고, 틀린 표(빈 표 · 겹친 이름 · 표에 없는 물 종류 · 틀린 발자국 도장 · 변형 격자 칸 없음)는 로드 때 거절한다(nogpu).
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
    SW_EXPECT_TRUE( pSnow->_footprint._fillSpeed > 0.0f );
    // 눈은 모래보다 깊고 느리게 메워진다(종류마다 도장 데이터 — 코드에 지형 이름이 없다).
    const SurfaceKindDef* pSand = pTable->findKind( "Sand" );
    SW_REQUIRE_NOT_NULL( pSand );
    SW_EXPECT_TRUE( pSnow->_footprint._depth > pSand->_footprint._depth );
    SW_EXPECT_TRUE( pSnow->_footprint._fillSpeed < pSand->_footprint._fillSpeed );
    SW_EXPECT_TRUE( pTable->_deformResolution > 0 && pTable->_deformExtent > 0.0f && pTable->_deformDepthRange >= pSnow->_footprint._depth );
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
        {                                                                                                                                                                                             R"(<SurfaceKindTable _waterKind="Water" _deformResolution="64" _deformExtent="8" _deformDepthRange="0.1"><_listKind></_listKind></SurfaceKindTable>)","Surface.KindTableEmpty"                                                                                                                                                                                                                                                                                                                                                            },
        {                                                                                               R"(<SurfaceKindTable _waterKind="Water" _deformResolution="64" _deformExtent="8" _deformDepthRange="0.1"><_listKind><SurfaceKindDef _name="Water" _footstep="water"/><SurfaceKindDef _name="water" _footstep="w"/></_listKind></SurfaceKindTable>)",
         "Surface.KindDuplicate"                                                                                                                                                                                                                                                                                                                                                       },
        {                                                                                                                                             R"(<SurfaceKindTable _waterKind="Lake" _deformResolution="64" _deformExtent="8" _deformDepthRange="0.1"><_listKind><SurfaceKindDef _name="Water" _footstep="water"/></_listKind></SurfaceKindTable>)", "Surface.WaterKindUnknown"},
        {                   R"(<SurfaceKindTable _waterKind="Water" _deformResolution="64" _deformExtent="8" _deformDepthRange="0.1"><_listKind><SurfaceKindDef _name="Water" _footstep="water"/><SurfaceKindDef _name="Snow" _footstep="snow"><_footprint _shape="Print" _size="0.1,0.2" _depth="0.05"/></SurfaceKindDef></_listKind></SurfaceKindTable>)",
         "Surface.StampFillMissing"                                                                                                                                                                                                                                                                                                                                                    },
        {                                                                                                                             R"(<SurfaceKindTable _waterKind="Water" _deformResolution="64" _deformExtent="8" _deformDepthRange="0.1"><_listKind><SurfaceKindDef _name="Water" _footstep="water" _moveScale="5"/></_listKind></SurfaceKindTable>)",
         "Serialization.ValueOutOfRange"                                                                                                                                                                                                                                                                                                                                               },
        {                                  R"(<SurfaceKindTable _waterKind="Water" _deformResolution="64" _deformExtent="8" _deformDepthRange="0.1"><_listKind><SurfaceKindDef _name="Water" _footstep="water"/><SurfaceKindDef _name="Mud" _footstep="mud"><_footprint _depth="0.05" _fillSpeed="0.01"/></SurfaceKindDef></_listKind></SurfaceKindTable>)",
         "Surface.StampSizeMissing"                                                                                                                                                                                                                                                                                                                                                    },
        {R"(<SurfaceKindTable _waterKind="Water" _deformResolution="64" _deformExtent="8" _deformDepthRange="0.1"><_listKind><SurfaceKindDef _name="Water" _footstep="water"/><SurfaceKindDef _name="Mud" _footstep="mud"><_footprint _size="0.1,0.2" _depth="0.05" _rimHeight="0.01" _fillSpeed="0.01"/></SurfaceKindDef></_listKind></SurfaceKindTable>)",
         "Surface.StampRimHalf"                                                                                                                                                                                                                                                                                                                                                        },
    };
} // namespace

/**
 * @brief [SurfaceKindTest] 틀린 표는 로드 시점에 오류다 — 빈 표, 겹친 이름(대소문자 무시), 표에 없는 물 종류, 메워지지 않는 · 크기 없는 · 테두리 반쪽 발자국 도장, 범위 밖 이동 계수.
 *        실패하면 표가 비어 코드 폴백이 없다
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
