#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"

#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/ParserUtil.h"

#include "ReflectionTest/TestReflectionFixtures.h"
#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <filesystem>

// ReflectionParser — 런타임이 아니라 **도구** 를 본다. 애노테이션·주석 파싱과 경로 판별.

namespace
{
    /**
     * @brief 이 빌드의 `ReflectionParser` 실행 파일 경로. 못 찾으면 빈 문자열.
     * @details **`Bin` 옆이 아니라 `BuildTools` 에 있다.** 그것을 모르고 `Bin` 만 보던 검사는
     *          늘 스스로 건너뛰었고(백로그에 "스킵 1건" 으로 적혀 있었다), 그래서 파서를 부르는
     *          유일한 테스트가 한 번도 돈 적이 없었다. 두 자리를 다 본다.
     */
    sw::string findReflectionParserExecutable()
    {
        const sw::string binDir    = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
        const sw::string buildRoot = sw::FileUtil::getDirectoryPart( binDir );

        const sw::string arrCandidate[] = {
            sw::FileUtil::joinPath( binDir, "ReflectionParser.exe" ),
            sw::FileUtil::joinPath( binDir, "ReflectionParser" ),
            sw::FileUtil::joinPath( sw::FileUtil::joinPath( buildRoot, "BuildTools" ), "ReflectionParser.exe" ),
            sw::FileUtil::joinPath( sw::FileUtil::joinPath( buildRoot, "BuildTools" ), "ReflectionParser" ),
        };
        for ( const sw::string& candidate : arrCandidate )
        {
            if ( sw::FileUtil::fileExists( candidate ) )
                return candidate;
        }
        return {};
    }
} // namespace

/**
 * @brief [ReflectionParserTest] 주석 내에 있는 매크로 문자열은 파싱되지 않아야 함
 */

SW_TEST_CASE( ReflectionParserTest, FallbackCommentTest )
{
    const sw::TypeInfo* info = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::TestScriptComponent" ) );
    SW_ASSERT_NOT_NULL( info );

    // _shouldNotBeParsed 프로퍼티는 등록되지 않아야 함 (주석 안에 PROPERTY()가 있으므로 무시되어야 함)
    const sw::PropertyInfo* badProp = info->findProperty( sw::hashed_string( "_shouldNotBeParsed" ) );
    SW_EXPECT_TRUE( badProp == nullptr );
}

/**
 * @brief [ReflectionParserTest] ParserUtil 경로 조합 및 Include 경로 추출 검증
 */
SW_TEST_CASE( ReflectionParserTest, ParserUtilPathAndIncludeGeneration )
{
    // 1) makeGeneratedPath 검증
    const sw::string genCpp = sw::ParserUtil::makeGeneratedPath( "build/Ninja-Debug/Bin", "Source/Engine/Input/KeyCodes.h", ".gen.cpp" );
    SW_EXPECT_EQUAL( sw::string( "build/Ninja-Debug/Bin/KeyCodes.gen.cpp" ), genCpp );

    const sw::string genH = sw::ParserUtil::makeGeneratedPath( "output/dir", "Foo/Bar/MyActor.hpp", ".gen.h" );
    SW_EXPECT_EQUAL( sw::string( "output/dir/MyActor.gen.h" ), genH );

    // 2) makeHeaderIncludePath 검증
    sw::vector<sw::string> listIncludeRoots;
    listIncludeRoots.push_back( "Source" );
    listIncludeRoots.push_back( "Test" );

    const sw::string includePath = sw::ParserUtil::makeHeaderIncludePath(
        "Source/Engine/Object/GameObject.h",
        listIncludeRoots );
    SW_EXPECT_EQUAL( sw::string( "Engine/Object/GameObject.h" ), includePath );
}

/**
 * @brief [ReflectionParserTest] ParserUtil splitCommaRespectingAngles 템플릿 중첩 쉼표 분할 검증
 */
SW_TEST_CASE( ReflectionParserTest, ParserUtilSplitCommaRespectingAngles )
{
    // 1) 단일 토큰
    const auto listSingle = sw::ParserUtil::splitCommaRespectingAngles( "int32" );
    SW_EXPECT_EQUAL( 1u, listSingle.size() );
    if ( listSingle.empty() == false )
        SW_EXPECT_EQUAL( sw::string( "int32" ), listSingle[0] );

    // 2) 복합 중첩 템플릿 (map<string, vector<int32>>, float32, pair<int32, int32>)
    const auto listComplex = sw::ParserUtil::splitCommaRespectingAngles(
        "int32, vector<string>, map<string, vector<int32>>, float32" );
    SW_EXPECT_EQUAL( 4u, listComplex.size() );
    if ( listComplex.size() == 4 )
    {
        SW_EXPECT_EQUAL( sw::string( "int32" ), listComplex[0] );
        SW_EXPECT_EQUAL( sw::string( "vector<string>" ), listComplex[1] );
        SW_EXPECT_EQUAL( sw::string( "map<string, vector<int32>>" ), listComplex[2] );
        SW_EXPECT_EQUAL( sw::string( "float32" ), listComplex[3] );
    }

    // 3) 깊은 중첩 < < < > > >
    const auto listDeep = sw::ParserUtil::splitCommaRespectingAngles( "A<B<C<D>>>, E<F>" );
    SW_EXPECT_EQUAL( 2u, listDeep.size() );
    if ( listDeep.size() == 2 )
    {
        SW_EXPECT_EQUAL( sw::string( "A<B<C<D>>>" ), listDeep[0] );
        SW_EXPECT_EQUAL( sw::string( "E<F>" ), listDeep[1] );
    }
}

/**
 * @brief [ReflectionParserTest] AnnotationMeta tryParseAnnotationKind 파싱 검증
 */
SW_TEST_CASE( ReflectionParserTest, AnnotationKindParsing )
{
    sw::AnnotationBinding::Kind kind = sw::AnnotationBinding::Kind::Flag;

    SW_EXPECT_TRUE( sw::tryParseAnnotationKind( "flag", kind ) );
    SW_EXPECT_TRUE( kind == sw::AnnotationBinding::Kind::Flag );

    SW_EXPECT_TRUE( sw::tryParseAnnotationKind( "bool", kind ) );
    SW_EXPECT_TRUE( kind == sw::AnnotationBinding::Kind::Bool );

    SW_EXPECT_TRUE( sw::tryParseAnnotationKind( "string", kind ) );
    SW_EXPECT_TRUE( kind == sw::AnnotationBinding::Kind::String );

    SW_EXPECT_TRUE( sw::tryParseAnnotationKind( "float", kind ) );
    SW_EXPECT_TRUE( kind == sw::AnnotationBinding::Kind::Float );

    SW_EXPECT_TRUE( sw::tryParseAnnotationKind( "netrole", kind ) );
    SW_EXPECT_TRUE( kind == sw::AnnotationBinding::Kind::NetRole );

    SW_EXPECT_FALSE( sw::tryParseAnnotationKind( "nonexistent", kind ) );
}

/**
 * @brief [ReflectionParserTest] 플래그를 `X = true` 로 적어도 단독 토큰과 같게 붙는다
 * @details AnnotationMeta.txt 는 단독 토큰(flag)과 `key=value`(bool)를 **따로** 적었고 그 둘이
 *          어긋나 있었다 — 플래그 열셋 중 여섯(Abstract·Static·AssetPath·Polymorphic·Reliable·
 *          Validate)에 bool 줄이 없어 `PROPERTY( Polymorphic = true )` 가 경고 한 줄 없이 버려졌다.
 *          어느 쪽이 빠졌는지는 애노테이션을 적는 자리에서 보이지 않는다. 이제 flag 한 줄이 두
 *          형태를 함께 등록한다.
 */
SW_TEST_CASE( ReflectionParserTest, AssignedFlagFormMatchesBareToken )
{
    const sw::TypeInfo* pAbstract = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AssignedAbstractBase" ) );
    SW_ASSERT_NOT_NULL( pAbstract );
    SW_EXPECT_TRUE( pAbstract->_bAbstract == SW_TRUE );

    const sw::TypeInfo* pStatic = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AssignedStaticLibrary" ) );
    SW_ASSERT_NOT_NULL( pStatic );
    SW_EXPECT_TRUE( pStatic->_bStatic == SW_TRUE );

    const sw::TypeInfo* pActor = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AssignedFlagActor" ) );
    SW_ASSERT_NOT_NULL( pActor );

    const sw::PropertyInfo* pAlbedo = pActor->findProperty( sw::hashed_string( "_albedo" ) );
    SW_ASSERT_NOT_NULL( pAlbedo );
    SW_EXPECT_TRUE( pAlbedo->_metadata._bAssetPath == SW_TRUE );

    const sw::PropertyInfo* pPayload = pActor->findProperty( sw::hashed_string( "_payload" ) );
    SW_ASSERT_NOT_NULL( pPayload );
    SW_EXPECT_TRUE( pPayload->_metadata._bPolymorphic == SW_TRUE );

    // 별칭 목록이 한쪽만 늘어나 있던 자리 — 단독 토큰으로는 `xmlAttribute` 가 먹혔다.
    const sw::PropertyInfo* pTag = pActor->findProperty( sw::hashed_string( "_tag" ) );
    SW_ASSERT_NOT_NULL( pTag );
    SW_EXPECT_TRUE( pTag->_metadata._bXmlAttribute == SW_TRUE );

    const sw::FunctionInfo* pPing = pActor->findMethod( sw::hashed_string( "ping" ) );
    SW_ASSERT_NOT_NULL( pPing );
    SW_EXPECT_TRUE( pPing->_metadata._bReliable == SW_TRUE );
    SW_EXPECT_TRUE( pPing->_metadata._bValidate == SW_TRUE );
    SW_EXPECT_TRUE( pPing->_metadata._netRole == sw::FunctionNetRole::Server );
}

/**
 * @brief [ReflectionParserTest] ReflectionParser 코드젠 출력 메타데이터 및 Static/Ctor 심볼 검증
 */
SW_TEST_CASE( ReflectionParserTest, CodegenStaticLibraryAndCtorMetadata )
{
    // 1) Static 라이브러리 함수 심볼 코드젠 검증 (StaticDemoLibrary::doubleInt)
    const sw::TypeInfo* pStaticType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::StaticDemoLibrary" ) );
    SW_ASSERT_NOT_NULL( pStaticType );
    SW_EXPECT_TRUE( pStaticType->_bStatic == SW_TRUE );

    const sw::FunctionInfo* pFunc = pStaticType->findMethod( sw::hashed_string( "doubleInt" ) );
    SW_ASSERT_NOT_NULL( pFunc );
    SW_EXPECT_TRUE( pFunc->_metadata._bStatic == SW_TRUE );
#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( sw::string( "Math" ), pFunc->_metadata._category );
    SW_EXPECT_EQUAL( sw::string( "Double Int" ), pFunc->_metadata._displayName );
#endif

    // 2) 명시적 생성자 코드젠 ($ctor, $ctor(int32)) 검증
    const sw::TypeInfo* pCtorType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::CtorDemoActor" ) );
    SW_ASSERT_NOT_NULL( pCtorType );

    const sw::FunctionInfo* pDefaultCtor = pCtorType->findMethod( sw::hashed_string( "$ctor" ) );
    SW_ASSERT_NOT_NULL( pDefaultCtor );
    SW_EXPECT_TRUE( pDefaultCtor->_metadata._bConstructor == SW_TRUE );

    const sw::FunctionInfo* pParamCtor = pCtorType->findMethod( sw::hashed_string( "$ctor(int32)" ) );
    SW_ASSERT_NOT_NULL( pParamCtor );
    SW_EXPECT_TRUE( pParamCtor->_metadata._bConstructor == SW_TRUE );

    // 3) AbstractDemoBase _bAbstract 검증
    const sw::TypeInfo* pAbstractType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AbstractDemoBase" ) );
    SW_ASSERT_NOT_NULL( pAbstractType );
    SW_EXPECT_TRUE( pAbstractType->_bAbstract == SW_TRUE );

    // 4) AssetPathActor 메타데이터 검증
    const sw::TypeInfo* pAssetPathType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AssetPathActor" ) );
    SW_ASSERT_NOT_NULL( pAssetPathType );
    const sw::PropertyInfo* pAlbedoProp = pAssetPathType->findProperty( sw::hashed_string( "_albedo" ) );
    SW_ASSERT_NOT_NULL( pAlbedoProp );
    SW_EXPECT_TRUE( pAlbedoProp->_metadata._bAssetPath == SW_TRUE );
    SW_EXPECT_EQUAL( sw::string( "Texture" ), pAlbedoProp->_metadata._assetType );
}

/**
 * @brief [ReflectionParserTest] 프로퍼티 다중 별칭, 타입 개명 호환 및 저작 기본값 코드젠 검증
 */
SW_TEST_CASE( ReflectionParserTest, MultiplePropertyAliasesAndRenameCompat )
{
    // 1) 프로퍼티 다중 별칭 (Alias = "hp, HitPoints")
    const sw::TypeInfo* pAliasType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AliasAndReorderTestActor" ) );
    SW_ASSERT_NOT_NULL( pAliasType );

    const sw::PropertyInfo* pMainProp = pAliasType->findProperty( sw::hashed_string( "_currentHp" ) );
    const sw::PropertyInfo* pAlias1   = pAliasType->findProperty( sw::hashed_string( "hp" ) );
    const sw::PropertyInfo* pAlias2   = pAliasType->findProperty( sw::hashed_string( "HitPoints" ) );

    SW_ASSERT_NOT_NULL( pMainProp );
    SW_EXPECT_EQUAL( pMainProp, pAlias1 );
    SW_EXPECT_EQUAL( pMainProp, pAlias2 );

    // 2) 타입 개명 호환 (Alias = LegacyRenameActor)
    const sw::TypeInfo* pTypeCurrent = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::RenameCompatActor" ) );
    const sw::TypeInfo* pTypeLegacy  = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::LegacyRenameActor" ) );
    SW_ASSERT_NOT_NULL( pTypeCurrent );
    SW_ASSERT_NOT_NULL( pTypeLegacy );
    SW_EXPECT_EQUAL( pTypeCurrent->_typeId, pTypeLegacy->_typeId );
    SW_EXPECT_TRUE( pTypeCurrent->_fullyQualifiedName == pTypeLegacy->_fullyQualifiedName );

    // 3) 저작 기본값 (PROPERTY(Default = "75"))
    const sw::TypeInfo* pDefaultType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DefaultValueTestActor" ) );
    SW_ASSERT_NOT_NULL( pDefaultType );
    const sw::PropertyInfo* pManaProp  = pDefaultType->findProperty( sw::hashed_string( "_mana" ) );
    const sw::PropertyInfo* pTitleProp = pDefaultType->findProperty( sw::hashed_string( "_title" ) );
    SW_ASSERT_NOT_NULL( pManaProp );
    SW_ASSERT_NOT_NULL( pTitleProp );
    SW_EXPECT_EQUAL( sw::string( "75" ), pManaProp->_metadata._defaultValue );
    SW_EXPECT_EQUAL( sw::string( "Apprentice" ), pTitleProp->_metadata._defaultValue );
    SW_EXPECT_TRUE( pTitleProp->_metadata._bXmlAttribute == SW_TRUE );
}

/**
 * @brief [ReflectionParserTest] ParserUtil 극단적 템플릿 중첩 및 경로 불일치 경계조건 검증
 */
SW_TEST_CASE( ReflectionParserTest, ParserUtilExtremeEdgeCases )
{
    // 1) 4단계 이상 깊은 중첩 템플릿 분할
    const auto listTokens = sw::ParserUtil::splitCommaRespectingAngles(
        "  tuple<int32, map<string, vector<pair<int32, int32>>>, float64> ,   bool  " );
    SW_EXPECT_EQUAL( 2u, listTokens.size() );
    if ( listTokens.size() == 2 )
    {
        SW_EXPECT_EQUAL( sw::string( "tuple<int32, map<string, vector<pair<int32, int32>>>, float64>" ), listTokens[0] );
        SW_EXPECT_EQUAL( sw::string( "bool" ), listTokens[1] );
    }

    // 2) 쉼표 없는 단일 표현식 및 빈 문자열
    const auto listEmpty = sw::ParserUtil::splitCommaRespectingAngles( "" );
    SW_EXPECT_EQUAL( 0u, listEmpty.size() );

    const auto listSpaces = sw::ParserUtil::splitCommaRespectingAngles( "   " );
    SW_EXPECT_EQUAL( 0u, listSpaces.size() );

    // 3) makeHeaderIncludePath 루트 불일치 시 파일명 fallback
    sw::vector<sw::string> listRoots;
    listRoots.push_back( "OtherProject/Source" );
    const sw::string fallback = sw::ParserUtil::makeHeaderIncludePath( "Projects/Source/Engine/Foo.h", listRoots );
    SW_EXPECT_EQUAL( sw::string( "Foo.h" ), fallback );
}

/**
 * @brief [ReflectionParserTest] RpcDemoActor 메타데이터 및 Invoker 실행 검증
 */
SW_TEST_CASE( ReflectionParserTest, RpcMethodMetadataAndInvokerExecution )
{
    const sw::TypeInfo* pRpcType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::RpcDemoActor" ) );
    SW_ASSERT_NOT_NULL( pRpcType );

    const sw::FunctionInfo* pMethod = pRpcType->findMethod( sw::hashed_string( "applyDamage" ) );
    SW_ASSERT_NOT_NULL( pMethod );

    SW_EXPECT_TRUE( pMethod->_metadata._netRole == sw::FunctionNetRole::Server );
    SW_EXPECT_TRUE( pMethod->_metadata._bReliable == SW_TRUE );
#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( sw::string( "Combat" ), pMethod->_metadata._category );
    SW_EXPECT_EQUAL( sw::string( "Apply Damage" ), pMethod->_metadata._displayName );
    SW_EXPECT_EQUAL( sw::string( "Subtracts amount from HP" ), pMethod->_metadata._tooltip );
#endif

    // Invoker 실행 검증
    sw::RpcDemoActor actor;
    actor._hp = 100;
    sw::TaskArgs args;
    args.add( int32{ 35 } );
    pMethod->_invoker( &actor, args );
    SW_EXPECT_EQUAL( 65, actor._hp );
}

/**
 * @brief [ReflectionParserTest] :2 이상 다중 비트 비트필드에 PROPERTY() 선언 시 빌드타임 컴파일 에러 진단 검증
 */
SW_TEST_CASE( ReflectionParserTest, MultiBitBitfieldCompilationErrorDiagnosis )
{
#if defined( SW_DEBUG )
    const sw::string binDir    = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );

    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string projectRoot    = sw::ResourceUtil::getProjectFolderPath();
    const sw::string tempHeaderPath = sw::FileUtil::joinPath( binDir, "InvalidBitfieldSample.h" );
    const sw::string outGenDir      = sw::FileUtil::joinPath( binDir, "temp_gen" );
    sw::FileUtil::ensureDirectoryExists( outGenDir );

    const sw::string headerContent = "#pragma once\n"
                                     "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                     "namespace sw\n"
                                     "{\n"
                                     "\tREFLECT()\n"
                                     "\tstruct InvalidBitfieldSampleActor\n"
                                     "\t{\n"
                                     "\t\tPROPERTY()\n"
                                     "\t\tuint8 _invalidMultiBit : 2;\n"
                                     "\t};\n"
                                     "}\n";

    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( tempHeaderPath, headerContent ) );

    sw::string                capturedLog;
    sw::ProcessOutputDelegate outputCb = SW_DELEGATE_LAMBDA(
        sw::ProcessOutputDelegate,
        [&capturedLog]( sw::string_view line )
    {
        capturedLog.append( line.data(), line.size() );
        capturedLog.push_back( '\n' );
    } );

    const sw::string cmd = "\"" + parserExe + "\" " +
                           "--input \"" + tempHeaderPath + "\" " +
                           "--output \"" + outGenDir + "\" " +
                           "--include \"" + sw::FileUtil::joinPath( projectRoot, "Source" ) + "\" " +
                           "--annotation-meta \"" + sw::FileUtil::joinPath( projectRoot, "Source/Core/Predefined/AnnotationMeta.txt" ) + "\" " +
                           "--builtins \"" + sw::FileUtil::joinPath( projectRoot, "Source/Engine/Reflection/ReflectBuiltins.xxx" ) + "\" " +
                           "--emit-templates \"" + sw::FileUtil::joinPath( projectRoot, "Tools/ReflectionParser/Templates" ) + "\"";

    sw::ProcessOptions options;
    options._workingDirectory = projectRoot;

    const int32 exitCode = sw::Process::execute( cmd, options, outputCb );

    // 에러 코드로 종료되어야 함 (exitCode != 0)
    SW_EXPECT_TRUE( exitCode != 0 );

    // 1비트 불리언 플래그만 지원한다는 정확한 진단 메시지 출력 확인
    const bool bFoundErrorDiagnosis = ( capturedLog.find( "bit width 2" ) != sw::string::npos ||
                                        capturedLog.find( "Only 1-bit bitfield boolean flags" ) != sw::string::npos );
    SW_EXPECT_TRUE( bFoundErrorDiagnosis );

    // 임시 파일 정리
    sw::FileUtil::removeFile( tempHeaderPath );
#else
    SW_TEST_SKIP( "ReflectionParser diagnostic logging is compiled out in Shipping builds" );
#endif
}

/**
 * @brief [ReflectionParserTest] 파서 자신이 새로워지면 산출물을 다시 만든다
 * @details **도구도 입력이다.** 예전에는 입력 헤더·템플릿(.tpl)·builtins 의 시간만 보고, 정작 그것을
 *          조립하는 실행 파일은 보지 않았다 — `CodeGenerator` 나 `AstVisitor` 를 고쳐 다시 빌드해도
 *          산출물이 예전 모양 그대로 남았다(CMake 는 exe 를 DEPENDS 에 걸어 파서를 다시 부르지만,
 *          파서가 스스로 "최신" 이라며 건너뛰었다). 그 상태에서 일부 파일만 다른 이유로 다시
 *          만들어지면 **두 모양이 섞인다.**
 *
 *          검사는 그 상황을 그대로 만든다: 한 번 생성한 뒤 산출물에 표식을 심고 그 파일을 파서보다
 *          **과거로** 돌린다. 다시 돌렸을 때 표식이 사라져 있으면 다시 만든 것이다.
 */
SW_TEST_CASE( ReflectionParserTest, RegeneratesWhenTheParserItselfIsNewer )
{
    const sw::string binDir    = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );

    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string projectRoot = sw::ResourceUtil::getProjectFolderPath();
    const sw::string headerPath  = sw::FileUtil::joinPath( binDir, "StalenessProbeSample.h" );
    const sw::string outGenDir   = sw::FileUtil::joinPath( binDir, "temp_gen_staleness" );
    sw::FileUtil::ensureDirectoryExists( outGenDir );

    const sw::string headerContent = "#pragma once\n"
                                     "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                     "namespace sw\n"
                                     "{\n"
                                     "    REFLECT()\n"
                                     "    struct StalenessProbeSampleActor\n"
                                     "    {\n"
                                     "        REFLECT_BODY();\n"
                                     "\n"
                                     "        PROPERTY()\n"
                                     "        int32 _value;\n"
                                     "    };\n"
                                     "}\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerPath, headerContent ) );

    const sw::string command = "\"" + parserExe + "\" " +
                               "--input \"" + headerPath + "\" " +
                               "--output \"" + outGenDir + "\" " +
                               "--include \"" + sw::FileUtil::joinPath( projectRoot, "Source" ) + "\" " +
                               "--annotation-meta \"" + sw::FileUtil::joinPath( projectRoot, "Source/Core/Predefined/AnnotationMeta.txt" ) + "\" " +
                               "--builtins \"" + sw::FileUtil::joinPath( projectRoot, "Source/Engine/Reflection/ReflectBuiltins.xxx" ) + "\" " +
                               "--emit-templates \"" + sw::FileUtil::joinPath( projectRoot, "Tools/ReflectionParser/Templates" ) + "\"";

    sw::ProcessOptions options;
    options._workingDirectory = projectRoot;

    SW_ASSERT_EQUAL( 0, sw::Process::execute( command, options, {} ) );

    const sw::string genPath = sw::FileUtil::joinPath( outGenDir, "StalenessProbeSample.gen.cpp" );
    SW_ASSERT_TRUE( sw::FileUtil::fileExists( genPath ) );

    // 표식을 심고, 산출물을 파서보다 한 시간 과거로 돌린다.
    sw::string generatedText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genPath, generatedText ) );
    generatedText += "\n// SW_STALE_PROBE\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( genPath, generatedText ) );

    // 시간을 셋으로 벌린다: 입력(2시간 전) < 산출물(1시간 전) < 파서(지금).
    // **입력을 같이 과거로 보내는 것이 핵심이다** — 안 그러면 "입력이 더 새롭다" 는 이유로 다시
    // 만들어져, 이 검사가 파서 시간을 보는지 아닌지를 구분하지 못한다(실제로 그렇게 통과했다).
    const std::filesystem::file_time_type parserTime = std::filesystem::last_write_time( parserExe.c_str() );
    std::filesystem::last_write_time( headerPath.c_str(), parserTime - std::chrono::hours( 2 ) );
    std::filesystem::last_write_time( genPath.c_str(), parserTime - std::chrono::hours( 1 ) );
    const sw::string genHeaderPath = sw::FileUtil::joinPath( outGenDir, "StalenessProbeSample.gen.h" );
    if ( sw::FileUtil::fileExists( genHeaderPath ) )
        std::filesystem::last_write_time( genHeaderPath.c_str(), parserTime - std::chrono::hours( 1 ) );

    SW_ASSERT_EQUAL( 0, sw::Process::execute( command, options, {} ) );

    sw::string regeneratedText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genPath, regeneratedText ) );
    SW_EXPECT_TRUE_MSG( regeneratedText.find( "SW_STALE_PROBE" ) == sw::string::npos,
                        "파서가 자기보다 오래된 산출물을 그대로 두었습니다 — 도구를 고쳐도 옛 모양이 남습니다" );

    sw::FileUtil::removeFile( headerPath );
    sw::FileUtil::removeFile( genPath );
    sw::FileUtil::removeFile( genHeaderPath );
}
