#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Graphics/Material/Dev/MaterialGraph.h"
#include "Engine/Graphics/Material/Dev/MaterialGraphCompiler.h"
#include "Engine/Graphics/Material/Dev/MaterialGraphCook.h"
#include "Engine/Graphics/Material/Dev/MaterialGraphNodeRegistry.h"
#include "Engine/Graphics/Material/Material.h"

using namespace sw;

namespace
{
    struct MaterialGraphTestUtil
    {
        static constexpr const utf8* kSource = "game/x/materials/test.matgraph.xml";

        /** @brief 노드 하나의 XML 입니다. @p links 는 `핀=노드.출력` 을 쉼표로 나눈 글입니다. */
        static string makeNode( string_view name, string_view kind, string_view links = {}, string_view values = {} )
        {
            string text{ "<MaterialGraphNodeDesc _name=\"" };
            text.append( name ).append( "\" _kind=\"" ).append( kind ).append( "\"><_listLink>" );
            for ( const string_view link : StringUtil::split( links, "," ) )
            {
                const size_t equals = link.find( '=' );
                const size_t dot    = link.find( '.' );
                if ( equals == string_view::npos )
                    continue;
                text.append( "<MaterialGraphLink _pin=\"" ).append( link.substr( 0, equals ) ).append( "\" _fromNode=\"" ).append( link.substr( equals + 1, dot - equals - 1 ) ).append( "\" _fromPin=\"" ).append( link.substr( dot + 1 ) ).append( "\" />" );
            }
            text.append( "</_listLink><_listValue>" );
            for ( const string_view value : StringUtil::split( values, "," ) )
            {
                const size_t equals = value.find( '=' );
                if ( equals == string_view::npos )
                    continue;
                text.append( "<MaterialGraphValue _name=\"" ).append( value.substr( 0, equals ) ).append( "\" _value=\"" ).append( value.substr( equals + 1 ) ).append( "\" />" );
            }
            text.append( "</_listValue></MaterialGraphNodeDesc>" );
            return text;
        }

        static string wrapGraph( string_view nodes )
        {
            string text{ "<MaterialGraphDef formatVersion=\"0\"><_listNode>" };
            text.append( nodes ).append( "</_listNode></MaterialGraphDef>" );
            return text;
        }

        [[nodiscard]] static bool compileNodes( string_view nodes, MaterialGraphCompileResult& outResult )
        {
            MaterialGraphDef graph;
            if ( graph.loadFromXMLText( wrapGraph( nodes ), kSource ) == false )
            {
                MaterialGraphIssue issue;
                issue._id = "Unreadable";
                outResult._listIssue.push_back( std::move( issue ) );
                return false;
            }
            return MaterialGraphCompiler::compile( graph, kSource, outResult );
        }

        [[nodiscard]] static bool hasIssue( const MaterialGraphCompileResult& result, string_view id, string_view nodeName )
        {
            for ( const MaterialGraphIssue& issue : result._listIssue )
            {
                if ( issue._id == id && issue._nodeName == nodeName )
                    return true;
            }
            return false;
        }

        [[nodiscard]] static string withoutCarriageReturns( string_view text )
        {
            string out;
            for ( const utf8 c : text )
            {
                if ( c != '\r' )
                    out.push_back( c );
            }
            return out;
        }
    };
} // namespace

/** @brief [MaterialGraphCompilerTest] 등록한 노드 종류의 선언 글은 모두 문법에 맞고, 입력 · 출력 · 설정이 형식을 가진다 */
SW_TEST_CASE( MaterialGraphCompilerTest, EveryRegisteredKindParses )
{
    vector<string> listProblem;
    MaterialGraphNodeRegistry::collectProblems( listProblem );
    for ( const string& problem : listProblem )
    {
        SW_EXPECT_EQUAL( string{}, problem );
    }
    SW_EXPECT_TRUE( MaterialGraphNodeRegistry::find( "Fresnel" ) != nullptr );
    SW_EXPECT_TRUE( MaterialGraphNodeRegistry::find( "NoSuchKind" ) == nullptr );
}

/** @brief [MaterialGraphCompilerTest] 조각 노드의 식은 모두 펼쳐진다 — 등록의 오타(`$없는핀`)는 노드를 쓰기 전에 잡힌다 */
SW_TEST_CASE( MaterialGraphCompilerTest, EverySnippetExpands )
{
    vector<const MaterialGraphNodeKind*> listKind;
    MaterialGraphNodeRegistry::collectKinds( listKind );
    for ( const MaterialGraphNodeKind* pKind : listKind )
    {
        if ( pKind->getRole() != MaterialGraphNodeRole::Snippet )
            continue;
        string values;
        for ( uint32 slot = 0; slot < pKind->getInputCount(); ++slot )
        {
            if ( pKind->getInput( slot )._bRequired )
                values.append( values.empty() ? "" : "," ).append( pKind->getInput( slot )._name ).append( "=1" );
        }
        string link{ "BaseColor=Node." };
        link.append( pKind->getOutput( 0 )._name );
        string nodes = MaterialGraphTestUtil::makeNode( "Node", pKind->getName(), {}, values );
        nodes.append( MaterialGraphTestUtil::makeNode( "Out", "Output", link ) );
        MaterialGraphCompileResult result;
        (void)MaterialGraphTestUtil::compileNodes( nodes, result ); // 출력 형식이 float3 이 아니면 형식 문제는 난다 — 여기서는 펼침만 본다
        SW_EXPECT_FALSE( MaterialGraphTestUtil::hasIssue( result, "BadSnippet", "Node" ) );
        SW_EXPECT_FALSE( MaterialGraphTestUtil::hasIssue( result, "BadDefault", "Node" ) );
    }
}

/** @brief [MaterialGraphCompilerTest] 파라미터는 머티리얼 칸이 되고, 생성된 글을 머티리얼이 읽으며 원소가 16 바이트 배수로 채워진다 */
SW_TEST_CASE( MaterialGraphCompilerTest, ParameterBecomesMaterialField )
{
    string nodes = MaterialGraphTestUtil::makeNode( "Tint", "ColorParameter", {}, "Parameter=_tint,Default=1 0 0 1" );
    nodes.append( MaterialGraphTestUtil::makeNode( "Rough", "ScalarParameter", {}, "Parameter=_rough,Default=0.25" ) );
    nodes.append( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=Tint.RGB,Roughness=Rough.Out" ) );
    MaterialGraphCompileResult result;
    SW_REQUIRE_TRUE( MaterialGraphTestUtil::compileNodes( nodes, result ) );

    SW_EXPECT_TRUE( StringUtil::contains( result._hlsl, "float4 _tint;" ) );
    SW_EXPECT_TRUE( StringUtil::contains( result._hlsl, "output._roughness = n2_Out;" ) );
    SW_EXPECT_TRUE( StringUtil::contains( result._hlsl, "uint _swPad0;" ) ); // float4 하나 + 스칼라 둘(컷오프 · 거칠기) → 채움 둘
    SW_EXPECT_TRUE( StringUtil::contains( result._hlsl, "uint _swPad1;" ) );
    SW_EXPECT_FALSE( result._bEmissive );

    const string               text     = MaterialGraphCompiler::makeMaterialText( result, "test", "game/x/shaders/matgraph/test_00000000.hlsl", MaterialGraphTestUtil::kSource );
    const shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromXML( text ) );
    const MaterialProperty* pTint = material->findProperty( hashed_string( "_tint" ) );
    SW_REQUIRE_TRUE( pTint != nullptr );
    SW_EXPECT_TRUE( pTint->_type == MaterialPropertyType::Color );
    float32 rough = 0.0f;
    SW_EXPECT_TRUE( material->getScalarParameter( hashed_string( "_rough" ), rough ) );
    SW_EXPECT_NEAR( 0.25f, rough, 1e-5f );
}

/** @brief [MaterialGraphCompilerTest] 자체 발광을 쓰면 포워드 전용이 된다 — G버퍼에 칸이 없다 */
SW_TEST_CASE( MaterialGraphCompilerTest, EmissiveIsForwardOnly )
{
    const string               nodes = MaterialGraphTestUtil::makeNode( "Out", "Output", {}, "BaseColor=1 1 1,Emissive=2 0 0" );
    MaterialGraphCompileResult result;
    SW_REQUIRE_TRUE( MaterialGraphTestUtil::compileNodes( nodes, result ) );
    SW_EXPECT_TRUE( result._bEmissive );
    SW_EXPECT_TRUE( StringUtil::contains( result._hlsl, "#define SW_MATERIAL_GRAPH_EMISSIVE" ) );
    SW_EXPECT_TRUE( StringUtil::contains( MaterialGraphCompiler::makeMaterialText( result, "test", "x", "y" ), "MATERIAL_FORWARD_ONLY" ) );
}

/** @brief [MaterialGraphCompilerTest] 순환은 그 길의 노드 이름과 함께 오류다 */
SW_TEST_CASE( MaterialGraphCompilerTest, CycleIsReportedWithNames )
{
    string nodes = MaterialGraphTestUtil::makeNode( "A", "Saturate", "In=B.Out" );
    nodes.append( MaterialGraphTestUtil::makeNode( "B", "Saturate", "In=A.Out" ) );
    nodes.append( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=A.Out" ) );
    MaterialGraphCompileResult result;
    SW_EXPECT_FALSE( MaterialGraphTestUtil::compileNodes( nodes, result ) );
    SW_REQUIRE_TRUE( result._listIssue.size() == 1 );
    SW_EXPECT_EQUAL( string( "Cycle" ), result._listIssue[0]._id );
    SW_EXPECT_TRUE( StringUtil::contains( result._listIssue[0]._message, "A -> B -> A" ) );
}

/** @brief [MaterialGraphCompilerTest] 끊긴 필수 핀은 노드 이름과 핀 이름으로 오류고, 값을 적으면 풀린다 */
SW_TEST_CASE( MaterialGraphCompilerTest, MissingRequiredPinIsReported )
{
    MaterialGraphCompileResult missing;
    string                     nodes = MaterialGraphTestUtil::makeNode( "Mul", "Multiply", {}, "A=2" );
    nodes.append( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=Mul.Out" ) );
    SW_EXPECT_FALSE( MaterialGraphTestUtil::compileNodes( nodes, missing ) );
    SW_EXPECT_TRUE( MaterialGraphTestUtil::hasIssue( missing, "MissingPin", "Mul" ) );

    MaterialGraphCompileResult filled;
    string                     filledNodes = MaterialGraphTestUtil::makeNode( "Mul", "Multiply", {}, "A=2,B=3" );
    filledNodes.append( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=Mul.Out" ) );
    SW_EXPECT_TRUE( MaterialGraphTestUtil::compileNodes( filledNodes, filled ) ); // float 는 float3 으로 넓어진다
}

/** @brief [MaterialGraphCompilerTest] 벡터 크기가 맞지 않으면 노드 이름과 함께 오류다(float 만 넓어진다) */
SW_TEST_CASE( MaterialGraphCompilerTest, TypeMismatchIsReported )
{
    string wrongPin = MaterialGraphTestUtil::makeNode( "Vec", "Constant4" );
    wrongPin.append( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=Vec.Out" ) );
    MaterialGraphCompileResult wrongPinResult;
    SW_EXPECT_FALSE( MaterialGraphTestUtil::compileNodes( wrongPin, wrongPinResult ) );
    SW_EXPECT_TRUE( MaterialGraphTestUtil::hasIssue( wrongPinResult, "TypeMismatch", "Out" ) );

    string wrongAny = MaterialGraphTestUtil::makeNode( "Two", "Constant2" );
    wrongAny.append( MaterialGraphTestUtil::makeNode( "Three", "Constant3" ) );
    wrongAny.append( MaterialGraphTestUtil::makeNode( "Add", "Add", "A=Two.Out,B=Three.Out" ) );
    wrongAny.append( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=Add.Out" ) );
    MaterialGraphCompileResult wrongAnyResult;
    SW_EXPECT_FALSE( MaterialGraphTestUtil::compileNodes( wrongAny, wrongAnyResult ) );
    SW_EXPECT_TRUE( MaterialGraphTestUtil::hasIssue( wrongAnyResult, "TypeMismatch", "Add" ) );
}

/** @brief [MaterialGraphCompilerTest] 없는 노드 · 모르는 종류 · 출력 노드 없음 · 같은 칸의 다른 형식은 오류다 */
SW_TEST_CASE( MaterialGraphCompilerTest, BrokenReferencesAreReported )
{
    MaterialGraphCompileResult missingNode;
    SW_EXPECT_FALSE( MaterialGraphTestUtil::compileNodes( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=Ghost.Out" ), missingNode ) );
    SW_EXPECT_TRUE( MaterialGraphTestUtil::hasIssue( missingNode, "MissingNode", "Out" ) );

    MaterialGraphCompileResult unknownKind;
    SW_EXPECT_FALSE( MaterialGraphTestUtil::compileNodes( MaterialGraphTestUtil::makeNode( "X", "NoSuchKind" ), unknownKind ) );
    SW_EXPECT_TRUE( MaterialGraphTestUtil::hasIssue( unknownKind, "UnknownKind", "X" ) );

    MaterialGraphCompileResult noOutput;
    SW_EXPECT_FALSE( MaterialGraphTestUtil::compileNodes( MaterialGraphTestUtil::makeNode( "C", "Constant" ), noOutput ) );
    SW_EXPECT_TRUE( MaterialGraphTestUtil::hasIssue( noOutput, "NoOutput", "(graph)" ) );

    string conflict = MaterialGraphTestUtil::makeNode( "P1", "ScalarParameter", {}, "Parameter=_same" );
    conflict.append( MaterialGraphTestUtil::makeNode( "P2", "ColorParameter", {}, "Parameter=_same" ) );
    conflict.append( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=P2.RGB,Roughness=P1.Out" ) );
    MaterialGraphCompileResult conflictResult;
    SW_EXPECT_FALSE( MaterialGraphTestUtil::compileNodes( conflict, conflictResult ) );
    SW_EXPECT_TRUE( MaterialGraphTestUtil::hasIssue( conflictResult, "ParameterConflict", "P1" ) || MaterialGraphTestUtil::hasIssue( conflictResult, "ParameterConflict", "P2" ) );
}

/** @brief [MaterialGraphCompilerTest] 하위 그래프는 펼쳐 넣고, 안의 문제는 `바깥/안` 이름이며, 스스로를 부르면 오류다 */
SW_TEST_CASE( MaterialGraphCompilerTest, SubgraphIsInlined )
{
    const test::ScopedScratchDomain domain;
    const string                    subgraphID = test::ScopedScratchDomain::makeID( "sub.matgraph.xml" ).str();
    string                          subNodes   = MaterialGraphTestUtil::makeNode( "In", "GraphInput", {}, "Name=Scale,Type=float" );
    subNodes.append( MaterialGraphTestUtil::makeNode( "Twice", "Multiply", "A=In.Out", "B=2" ) );
    subNodes.append( MaterialGraphTestUtil::makeNode( "Result", "GraphOutput", "Value=Twice.Out", "Name=Doubled" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "sub.matgraph.xml" ), MaterialGraphTestUtil::wrapGraph( subNodes ) ) );

    MaterialGraphDef graph;
    string           nodes = MaterialGraphTestUtil::makeNode( "Two", "Constant", {}, "Value=2" );
    nodes.append( MaterialGraphTestUtil::makeNode( "Sub", "Subgraph", "Scale=Two.Out" ) );
    nodes.append( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=Sub.Doubled" ) );
    SW_REQUIRE_TRUE( graph.loadFromXMLText( MaterialGraphTestUtil::wrapGraph( nodes ), MaterialGraphTestUtil::kSource ) );
    graph._listNode[1]._assetPath = subgraphID;

    MaterialGraphCompileResult result;
    SW_EXPECT_TRUE( MaterialGraphCompiler::compile( graph, MaterialGraphTestUtil::kSource, result ) );
    SW_EXPECT_TRUE( StringUtil::contains( result._hlsl, "// Sub/Twice (Multiply)" ) );

    vector<MaterialGraphPinInfo> listInput;
    vector<MaterialGraphPinInfo> listOutput;
    SW_REQUIRE_TRUE( MaterialGraphCompiler::collectNodePins( graph._listNode[1], listInput, listOutput ) );
    SW_REQUIRE_TRUE( listInput.size() == 1 && listOutput.size() == 1 );
    SW_EXPECT_EQUAL( string( "Scale" ), listInput[0]._name );
    SW_EXPECT_EQUAL( string( "Doubled" ), listOutput[0]._name );

    // 스스로를 펼치는 하위 그래프
    string selfNodes = MaterialGraphTestUtil::makeNode( "Self", "Subgraph" );
    selfNodes.append( MaterialGraphTestUtil::makeNode( "Result", "GraphOutput", "Value=Self.Doubled", "Name=Doubled" ) );
    MaterialGraphDef selfGraph;
    SW_REQUIRE_TRUE( selfGraph.loadFromXMLText( MaterialGraphTestUtil::wrapGraph( selfNodes ), "self" ) );
    selfGraph._listNode[0]._assetPath = subgraphID;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "sub.matgraph.xml" ), selfGraph.toXMLText() ) );
    MaterialGraphCompileResult cycleResult;
    SW_EXPECT_FALSE( MaterialGraphCompiler::compile( graph, MaterialGraphTestUtil::kSource, cycleResult ) );
    SW_EXPECT_TRUE( MaterialGraphTestUtil::hasIssue( cycleResult, "SubgraphCycle", "Sub/Self" ) );
}

/** @brief [MaterialGraphCompilerTest] 그래프 변형은 `_base` 와 열쇠로 부모 노드의 값만 바꾼다 */
SW_TEST_CASE( MaterialGraphCompilerTest, BaseGraphIsInherited )
{
    const test::ScopedScratchDomain domain;
    string                          parentNodes = MaterialGraphTestUtil::makeNode( "Tint", "ColorParameter", {}, "Parameter=_tint,Default=1 1 1 1" );
    parentNodes.append( MaterialGraphTestUtil::makeNode( "Out", "Output", "BaseColor=Tint.RGB" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "parent.matgraph.xml" ), MaterialGraphTestUtil::wrapGraph( parentNodes ) ) );

    string child{ "<MaterialGraphDef _base=\"" };
    child.append( test::ScopedScratchDomain::makeID( "parent.matgraph.xml" ).str() );
    child.append( "\"><_listNode><MaterialGraphNodeDesc _name=\"Tint\"><_listValue><MaterialGraphValue _name=\"Default\" _value=\"0 1 0 1\" /></_listValue></MaterialGraphNodeDesc></_listNode></MaterialGraphDef>" );
    MaterialGraphDef graph;
    SW_REQUIRE_TRUE( graph.loadFromXMLText( child, "child" ) );
    MaterialGraphCompileResult result;
    SW_REQUIRE_TRUE( MaterialGraphCompiler::compile( graph, "child", result ) );
    bool bFound = false;
    for ( const MaterialGraphField& field : result._listField )
    {
        if ( field._name != "_tint" )
            continue;
        bFound = true;
        SW_EXPECT_EQUAL( string( "0 1 0 1" ), field._defaultValue );
    }
    SW_EXPECT_TRUE( bFound );
}

/** @brief [MaterialGraphCompilerTest] 생성물 자리는 그래프 경로에서 정해지고, materials 폴더 밖 그래프는 오류다 */
SW_TEST_CASE( MaterialGraphCompilerTest, GeneratedPathsFollowGraphPath )
{
    MaterialGraphGeneratedPaths paths;
    SW_REQUIRE_TRUE( MaterialGraphCook::makeGeneratedPaths( "game/x/materials/a/b.matgraph.xml", paths ) );
    SW_EXPECT_EQUAL( string( "game/x/shaders/matgraph" ), paths._shadersFolder );
    SW_EXPECT_EQUAL( string( "a_b" ), paths._stem );
    SW_EXPECT_EQUAL( string( "game/x/materials/a/b.gen.material" ), paths._materialPath );
    SW_EXPECT_LOG( "MaterialGraph.PathNotInMaterials" );
    SW_EXPECT_FALSE( MaterialGraphCook::makeGeneratedPaths( "game/x/maps/b.matgraph.xml", paths ) );
}

/** @brief [MaterialGraphCompilerTest] 저장소의 생성물은 그래프와 같다 — 다르면 `App --cook-shaders` 로 다시 쓴다(그래프를 손으로 고치고 생성하지 않은 것이다) */
SW_TEST_CASE( MaterialGraphCompilerTest, SampleGeneratedAssetsAreCurrent )
{
    constexpr const utf8* kGraphID = "game/empty/materials/matgraph/rim_glow.matgraph.xml";
    MaterialGraphDef      graph;
    SW_REQUIRE_TRUE( graph.loadFromAsset( AssetID{ kGraphID } ) );
    MaterialGraphCompileResult result;
    SW_REQUIRE_TRUE( MaterialGraphCompiler::compile( graph, kGraphID, result ) );
    MaterialGraphGeneratedPaths paths;
    SW_REQUIRE_TRUE( MaterialGraphCook::makeGeneratedPaths( kGraphID, paths ) );
    const string shaderPath = MaterialGraphCook::makeShaderPath( paths, result );
    string       shaderText;
    string       materialText;
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ shaderPath }, shaderText ) );
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ paths._materialPath }, materialText ) );
    SW_EXPECT_EQUAL( result._hlsl, MaterialGraphTestUtil::withoutCarriageReturns( shaderText ) );
    SW_EXPECT_EQUAL( MaterialGraphCompiler::makeMaterialText( result, paths._stem, shaderPath, kGraphID ), MaterialGraphTestUtil::withoutCarriageReturns( materialText ) );
}
