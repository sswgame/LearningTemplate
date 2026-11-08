#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyChannel.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyProfile.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

using namespace sw;

// BodyCustomizationTest — 몸 채널 층(우선순위 · 섞기), 장비 채널이 몸 체형 축 값이 됨, 플레이어 숨김, 구간 별칭으로 고르는 염색(include · exclude · dyeable), 견본 묶음,
// 몸 프로필 읽기 · 상속(`_base`), 동공 순응(좁힘 빠름 · 넓힘 느림), 체액 경로(nogpu).

namespace
{
    constexpr const utf8* kSlotsXML = R"(<SlotCatalog><Slot name="Torso"/><Slot name="Feet"/><Slot name="Head"/></SlotCatalog>)";

    constexpr const utf8* kVisualsXML = R"(<ItemVisualCatalog>
        <ItemVisual id="body"><Part name="Body" kind="Skinned" mesh="body.mesh" material="body.material"/></ItemVisual>
        <ItemVisual id="corset" customization="Cloth">
          <Part name="Coat" kind="Skinned" mesh="coat.mesh">
            <Section name="mi_coat" material="coat.material" alias="cloth_main" groups="cloth"/>
            <Section name="mi_trim" material="trim.material" alias="trim" groups="cloth"/>
            <Section name="mi_buckle" material="buckle.material" alias="buckle" groups="cloth,metal" dyeable="false"/>
          </Part>
          <BodyChannel name="Bust.Compression" value="0.6" priority="10"/>
          <BodyChannel name="Waist.Width" value="-0.2" blend="Additive"/>
        </ItemVisual>
        <ItemVisual id="heels"><Part name="Shoe" kind="SocketPrefab" prefab="shoe.prefab" socket="Foot"/><BodyChannel name="Heel.Height" value="0.08" blend="Max"/></ItemVisual>
        <ItemVisual id="boots"><Part name="Boot" kind="SocketPrefab" prefab="boot.prefab" socket="Foot"/><BodyChannel name="Heel.Height" value="0.03" blend="Max"/></ItemVisual>
    </ItemVisualCatalog>)";

    constexpr const utf8* kSchemaXML = R"(<CustomizationSchemaCatalog>
        <Palette id="Fabric"><Swatch name="Crimson" color="0.8 0.1 0.1 1"/><Swatch name="Navy" color="0.1 0.1 0.5 1"/></Palette>
        <Schema id="Human"><Slider name="Bust" min="-1" max="1" default="0"><Drive kind="BoneProportion" target="Bust.Compression"/></Slider></Schema>
        <Schema id="Cloth">
          <Color name="Primary" palette="Fabric" default="0.8 0.1 0.1 1"><Drive kind="DyeChannel" target="_dye" channel="0" include="cloth" exclude="trim"/></Color>
          <Slider name="Shine" min="0" max="1" default="0.5"><Drive kind="MaterialScalar" target="_dyeRoughness0" include="cloth_main"/></Slider>
        </Schema>
    </CustomizationSchemaCatalog>)";

    constexpr const utf8* kProfilesXML = R"(<BodyProfileCatalog>
        <BodyProfile id="Base" wrinkleIntensity="0.5">
          <BodyChannel name="Bust.Compression" value="-0.1" priority="-10" blend="Additive"/>
          <Jiggle group="Bust" stiffness="0.25" damping="0.2" maxSwingDegrees="18"/>
          <ActionScale tag="State.Run" scale="1.5"/>
          <ActionScale tag="State.Attack" scale="1.8"/>
          <Wrinkle region="Forehead" mask="0"><Source expression="BrowUp" weight="1"/></Wrinkle>
          <Pupil parameter="_pupilDilation" minSize="0.2" maxSize="0.8" constrictSeconds="0.3" dilateSeconds="4"/>
          <Fluid name="TearL" channel="Tear" residueChannel="TearResidue" trigger="Cry" path="0.4 0.4; 0.4 0.5; 0.4 0.6"/>
        </BodyProfile>
        <BodyProfile id="Heroine" _base="Base"><Jiggle group="Bust" stiffness="0.4"/></BodyProfile>
    </BodyProfileCatalog>)";

    /** @brief 부모 파일이 없는 원천(같은 파일 안 상속만 쓴다)입니다. */
    class NoParentSource final : public IDataInheritanceSource
    {
    public:
        [[nodiscard]] bool readText( string_view assetID, string& outText ) override
        {
            (void)assetID;
            (void)outText;
            return false;
        }
    };

    struct BodyCustomizationTest
    {
        AppearanceDatabase      _database;
        CharacterAppearanceSpec _spec;
        ResolvedAppearance      _resolved;

        BodyCustomizationTest()
            : _database{}
            , _spec{}
            , _resolved{}
        {
            load( kSlotsXML );
            load( kVisualsXML );
            load( kSchemaXML );
            _spec._bodyVisual = hashed_string( "body" );
        }

        void load( const utf8* pXML ) { SW_REQUIRE_TRUE( _database.loadSectionFromXMLText( pXML, "test" ) ); }

        AppearanceSlotRequest& equip( const utf8* pSlot, const utf8* pVisual )
        {
            AppearanceSlotRequest request;
            request._slot          = hashed_string( pSlot );
            request._itemID        = hashed_string( pVisual );
            request._visibleVisual = hashed_string( pVisual );
            _spec._listSlot.push_back( request );
            return _spec._listSlot.back();
        }

        void resolve() { ResolvedAppearance::resolve( _database, _spec, _resolved ); }

        float32 findChannel( const utf8* pName ) const
        {
            for ( const ResolvedBoneProportion& proportion : _resolved._listBoneProportion )
            {
                if ( proportion._owner.empty() && proportion._name == hashed_string( pName ) )
                    return proportion._value;
            }
            return -100.0f;
        }
    };
} // namespace

/**
 * @brief [BodyCustomizationTest] 층은 우선순위 오름차순으로 얹힌다 — 덮어쓰기 · 더하기 · 큰 쪽, 같은 우선순위는 적힌 순서
 */
SW_TEST_CASE( BodyChannelTest, LayersBlendByPriorityAndMode )
{
    vector<BodyChannelLayerDef> listLayer;
    const auto                  addLayer = [&listLayer]( const utf8* pName, float32 value, int32 priority, BodyChannelBlend blend )
    {
        BodyChannelLayerDef layer;
        layer._name     = hashed_string( pName );
        layer._value    = value;
        layer._priority = priority;
        layer._blend    = blend;
        listLayer.push_back( layer );
    };
    addLayer( "Bust", 0.6f, 10, BodyChannelBlend::Override );
    addLayer( "Bust", 0.1f, 20, BodyChannelBlend::Additive );
    addLayer( "Bust", 0.3f, 0, BodyChannelBlend::Additive );
    addLayer( "Heel", 0.08f, 0, BodyChannelBlend::Max );
    addLayer( "Heel", 0.03f, 0, BodyChannelBlend::Max );
    vector<BodyChannelValue> listValue{
        BodyChannelValue{ hashed_string( "Bust" ), 0.5f }
    };
    BodyChannelLayerDef::blendLayers( span<const BodyChannelLayerDef>( listLayer.data(), listLayer.size() ), listValue );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listValue.size() );
    SW_EXPECT_NEAR( 0.7f, listValue[0]._value, 1e-6f ); // 0.5 + 0.3(우선 0) → 0.6 덮음(10) → + 0.1(20)
    SW_EXPECT_NEAR( 0.08f, listValue[1]._value, 1e-6f );
}

/**
 * @brief [BodyCustomizationTest] 장비 채널이 슬라이더 위에 얹혀 몸 체형 축 값이 되고, 가장 높은 굽이 이기며, 플레이어가 숨긴 장비는 몸을 누르지 않는다
 */
SW_TEST_CASE_F( BodyCustomizationTest, EquipmentChannelsBecomeBodyShapeValues )
{
    _spec._schema = hashed_string( "Human" );
    _spec._customization.setNumber( hashed_string( "Bust" ), 0.2f );
    equip( "Torso", "corset" );
    equip( "Feet", "heels" );
    equip( "Head", "boots" ); // 두 번째 신발(시험용 — 슬롯은 아무거나)
    resolve();
    SW_EXPECT_NEAR( 0.6f, findChannel( "Bust.Compression" ), 1e-4f );
    SW_EXPECT_NEAR( -0.2f, findChannel( "Waist.Width" ), 1e-6f );
    SW_EXPECT_NEAR( 0.08f, findChannel( "Heel.Height" ), 1e-6f );

    _spec.findSlot( hashed_string( "Torso" ) )->_bHiddenByPlayer = SW_TRUE;
    resolve();
    SW_EXPECT_NEAR( 0.2f, findChannel( "Bust.Compression" ), 1e-4f ); // 슬라이더만 남는다
    SW_EXPECT_TRUE( _resolved.findPart( hashed_string( "Torso" ), hashed_string( "Coat" ) ) == nullptr );
}

/**
 * @brief [BodyCustomizationTest] 염색은 별칭 묶음 `cloth` 에서 `trim` 을 빼고 `dyeable="false"` 버클을 건너뛰어 코트 구간 하나에만 걸린다, 머티리얼 스칼라는 별칭 하나를 고른다
 */
SW_TEST_CASE_F( BodyCustomizationTest, DyeTargetsSectionAliasesWithExclusion )
{
    equip( "Torso", "corset" );
    resolve();
    vector<hashed_string> listDyed;
    vector<hashed_string> listShine;
    for ( const ResolvedMaterialValue& value : _resolved._listMaterialValue )
    {
        if ( value._target == ResolvedMaterialTarget::DyeChannel )
            listDyed.push_back( value._section );
        if ( value._name == hashed_string( "_dyeRoughness0" ) )
            listShine.push_back( value._section );
    }
    SW_REQUIRE_EQUAL( size_t{ 1 }, listDyed.size() );
    SW_EXPECT_TRUE( listDyed[0] == hashed_string( "mi_coat" ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listShine.size() );
    SW_EXPECT_TRUE( listShine[0] == hashed_string( "mi_coat" ) );
}

/**
 * @brief [BodyCustomizationTest] 외형에 없는 별칭을 드라이브가 가리키면 로드 오류다
 */
SW_TEST_CASE( BodyCustomizationLoadTest, UnknownSectionAliasIsError )
{
    AppearanceDatabase database;
    SW_EXPECT_LOG( "Appearance.SectionTagUnknown" );
    SW_REQUIRE_TRUE( database.loadSectionFromXMLText( kSlotsXML, "test" ) );
    SW_REQUIRE_TRUE( database.loadSectionFromXMLText( R"(<ItemVisualCatalog><ItemVisual id="cape" customization="Cape">
        <Part name="Cape" kind="Skinned" mesh="cape.mesh"><Section name="mi_cape" material="cape.material" alias="cloth_main"/></Part></ItemVisual></ItemVisualCatalog>)",
                                                      "test" ) );
    SW_REQUIRE_TRUE( database.loadSectionFromXMLText( R"(<CustomizationSchemaCatalog><Schema id="Cape">
        <Color name="Primary"><Drive kind="DyeChannel" target="_dye" include="lining"/></Color></Schema></CustomizationSchemaCatalog>)",
                                                      "test" ) );
    SW_EXPECT_FALSE( database.finishLoad( nullptr ) );
}

/**
 * @brief [BodyCustomizationTest] 견본 묶음이 있는 색은 정규화가 가장 가까운 견본으로 맞춘다(공유 코드로 온 임의 색도)
 */
SW_TEST_CASE_F( BodyCustomizationTest, PaletteSnapsToNearestSwatch )
{
    const CustomizationSchemaDef* pSchema = _database.getSchemas().findSchema( hashed_string( "Cloth" ) );
    SW_REQUIRE_NOT_NULL( pSchema );
    CustomizationValueSet given;
    given.setColor( hashed_string( "Primary" ), float4{ 0.2f, 0.15f, 0.45f, 1.0f } );
    CustomizationValueSet normalized;
    pSchema->normalize( given, normalized, nullptr );
    const CustomizationValue* pValue = normalized.findValue( hashed_string( "Primary" ) );
    SW_REQUIRE_NOT_NULL( pValue );
    SW_EXPECT_NEAR( 0.1f, pValue->_number._x, 1.0f / 255.0f );
    SW_EXPECT_NEAR( 0.5f, pValue->_number._z, 1.0f / 255.0f );
}

/**
 * @brief [BodyCustomizationTest] 몸 프로필의 `_base` 상속: 자식이 적은 지글 강성만 바뀌고 감쇠 · 다른 칸은 물려받는다, 동작 배율은 맞는 태그 중 큰 값
 */
SW_TEST_CASE( BodyProfileTest, InheritsAndOverridesByKey )
{
    DataInheritanceSchema schema{ "BodyProfileCatalog" };
    bool                  bListed = false;
    SW_REQUIRE_TRUE( DataCatalogInheritance::loadSchema( "BodyProfileCatalog", schema, bListed ) );
    SW_REQUIRE_TRUE( bListed );
    NoParentSource source;
    string         flatText;
    SW_REQUIRE_TRUE( DataCatalogInheritance::flatten( kProfilesXML, "bodyprofiles.xml", schema, source, flatText ) );
    XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( flatText ) );
    CharacterBodyProfileCatalog catalog;
    AppearanceLoadReport        report;
    SW_REQUIRE_TRUE( catalog.loadFromXML( document.getRoot( "BodyProfileCatalog" ), report, "test" ) );
    const CharacterBodyProfileDef* pHeroine = catalog.findProfile( hashed_string( "Heroine" ) );
    SW_REQUIRE_NOT_NULL( pHeroine );
    SW_REQUIRE_EQUAL( size_t{ 1 }, pHeroine->_listJiggle.size() );
    SW_EXPECT_NEAR( 0.4f, pHeroine->_listJiggle[0]._stiffness, 1e-6f );
    SW_EXPECT_NEAR( 0.2f, pHeroine->_listJiggle[0]._damping, 1e-6f );
    SW_EXPECT_NEAR( 0.5f, pHeroine->_wrinkleIntensity, 1e-6f );
    SW_EXPECT_EQUAL( size_t{ 1 }, pHeroine->_listFluid.size() );
    SW_EXPECT_TRUE( pHeroine->_bHasPupil == SW_TRUE );

    TagContainer tags;
    tags.addTag( TagID::request( "State.Run" ) );
    tags.addTag( TagID::request( "State.Attack" ) );
    SW_EXPECT_NEAR( 1.8f, pHeroine->computeActionScale( tags ), 1e-6f );
    SW_EXPECT_NEAR( 1.0f, pHeroine->computeActionScale( TagContainer{} ), 1e-6f );
}

/**
 * @brief [BodyCustomizationTest] 동공: 밝아지면 0.3 초 상수로 빨리 좁아지고, 어두워지면 4 초 상수로 느리게 넓어진다
 */
SW_TEST_CASE( BodyProfileTest, PupilConstrictsFastDilatesSlow )
{
    BodyPupilDef pupil;
    pupil._parameter     = hashed_string( "_pupilDilation" );
    const float32 bright = pupil.computeTargetSize( 1000.0f );
    const float32 dark   = pupil.computeTargetSize( 0.0001f );
    SW_EXPECT_NEAR( pupil._minSize, bright, 1e-5f );
    SW_EXPECT_NEAR( pupil._maxSize, dark, 1e-5f );
    const float32 afterConstrict = pupil.advance( dark, bright, 0.3f ); // τ 만큼 — 63% 따라간다
    const float32 afterDilate    = pupil.advance( bright, dark, 0.3f );
    SW_EXPECT_NEAR( dark + ( bright - dark ) * 0.632f, afterConstrict, 0.01f );
    SW_EXPECT_TRUE( ( afterDilate - bright ) < ( dark - bright ) * 0.1f );
}

/**
 * @brief [BodyCustomizationTest] 체액 경로: 길이와 거리로 짚은 점(끝을 넘으면 끝)
 */
SW_TEST_CASE( BodyProfileTest, FluidPathSamplesByDistance )
{
    BodyFluidDef fluid;
    fluid._listPathPoint = {
        float2{0.4f, 0.4f},
        float2{0.4f, 0.5f},
        float2{0.5f, 0.5f}
    };
    SW_EXPECT_NEAR( 0.2f, fluid.computePathLength(), 1e-6f );
    SW_EXPECT_NEAR( 0.45f, fluid.samplePath( 0.05f )._y, 1e-6f );
    SW_EXPECT_NEAR( 0.45f, fluid.samplePath( 0.15f )._x, 1e-6f );
    SW_EXPECT_NEAR( 0.5f, fluid.samplePath( 1.0f )._x, 1e-6f );
}

/**
 * @brief [BodyCustomizationTest] 머리카락: 몸 채널 `Hair.Length` 를 고른 머리 부품의 `<ChannelMaterial>` 이 `_length` 로 옮기고(장비 층이 슬라이더 위에 얹힌다), 몸 스키마 색은
 *        `owner="Hair"` 로 머리 부품에 닿으며, 머티리얼만 받는 채널로 적힌다(체형 축 데이터가 없어도 오류가 아니다)
 */
SW_TEST_CASE( BodyCustomizationLoadTest, HairChannelDrivesHairMaterial )
{
    AppearanceDatabase database;
    SW_REQUIRE_TRUE( database.loadSectionFromXMLText( kSlotsXML, "test" ) );
    SW_REQUIRE_TRUE( database.loadSectionFromXMLText( R"(<ItemVisualCatalog>
        <ItemVisual id="body"><Part name="Body" kind="Skinned" mesh="body.mesh" material="body.material"/></ItemVisual>
        <ItemVisual id="hair_long"><Part name="Hair" kind="Skinned" mesh="hair.mesh" material="hair.material">
          <ChannelMaterial channel="Hair.Length" name="_length" channelMin="-1" channelMax="0" valueMin="0.4" valueMax="1"/></Part></ItemVisual>
        <ItemVisual id="cap"><Part name="Cap" kind="SocketPrefab" prefab="cap.prefab" socket="Head"/><BodyChannel name="Hair.Length" value="-0.5" blend="Additive"/></ItemVisual>
    </ItemVisualCatalog>)",
                                                      "test" ) );
    SW_REQUIRE_TRUE( database.loadSectionFromXMLText( R"(<CustomizationSchemaCatalog><Schema id="Folk">
        <Choice name="Hair" default="Long"><Option name="Long" visual="hair_long"/></Choice>
        <Slider name="HairLength" min="-1" max="0" default="0"><Drive kind="BoneProportion" target="Hair.Length"/></Slider>
        <Color name="HairRoot" default="1 1 1 1"><Drive kind="MaterialColor" target="_rootColor" owner="Hair"/></Color>
    </Schema></CustomizationSchemaCatalog>)",
                                                      "test" ) );
    SW_REQUIRE_TRUE( database.finishLoad( nullptr ) );

    CharacterAppearanceSpec spec;
    spec._bodyVisual = hashed_string( "body" );
    spec._schema     = hashed_string( "Folk" );
    spec._customization.setNumber( hashed_string( "HairLength" ), -0.25f );
    spec._customization.setColor( hashed_string( "HairRoot" ), float4{ 0.5f, 0.25f, 0.1f, 1.0f } );
    const auto findValue = []( const ResolvedAppearance& resolved, const utf8* pName ) -> const ResolvedMaterialValue*
    {
        for ( const ResolvedMaterialValue& value : resolved._listMaterialValue )
        {
            if ( value._name == hashed_string( pName ) )
                return &value;
        }
        return nullptr;
    };

    ResolvedAppearance resolved;
    ResolvedAppearance::resolve( database, spec, resolved );
    const ResolvedMaterialValue* pLength = findValue( resolved, "_length" );
    SW_REQUIRE_NOT_NULL( pLength );
    SW_EXPECT_TRUE( pLength->_owner == hashed_string( "Hair" ) );
    SW_EXPECT_TRUE( pLength->_part == hashed_string( "Hair" ) );
    SW_EXPECT_TRUE( pLength->_shape == ResolvedMaterialShape::Scalar );
    SW_EXPECT_NEAR( 0.85f, pLength->_value._x, 1e-5f ); // -0.25 → 0.4 + 0.6 × 0.75
    SW_EXPECT_TRUE( VectorUtil::contains( resolved._listMaterialChannel, hashed_string( "Hair.Length" ) ) );
    const ResolvedMaterialValue* pRoot = findValue( resolved, "_rootColor" );
    SW_REQUIRE_NOT_NULL( pRoot );
    SW_EXPECT_TRUE( pRoot->_owner == hashed_string( "Hair" ) );

    // 모자 층(-0.5 더하기)이 슬라이더 위에 얹혀 -0.75 → 0.55, 범위 밖(-1 아래)은 가장 짧은 값에서 멈춘다.
    AppearanceSlotRequest request;
    request._slot          = hashed_string( "Head" );
    request._itemID        = hashed_string( "cap" );
    request._visibleVisual = hashed_string( "cap" );
    spec._listSlot.push_back( request );
    ResolvedAppearance::resolve( database, spec, resolved );
    pLength = findValue( resolved, "_length" );
    SW_REQUIRE_NOT_NULL( pLength );
    SW_EXPECT_NEAR( 0.55f, pLength->_value._x, 1e-5f );
    spec._customization.setNumber( hashed_string( "HairLength" ), -1.0f );
    ResolvedAppearance::resolve( database, spec, resolved );
    pLength = findValue( resolved, "_length" );
    SW_REQUIRE_NOT_NULL( pLength );
    SW_EXPECT_NEAR( 0.4f, pLength->_value._x, 1e-5f );
}

/**
 * @brief [BodyCustomizationTest] 드라이브의 `owner` 는 같은 스키마의 고르기여야 하고, 머티리얼 드라이브에만 적는다
 */
SW_TEST_CASE( BodyCustomizationLoadTest, DriveOwnerMustBeChoice )
{
    AppearanceDatabase database;
    SW_EXPECT_LOG( "Appearance.DriveOwnerInvalid" ); // 슬라이더(본 비율에 주인)는 읽기에서, 색(고르기가 아닌 주인)은 스키마를 다 읽은 뒤에
    SW_REQUIRE_TRUE( database.loadSectionFromXMLText( kSlotsXML, "test" ) );
    (void)database.loadSectionFromXMLText( R"(<CustomizationSchemaCatalog><Schema id="Folk">
        <Color name="HairRoot" default="1 1 1 1"><Drive kind="MaterialColor" target="_rootColor" owner="Hair"/></Color>
        <Slider name="HairLength" min="-1" max="0" default="0"><Drive kind="BoneProportion" target="Hair.Length" owner="Hair"/></Slider>
    </Schema></CustomizationSchemaCatalog>)",
                                           "test" );
    SW_EXPECT_FALSE( database.finishLoad( nullptr ) );
}
