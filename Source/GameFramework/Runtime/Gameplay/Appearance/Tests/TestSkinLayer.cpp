// SkinLayerTest — 피부 층(화장 · 문신): 섞기 식과 순서, 배치 변환(이동 · 회전 · 크기 · 대칭), 영역 경계, 번짐(나르기 · 묽힘 · 저항 · 씻기),
// 공유 코드 판 3 왕복(층 · 플레이어 숨김)과 판 1 코드 읽기(nogpu).
#include "pch.h"

#include "Core/Encoding/Base64Util.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/SkinLayer.h"

using namespace sw;

namespace
{
    struct SkinLayerSelectionTest
    {
        AppearanceDatabase _database;

        SkinLayerSelectionTest()
            : _database{}
        {
            load( R"(<SlotCatalog><Slot name="Head"/></SlotCatalog>)" );
            load( R"(<ItemVisualCatalog><ItemVisual id="body"><Part name="Body" kind="Skinned" mesh="body.mesh" material="body.material"/></ItemVisual>
                <ItemVisual id="helm"><Part name="Shell" kind="SocketPrefab" prefab="helm.prefab" socket="Head"/></ItemVisual></ItemVisualCatalog>)" );
            load( R"(<CustomizationSchemaCatalog><Palette id="Lips"><Swatch name="Red" color="0.8 0.05 0.1 1"/><Swatch name="Nude" color="0.7 0.45 0.4 1"/></Palette></CustomizationSchemaCatalog>)" );
            load( R"(<SkinLayerCatalog>
                <SkinLayer id="lipstick" category="Makeup" mask="lips.png" palette="Lips" roughness="0.15" roughnessWeight="1" smearResistance="0.2"/>
                <SkinLayer id="mascara" category="Makeup" mask="lashes.png" blend="Multiply" smearResistance="0"/>
                <SkinLayer id="rose_tattoo" category="Tattoo" mask="rose.png" placeable="true" regionMin="0.5 0" regionMax="1 0.5" smearResistance="1"/>
            </SkinLayerCatalog>)" );
            load( R"(<CharacterAppearanceCatalog><CharacterAppearance id="Base" body="body"/></CharacterAppearanceCatalog>)" );
            SW_REQUIRE_TRUE( _database.finishLoad( nullptr ) );
        }

        void load( const utf8* pXML ) { SW_REQUIRE_TRUE( _database.loadSectionFromXMLText( pXML, "test" ) ); }

        AppearanceSelection makeSelection() const
        {
            AppearanceSelection selection;
            selection._basePresetID = hashed_string( "Base" );
            AppearanceSlotRequest helm;
            helm._slot            = hashed_string( "Head" );
            helm._bHiddenByPlayer = SW_TRUE;
            selection._listSlot.push_back( helm );
            SkinLayerValue lips;
            lips._layer   = hashed_string( "lipstick" );
            lips._color   = float4{ 0.8f, 0.05f, 0.1f, 1.0f };
            lips._opacity = 0.75f;
            SkinLayerValue rose;
            rose._layer           = hashed_string( "rose_tattoo" );
            rose._offset          = float2{ 0.7f, 0.25f };
            rose._scale           = float2{ 0.2f, 0.1f };
            rose._rotationDegrees = 30.0f;
            rose._bMirror         = SW_TRUE;
            selection._listSkinLayer.push_back( lips );
            selection._listSkinLayer.push_back( rose );
            return selection;
        }

        /** @brief 판 @p version 으로 쓴 공유 코드입니다(머리 판 · 선택 비트 · CRC32 — `encode` 와 같은 꼴). */
        string encodeWithVersion( const AppearanceSelection& selection, uint32 version ) const
        {
            BitWriter writer;
            writer.writeBits( version, 8 );
            AppearanceSelection::writeSelection( writer, selection, _database, version );
            vector<uint8> bytes = writer.releaseBytes();
            const uint32  crc   = HashUtil::computeCRC32( bytes.data(), bytes.size() );
            for ( uint32 byteIndex = 0; byteIndex < 4; ++byteIndex )
                bytes.push_back( static_cast<uint8>( ( crc >> ( byteIndex * 8 ) ) & 0xffu ) );
            return Base64Util::encodeURL( bytes.data(), bytes.size() );
        }
    };
} // namespace

/**
 * @brief [SkinLayerTest] 섞기: 보통은 lerp, 곱하기는 아래 × 층, 오버레이는 어두운 바탕에서 곱하기 · 밝은 바탕에서 스크린이다
 */
SW_TEST_CASE( SkinLayerTest, BlendModesMatchFormula )
{
    const float3 below{ 0.25f, 0.75f, 0.5f };
    const float3 layer{ 0.5f, 0.5f, 1.0f };
    const float3 normal   = SkinSurfaceSample::blend( below, layer, 0.5f, SkinLayerBlend::Normal );
    const float3 multiply = SkinSurfaceSample::blend( below, layer, 1.0f, SkinLayerBlend::Multiply );
    const float3 overlay  = SkinSurfaceSample::blend( below, layer, 1.0f, SkinLayerBlend::Overlay );
    SW_EXPECT_NEAR( 0.375f, normal._x, 1e-6f );
    SW_EXPECT_NEAR( 0.375f, multiply._y, 1e-6f );
    SW_EXPECT_NEAR( 0.25f, overlay._x, 1e-6f ); // 2 × 0.25 × 0.5
    SW_EXPECT_NEAR( 0.75f, overlay._y, 1e-6f ); // 1 − 2 × 0.25 × 0.5
    SW_EXPECT_NEAR( 0.5f, SkinSurfaceSample::blend( below, layer, 0.0f, SkinLayerBlend::Multiply )._z, 1e-6f );
}

/**
 * @brief [SkinLayerTest] 층은 앞이 아래다 — 순서를 바꾸면 결과가 바뀌고, 립스틱의 거칠기 덮어쓰기는 덮임만큼 건다
 */
SW_TEST_CASE( SkinLayerTest, LayerOrderAndMaterialOverrides )
{
    SkinSurfaceSample base;
    base._albedo    = float3{ 0.6f, 0.5f, 0.4f };
    base._roughness = 0.6f;
    SkinLayerSample red;
    red._color    = float4{ 1.0f, 0.0f, 0.0f, 1.0f };
    red._coverage = 1.0f;
    SkinLayerSample shadow;
    shadow._color                            = float4{ 0.5f, 0.5f, 0.5f, 1.0f };
    shadow._coverage                         = 1.0f;
    shadow._blend                            = SkinLayerBlend::Multiply;
    shadow._roughness                        = 0.1f;
    shadow._roughnessWeight                  = 0.5f;
    const SkinLayerSample   arrRedFirst[]    = { red, shadow };
    const SkinLayerSample   arrShadowFirst[] = { shadow, red };
    const SkinSurfaceSample redFirst         = base.composite( span<const SkinLayerSample>( arrRedFirst, 2 ) );
    const SkinSurfaceSample shadowFirst      = base.composite( span<const SkinLayerSample>( arrShadowFirst, 2 ) );
    SW_EXPECT_NEAR( 0.5f, redFirst._albedo._x, 1e-6f );
    SW_EXPECT_NEAR( 1.0f, shadowFirst._albedo._x, 1e-6f );
    SW_EXPECT_NEAR( 0.35f, redFirst._roughness, 1e-6f ); // lerp(0.6, 0.1, 0.5)
}

/**
 * @brief [SkinLayerTest] 배치: 층 가운데는 층 UV 0.5, 90 도 돌리면 축이 바뀌고, 대칭 짝은 u 를 뒤집은 자리를 같은 층 UV(거울상)로 덮는다
 */
SW_TEST_CASE( SkinLayerTest, PlacementTransformsSkinToLayerUV )
{
    SkinLayerValue value;
    value._offset       = float2{ 0.7f, 0.25f };
    value._scale        = float2{ 0.2f, 0.1f };
    const float2 center = value.toLayerUV( float2{ 0.7f, 0.25f }, false );
    SW_EXPECT_NEAR( 0.5f, center._x, 1e-5f );
    SW_EXPECT_NEAR( 0.5f, center._y, 1e-5f );
    const float2 right = value.toLayerUV( float2{ 0.8f, 0.25f }, false );
    SW_EXPECT_NEAR( 1.0f, right._x, 1e-5f );

    value._rotationDegrees = 90.0f;
    const float2 rotated   = value.toLayerUV( float2{ 0.7f, 0.30f }, false ); // 돌린 층의 +x 가 피부의 +y 다
    SW_EXPECT_NEAR( 0.75f, rotated._x, 1e-4f );
    SW_EXPECT_NEAR( 0.5f, rotated._y, 1e-4f );

    value._rotationDegrees = 0.0f;
    const float2 mirrored  = value.toLayerUV( float2{ 0.2f, 0.25f }, true ); // 1 − 0.2 = 0.8 → 층 u 1 → 거울 0
    SW_EXPECT_NEAR( 0.0f, mirrored._x, 1e-5f );

    SkinLayerDef def;
    def._regionMin = float2{ 0.5f, 0.0f };
    def._regionMax = float2{ 1.0f, 0.5f };
    SW_EXPECT_TRUE( def.isInsideRegion( float2{ 0.7f, 0.25f } ) );
    SW_EXPECT_FALSE( def.isInsideRegion( float2{ 0.4f, 0.25f } ) );
}

/**
 * @brief [SkinLayerTest] 번짐: 흐름이 색을 아래로 끌고 묽게 하며, 저항 1 은 아무것도 바꾸지 않고, 씻기가 지운다
 */
SW_TEST_CASE( SkinLayerTest, SmearTransportsDilutesAndRespectsResistance )
{
    SkinLayerSmearState smear;
    smear.initialize( 64 );
    const float2 eye{ 0.4f, 0.4f };
    smear.advect( eye, float2{ 0.0f, 1.0f }, 0.05f, 0.8f, 0.0f, 0.5f );
    SW_EXPECT_TRUE( smear.sampleOffset( eye )._y > 0.0f );
    const float32 dilution = smear.sampleDilution( eye ); // 0.8 × 0.5 × 가운데 칸의 감쇠(칸 중심이 점에서 조금 떨어져 1 보다 작다)
    SW_EXPECT_TRUE( dilution > 0.3f && dilution <= 0.4f );
    SW_EXPECT_NEAR( 0.0f, smear.sampleDilution( float2{ 0.9f, 0.9f } ), 1e-6f );

    SkinLayerSmearState waterproof;
    waterproof.initialize( 64 );
    waterproof.advect( eye, float2{ 0.0f, 1.0f }, 0.05f, 0.8f, 1.0f, 0.5f );
    SW_EXPECT_TRUE( waterproof.isClean() );

    const float2 before = smear.sampleOffset( eye );
    smear.drag( eye, float2{ 1.0f, 0.0f }, 0.05f, 0.5f, 0.5f ); // 닦기 — 묽음은 그대로
    SW_EXPECT_TRUE( smear.sampleOffset( eye )._x > before._x );
    SW_EXPECT_NEAR( dilution, smear.sampleDilution( eye ), 1e-6f );

    smear.clear();
    SW_EXPECT_TRUE( smear.isClean() );
    SW_EXPECT_NEAR( 0.0f, smear.sampleOffset( eye )._y, 1e-6f );
}

/**
 * @brief [SkinLayerTest] 공유 코드 판 3: 층 순서 · 색 · 배치 · 대칭과 플레이어 숨김이 되돌아오고, 해석은 립스틱 색을 견본으로 맞춘다
 */
SW_TEST_CASE_F( SkinLayerSelectionTest, ShareCodeV2RoundTripsLayersAndHiddenSlots )
{
    const AppearanceSelection selection = makeSelection();
    const string              code      = AppearanceSelection::encode( selection, _database );
    AppearanceSelection       decoded;
    string                    reason;
    SW_REQUIRE_TRUE( AppearanceSelection::decode( code, _database, decoded, &reason ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, decoded._listSkinLayer.size() );
    SW_EXPECT_TRUE( decoded._listSkinLayer[0]._layer == hashed_string( "lipstick" ) );
    SW_EXPECT_TRUE( decoded._listSkinLayer[1]._layer == hashed_string( "rose_tattoo" ) );
    SW_EXPECT_NEAR( 0.75f, decoded._listSkinLayer[0]._opacity, 1.0f / 255.0f );
    SW_EXPECT_NEAR( 30.0f, decoded._listSkinLayer[1]._rotationDegrees, 0.01f );
    SW_EXPECT_NEAR( 0.2f, decoded._listSkinLayer[1]._scale._x, 1e-3f );
    SW_EXPECT_EQUAL( SW_TRUE, decoded._listSkinLayer[1]._bMirror );
    SW_REQUIRE_EQUAL( size_t{ 1 }, decoded._listSlot.size() );
    SW_EXPECT_EQUAL( SW_TRUE, decoded._listSlot[0]._bHiddenByPlayer );
    SW_EXPECT_EQUAL( code, AppearanceSelection::encode( decoded, _database ) );

    // 받은 값을 다시 맞춰도 같다(전송 정밀도) — 보낸 쪽과 받은 쪽의 굽기가 같다
    SkinLayerValue requantized = decoded._listSkinLayer[1];
    requantized.quantize();
    SW_EXPECT_TRUE( requantized == decoded._listSkinLayer[1] );

    CharacterAppearanceSpec spec;
    spec._bodyVisual              = hashed_string( "body" );
    spec._listSkinLayer           = decoded._listSkinLayer;
    spec._listSkinLayer[0]._color = float4{ 0.75f, 0.1f, 0.1f, 1.0f };
    ResolvedAppearance resolved;
    ResolvedAppearance::resolve( _database, spec, resolved );
    SW_REQUIRE_EQUAL( size_t{ 2 }, resolved._listSkinLayer.size() );
    SW_EXPECT_NEAR( 0.05f, resolved._listSkinLayer[0]._value._color._y, 1.0f / 255.0f ); // 견본 Red
    SW_EXPECT_NEAR( 1.0f, resolved._listSkinLayer[0]._roughnessWeight, 1e-6f );
    SW_EXPECT_EQUAL( SW_TRUE, resolved._listSkinLayer[1]._bPlaceable );
}

/**
 * @brief [SkinLayerTest] 판 1 공유 코드(층 · 숨김이 없던 판)도 읽힌다 — 숨김 없음, 층 없음
 */
SW_TEST_CASE_F( SkinLayerSelectionTest, ShareCodeV1StillDecodes )
{
    const string        oldCode = encodeWithVersion( makeSelection(), AppearanceSelection::kOldestShareCodeVersion );
    AppearanceSelection decoded;
    string              reason;
    SW_REQUIRE_TRUE( AppearanceSelection::decode( oldCode, _database, decoded, &reason ) );
    SW_EXPECT_TRUE( decoded._basePresetID == hashed_string( "Base" ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, decoded._listSlot.size() );
    SW_EXPECT_EQUAL( SW_FALSE, decoded._listSlot[0]._bHiddenByPlayer );
    SW_EXPECT_TRUE( decoded._listSkinLayer.empty() );

    const string futureCode = encodeWithVersion( makeSelection(), AppearanceSelection::kShareCodeVersion + 1 );
    SW_EXPECT_FALSE( AppearanceSelection::decode( futureCode, _database, decoded, &reason ) );
    SW_EXPECT_EQUAL( string( "unsupported share code version" ), reason );
}
