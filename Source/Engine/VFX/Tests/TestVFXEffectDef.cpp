#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/VFX/VFXCPUSimulation.h"
#include "Engine/VFX/VFXEffectDef.h"

using namespace sw;

namespace
{
    struct VFXEffectDefTestUtil
    {
        static FloatCurve makeCurve()
        {
            FloatCurve    curve;
            FloatCurveKey first;
            first._time  = 0.0f;
            first._value = 1.0f;
            FloatCurveKey last;
            last._time  = 1.0f;
            last._value = 0.0f;
            curve._listKey.push_back( first );
            curve._listKey.push_back( last );
            return curve;
        }

        /** @brief 모든 필수 칸을 채운 빌보드 방출기입니다. 테스트는 여기서 한 칸씩 망가뜨립니다. */
        static VFXEmitterDef makeEmitter( string_view name = "Sparks" )
        {
            VFXEmitterDef emitter;
            emitter._name                 = string( name );
            emitter._texturePath          = "game/vfx/spark.dds";
            emitter._maxParticles         = 64;
            emitter._duration             = 1.0f;
            emitter._spawnRate            = 10.0f;
            emitter._lifetimeMin          = 0.5f;
            emitter._lifetimeMax          = 1.0f;
            emitter._sizeOverLife         = makeCurve();
            emitter._colorOverLife._red   = makeCurve();
            emitter._colorOverLife._green = makeCurve();
            emitter._colorOverLife._blue  = makeCurve();
            emitter._colorOverLife._alpha = makeCurve();
            return emitter;
        }

        /** @brief 빌보드를 메시 입자로 바꿉니다(메시 필수, 텍스처 금지). */
        static VFXEmitterDef makeMeshEmitter()
        {
            VFXEmitterDef emitter = makeEmitter();
            emitter._renderMode   = VFXRenderMode::Mesh;
            emitter._texturePath.clear();
            emitter._mesh._meshID = "Cube";
            return emitter;
        }

        using BreakFunction = void ( * )( VFXEmitterDef& );

        /** @brief 망가뜨린 칸 하나만으로 검증이 지는지 봅니다(칸마다 따로 — 다른 칸의 오류에 묻히지 않게). */
        static void expectRejected( const utf8* pWhat, BreakFunction pBreak )
        {
            VFXEmitterDef emitter = makeEmitter();
            pBreak( emitter );
            SW_EXPECT_FALSE_MSG( emitter.validate( "test" ), pWhat );
        }

        static void expectAccepted( const utf8* pWhat, BreakFunction pChange )
        {
            VFXEmitterDef emitter = makeEmitter();
            pChange( emitter );
            SW_EXPECT_TRUE_MSG( emitter.validate( "test" ), pWhat );
        }

        static string makeCurveXML( const utf8* pTag )
        {
            // 키를 일부러 뒤집어 적는다 — 읽기가 정렬한다.
            return string( "<" ) + pTag + "><_listKey><FloatCurveKey _time=\"1\" _value=\"0\"/><FloatCurveKey _time=\"0\" _value=\"1\"/></_listKey></" + pTag + ">";
        }

        /** @brief 방출기 하나의 효과 문서입니다. @p attributes 를 방출기 원소에 그대로 넣습니다. */
        static string makeEffectXML( string_view attributes )
        {
            string xml = "<VFXEffectDef><_listEmitter><VFXEmitterDef ";
            xml += string( attributes );
            xml += ">";
            xml += makeCurveXML( "_sizeOverLife" );
            xml += "<_colorOverLife>";
            xml += makeCurveXML( "_red" );
            xml += makeCurveXML( "_green" );
            xml += makeCurveXML( "_blue" );
            xml += makeCurveXML( "_alpha" );
            xml += "</_colorOverLife></VFXEmitterDef></_listEmitter></VFXEffectDef>";
            return xml;
        }

        static constexpr string_view kValidAttributes = "_name=\"Sparks\" _texturePath=\"game/vfx/spark.dds\" _blendMode=\"Additive\" _maxParticles=\"64\" _duration=\"1\" _spawnRate=\"40\" "
                                                        "_lifetimeMin=\"0.4\" _lifetimeMax=\"0.8\" _velocityMin=\"-1, 2, -1\" _velocityMax=\"1, 4, 1\" _gravity=\"0, -9.8, 0\"";

        [[nodiscard]] static bool writeScratchFile( const string& path, string_view text ) { return FileUtil::writeFile( path, reinterpret_cast<const uint8*>( text.data() ), text.size() ); }
    };
    using Util = VFXEffectDefTestUtil;

    /** @brief 부모 효과 글을 한 벌 돌려주는 곳입니다(`DataInheritance::flatten` 의 입력). */
    class VFXParentSource final : public IDataInheritanceSource
    {
    public:
        explicit VFXParentSource( string_view text )
            : _text{ text }
        {
        }
        [[nodiscard]] bool readText( string_view /*assetID*/, string& outText ) override
        {
            outText = _text;
            return true;
        }

    private:
        string _text;
    };
} // namespace

/** @brief [VFXEffectDefTest] 변형은 방출기를 이름으로 합친다 — 적은 칸만 바뀌고 물려받은 칸(커브)은 그대로이며, 새 이름은 끝에 더해지고 _op="Remove" 는 지운다 */
SW_TEST_CASE( VFXEffectDefTest, VariantMergesEmittersByName )
{
    string parent = "<VFXEffectDef><_listEmitter>";
    for ( const utf8* pName : { "Sparks", "Smoke" } )
    {
        string       one   = Util::makeEffectXML( string( "_name=\"" ) + pName + "\" _texturePath=\"game/vfx/spark.dds\" _maxParticles=\"64\" _duration=\"1\" _spawnRate=\"40\" _lifetimeMin=\"0.4\" _lifetimeMax=\"0.8\"" );
        const size_t first = one.find( "<VFXEmitterDef " );
        const size_t last  = one.find( "</VFXEmitterDef>" ) + string_view( "</VFXEmitterDef>" ).size();
        parent += one.substr( first, last - first );
    }
    parent += "</_listEmitter></VFXEffectDef>";

    VFXParentSource source{ parent };
    const string    child = "<VFXEffectDef _base=\"parent.vfx.xml\"><_listEmitter><VFXEmitterDef _name=\"Sparks\" _maxParticles=\"8\" _spawnRate=\"4\" /><VFXEmitterDef _name=\"Smoke\" _op=\"Remove\" /></_listEmitter></VFXEffectDef>";
    string          flat;
    SW_REQUIRE_TRUE( DataInheritance::flatten( child, "child.vfx.xml", DataInheritanceSchema::makeFromType( *VFXEffectDef::StaticType() ), source, flat ) );
    const string path = test::makeScratchPath( "variant.vfx.xml" );
    SW_REQUIRE_TRUE( Util::writeScratchFile( path, flat ) );

    VFXEffectDef effect;
    SW_REQUIRE_TRUE( effect.loadFromDiskFile( path ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( effect._listEmitter.size() ) );
    SW_EXPECT_EQUAL( 8u, effect._listEmitter[0]._maxParticles );
    SW_EXPECT_NEAR( 0.8f, effect._listEmitter[0]._lifetimeMax, 0.0001f );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( effect._listEmitter[0]._sizeOverLife._listKey.size() ) );
}

/** @brief [VFXEffectDefTest] 묶음이 없는 사용자 파라미터를 가리키거나 파라미터 이름이 겹치면 로드를 멈춘다 */
SW_TEST_CASE( VFXEffectDefTest, UserParameterNamesAreChecked )
{
    SW_EXPECT_LOG( "VFXEffectDef.VFXEmitterUnknownUserParameter" );
    SW_EXPECT_LOG( "VFXEffectDef.VFXUserParameterName" );
    VFXEffectDef effect;
    effect._listEmitter.push_back( Util::makeEmitter() );
    effect._listEmitter[0]._tintParameter = "Tint";
    SW_EXPECT_FALSE( effect.validate( "test" ) );

    VFXUserParameterDef tint;
    tint._name = "Tint";
    effect._listUserParameter.push_back( tint );
    SW_EXPECT_TRUE( effect.validate( "test" ) );
    SW_REQUIRE_NOT_NULL( effect.findUserParameter( "Tint" ) );

    effect._listUserParameter.push_back( tint );
    SW_EXPECT_FALSE( effect.validate( "test" ) );
}

/** @brief [VFXEffectDefTest] 사용자 값은 낸 입자의 색 · 크기에 곱하고 시뮬레이션 상태는 건드리지 않는다 */
SW_TEST_CASE( VFXEffectDefTest, UserValuesMultiplyTheOutput )
{
    shared_ptr<const VFXEmitterDef> pDef = make_shared<const VFXEmitterDef>( Util::makeEmitter() );
    VFXCPUSimulation                simulation{ pDef, 7u, 1.0f };
    simulation.step( 0.2f, nullptr );
    SW_REQUIRE_TRUE( simulation.getAliveCount() > 0u );
    vector<GPUParticle> listPlain;
    simulation.writeParticles( listPlain );

    VFXEmitterUserValues values{};
    values._tint      = float4{ 0.5f, 0.25f, 2.0f, 1.0f };
    values._sizeScale = 3.0f;
    simulation.setUserValues( values );
    vector<GPUParticle> listScaled;
    simulation.writeParticles( listScaled );
    SW_REQUIRE_EQUAL( static_cast<uint32>( listPlain.size() ), static_cast<uint32>( listScaled.size() ) );
    SW_EXPECT_NEAR( listPlain[0]._positionSize._w * 3.0f, listScaled[0]._positionSize._w, 0.0001f );
    SW_EXPECT_NEAR( listPlain[0]._positionSize._x, listScaled[0]._positionSize._x, 0.0f );
    SW_EXPECT_NEAR( listPlain[0]._color._x * 0.5f, listScaled[0]._color._x, 0.0001f );
    SW_EXPECT_NEAR( listPlain[0]._color._z * 2.0f, listScaled[0]._color._z, 0.0001f );
}

/** @brief [VFXEffectDefTest] 방출기의 모든 리플렉션 칸이 모듈 표에 줄을 가진다 — 칸을 더하고 대상을 선언하지 않는 길을 막는다 */
SW_TEST_CASE( VFXEffectDefTest, EveryEmitterFieldDeclaresItsTargets )
{
    const PropertyListView listProperty = VFXEmitterDef::StaticType()->getPropertiesWithBase();
    SW_REQUIRE_FALSE( listProperty.empty() );
    for ( const PropertyInfo& property : listProperty )
    {
        SW_EXPECT_TRUE_MSG( VFXEmitterDef::hasModuleRow( property._name.view() ), property._name.view() );
    }
    SW_EXPECT_FALSE( VFXEmitterDef::hasModuleRow( "_notAField" ) );
}

/** @brief [VFXEffectDefTest] 대상 집합 — 모두 · 하나 · 교집합 · 빈 집합 */
SW_TEST_CASE( VFXEffectDefTest, TargetSetOperations )
{
    const VFXSimulationTargetSet all = VFXSimulationTargetSet::makeAll();
    SW_EXPECT_TRUE( all.has( VFXSimulationTarget::CPU ) );
    SW_EXPECT_TRUE( all.has( VFXSimulationTarget::GPU ) );
    SW_EXPECT_FALSE( all.isEmpty() );

    const VFXSimulationTargetSet cpu = VFXSimulationTargetSet::makeOnly( VFXSimulationTarget::CPU );
    const VFXSimulationTargetSet gpu = VFXSimulationTargetSet::makeOnly( VFXSimulationTarget::GPU );
    SW_EXPECT_TRUE( cpu.has( VFXSimulationTarget::CPU ) );
    SW_EXPECT_FALSE( cpu.has( VFXSimulationTarget::GPU ) );
    SW_EXPECT_TRUE( all.intersect( gpu ).has( VFXSimulationTarget::GPU ) );
    SW_EXPECT_FALSE( all.intersect( gpu ).has( VFXSimulationTarget::CPU ) );
    SW_EXPECT_TRUE( cpu.intersect( gpu ).isEmpty() );
    SW_EXPECT_TRUE( VFXSimulationTargetSet{}.isEmpty() );
}

/** @brief [VFXEffectDefTest] 충돌의 방법이 대상을 정한다. 꺼져 있으면 방법과 상관없이 둘 다다 */
SW_TEST_CASE( VFXEffectDefTest, CollisionMethodChoosesTheTarget )
{
    VFXCollisionDef collision;
    collision._mode   = VFXCollisionMode::None;
    collision._method = VFXCollisionMethod::SceneDepth;
    SW_EXPECT_TRUE( collision.getSupportedTargets().has( VFXSimulationTarget::CPU ) );
    SW_EXPECT_TRUE( collision.getSupportedTargets().has( VFXSimulationTarget::GPU ) );

    collision._mode   = VFXCollisionMode::Bounce;
    collision._method = VFXCollisionMethod::PhysicsRaycast;
    SW_EXPECT_TRUE( collision.getSupportedTargets().has( VFXSimulationTarget::CPU ) );
    SW_EXPECT_FALSE( collision.getSupportedTargets().has( VFXSimulationTarget::GPU ) );

    collision._mode   = VFXCollisionMode::Kill;
    collision._method = VFXCollisionMethod::SceneDepth;
    SW_EXPECT_FALSE( collision.getSupportedTargets().has( VFXSimulationTarget::CPU ) );
    SW_EXPECT_TRUE( collision.getSupportedTargets().has( VFXSimulationTarget::GPU ) );
}

/** @brief [VFXEffectDefTest] 방출기의 대상은 모듈의 교집합이고, 빠진 대상의 까닭은 그 모듈 칸 이름이다 */
SW_TEST_CASE( VFXEffectDefTest, EmitterTargetsAreTheIntersection )
{
    VFXEmitterDef emitter = Util::makeEmitter();
    SW_EXPECT_TRUE( emitter.computeSupportedTargets().has( VFXSimulationTarget::CPU ) );
    SW_EXPECT_TRUE( emitter.computeSupportedTargets().has( VFXSimulationTarget::GPU ) );
    SW_EXPECT_NULL( emitter.findModuleExcluding( VFXSimulationTarget::CPU ) );
    SW_EXPECT_NULL( emitter.findModuleExcluding( VFXSimulationTarget::GPU ) );
    SW_EXPECT_TRUE( emitter.isLifetimeBoundedByMax() );

    emitter._collision._mode   = VFXCollisionMode::Bounce;
    emitter._collision._method = VFXCollisionMethod::PhysicsRaycast;
    SW_EXPECT_FALSE( emitter.computeSupportedTargets().has( VFXSimulationTarget::GPU ) );
    const utf8* pModule = emitter.findModuleExcluding( VFXSimulationTarget::GPU );
    SW_REQUIRE_NOT_NULL( pModule );
    SW_EXPECT_TRUE( string_view( pModule ) == "_collision" );
    SW_EXPECT_NULL( emitter.findModuleExcluding( VFXSimulationTarget::CPU ) );
}

/** @brief [VFXEffectDefTest] 어느 곳에서도 돌 수 없는 방출기는 로드를 멈춘다(읽기가 거절하는 값이 코드로 들어온 경우 — 빈 집합이 그 안전망이다) */
SW_TEST_CASE( VFXEffectDefTest, NoSimulationTargetFailsValidation )
{
    SW_EXPECT_LOG( "VFX.NoSimulationTargetForEmitter" );
    VFXEmitterDef emitter      = Util::makeEmitter();
    emitter._collision._mode   = VFXCollisionMode::Bounce;
    emitter._collision._method = static_cast<VFXCollisionMethod>( 200 );
    SW_EXPECT_TRUE( emitter.computeSupportedTargets().isEmpty() );
    SW_EXPECT_FALSE( emitter.validate( "test" ) );
}

/** @brief [VFXEffectDefTest] 필수 칸이 빠지거나 값이 틀린 칸은 각각 혼자서 검증을 지게 한다 */
SW_TEST_CASE( VFXEffectDefTest, EachBrokenFieldFailsValidation )
{
    SW_EXPECT_LOG( "VFXEffectDef.VFXEmitter" );
    Util::expectRejected( "name", []( VFXEmitterDef& emitter )
    { emitter._name.clear(); } );
    Util::expectRejected( "texture", []( VFXEmitterDef& emitter )
    { emitter._texturePath.clear(); } );
    Util::expectRejected( "mesh on billboard", []( VFXEmitterDef& emitter )
    { emitter._mesh._meshID = "Cube"; } );
    Util::expectRejected( "material on billboard", []( VFXEmitterDef& emitter )
    { emitter._mesh._materialPath = "game/m.material"; } );
    Util::expectRejected( "no particles", []( VFXEmitterDef& emitter )
    { emitter._maxParticles = 0; } );
    Util::expectRejected( "one particle ribbon", []( VFXEmitterDef& emitter )
    {
        emitter._renderMode   = VFXRenderMode::Ribbon;
        emitter._maxParticles = 1;
    } );
    Util::expectRejected( "light without radius", []( VFXEmitterDef& emitter )
    {
        emitter._light._maxLights = 1;
        emitter._light._intensity = 1.0f;
    } );
    Util::expectRejected( "light without intensity", []( VFXEmitterDef& emitter )
    {
        emitter._light._maxLights = 1;
        emitter._light._radius    = 1.0f;
    } );
    Util::expectRejected( "zero duration", []( VFXEmitterDef& emitter )
    { emitter._duration = 0.0f; } );
    Util::expectRejected( "zero lifetime", []( VFXEmitterDef& emitter )
    { emitter._lifetimeMin = 0.0f; } );
    Util::expectRejected( "lifetime order", []( VFXEmitterDef& emitter )
    { emitter._lifetimeMax = 0.25f; } );
    Util::expectRejected( "never emits", []( VFXEmitterDef& emitter )
    { emitter._spawnRate = 0.0f; } );
    Util::expectRejected( "velocity x order", []( VFXEmitterDef& emitter )
    { emitter._velocityMin._x = 1.0f; } );
    Util::expectRejected( "velocity y order", []( VFXEmitterDef& emitter )
    { emitter._velocityMin._y = 1.0f; } );
    Util::expectRejected( "velocity z order", []( VFXEmitterDef& emitter )
    { emitter._velocityMin._z = 1.0f; } );
    Util::expectRejected( "empty burst", []( VFXEmitterDef& emitter )
    {
        VFXBurstDef burst;
        burst._count = 0;
        emitter._listBurst.push_back( burst );
    } );
    Util::expectRejected( "late burst", []( VFXEmitterDef& emitter )
    {
        VFXBurstDef burst;
        burst._count = 1;
        burst._time  = 1.5f;
        emitter._listBurst.push_back( burst );
    } );
    Util::expectRejected( "scene depth without thickness", []( VFXEmitterDef& emitter )
    {
        emitter._collision._mode   = VFXCollisionMode::Kill;
        emitter._collision._method = VFXCollisionMethod::SceneDepth;
    } );
}

/** @brief [VFXEffectDefTest] 필수 커브 다섯은 각각 키가 있어야 한다 */
SW_TEST_CASE( VFXEffectDefTest, EveryCurveNeedsKeys )
{
    SW_EXPECT_LOG( "VFXEffectDef.VFXEmitterNeedsKeys" );
    Util::expectRejected( "size", []( VFXEmitterDef& emitter )
    { emitter._sizeOverLife._listKey.clear(); } );
    Util::expectRejected( "red", []( VFXEmitterDef& emitter )
    { emitter._colorOverLife._red._listKey.clear(); } );
    Util::expectRejected( "green", []( VFXEmitterDef& emitter )
    { emitter._colorOverLife._green._listKey.clear(); } );
    Util::expectRejected( "blue", []( VFXEmitterDef& emitter )
    { emitter._colorOverLife._blue._listKey.clear(); } );
    Util::expectRejected( "alpha", []( VFXEmitterDef& emitter )
    { emitter._colorOverLife._alpha._listKey.clear(); } );
}

/** @brief [VFXEffectDefTest] 커브 키 시각은 수명 비율 0..1 안이다 — 경계는 되고 밖은 안 된다 */
SW_TEST_CASE( VFXEffectDefTest, CurveKeyTimeIsALifeRatio )
{
    SW_EXPECT_LOG( "VFXEffectDef.VFXEmitterKeyTime" );
    Util::expectRejected( "above one", []( VFXEmitterDef& emitter )
    { emitter._sizeOverLife._listKey[1]._time = 1.001f; } );
    Util::expectRejected( "below zero", []( VFXEmitterDef& emitter )
    { emitter._colorOverLife._alpha._listKey[0]._time = -0.001f; } );
    Util::expectAccepted( "both ends", []( VFXEmitterDef& /*emitter*/ ) {} );
}

/** @brief [VFXEffectDefTest] 경계값은 받는다 — 같은 수명 범위, 끝 시각의 버스트, 버스트만 있는 방출기, 입자 둘인 띠, 메시 입자 */
SW_TEST_CASE( VFXEffectDefTest, BoundaryValuesAreAccepted )
{
    Util::expectAccepted( "equal lifetime", []( VFXEmitterDef& emitter )
    { emitter._lifetimeMax = emitter._lifetimeMin; } );
    Util::expectAccepted( "burst at the end of the duration", []( VFXEmitterDef& emitter )
    {
        VFXBurstDef burst;
        burst._count = 1;
        burst._time  = emitter._duration;
        emitter._listBurst.push_back( burst );
    } );
    Util::expectAccepted( "burst only", []( VFXEmitterDef& emitter )
    {
        VFXBurstDef burst;
        burst._count = 1;
        emitter._listBurst.push_back( burst );
        emitter._spawnRate = 0.0f;
    } );
    Util::expectAccepted( "two particle ribbon", []( VFXEmitterDef& emitter )
    {
        emitter._renderMode   = VFXRenderMode::Ribbon;
        emitter._maxParticles = 2;
    } );
    Util::expectAccepted( "one particle billboard", []( VFXEmitterDef& emitter )
    { emitter._maxParticles = 1; } );
    Util::expectAccepted( "light off ignores its values", []( VFXEmitterDef& emitter )
    { emitter._light._maxLights = 0; } );
    Util::expectAccepted( "thickness is only for scene depth", []( VFXEmitterDef& emitter )
    {
        emitter._collision._mode   = VFXCollisionMode::Bounce;
        emitter._collision._method = VFXCollisionMethod::PhysicsRaycast;
    } );
    SW_EXPECT_TRUE( Util::makeMeshEmitter().validate( "test" ) );
}

/** @brief [VFXEffectDefTest] 메시 입자는 메시가 필수이고 텍스처를 받지 않는다 */
SW_TEST_CASE( VFXEffectDefTest, MeshModeRules )
{
    SW_EXPECT_LOG( "VFXEffectDef.VFXEmitter" );
    VFXEmitterDef noMesh = Util::makeMeshEmitter();
    noMesh._mesh._meshID.clear();
    SW_EXPECT_FALSE( noMesh.validate( "test" ) );
    VFXEmitterDef withTexture = Util::makeMeshEmitter();
    withTexture._texturePath  = "game/vfx/spark.dds";
    SW_EXPECT_FALSE( withTexture.validate( "test" ) );
}

/** @brief [VFXEffectDefTest] 효과는 방출기가 하나 이상이고 이름이 겹치지 않으며, 방출기 하나의 오류도 효과를 지게 한다 */
SW_TEST_CASE( VFXEffectDefTest, EffectValidationRules )
{
    SW_EXPECT_LOG( "VFXEffectDef.VFXNoEmittersListEmitter" );
    SW_EXPECT_LOG( "VFXEffectDef.VFXEmitterNameUsed" );
    SW_EXPECT_LOG( "VFXEffectDef.VFXEmitter" );

    VFXEffectDef effect;
    SW_EXPECT_FALSE( effect.validate( "test" ) );

    effect._listEmitter.push_back( Util::makeEmitter( "A" ) );
    effect._listEmitter.push_back( Util::makeEmitter( "B" ) );
    SW_EXPECT_TRUE( effect.validate( "test" ) );

    effect._listEmitter.push_back( Util::makeEmitter( "A" ) );
    SW_EXPECT_FALSE( effect.validate( "test" ) );

    effect._listEmitter.pop_back();
    effect._listEmitter[1]._duration = 0.0f;
    SW_EXPECT_FALSE( effect.validate( "test" ) );
}

/** @brief [VFXEffectDefTest] 파일을 읽으면 값이 칸에 들어오고 뒤집어 적은 커브 키는 시각 순으로 정렬된다 */
SW_TEST_CASE( VFXEffectDefTest, LoadReadsTheDocumentAndSortsCurves )
{
    const string path = test::makeScratchPath( "valid.vfx.xml" );
    SW_REQUIRE_TRUE( Util::writeScratchFile( path, Util::makeEffectXML( Util::kValidAttributes ) ) );

    VFXEffectDef effect;
    SW_REQUIRE_TRUE( effect.loadFromDiskFile( path ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( effect._listEmitter.size() ) );
    const VFXEmitterDef& emitter = effect._listEmitter[0];
    SW_EXPECT_TRUE( emitter._name == "Sparks" );
    SW_EXPECT_EQUAL( VFXBlendMode::Additive, emitter._blendMode );
    SW_EXPECT_EQUAL( 64u, emitter._maxParticles );
    SW_EXPECT_NEAR( 0.8f, emitter._lifetimeMax, 0.0001f );
    SW_EXPECT_NEAR( -9.8f, emitter._gravity._y, 0.0001f );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( emitter._sizeOverLife._listKey.size() ) );
    SW_EXPECT_NEAR( 0.0f, emitter._sizeOverLife._listKey[0]._time, 0.0f );
    SW_EXPECT_NEAR( 1.0f, emitter._sizeOverLife._listKey[0]._value, 0.0f );
    SW_EXPECT_NEAR( 1.0f, emitter._colorOverLife._alpha._listKey[1]._time, 0.0f );
}

/** @brief [VFXEffectDefTest] 모르는 이름은 오류이고 효과가 빈다 — 앞서 읽은 내용도 남지 않는다 */
SW_TEST_CASE( VFXEffectDefTest, UnknownAttributeLeavesAnEmptyEffect )
{
    SW_EXPECT_LOG( "Serialization.OrphanValue" );
    const string goodPath = test::makeScratchPath( "good.vfx.xml" );
    SW_REQUIRE_TRUE( Util::writeScratchFile( goodPath, Util::makeEffectXML( Util::kValidAttributes ) ) );
    const string badPath = test::makeScratchPath( "bad.vfx.xml" );
    SW_REQUIRE_TRUE( Util::writeScratchFile( badPath, Util::makeEffectXML( string( Util::kValidAttributes ) + " _notAField=\"1\"" ) ) );

    VFXEffectDef effect;
    SW_REQUIRE_TRUE( effect.loadFromDiskFile( goodPath ) );
    SW_EXPECT_FALSE( effect.loadFromDiskFile( badPath ) );
    SW_EXPECT_TRUE( effect._listEmitter.empty() );
}

/** @brief [VFXEffectDefTest] 필수 칸이 빠지면 칸의 최솟값 검사가 읽기에서 거절하고 효과가 빈다 */
SW_TEST_CASE( VFXEffectDefTest, MissingRequiredFieldLeavesAnEmptyEffect )
{
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    const string path = test::makeScratchPath( "partial.vfx.xml" );
    SW_REQUIRE_TRUE( Util::writeScratchFile( path, Util::makeEffectXML( "_name=\"Sparks\" _duration=\"1\" _spawnRate=\"40\"" ) ) );
    VFXEffectDef effect;
    SW_EXPECT_FALSE( effect.loadFromDiskFile( path ) );
    SW_EXPECT_TRUE( effect._listEmitter.empty() );
}

/** @brief [VFXEffectDefTest] 없는 파일 · 빈 파일 · 방출기 없는 문서는 모두 실패다 */
SW_TEST_CASE( VFXEffectDefTest, MissingOrEmptyFilesFail )
{
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "Xml.FileNotFound" );
    SW_EXPECT_LOG( "Xml.ParseError" );
    SW_EXPECT_LOG( "Xml.NotRead" );
    SW_EXPECT_LOG( "Xml.WrongRoot" );
    SW_EXPECT_LOG( "VFXEffectDef.VFXNoEmittersListEmitter" );

    VFXEffectDef effect;
    SW_EXPECT_FALSE( effect.loadFromDiskFile( test::makeScratchPath( "missing.vfx.xml" ) ) );

    const string brokenPath = test::makeScratchPath( "broken.vfx.xml" );
    SW_REQUIRE_TRUE( Util::writeScratchFile( brokenPath, "<VFXEffectDef" ) );
    SW_EXPECT_FALSE( effect.loadFromDiskFile( brokenPath ) );

    const string otherKindPath = test::makeScratchPath( "otherkind.vfx.xml" );
    SW_REQUIRE_TRUE( Util::writeScratchFile( otherKindPath, "<RenderPipelineDesc></RenderPipelineDesc>" ) );
    SW_EXPECT_FALSE( effect.loadFromDiskFile( otherKindPath ) );

    const string noEmitterPath = test::makeScratchPath( "noemitter.vfx.xml" );
    SW_REQUIRE_TRUE( Util::writeScratchFile( noEmitterPath, "<VFXEffectDef></VFXEffectDef>" ) );
    SW_EXPECT_FALSE( effect.loadFromDiskFile( noEmitterPath ) );
}

/** @brief [VFXEffectDefTest] 풀이 찰 수 있는 일정은 경고하지만 로드는 된다(차면 새 입자를 만들지 않는 것이 규칙이다) */
SW_TEST_CASE( VFXEffectDefTest, PoolTooSmallWarnsButLoads )
{
    SW_EXPECT_LOG( "VFXEffectDef.VFXEmitterMayNeed" );
    const string path = test::makeScratchPath( "pool.vfx.xml" );
    SW_REQUIRE_TRUE( Util::writeScratchFile(
        path, Util::makeEffectXML( "_name=\"Sparks\" _texturePath=\"game/vfx/spark.dds\" _maxParticles=\"2\" _duration=\"1\" _spawnRate=\"40\" _lifetimeMin=\"0.4\" _lifetimeMax=\"0.8\"" ) ) );
    VFXEffectDef effect;
    SW_EXPECT_TRUE( effect.loadFromDiskFile( path ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( effect._listEmitter.size() ) );
}

/**
 * @brief [VFXEffectDefTest] 같은 경로는 공유 캐시의 같은 객체이고, 다시 읽으면 쥔 쪽은 옛 효과를 지키며 다시 받은 쪽은 새 효과를 받는지 검증
 * @details 시뮬레이션이 방출기 정의를 효과의 별칭 포인터로 쥐므로 다시 읽기는 떼어 낸다(`Detach`) — 쥔 객체가 제자리에서 바뀌면 렌더 스레드가 읽는 정의가 흔들린다.
 */
SW_TEST_CASE( VFXEffectDefTest, SharedCacheGivesOneObjectAndReloadDetaches )
{
    const test::ScopedScratchDomain domain;
    const string                    id = test::ScopedScratchDomain::makeID( "sparks.vfx.xml" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "sparks.vfx.xml" ), Util::makeEffectXML( Util::kValidAttributes ) ) );

    const shared_ptr<const VFXEffectDef> pHeld  = SharedAssetCache<VFXEffectDef>::acquire( id );
    const shared_ptr<const VFXEffectDef> pAgain = SharedAssetCache<VFXEffectDef>::acquire( id );
    SW_REQUIRE_TRUE( pHeld != nullptr );
    SW_EXPECT_TRUE( pHeld.get() == pAgain.get() );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( pHeld->_listEmitter.size() ) );
    SW_EXPECT_EQUAL( 64u, pHeld->_listEmitter[0]._maxParticles );

    string       changed = Util::makeEffectXML( Util::kValidAttributes );
    const size_t at      = changed.find( "_maxParticles=\"64\"" );
    SW_REQUIRE_TRUE( at != string::npos );
    changed.replace( at, string_view( "_maxParticles=\"64\"" ).size(), "_maxParticles=\"128\"" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "sparks.vfx.xml" ), changed ) );
    SW_REQUIRE_TRUE( SharedAssetCache<VFXEffectDef>::reloadShared( id ) );
    SW_EXPECT_EQUAL( 64u, pHeld->_listEmitter[0]._maxParticles ); // 쥔 쪽은 옛 객체 그대로(Detach)

    const shared_ptr<const VFXEffectDef> pFresh = SharedAssetCache<VFXEffectDef>::acquire( id );
    SW_REQUIRE_TRUE( pFresh != nullptr );
    SW_EXPECT_TRUE( pFresh.get() != pHeld.get() );
    SW_EXPECT_EQUAL( 128u, pFresh->_listEmitter[0]._maxParticles );
}
