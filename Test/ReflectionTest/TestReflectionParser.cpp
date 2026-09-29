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
     *
     *          **찾는 순서는 `BuildTools` 가 먼저다.** 지금 빌드가 파서를 놓는 곳이 거기이기
     *          때문이다. 예전 배치에서는 `Bin` 에도 놓았는데, 그때 만들어진 실행 파일이 빌드
     *          디렉터리에 그대로 남아 있으면(정리되지 않는다) `Bin` 을 먼저 보는 순서에서는
     *          **몇 주 전 파서로 검사를 돌게 된다** — 실제로 ASAN 빌드에서 3주 묵은 바이너리가
     *          집혀 새 검사가 이유 없이 졌다. 초록이든 빨강이든 그 결과는 지금 코드에 대한
     *          답이 아니다.
     */
    sw::string findReflectionParserExecutable()
    {
        const sw::string binDir    = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
        const sw::string buildRoot = sw::FileUtil::getDirectoryPart( binDir );
        const sw::string toolsDir  = sw::FileUtil::joinPath( buildRoot, "BuildTools" );

        const sw::string arrCandidate[] = {
            sw::FileUtil::joinPath( toolsDir, "ReflectionParser.exe" ),
            sw::FileUtil::joinPath( toolsDir, "ReflectionParser" ),
            sw::FileUtil::joinPath( binDir, "ReflectionParser.exe" ),
            sw::FileUtil::joinPath( binDir, "ReflectionParser" ),
        };
        for ( const sw::string& candidate : arrCandidate )
        {
            if ( sw::FileUtil::fileExists( candidate ) )
                return candidate;
        }
        return {};
    }

    /**
     * @brief 헤더 하나를 파서에 넣는 명령줄입니다. 파서를 프로세스로 부르는 케이스들이 같은 인자 한 벌을 씁니다.
     * @details 예전에는 케이스마다 이 여덟 줄을 따로 적었습니다 — 파서에 인자가 하나 늘면 네 곳을 고쳐야 했습니다.
     */
    sw::string makeParserCommand( const sw::string& parserExe, const sw::string& headerPath, const sw::string& outGenDir,
                                  const sw::string& projectRoot )
    {
        return "\"" + parserExe + "\" " +
               "--input \"" + headerPath + "\" " +
               "--output \"" + outGenDir + "\" " +
               "--include \"" + sw::FileUtil::joinPath( projectRoot, "Source" ) + "\" " +
               "--annotation-meta \"" + sw::FileUtil::joinPath( projectRoot, "Source/Core/Predefined/AnnotationMeta.txt" ) + "\" " +
               "--builtins \"" + sw::FileUtil::joinPath( projectRoot, "Source/Engine/Reflection/ReflectBuiltins.xxx" ) + "\" " +
               "--emit-templates \"" + sw::FileUtil::joinPath( projectRoot, "Tools/ReflectionParser/Templates" ) + "\"";
    }

    /** @brief 파서에 넣을 임시 헤더 하나입니다. */
    struct TempHeader
    {
        sw::string _fileStem;
        sw::string _content;
    };

    /** @brief 파서를 임시 헤더들에 돌린 결과입니다. */
    struct ParserRunResult
    {
        int32                  _exitCode = -1;
        sw::string             _log;
        sw::vector<sw::string> _listGeneratedCpp; ///< 헤더 순서대로 .gen.cpp 내용(지우기 전에 읽는다). 안 만들어졌으면 빈 문자열
    };

    /**
     * @brief 임시 헤더들을 **한 번의 파서 실행**에 넣고 종료 코드 · 로그 · 산출물을 돌려줍니다. 헤더와 산출물은 돌린 뒤 지웁니다.
     * @details 진단 메시지를 보는 케이스들이 헤더 쓰기 · 명령줄 · 출력 수집 · 정리 스무 줄을 각자 들고 있었습니다. 헤더가 둘
     *          이상이면 파서는 그것들을 한 번역 단위로 묶습니다. `ResourceUtil::initialize()` 를 먼저 불러 두어야 합니다.
     */
    [[maybe_unused]] ParserRunResult runParserOnTempHeaders( const sw::string& parserExe, const sw::vector<TempHeader>& listHeader )
    {
        const sw::string binDir      = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
        const sw::string projectRoot = sw::ResourceUtil::getProjectFolderPath();
        const sw::string outGenDir   = sw::FileUtil::joinPath( binDir, "temp_gen_diagnostics" );
        sw::FileUtil::ensureDirectoryExists( outGenDir );

        ParserRunResult result;
        sw::string      command;
        for ( const TempHeader& header : listHeader )
        {
            const sw::string headerPath = sw::FileUtil::joinPath( binDir, header._fileStem + ".h" );
            if ( sw::FileUtil::writeTextFile( headerPath, header._content ) == false )
            {
                result._log = "failed to write " + headerPath;
                return result;
            }
            // 첫 헤더로 명령줄 한 벌을 만들고, 나머지는 --input 을 덧붙인다.
            if ( command.empty() )
                command = makeParserCommand( parserExe, headerPath, outGenDir, projectRoot );
            else
                command += " --input \"" + headerPath + "\"";
        }

        sw::ProcessOutputDelegate outputCb = SW_DELEGATE_LAMBDA(
            sw::ProcessOutputDelegate,
            [&result]( sw::string_view line )
        {
            result._log.append( line.data(), line.size() );
            result._log.push_back( '\n' );
        } );

        sw::ProcessOptions options;
        options._workingDirectory = projectRoot;
        result._exitCode          = sw::Process::execute( command, options, outputCb );

        for ( const TempHeader& header : listHeader )
        {
            const sw::string genCpp = sw::FileUtil::joinPath( outGenDir, header._fileStem + ".gen.cpp" );
            sw::string       generated;
            if ( sw::FileUtil::fileExists( genCpp ) )
                sw::FileUtil::readTextFile( genCpp, generated );
            result._listGeneratedCpp.push_back( generated );

            sw::FileUtil::removeFile( sw::FileUtil::joinPath( binDir, header._fileStem + ".h" ) );
            sw::FileUtil::removeFile( genCpp );
            sw::FileUtil::removeFile( sw::FileUtil::joinPath( outGenDir, header._fileStem + ".gen.h" ) );
            sw::FileUtil::removeFile( genCpp + ".stamp" );
        }
        return result;
    }

    /** @brief 임시 헤더 하나짜리 `runParserOnTempHeaders` 입니다. */
    [[maybe_unused]] ParserRunResult runParserOnTempHeader( const sw::string& parserExe, const sw::string& fileStem, const sw::string& headerContent )
    {
        sw::vector<TempHeader> listHeader;
        listHeader.push_back( TempHeader{ fileStem, headerContent } );
        return runParserOnTempHeaders( parserExe, listHeader );
    }

    /** @brief 멤버 하나에 PROPERTY 를 단 REFLECT 타입 헤더입니다. propertyArgs 가 PROPERTY( … ) 안에, memberType 이 멤버 타입 자리에 들어갑니다. */
    [[maybe_unused]] sw::string makeReflectedHeader( const sw::string& typeName, const sw::string& propertyArgs, const sw::string& memberType )
    {
        return "#pragma once\n"
               "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
               "namespace sw\n"
               "{\n"
               "\tREFLECT()\n"
               "\tstruct " +
               typeName + "\n"
                          "\t{\n"
                          "\t\tREFLECT_BODY();\n"
                          "\t\tPROPERTY( " +
               propertyArgs + " )\n"
                              "\t\t" +
               memberType + " _value;\n"
                            "\t};\n"
                            "}\n";
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
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "InvalidBitfieldSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct InvalidBitfieldSampleActor\n"
                                                       "\t{\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tuint8 _invalidMultiBit : 2;\n"
                                                       "\t};\n"
                                                       "}\n" );

    // 에러 코드로 끝나고, 1비트 불리언 플래그만 받는다는 진단이 나와야 한다.
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    const bool bFoundErrorDiagnosis = ( run._log.find( "bit width 2" ) != sw::string::npos ||
                                        run._log.find( "Only 1-bit bitfield boolean flags" ) != sw::string::npos );
    SW_EXPECT_TRUE_MSG( bFoundErrorDiagnosis, run._log.c_str() );
#else
    SW_TEST_SKIP( "ReflectionParser diagnostic logging is compiled out in Shipping builds" );
#endif
}

/**
 * @brief [ReflectionParserTest] AnnotationMeta.txt 에 없는 토큰은 조용히 버리지 않고 빌드를 세운다
 * @details 예전에는 모르는 토큰을 아무 말 없이 버렸다. 조명 컴포넌트 셋이 `PROPERTY( …, Color, … )` 로 색 선택기를
 *          요청하고 있었는데 그 토큰은 한 번도 생성 코드에 닿지 않았다(올바른 철자는 `Meta = "Color"` 다). 멤버 이름에
 *          color 가 들어 있어 인스펙터의 이름 휴리스틱이 증상을 가리고 있었다. 오타 하나가 기능 하나를 소리 없이 끄는
 *          구조라, 이제는 어느 타입 · 멤버의 어느 토큰인지 적고 멈춘다.
 */
SW_TEST_CASE( ReflectionParserTest, UnknownAnnotationTokenStopsTheBuild )
{
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "UnknownTokenSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct UnknownTokenSampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( Category = \"Light\", Colr )\n"
                                                       "\t\tint32 _value{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );

    // 무엇을 고쳐야 하는지가 메시지에 다 있어야 한다: 토큰 · 멤버.
    const bool bNamesTheToken  = run._log.find( "unknown token 'Colr'" ) != sw::string::npos;
    const bool bNamesTheMember = run._log.find( "sw::UnknownTokenSampleActor::_value" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bNamesTheToken && bNamesTheMember, run._log.c_str() );
#else
    SW_TEST_SKIP( "ReflectionParser diagnostic logging is compiled out in Shipping builds" );
#endif
}

/**
 * @brief [ReflectionParserTest] 값 참조를 돌려주는 메서드의 PROPERTY 는 오프셋 대신 값 접근자를 낸다
 * @details 씬 컴포넌트의 로컬 TRS 가 트랜스폼 저장소로 옮겨 가며 생긴 모양이다. 값은 객체 밖에 있고 이름(`Name`)은 옛 필드 이름을
 *          이어 쓴다. 모양이 틀리면(값으로 돌려준다 — 쓸 자리가 없다) 조용히 넘기지 않고 멈춘다.
 */
SW_TEST_CASE( ReflectionParserTest, AccessorPropertyEmitsValueAccessor )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "AccessorPropertySample",
                                                       "#pragma once\n"
                                                       "#include \"Core/Common/Types.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct AccessorPropertySampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( Name = \"_value\", Category = \"Sample\" )\n"
                                                       "\t\tint32& getValueRef();\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tint32 _level{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), run._listGeneratedCpp.size() );
    const sw::string& generated = run._listGeneratedCpp[0];
    // 이름은 Name 이 준 것, 자리는 메서드가 찾는다. 오프셋 식(offsetof)은 필드인 `_level` 에만 있다.
    SW_EXPECT_TRUE_MSG( generated.find( "::sw::hashed_string( \"_value\" )" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._pValueAccessor" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "->getValueRef()" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "offsetof(sw::AccessorPropertySampleActor, getValueRef)" ) == sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "offsetof(sw::AccessorPropertySampleActor, _level)" ) != sw::string::npos, generated.c_str() );

#if defined( SW_DEBUG )
    const ParserRunResult badRun = runParserOnTempHeader( parserExe, "AccessorPropertyByValueSample",
                                                          "#pragma once\n"
                                                          "#include \"Core/Common/Types.h\"\n"
                                                          "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                          "namespace sw\n"
                                                          "{\n"
                                                          "\tREFLECT()\n"
                                                          "\tstruct AccessorPropertyByValueSampleActor\n"
                                                          "\t{\n"
                                                          "\t\tREFLECT_BODY();\n"
                                                          "\t\tPROPERTY()\n"
                                                          "\t\tint32 getValue() const;\n"
                                                          "\t};\n"
                                                          "}\n" );
    SW_EXPECT_TRUE_MSG( badRun._exitCode != 0, badRun._log.c_str() );
    SW_EXPECT_TRUE_MSG( badRun._log.find( "sw::AccessorPropertyByValueSampleActor::getValue" ) != sw::string::npos, badRun._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 추상 컴포넌트는 컴포넌트 팩토리를 내지 않고, 그 파생은 낸다
 * @details 팩토리는 `addComponent<T>()` 로 T 를 만든다. 예전에는 컴포넌트에서 파생했으면 무조건 냈기 때문에, 공통 기반 컴포넌트
 *          (`REFLECT( Abstract )` · 보호된 생성자)를 두면 생성된 코드가 컴파일되지 않았다. `LightComponent` 가 첫 예다.
 */
SW_TEST_CASE( ReflectionParserTest, AbstractComponentGetsNoFactory )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "AbstractFactorySample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Object/Component/Component.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT( Abstract )\n"
                                                       "\tclass AbstractFactorySampleBase : public Component\n"
                                                       "\t{\n"
                                                       "\tpublic:\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\tprotected:\n"
                                                       "\t\texplicit AbstractFactorySampleBase( int32 kind ) : _kind{ kind } {}\n"
                                                       "\tprivate:\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tint32 _kind;\n"
                                                       "\t};\n"
                                                       "\tREFLECT()\n"
                                                       "\tclass AbstractFactorySampleConcrete : public AbstractFactorySampleBase\n"
                                                       "\t{\n"
                                                       "\tpublic:\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tAbstractFactorySampleConcrete() : AbstractFactorySampleBase( 1 ) {}\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( size_t( 1 ), run._listGeneratedCpp.size() );
    const sw::string& generated = run._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "registerComponentType<sw::AbstractFactorySampleConcrete>" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "registerComponentType<sw::AbstractFactorySampleBase>" ) == sw::string::npos, generated.c_str() );
}

/**
 * @brief [ReflectionParserTest] REFLECT() 없는 타입의 REFLECT_BODY() 는 빌드를 세운다
 * @details 이 검사는 처음부터 있었지만 **한 번도 돈 적이 없었다.** `REFLECT_BODY()` 가 만드는 마커 함수는 매크로
 *          전개 위치에 있고, 파서는 "주 파일에 있나" 를 `clang_Location_isFromMainFile` 로 물었는데 그 함수는 매크로
 *          위치를 늘 "아니다" 로 답한다 — 마커가 검사에 닿지 않았다. 지금은 선언을 매크로를 **쓴** 자리의 파일로 센다.
 */
SW_TEST_CASE( ReflectionParserTest, ReflectBodyWithoutReflectStopsTheBuild )
{
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "OrphanBodySample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tstruct OrphanBodySampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tint32 _value{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    const bool bNamesTheType = run._log.find( "REFLECT_BODY() is used in class/struct 'sw::OrphanBodySampleActor'" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bNamesTheType, run._log.c_str() );
#else
    SW_TEST_SKIP( "ReflectionParser diagnostic logging is compiled out in Shipping builds" );
#endif
}

/**
 * @brief [ReflectionParserTest] 헤더 여럿을 한 번역 단위로 묶어도, 깨진 헤더 하나가 나머지를 막지 않는다
 * @details 파서는 파싱할 헤더가 둘 이상이면 한 TU 로 묶는다 — 공통 include(CoreMinimal · Windows · D3D)가 비용의 거의 전부라,
 *          헤더마다 TU 를 따로 만들면 같은 것을 헤더 수만큼 다시 파싱한다(Engine 27 개: 3.37 → 1.04 초, CPU 39 → 1 초).
 *          묶으면 새 실패 경로가 둘 생긴다. (1) 한 헤더의 애노테이션 오류 — 예전 순회는 오류에서 멈췄으므로 그대로 두면 다른 헤더의
 *          수집까지 끊긴다 → 오류는 헤더 단위로 남기고 계속 돈다. (2) 한 헤더의 C++ 오류 — 묶음 전체가 파싱되지 않는다 →
 *          헤더마다 다시 파싱해 그 헤더의 오류로 알린다. 두 경우 모두 성한 헤더의 산출물은 만들어져야 한다.
 */
SW_TEST_CASE( ReflectionParserTest, OneBrokenHeaderDoesNotBlockTheOthers )
{
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    // (1) 애노테이션 오류 — 묶음은 파싱되고, 그 헤더만 실패한다. 깨진 헤더를 **앞에** 둔다 — 오류에서 순회가 멈추면 뒤 헤더가 빈다.
    sw::vector<TempHeader> listAnnotationCase;
    listAnnotationCase.push_back( TempHeader{ "BatchTypoSample", makeReflectedHeader( "BatchTypoSampleActor", "Colr", "int32" ) } );
    listAnnotationCase.push_back( TempHeader{ "BatchGoodSample", makeReflectedHeader( "BatchGoodSampleActor", "Category = \"A\"", "int32" ) } );
    const ParserRunResult annotationRun = runParserOnTempHeaders( parserExe, listAnnotationCase );
    SW_EXPECT_TRUE_MSG( annotationRun._exitCode != 0, annotationRun._log.c_str() );
    SW_EXPECT_TRUE_MSG( annotationRun._listGeneratedCpp[1].find( "BatchGoodSampleActor" ) != sw::string::npos, annotationRun._log.c_str() );
    SW_EXPECT_TRUE_MSG( annotationRun._log.find( "unknown token 'Colr'" ) != sw::string::npos, annotationRun._log.c_str() );

    // (2) C++ 오류 — 묶음이 파싱되지 않아 헤더마다 다시 한다. 깨진 헤더가 오류로 나오고, 성한 헤더는 만들어진다.
    sw::vector<TempHeader> listSyntaxCase;
    listSyntaxCase.push_back( TempHeader{ "BatchGoodSample", makeReflectedHeader( "BatchGoodSampleActor", "Category = \"A\"", "int32" ) } );
    listSyntaxCase.push_back( TempHeader{ "BatchBrokenSample", makeReflectedHeader( "BatchBrokenSampleActor", "", "NoSuchTypeAnywhere" ) } );
    const ParserRunResult syntaxRun = runParserOnTempHeaders( parserExe, listSyntaxCase );
    SW_EXPECT_TRUE_MSG( syntaxRun._exitCode != 0, syntaxRun._log.c_str() );
    SW_EXPECT_TRUE_MSG( syntaxRun._listGeneratedCpp[0].find( "BatchGoodSampleActor" ) != sw::string::npos, syntaxRun._log.c_str() );
    SW_EXPECT_TRUE_MSG( syntaxRun._log.find( "Parse failed:" ) != sw::string::npos &&
                            syntaxRun._log.find( "BatchBrokenSample.h" ) != sw::string::npos,
                        syntaxRun._log.c_str() );
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

    const sw::string command = makeParserCommand( parserExe, headerPath, outGenDir, projectRoot );

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
    // 최신 판정의 기준은 산출물이 아니라 스탬프(마지막으로 성공한 생성)다 — 같이 과거로 보낸다.
    const sw::string stampPath = genPath + ".stamp";
    SW_ASSERT_TRUE( sw::FileUtil::fileExists( stampPath ) );
    std::filesystem::last_write_time( stampPath.c_str(), parserTime - std::chrono::hours( 1 ) );

    SW_ASSERT_EQUAL( 0, sw::Process::execute( command, options, {} ) );

    sw::string regeneratedText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genPath, regeneratedText ) );
    SW_EXPECT_TRUE_MSG( regeneratedText.find( "SW_STALE_PROBE" ) == sw::string::npos,
                        "파서가 자기보다 오래된 산출물을 그대로 두었습니다 — 도구를 고쳐도 옛 모양이 남습니다" );

    sw::FileUtil::removeFile( headerPath );
    sw::FileUtil::removeFile( genPath );
    sw::FileUtil::removeFile( genHeaderPath );
    sw::FileUtil::removeFile( stampPath );
}

/**
 * @brief [ReflectionParserTest] 같은 이름의 헤더 둘이 같은 산출물을 노리면 **조용히 덮지 않는다**
 * @details 생성 파일 이름은 소스의 **파일 이름만** 으로 짓는다(`makeGeneratedPath`). 그래서 한 모듈
 *          안에 같은 이름의 헤더가 둘 있으면 나중에 도는 쪽이 앞의 것을 덮고, **앞 헤더의 타입들은
 *          아무 말 없이 등록되지 않는다** — 증상은 한참 뒤 "씬이 그 컴포넌트를 못 찾는다" 로 나타나
 *          원인이 코드젠이라는 것을 짚기 어렵다. 지금은 그 자리에서 빌드를 세우고 두 경로를 다 적는다.
 *
 *          헤더를 **옮긴** 경우(옛 경로가 더는 없다)는 정상이므로 조용히 덮어써야 한다 — 이 케이스는
 *          그것도 함께 본다. 그러지 않으면 파일을 옮길 때마다 빌드가 막힌다.
 */
SW_TEST_CASE( ReflectionParserTest, SameFileNameInOneOutputDirIsRejected )
{
    const sw::string binDir    = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );

    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string projectRoot = sw::ResourceUtil::getProjectFolderPath();

    const sw::string caseRoot  = sw::FileUtil::joinPath( binDir, "temp_collide" );
    const sw::string dirA      = sw::FileUtil::joinPath( caseRoot, "A" );
    const sw::string dirB      = sw::FileUtil::joinPath( caseRoot, "B" );
    const sw::string outGenDir = sw::FileUtil::joinPath( caseRoot, "gen" );
    sw::FileUtil::ensureDirectoryExists( dirA );
    sw::FileUtil::ensureDirectoryExists( dirB );
    sw::FileUtil::ensureDirectoryExists( outGenDir );

    const sw::string headerA = sw::FileUtil::joinPath( dirA, "CollidingSample.h" );
    const sw::string headerB = sw::FileUtil::joinPath( dirB, "CollidingSample.h" );

    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [headerA, headerB, outGenDir]()
    {
        sw::FileUtil::removeFile( headerA );
        sw::FileUtil::removeFile( headerB );
        sw::FileUtil::removeFile( sw::FileUtil::joinPath( outGenDir, "CollidingSample.gen.cpp" ) );
        sw::FileUtil::removeFile( sw::FileUtil::joinPath( outGenDir, "CollidingSample.gen.h" ) );
    } ) );

    const auto makeHeader = []( const utf8* pTypeName ) -> sw::string
    {
        sw::string content = "#pragma once\n"
                             "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                             "namespace sw\n"
                             "{\n"
                             "\tREFLECT()\n"
                             "\tstruct ";
        content += pTypeName;
        content += "\n"
                   "\t{\n"
                   "\t\tREFLECT_BODY();\n"
                   "\t\tPROPERTY()\n"
                   "\t\tint32 _value{ 0 };\n"
                   "\t};\n"
                   "}\n";
        return content;
    };

    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerA, makeHeader( "CollidingSampleA" ) ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerB, makeHeader( "CollidingSampleB" ) ) );

    sw::string outLog;
    const auto runParser = [&]( const sw::string& headerPath ) -> int32
    {
        const sw::string cmd = makeParserCommand( parserExe, headerPath, outGenDir, projectRoot );

        sw::ProcessOptions options;
        options._workingDirectory = projectRoot;

        // 파서가 무엇 때문에 졌는지 실패 메시지에 담는다 — 안 그러면 "exit 0" 만 남는다.
        outLog.clear();
        sw::ProcessOutputDelegate outputCb = SW_DELEGATE_LAMBDA(
            sw::ProcessOutputDelegate,
            [&outLog]( sw::string_view line )
        {
            outLog.append( line.data(), line.size() );
            outLog.push_back( '\n' );
        } );
        return sw::Process::execute( cmd, options, outputCb );
    };

    // **남아 있는 산출물을 먼저 지운다.** 파서는 "이미 최신" 이면 통째로 건너뛰므로, 앞선 실행이
    // 남긴 파일이 있으면 첫 단계가 아무것도 만들지 않고 지나가 이 케이스가 제 할 일을 못 한다.
    const sw::string genCppPath = sw::FileUtil::joinPath( outGenDir, "CollidingSample.gen.cpp" );
    sw::FileUtil::removeFile( genCppPath );
    sw::FileUtil::removeFile( sw::FileUtil::joinPath( outGenDir, "CollidingSample.gen.h" ) );

    // 첫 헤더는 정상적으로 산출물을 만든다.
    const int32 firstExit = runParser( headerA );
    SW_EXPECT_TRUE_MSG( firstExit == 0, outLog.c_str() );
    SW_ASSERT_EQUAL( 0, firstExit );
    SW_ASSERT_TRUE( sw::FileUtil::fileExists( genCppPath ) );

    // 정말 A 로 만들어졌는지 확인한다 — 이 다음 단계가 그 사실에 기댄다.
    sw::string firstGenerated;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genCppPath, firstGenerated ) );
    SW_ASSERT_TRUE( firstGenerated.find( "CollidingSampleA" ) != sw::string::npos );

    // 같은 이름의 다른 헤더는 그 산출물을 덮지 못한다.
    const int32 collideExit = runParser( headerB );
    SW_EXPECT_TRUE_MSG( collideExit != 0 && outLog.find( "file name collision" ) != sw::string::npos,
                        outLog.c_str() );

    // 앞 헤더의 타입이 산출물에 그대로 남아 있어야 한다.
    sw::string generated;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genCppPath, generated ) );
    SW_EXPECT_TRUE( generated.find( "CollidingSampleA" ) != sw::string::npos );

    // **옮긴 헤더는 막지 않는다.** 앞 헤더를 지우면 B 가 그 자리를 이어받을 수 있어야 한다.
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( headerA ) );
    SW_EXPECT_TRUE_MSG( runParser( headerB ) == 0,
                        "옛 헤더가 사라졌는데도 산출물 갱신을 막았습니다 — 파일을 옮길 때마다 빌드가 막힙니다" );
}
