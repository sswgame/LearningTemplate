#include "pch.h"

#include "Engine/Reflection/PropertyMetaHint.h"

#include "TestFramework/TestFramework.h"

// Engine_Reflection — PROPERTY 메타에서 에디터 위젯을 고르는 규칙.
// ------------------------------------------------------------------------------
// 7) PropertyMetaHint UI 위젯 판별
// ------------------------------------------------------------------------------

SW_TEST_CASE( Engine_Reflection, PropertyMetaHintWidgetDeduction )
{
    sw::PropertyMetadata rangeMeta{};
    rangeMeta._bHasRange = SW_TRUE;
    rangeMeta._minRange  = 0.0f;
    rangeMeta._maxRange  = 100.0f;
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::PropertyWidgetType::Slider ), static_cast<uint32>( sw::PropertyMetaHint::deduceWidgetType( rangeMeta, "float32" ) ) );

    float32 minVal = 0.0f;
    float32 maxVal = 0.0f;
    SW_EXPECT_TRUE( sw::PropertyMetaHint::getSliderRange( rangeMeta, minVal, maxVal ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, minVal, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, maxVal, 0.0001f );

    sw::PropertyMetadata assetMeta{};
    assetMeta._bAssetPath = SW_TRUE;
    assetMeta._assetType  = "Texture";
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::PropertyWidgetType::AssetPicker ), static_cast<uint32>( sw::PropertyMetaHint::deduceWidgetType( assetMeta, "string" ) ) );
}
