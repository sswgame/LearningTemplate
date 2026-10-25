#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Resource/ResourcePaths.h"

#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/ParserUtil.h"

#include "ReflectionTest/TestReflectionFixtures.h"
#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// ReflectionParser — 런타임이 아니라 **도구** 를 본다. 애노테이션·주석 파싱과 경로 판별.

namespace
{
    /** @brief 파일 시각 한 시간(`FileUtil` 의 100 ns 눈금)입니다. */
    constexpr int64 kHourTicks = 3600 * sw::FileUtil::kFileTimeTicksPerSecond;

    /**
     * @brief 이 빌드의 `ReflectionParser` 실행 파일 경로. 못 찾으면 빈 문자열.
     * @details **`Bin` 옆이 아니라 `BuildTools` 에 있다.** `Bin` 만 보면 검사가 늘 스스로 건너뛰어
     *          파서를 부르는 케이스가 돌지 않는다. 두 자리를 다 본다.
     *
     *          **찾는 순서는 `BuildTools` 가 먼저다.** 지금 빌드가 파서를 놓는 곳이 거기이기
     *          때문이다. 빌드 디렉터리의 `Bin` 에 낡은 파서 실행 파일이 남아 있으면(정리되지 않는다)
     *          `Bin` 을 먼저 보는 순서에서는 **낡은 파서로 검사를 돌게 된다** — 초록이든 빨강이든
     *          그 결과는 지금 코드에 대한 답이 아니다.
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
            if ( sw::FileUtil::exists( candidate ) )
                return candidate;
        }
        return {};
    }

    /**
     * @brief 헤더 하나를 파서에 넣는 명령줄입니다. 파서를 프로세스로 부르는 케이스들이 같은 인자 한 벌을 씁니다.
     * @details 파서에 인자가 하나 늘면 여기 한 곳만 고칩니다.
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
        sw::vector<sw::string> _listGeneratedCpp;    ///< 헤더 순서대로 .gen.cpp 내용(지우기 전에 읽는다). 안 만들어졌으면 빈 문자열
        sw::vector<sw::string> _listGeneratedHeader; ///< 헤더 순서대로 .gen.h 내용. 안 만들어졌으면 빈 문자열
    };

    /**
     * @brief 임시 헤더들을 **한 번의 파서 실행**에 넣고 종료 코드 · 로그 · 산출물을 돌려줍니다.
     * @details 헤더가 둘 이상이면 파서는 그것들을 한 번역 단위로 묶습니다. `ResourcePaths::initialize()` 를 먼저 불러 두어야 합니다.
     *          실행마다 **새 폴더**(이 케이스의 임시 경로 아래)를 쓰므로 앞 실행의 산출물이 "이미 최신" 으로 읽히지 않고,
     *          케이스가 끝나면 프레임워크가 통째로 지웁니다. 실행 파일 폴더(`Bin`)에 두면 같은 `Bin` 을 쓰는 두 프로세스가
     *          서로의 산출물을 밟습니다.
     */
    [[maybe_unused]] ParserRunResult runParserOnTempHeaders( const sw::string& parserExe, const sw::vector<TempHeader>& listHeader,
                                                             const sw::string& extraArguments = {} )
    {
        static uint32 s_runIndex = 0;

        sw::StringBuilder<sw::constant::kMaxBuffer64> runName;
        runName.append( "parser_run_" );
        runName.append( static_cast<int32>( ++s_runIndex ) );
        const sw::string runRoot     = test::makeTempPath( runName.c_str() );
        const sw::string projectRoot = sw::ResourcePaths::getProjectFolderPath();
        const sw::string outGenDir   = sw::FileUtil::joinPath( runRoot, "gen" );
        sw::FileUtil::ensureDirectoryExists( outGenDir );

        ParserRunResult result;
        sw::string      command;
        for ( const TempHeader& header : listHeader )
        {
            const sw::string headerPath = sw::FileUtil::joinPath( runRoot, header._fileStem + ".h" );
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
        if ( extraArguments.empty() == false )
            command += " " + extraArguments;

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
            if ( sw::FileUtil::exists( genCpp ) && sw::FileUtil::readTextFile( genCpp, generated ) == false )
                generated.clear(); // 못 읽은 산출물은 빈 것으로 본다 — 테스트가 내용으로 진다
            result._listGeneratedCpp.push_back( generated );

            const sw::string genHeader = sw::FileUtil::joinPath( outGenDir, header._fileStem + ".gen.h" );
            sw::string       generatedHeader;
            if ( sw::FileUtil::exists( genHeader ) && sw::FileUtil::readTextFile( genHeader, generatedHeader ) == false )
                generatedHeader.clear();
            result._listGeneratedHeader.push_back( generatedHeader );
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

    /** @brief 서버 버스 본문 타입(`kBusVersion`) 하나짜리 헤더입니다. exportMacro 가 `struct` 와 이름 사이에 들어갑니다(빈 문자열이면 내보내기 없음). */
    [[maybe_unused]] sw::string makeBusBodyHeader( const sw::string& typeName, const sw::string& exportMacro )
    {
        return "#pragma once\n"
               "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
               "#include \"RuntimeAPI/Export/GameFrameworkAPI.h\"\n"
               "namespace sw\n"
               "{\n"
               "\tREFLECT()\n"
               "\tstruct " +
               exportMacro + " " + typeName +
               "\n"
               "\t{\n"
               "\t\tREFLECT_BODY();\n"
               "\t\tstatic constexpr uint32 kBusVersion = 1;\n"
               "\t\tPROPERTY()\n"
               "\t\tint32 _value{ 0 };\n"
               "\t};\n"
               "}\n";
    }
} // namespace

/**
 * @brief [ReflectionParserTest] 주석 내에 있는 매크로 문자열은 파싱되지 않아야 함
 */
SW_TEST_CASE( ReflectionParserTest, FallbackCommentTest )
{
    const sw::TypeInfo* info = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::TestScriptComponent" ) );
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
    const sw::string genCpp = sw::ParserUtil::makeGeneratedPath( "build/Ninja-Debug/Bin", "Source/Engine/Input/KeyCodeUtil.h", ".gen.cpp" );
    SW_EXPECT_EQUAL( sw::string( "build/Ninja-Debug/Bin/KeyCodeUtil.gen.cpp" ), genCpp );

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
 * @details AnnotationMeta.txt 의 flag 한 줄이 두 형태를 함께 등록한다. 단독 토큰(flag)과 `key=value`(bool)를
 *          **따로** 적으면 둘이 어긋나 `PROPERTY( Polymorphic = true )` 같은 것이 경고 한 줄 없이 버려지고,
 *          어느 쪽이 빠졌는지는 애노테이션을 적는 자리에서 보이지 않는다.
 */
SW_TEST_CASE( ReflectionParserTest, AssignedFlagFormMatchesBareToken )
{
    const sw::TypeInfo* pAbstract = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::AssignedAbstractBase" ) );
    SW_ASSERT_NOT_NULL( pAbstract );
    SW_EXPECT_TRUE( pAbstract->_bAbstract == SW_TRUE );

    const sw::TypeInfo* pStatic = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::AssignedStaticLibrary" ) );
    SW_ASSERT_NOT_NULL( pStatic );
    SW_EXPECT_TRUE( pStatic->_bStatic == SW_TRUE );

    const sw::TypeInfo* pActor = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::AssignedFlagActor" ) );
    SW_ASSERT_NOT_NULL( pActor );

    const sw::PropertyInfo* pAlbedo = pActor->findProperty( sw::hashed_string( "_albedo" ) );
    SW_ASSERT_NOT_NULL( pAlbedo );
    SW_EXPECT_TRUE( pAlbedo->_metadata->_bAssetPath == SW_TRUE );

    const sw::PropertyInfo* pPayload = pActor->findProperty( sw::hashed_string( "_payload" ) );
    SW_ASSERT_NOT_NULL( pPayload );
    SW_EXPECT_TRUE( pPayload->_metadata->_bPolymorphic == SW_TRUE );

    // 별칭 목록이 한쪽만 늘어나 있던 자리 — 단독 토큰으로는 `xmlAttribute` 가 먹혔다.
    const sw::PropertyInfo* pTag = pActor->findProperty( sw::hashed_string( "_tag" ) );
    SW_ASSERT_NOT_NULL( pTag );
    SW_EXPECT_TRUE( pTag->_metadata->_bXMLAttribute == SW_TRUE );

    const sw::FunctionInfo* pPing = pActor->findMethod( sw::hashed_string( "ping" ) );
    SW_ASSERT_NOT_NULL( pPing );
    SW_EXPECT_TRUE( pPing->_metadata->_bReliable == SW_TRUE );
    SW_EXPECT_TRUE( pPing->_metadata->_bValidate == SW_TRUE );
    SW_EXPECT_TRUE( pPing->_metadata->_netRole == sw::FunctionNetRole::Server );
}

/**
 * @brief [ReflectionParserTest] ReflectionParser 코드젠 출력 메타데이터 및 Static/Ctor 심볼 검증
 */
SW_TEST_CASE( ReflectionParserTest, CodegenStaticLibraryAndCtorMetadata )
{
    // 1) Static 라이브러리 함수 심볼 코드젠 검증 (StaticDemoLibrary::doubleInt)
    const sw::TypeInfo* pStaticType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::StaticDemoLibrary" ) );
    SW_ASSERT_NOT_NULL( pStaticType );
    SW_EXPECT_TRUE( pStaticType->_bStatic == SW_TRUE );

    const sw::FunctionInfo* pFunc = pStaticType->findMethod( sw::hashed_string( "doubleInt" ) );
    SW_ASSERT_NOT_NULL( pFunc );
    SW_EXPECT_TRUE( pFunc->_metadata->_bStatic == SW_TRUE );
#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( sw::string( "Math" ), pFunc->_metadata->_category );
    SW_EXPECT_EQUAL( sw::string( "Double Int" ), pFunc->_metadata->_displayName );
#endif

    // 2) 명시적 생성자 코드젠 ($ctor, $ctor(int32)) 검증
    const sw::TypeInfo* pCtorType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::CtorDemoActor" ) );
    SW_ASSERT_NOT_NULL( pCtorType );

    const sw::FunctionInfo* pDefaultCtor = pCtorType->findMethod( sw::hashed_string( "$ctor" ) );
    SW_ASSERT_NOT_NULL( pDefaultCtor );
    SW_EXPECT_TRUE( pDefaultCtor->_metadata->_bConstructor == SW_TRUE );

    const sw::FunctionInfo* pParamCtor = pCtorType->findMethod( sw::hashed_string( "$ctor(int32)" ) );
    SW_ASSERT_NOT_NULL( pParamCtor );
    SW_EXPECT_TRUE( pParamCtor->_metadata->_bConstructor == SW_TRUE );

    // 3) AbstractDemoBase _bAbstract 검증
    const sw::TypeInfo* pAbstractType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::AbstractDemoBase" ) );
    SW_ASSERT_NOT_NULL( pAbstractType );
    SW_EXPECT_TRUE( pAbstractType->_bAbstract == SW_TRUE );

    // 4) AssetPathActor 메타데이터 검증
    const sw::TypeInfo* pAssetPathType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::AssetPathActor" ) );
    SW_ASSERT_NOT_NULL( pAssetPathType );
    const sw::PropertyInfo* pAlbedoProp = pAssetPathType->findProperty( sw::hashed_string( "_albedo" ) );
    SW_ASSERT_NOT_NULL( pAlbedoProp );
    SW_EXPECT_TRUE( pAlbedoProp->_metadata->_bAssetPath == SW_TRUE );
    SW_EXPECT_EQUAL( sw::string( "Texture" ), pAlbedoProp->_metadata->_assetType );
}

/**
 * @brief [ReflectionParserTest] 프로퍼티 다중 별칭, 타입 개명 호환 및 저작 기본값 코드젠 검증
 */
SW_TEST_CASE( ReflectionParserTest, MultiplePropertyAliasesAndRenameCompat )
{
    // 1) 프로퍼티 다중 별칭 (Alias = "hp, HitPoints")
    const sw::TypeInfo* pAliasType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::AliasAndReorderTestActor" ) );
    SW_ASSERT_NOT_NULL( pAliasType );

    const sw::PropertyInfo* pMainProp = pAliasType->findProperty( sw::hashed_string( "_currentHp" ) );
    const sw::PropertyInfo* pAlias1   = pAliasType->findProperty( sw::hashed_string( "hp" ) );
    const sw::PropertyInfo* pAlias2   = pAliasType->findProperty( sw::hashed_string( "HitPoints" ) );

    SW_ASSERT_NOT_NULL( pMainProp );
    SW_EXPECT_EQUAL( pMainProp, pAlias1 );
    SW_EXPECT_EQUAL( pMainProp, pAlias2 );

    // 2) 타입 개명 호환 (Alias = LegacyRenameActor)
    const sw::TypeInfo* pTypeCurrent = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::RenameCompatActor" ) );
    const sw::TypeInfo* pTypeLegacy  = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::LegacyRenameActor" ) );
    SW_ASSERT_NOT_NULL( pTypeCurrent );
    SW_ASSERT_NOT_NULL( pTypeLegacy );
    SW_EXPECT_EQUAL( pTypeCurrent->_typeID, pTypeLegacy->_typeID );
    SW_EXPECT_TRUE( pTypeCurrent->_fullyQualifiedName == pTypeLegacy->_fullyQualifiedName );

    // 3) 저작 기본값 (PROPERTY(Default = "75"))
    const sw::TypeInfo* pDefaultType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::DefaultValueTestActor" ) );
    SW_ASSERT_NOT_NULL( pDefaultType );
    const sw::PropertyInfo* pManaProp  = pDefaultType->findProperty( sw::hashed_string( "_mana" ) );
    const sw::PropertyInfo* pTitleProp = pDefaultType->findProperty( sw::hashed_string( "_title" ) );
    SW_ASSERT_NOT_NULL( pManaProp );
    SW_ASSERT_NOT_NULL( pTitleProp );
    SW_EXPECT_EQUAL( sw::string( "75" ), pManaProp->_metadata->_defaultValue );
    SW_EXPECT_EQUAL( sw::string( "Apprentice" ), pTitleProp->_metadata->_defaultValue );
    SW_EXPECT_TRUE( pTitleProp->_metadata->_bXMLAttribute == SW_TRUE );
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
 * @brief [ReflectionParserTest] RPCDemoActor 메타데이터 및 Invoker 실행 검증
 */
SW_TEST_CASE( ReflectionParserTest, RPCMethodMetadataAndInvokerExecution )
{
    const sw::TypeInfo* pRPCType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::RPCDemoActor" ) );
    SW_ASSERT_NOT_NULL( pRPCType );

    const sw::FunctionInfo* pMethod = pRPCType->findMethod( sw::hashed_string( "applyDamage" ) );
    SW_ASSERT_NOT_NULL( pMethod );

    SW_EXPECT_TRUE( pMethod->_metadata->_netRole == sw::FunctionNetRole::Server );
    SW_EXPECT_TRUE( pMethod->_metadata->_bReliable == SW_TRUE );
#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( sw::string( "Combat" ), pMethod->_metadata->_category );
    SW_EXPECT_EQUAL( sw::string( "Apply Damage" ), pMethod->_metadata->_displayName );
    SW_EXPECT_EQUAL( sw::string( "Subtracts amount from HP" ), pMethod->_metadata->_tooltip );
#endif

    // Invoker 실행 검증
    sw::RPCDemoActor actor;
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
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

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
#endif
}

/**
 * @brief [ReflectionParserTest] AnnotationMeta.txt 에 없는 토큰은 조용히 버리지 않고 빌드를 세운다
 * @details 모르는 토큰을 아무 말 없이 버리면 오타 하나가 기능 하나를 소리 없이 끈다 — 예컨대 `PROPERTY( …, Color, … )` 는
 *          색 선택기를 요청한 것처럼 보이지만 올바른 철자는 `Meta = "Color"` 이고, 멤버 이름에 color 가 들어 있으면 인스펙터의
 *          이름 휴리스틱이 증상을 가린다. 그래서 어느 타입 · 멤버의 어느 토큰인지 적고 멈춘다.
 */
SW_TEST_CASE( ReflectionParserTest, UnknownAnnotationTokenStopsTheBuild )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

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
                                                       "\tREFLECT()\n"
                                                       "\tstruct BadNumberSampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( Min = 0.5f )\n"
                                                       "\t\tfloat32 _ratio{ 0.0f };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );

    // 무엇을 고쳐야 하는지가 메시지에 다 있어야 한다: 토큰 · 멤버.
    const bool bNamesTheToken  = run._log.find( "unknown token 'Colr'" ) != sw::string::npos;
    const bool bNamesTheMember = run._log.find( "sw::UnknownTokenSampleActor::_value" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bNamesTheToken && bNamesTheMember, run._log.c_str() );

    // 숫자를 받는 자리에 숫자가 아닌 값(C++ 습관의 접미사 f)은 조용히 0 이 되지 않고 멈춘다.
    const bool bNamesTheBadNumber = run._log.find( "Min = 0.5f  (value is not a number or a list of numbers and field names)" ) != sw::string::npos &&
                                    run._log.find( "sw::BadNumberSampleActor::_ratio" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bNamesTheBadNumber, run._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 옛 이름 키는 `Alias` 하나다 — `PreviousName` · `PreviousNames` 는 모르는 토큰이다
 * @details 같은 필드에 철자가 셋이면 헤더마다 다른 철자가 퍼지고, 옛 이름을 찾는 grep 이 한 철자만 보고 끝난다.
 */
SW_TEST_CASE( ReflectionParserTest, AliasHasOneSpelling )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "AliasSpellingSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT( PreviousName = \"OldSample\" )\n"
                                                       "\tstruct AliasSpellingSampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t};\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct AliasSpellingSampleHolder\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( PreviousNames = \"hp\" )\n"
                                                       "\t\tint32 _health{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "unknown token 'PreviousName" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "unknown token 'PreviousNames" ) != sw::string::npos, run._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 한 타입의 애노테이션 오류는 첫 하나에서 멈추지 않고 모두 알린다
 * @details 오류마다 빌드를 한 번씩 되풀이하지 않게 한 번의 실행이 그 타입의 오류를 전부 적는다. 오류 난 멤버는 코드젠에 넣지 않으므로
 *          실행은 여전히 실패로 끝난다.
 */
SW_TEST_CASE( ReflectionParserTest, EveryAnnotationErrorOfATypeIsReported )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "ManyErrorsSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct ManyErrorsSampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( Colr )\n"
                                                       "\t\tint32 _first{ 0 };\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tuint8 _second : 2;\n"
                                                       "\t\tPROPERTY( Min = 0.5f )\n"
                                                       "\t\tfloat32 _third{ 0.0f };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "unknown token 'Colr'" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "bit width 2" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "sw::ManyErrorsSampleActor::_third" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._listGeneratedCpp.empty() || run._listGeneratedCpp[0].empty(), "an errored header must not emit code" );
#endif
}

/**
 * @brief [ReflectionParserTest] 고정 배치형 키를 잘못 적으면 헤더 · 줄 · 필드와 함께 생성이 멈춥니다
 * @details 범위 필드가 뒤에 있음(읽는 쪽이 범위를 모릅니다), float32 로 정확히 나타낼 수 없는 정수 경계(경계는 float32 로 보관합니다),
 *          실수 필드를 범위로 쓰는 정수 필드, 찾지 못한 범위 이름, 가변 길이가 아닌 필드의 MaxCount, 정수가 아닌 MaxCount, bool 필드의 Min · Max 입니다.
 *          적격이 아닌 것(포인터)은 오류가 아닙니다. 코덱 대신 이유가 생성 주석에 남습니다.
 */
SW_TEST_CASE( ReflectionParserTest, FixedLayoutAnnotationMistakesStopTheBuild )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "FixedMistakeSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct FixedLateRangeSample\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( Step = 0.5, Min = \"-_half\", Max = \"_half\" )\n"
                                                       "\t\tfloat32 _offset{ 0.0f };\n"
                                                       "\t\tPROPERTY( Min = 1, Max = 100 )\n"
                                                       "\t\tfloat32 _half{ 1.0f };\n"
                                                       "\t};\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct FixedWideRangeSample\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( Min = 0, Max = 16777217 )\n"
                                                       "\t\tint32 _value{ 0 };\n"
                                                       "\t\tPROPERTY( Min = 0, Max = 2000000000 )\n"
                                                       "\t\tint32 _exactWide{ 0 };\n"
                                                       "\t\tPROPERTY( MaxCount = 4 )\n"
                                                       "\t\tint32 _count{ 0 };\n"
                                                       "\t\tPROPERTY( Min = \"_missing\" )\n"
                                                       "\t\tint32 _orphan{ 0 };\n"
                                                       "\t\tPROPERTY( Min = 1, Max = 10 )\n"
                                                       "\t\tfloat32 _scale{ 1.0f };\n"
                                                       "\t\tPROPERTY( Min = 0, Max = \"_scale\" )\n"
                                                       "\t\tint32 _steps{ 0 };\n"
                                                       "\t\tPROPERTY( Min = 0, Max = 1 )\n"
                                                       "\t\tbool _bToggle{ false };\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tint32* _pIgnored{ nullptr };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    const bool bLateRange = run._log.find( "FixedMistakeSample.h(10): fixed layout of 'sw::FixedLateRangeSample::_offset': range field '_half' must be "
                                           "declared before this field" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bLateRange, run._log.c_str() );
    const bool bWideRange = run._log.find( "fixed layout of 'sw::FixedWideRangeSample::_value': integer range term '16777217' is not exactly representable as float32" ) !=
                            sw::string::npos;
    SW_EXPECT_TRUE_MSG( bWideRange, run._log.c_str() );
    // 2e9 는 float32 로 정확합니다. 크기가 아니라 정확성이 기준입니다.
    SW_EXPECT_TRUE_MSG( run._log.find( "FixedWideRangeSample::_exactWide" ) == sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "'sw::FixedWideRangeSample::_steps': range field '_scale' is floating-point" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "'sw::FixedWideRangeSample::_bToggle': Min/Max apply to integer" ) != sw::string::npos, run._log.c_str() );

    // MaxCount 는 정수로 읽습니다. 실수로 읽어 반올림하지 않습니다. 애노테이션 단계의 오류라 따로 돌립니다(그 헤더는 코덱 생성까지 가지 않습니다).
    const ParserRunResult countRun = runParserOnTempHeader( parserExe, "FixedFractionCountSample",
                                                            "#pragma once\n"
                                                            "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                            "#include \"Core/Container/vector.h\"\n"
                                                            "namespace sw\n"
                                                            "{\n"
                                                            "\tREFLECT()\n"
                                                            "\tstruct FixedFractionCountSample\n"
                                                            "\t{\n"
                                                            "\t\tREFLECT_BODY();\n"
                                                            "\t\tPROPERTY( MaxCount = 2.5 )\n"
                                                            "\t\tvector<int32> _listValue;\n"
                                                            "\t};\n"
                                                            "}\n" );
    SW_EXPECT_TRUE_MSG( countRun._exitCode != 0, countRun._log.c_str() );
    SW_EXPECT_TRUE_MSG( countRun._log.find( "MaxCount = 2.5  (value is not a whole number of at least 1 or an integer constant name)" ) != sw::string::npos,
                        countRun._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "'sw::FixedWideRangeSample::_count': MaxCount applies to string, vector and DynamicBitset fields only" ) != sw::string::npos,
                        run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "'sw::FixedWideRangeSample::_orphan': range name '_missing' is not a field of this type" ) != sw::string::npos,
                        run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "_pIgnored" ) == sw::string::npos, "an ineligible field is not an error" );
#endif
}

/**
 * @brief [ReflectionParserTest] `MaxCount` 는 C++ 정수 상수 이름을 받아 그 값을 코덱 · 정규 문자열 · `PropertyMetadata::_maxCount` 에 넣습니다
 * @details 상한의 기준을 상수 하나로 두려는 것입니다. 이름은 필드가 든 타입에서 먼저 찾고, 없으면 바깥 네임스페이스에서 한정 이름(`Limit::kMax`)으로 찾습니다.
 *          찾지 못한 이름, 정수가 아닌 상수, 1 보다 작은 값은 생성 오류입니다 — 상한을 조용히 빼면 그 필드는 코덱 없이 넘어갑니다.
 */
SW_TEST_CASE( ReflectionParserTest, MaxCountReadsANamedIntegerConstant )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "NamedCountSample",
                                                       "#pragma once\n"
                                                       "#include \"Core/Common/Types.h\"\n"
                                                       "#include \"Core/Container/string.h\"\n"
                                                       "#include \"Core/Container/vector.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tstruct NamedCountLimit\n"
                                                       "\t{\n"
                                                       "\t\tstatic constexpr int32 kMaxNameSize = 24;\n"
                                                       "\t};\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct NamedCountSample\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tstatic constexpr uint32 kMaxSlotCount = 37;\n"
                                                       "\t\tPROPERTY( MaxCount = kMaxSlotCount )\n"
                                                       "\t\tvector<int32> _listSlot;\n"
                                                       "\t\tPROPERTY( MaxCount = NamedCountLimit::kMaxNameSize )\n"
                                                       "\t\tstring _name;\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), run._listGeneratedCpp.size() );
    const sw::string generated = run._listGeneratedCpp[0] + run._listGeneratedHeader[0];
    SW_EXPECT_TRUE_MSG( generated.find( "_listSlot:" ) != sw::string::npos && generated.find( ":maxcount=37" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( ":maxcount=24" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_maxCount     = 37u;" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_maxCount     = 24u;" ) != sw::string::npos, generated.c_str() );

#if defined( SW_DEBUG )
    const ParserRunResult badRun = runParserOnTempHeader( parserExe, "NamedCountMistakeSample",
                                                          "#pragma once\n"
                                                          "#include \"Core/Common/Types.h\"\n"
                                                          "#include \"Core/Container/vector.h\"\n"
                                                          "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                          "namespace sw\n"
                                                          "{\n"
                                                          "\tREFLECT()\n"
                                                          "\tstruct NamedCountMistakeSample\n"
                                                          "\t{\n"
                                                          "\t\tREFLECT_BODY();\n"
                                                          "\t\tstatic constexpr int32   kNegativeCount = -1;\n"
                                                          "\t\tstatic constexpr float32 kRatio         = 2.5f;\n"
                                                          "\t\tPROPERTY( MaxCount = kMissingCount )\n"
                                                          "\t\tvector<int32> _listMissing;\n"
                                                          "\t\tPROPERTY( MaxCount = kNegativeCount )\n"
                                                          "\t\tvector<int32> _listNegative;\n"
                                                          "\t\tPROPERTY( MaxCount = kRatio )\n"
                                                          "\t\tvector<int32> _listRatio;\n"
                                                          "\t};\n"
                                                          "}\n" );
    SW_EXPECT_TRUE_MSG( badRun._exitCode != 0, badRun._log.c_str() );
    const bool bMissing =
        badRun._log.find( "'sw::NamedCountMistakeSample::_listMissing': MaxCount constant 'kMissingCount' is not declared" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bMissing, badRun._log.c_str() );
    const bool bNegative =
        badRun._log.find( "'sw::NamedCountMistakeSample::_listNegative': MaxCount constant 'kNegativeCount' must be an integer constant" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bNegative, badRun._log.c_str() );
    const bool bRatio = badRun._log.find( "'sw::NamedCountMistakeSample::_listRatio': MaxCount constant 'kRatio' must be an integer constant" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bRatio, badRun._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] `Min` · `Max` 는 C++ 숫자 상수 이름을 받아 그 값을 코덱 · 정규 문자열 · 인스펙터 범위에 넣습니다
 * @details `MaxCount` 와 같은 이름 찾기(타입 → 바깥 타입 → 네임스페이스)를 씁니다. 앞에 `-` 를 붙일 수 있고 실수 상수도 받습니다.
 *          `_` 로 시작하는 이름은 상수가 아니라 필드 이름이라 그대로 둡니다. 못 찾은 상수는 생성 오류입니다 — 경계를 조용히 빼면 범위 검사가 사라집니다.
 */
SW_TEST_CASE( ReflectionParserTest, MinMaxReadANamedNumericConstant )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "NamedRangeSample",
                                                       "#pragma once\n"
                                                       "#include \"Core/Common/Types.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tstruct NamedRangeLimit\n"
                                                       "\t{\n"
                                                       "\t\tstatic constexpr int32   kMaxLevel = 40;\n"
                                                       "\t\tstatic constexpr float32 kMaxRatio = 2.5f;\n"
                                                       "\t};\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct NamedRangeSample\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tstatic constexpr int32 kMinSlot = 3;\n"
                                                       "\t\tPROPERTY( Min = kMinSlot, Max = NamedRangeLimit::kMaxLevel )\n"
                                                       "\t\tint32 _level{ 3 };\n"
                                                       "\t\tPROPERTY( Step = 0.5, Min = -NamedRangeLimit::kMaxRatio, Max = NamedRangeLimit::kMaxRatio )\n"
                                                       "\t\tfloat32 _offset{ 0.0f };\n"
                                                       "\t\tPROPERTY( Min = \"_level\", Max = NamedRangeLimit::kMaxLevel )\n"
                                                       "\t\tint32 _upper{ 3 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), run._listGeneratedCpp.size() );
    const sw::string generated = run._listGeneratedCpp[0] + run._listGeneratedHeader[0];
    SW_EXPECT_TRUE_MSG( generated.find( "F=_level:int32:min=3:max=40:" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( ":min=-2.5:max=2.5:" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_minRange     = 3.0f;" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_maxRange     = 40.0f;" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_minRange     = -2.5f;" ) != sw::string::npos, generated.c_str() );
    // 필드 이름은 상수로 풀리지 않고 필드를 읽는 함수로 남습니다.
    SW_EXPECT_TRUE_MSG( generated.find( "F=_upper:int32:min=_level:max=40:" ) != sw::string::npos, generated.c_str() );

#if defined( SW_DEBUG )
    const ParserRunResult badRun = runParserOnTempHeader( parserExe, "NamedRangeMistakeSample",
                                                          "#pragma once\n"
                                                          "#include \"Core/Common/Types.h\"\n"
                                                          "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                          "namespace sw\n"
                                                          "{\n"
                                                          "\tREFLECT()\n"
                                                          "\tstruct NamedRangeMistakeSample\n"
                                                          "\t{\n"
                                                          "\t\tREFLECT_BODY();\n"
                                                          "\t\tPROPERTY( Min = 0, Max = kMissingLimit )\n"
                                                          "\t\tint32 _level{ 0 };\n"
                                                          "\t};\n"
                                                          "}\n" );
    SW_EXPECT_TRUE_MSG( badRun._exitCode != 0, badRun._log.c_str() );
    const bool bMissing = badRun._log.find( "'sw::NamedRangeMistakeSample::_level': Max constant 'kMissingLimit' is not declared" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bMissing, badRun._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 이름 변경 리다이렉트를 잘못 적으면 생성이 멈춥니다— 대상 없는 리다이렉트를 말없이 버리지 않습니다
 * @details 열거자 리다이렉트(`ValueAlias` · `Removed`)는 `옛 이름:대상` 꼴이고, 대상은 지금 열거자, 옛 이름은 지금 없는 이름이며 한 번만 적습니다.
 *          프로퍼티의 옛 이름(`Alias`)은 같은 타입의 지금 필드 이름이나 다른 옛 이름과 겹치지 않습니다. 맞게 적은 리다이렉트는 `_mapRedirectNameToValue` 로 나갑니다.
 */
SW_TEST_CASE( ReflectionParserTest, RenameRedirectMistakesStopTheBuild )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "RedirectMistakeSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tENUM( ValueAlias = \"Walking:Missing, Broken\" )\n"
                                                       "\tenum class RedirectMistakeA : uint8 { Idle, Moving };\n"
                                                       "\tENUM( Removed = \"Moving:Idle, Gone:Idle, gone:Moving\" )\n"
                                                       "\tenum class RedirectMistakeB : uint8 { Idle, Moving };\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct RedirectMistakeActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( Alias = \"_other\" )\n"
                                                       "\t\tint32 _first{ 0 };\n"
                                                       "\t\tPROPERTY( Alias = \"legacy\" )\n"
                                                       "\t\tint32 _other{ 0 };\n"
                                                       "\t\tPROPERTY( Alias = \"Legacy\" )\n"
                                                       "\t\tint32 _third{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "'Missing' is not an enumerator" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "expects \"Old:Target\", got 'Broken:'" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "'Moving' is still an enumerator" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "old enumerator name 'gone' twice" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "is the current name of '_other'" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "Alias = Legacy ) on 'sw::RedirectMistakeActor::_third' is declared twice" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._listGeneratedCpp.empty() || run._listGeneratedCpp[0].empty(), "an errored header must not emit code" );

    const ParserRunResult goodRun = runParserOnTempHeader( parserExe, "RedirectGoodSample",
                                                           "#pragma once\n"
                                                           "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                           "namespace sw\n"
                                                           "{\n"
                                                           "\tENUM( ValueAlias = \"Standing:Idle\", Removed = \"Running:Moving\" )\n"
                                                           "\tenum class RedirectGood : uint8 { Idle, Moving };\n"
                                                           "}\n" );
    SW_ASSERT_TRUE_MSG( goodRun._exitCode == 0, goodRun._log.c_str() );
    SW_ASSERT_TRUE( goodRun._listGeneratedCpp.empty() == false );
    const sw::string& generated = goodRun._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "info._mapRedirectNameToValue =" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "{ ::sw::hashed_string( \"Standing\" ), static_cast<int64>( ::sw::RedirectGood::Idle ) }," ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "{ ::sw::hashed_string( \"Running\" ), static_cast<int64>( ::sw::RedirectGood::Moving ) }," ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "\"Standing\" )" ) == generated.rfind( "\"Standing\" )" ), "an old name is not in the current-name table" );
#endif
}

/**
 * @brief [ReflectionParserTest] 매크로가 이름을 바꾼 PROPERTY 칸은 생성이 멈춥니다
 * @details Windows CRT 의 `#define _stat _stat64i32` 처럼 칸 이름이 매크로면 리플렉션 이름이 소스와 달라져 데이터 속성 이름이 조용히 어긋난다.
 */
SW_TEST_CASE( ReflectionParserTest, PropertyRenamedByAMacroStopsTheBuild )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "MacroRenamedSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "#define _renamed _renamedByMacro\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct MacroRenamed\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tint32 _renamed{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "'_renamedByMacro' in 'sw::MacroRenamed' is renamed by a macro" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._listGeneratedCpp.empty() || run._listGeneratedCpp[0].empty(), "an errored header must not emit code" );
#endif
}

/**
 * @brief [ReflectionParserTest] 컴포넌트 핸들 PROPERTY 는 타입 핸들 한 값과 그 시퀀스만 받고 내장 `ComponentHandle` 로 기록하며, 나머지 모양은 생성이 멈춥니다
 * @details 타입 핸들의 대상 타입은 코드에 `_handleTargetTypeName` 으로 실려 묶음이 끝날 때 타입을 확인한다. 타입 없는 핸들 · 맵 · 중첩은 확인할 자리가 없다(docs/09_Decisions.md 5-6 타입 핸들 직렬화).
 */
SW_TEST_CASE( ReflectionParserTest, ComponentHandlePropertyShapes )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const auto makeHeader = []( const utf8* pFieldDeclarations )
    {
        return sw::string( "#pragma once\n"
                           "#include \"Core/Container/ComponentHandle.h\"\n"
                           "#include \"Core/Container/map.h\"\n"
                           "#include \"Core/Container/vector.h\"\n"
                           "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                           "namespace sw\n"
                           "{\n"
                           "\tstruct HandleTargetSample { void* getHandle() const { return nullptr; } };\n"
                           "\tREFLECT()\n"
                           "\tstruct HandleShapeSample\n"
                           "\t{\n"
                           "\t\tREFLECT_BODY();\n" ) +
               pFieldDeclarations + "\t};\n}\n";
    };

    // 허용: 타입 핸들 한 값과 그 벡터. 내장 이름으로 기록하고 대상 타입을 싣는다.
    const ParserRunResult allowed = runParserOnTempHeader( parserExe, "HandleShapeAllowed",
                                                           makeHeader( "\t\tPROPERTY()\n\t\tTComponentHandle<HandleTargetSample> _single;\n"
                                                                       "\t\tPROPERTY()\n\t\tvector<TComponentHandle<HandleTargetSample>> _list;\n" ) );
    SW_ASSERT_TRUE_MSG( allowed._exitCode == 0, allowed._log.c_str() );
    SW_ASSERT_TRUE( allowed._listGeneratedCpp.empty() == false );
    const sw::string& generated = allowed._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "p._handleTargetTypeName = ::sw::hashed_string( \"HandleTargetSample\" );" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "\"TComponentHandle" ) == sw::string::npos, "the reflection type names must be the builtin ComponentHandle" );
    SW_EXPECT_TRUE_MSG( generated.find( "\"ComponentHandle\"" ) != sw::string::npos, generated.c_str() );

    // 거절: 타입 없는 핸들, 맵, 중첩 컨테이너.
    const utf8* const arrRejected[] = {
        "\t\tPROPERTY()\n\t\tComponentHandle _untyped;\n",
        "\t\tPROPERTY()\n\t\tmap<int32, TComponentHandle<HandleTargetSample>> _map;\n",
        "\t\tPROPERTY()\n\t\tvector<vector<TComponentHandle<HandleTargetSample>>> _nested;\n",
    };
    for ( const utf8* pDeclaration : arrRejected )
    {
        const ParserRunResult rejected = runParserOnTempHeader( parserExe, "HandleShapeRejected", makeHeader( pDeclaration ) );
        SW_EXPECT_TRUE_MSG( rejected._exitCode != 0, pDeclaration );
        SW_EXPECT_TRUE_MSG( rejected._log.find( "holds a component handle" ) != sw::string::npos, rejected._log.c_str() );
    }
#endif
}

/**
 * @brief [ReflectionParserTest] `RuntimeState` 를 `Transient` 나 `Required` 와 함께 붙이면 생성이 멈추고, 혼자 붙이면 메타데이터 비트로 나갑니다
 * @details `RuntimeState` 는 저작 저장만 빼는 필드다. 어디에도 쓰지 않는 `Transient`, 데이터 파일이 반드시 적는 `Required` 와는 뜻이 모순이다.
 */
SW_TEST_CASE( ReflectionParserTest, RuntimeStateContradictionsStopTheBuild )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "RuntimeStateMistakeSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct RuntimeStateMistake\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( RuntimeState, Transient )\n"
                                                       "\t\tint32 _hidden{ 0 };\n"
                                                       "\t\tPROPERTY( RuntimeState, Required )\n"
                                                       "\t\tint32 _needed{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "RuntimeState, Transient ) on 'sw::RuntimeStateMistake::_hidden'" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "RuntimeState, Required ) on 'sw::RuntimeStateMistake::_needed'" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._listGeneratedCpp.empty() || run._listGeneratedCpp[0].empty(), "an errored header must not emit code" );

    const ParserRunResult goodRun = runParserOnTempHeader( parserExe, "RuntimeStateGoodSample",
                                                           "#pragma once\n"
                                                           "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                           "namespace sw\n"
                                                           "{\n"
                                                           "\tREFLECT()\n"
                                                           "\tstruct RuntimeStateGood\n"
                                                           "\t{\n"
                                                           "\t\tREFLECT_BODY();\n"
                                                           "\t\tPROPERTY( RuntimeState, SaveGame )\n"
                                                           "\t\tint32 _state{ 0 };\n"
                                                           "\t};\n"
                                                           "}\n" );
    SW_ASSERT_TRUE_MSG( goodRun._exitCode == 0, goodRun._log.c_str() );
    SW_ASSERT_TRUE( goodRun._listGeneratedCpp.empty() == false );
    const sw::string& generated = goodRun._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_bRuntimeState" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_bSaveGame" ) != sw::string::npos, generated.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 생성자 호출기는 받은 저장소에 `sw_placement_new` 로 만든다 — 맨 `new ( p )` 를 내지 않는다
 * @details 손으로 쓴 코드는 `Style/PlacementNew` 가 막지만 생성 코드는 그 린트 밖이다. 생성기가 같은 모양을 낸다.
 */
SW_TEST_CASE( ReflectionParserTest, ConstructorInvokerUsesPlacementNewMacro )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "CtorInvokerSample",
                                                       "#pragma once\n"
                                                       "#include \"Core/Common/Types.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct CtorInvokerSampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tCtorInvokerSampleActor() = default;\n"
                                                       "\t\texplicit CtorInvokerSampleActor( int32 value ) : _value{ value } {}\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tint32 _value{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_ASSERT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), run._listGeneratedCpp.size() );
    const sw::string& generated = run._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "sw_placement_new( self ) sw::CtorInvokerSampleActor(" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "new ( self )" ) == sw::string::npos, generated.c_str() );
}

/**
 * @brief [ReflectionParserTest] 파서는 헤더를 이 빌드와 같은 타깃 매크로(플랫폼 · 아키텍처 · `SW_COMPILER_CLANG`)로 읽는다
 * @details libclang 은 CMake 를 거치지 않으므로 `SW_PLATFORM_*` · `SW_X64` / `SW_ARM64` · `SW_COMPILER_*` 를 파서가 직접 넘긴다
 *          (`ParserConfig::load`). 빠지면 헤더가 그 매크로로 고른 분기가 파서와 컴파일러에서 달라진다. 이 테스트 바이너리가 받은
 *          매크로를 헤더 안에서 `#error` 로 대조한다.
 */
SW_TEST_CASE( ReflectionParserTest, ParserDefinesTargetMacros )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

#if defined( SW_PLATFORM_WINDOWS )
    const sw::string platformMacro = "SW_PLATFORM_WINDOWS";
#else
    const sw::string platformMacro = "SW_PLATFORM_LINUX";
#endif
#if defined( SW_X64 )
    const sw::string architectureMacro = "SW_X64";
#else
    const sw::string architectureMacro = "SW_ARM64";
#endif

    const sw::string header = "#pragma once\n"
                              "#include \"Core/Common/Types.h\"\n"
                              "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                              "#if !defined( " +
                              platformMacro + " ) || !defined( " + architectureMacro +
                              " ) || !defined( SW_COMPILER_CLANG )\n"
                              "#error \"ReflectionParser did not define the target macros\"\n"
                              "#endif\n"
                              "namespace sw\n"
                              "{\n"
                              "\tREFLECT()\n"
                              "\tstruct TargetMacroSampleActor\n"
                              "\t{\n"
                              "\t\tREFLECT_BODY();\n"
                              "\t\tPROPERTY()\n"
                              "\t\tint32 _value{ 0 };\n"
                              "\t};\n"
                              "}\n";
    const ParserRunResult run = runParserOnTempHeader( parserExe, "TargetMacroSample", header );
    SW_ASSERT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), run._listGeneratedCpp.size() );
    SW_EXPECT_TRUE_MSG( run._listGeneratedCpp[0].find( "TargetMacroSampleActor" ) != sw::string::npos, run._listGeneratedCpp[0].c_str() );
}

/**
 * @brief [ReflectionParserTest] 값 참조를 돌려주는 메서드의 PROPERTY 는 오프셋 대신 값 접근자를 낸다
 * @details 씬 컴포넌트의 로컬 TRS 처럼 값이 객체 밖(트랜스폼 저장소)에 있고 저장 이름은 `Name` 으로 정하는 모양이다.
 *          모양이 틀리면(값으로 돌려준다 — 쓸 자리가 없다) 조용히 넘기지 않고 멈춘다.
 */
SW_TEST_CASE( ReflectionParserTest, AccessorPropertyEmitsValueAccessor )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

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
 * @details 팩토리는 `addComponent<T>()` 로 T 를 만든다. 컴포넌트에서 파생했다고 무조건 내면 공통 기반 컴포넌트
 *          (`REFLECT( Abstract )` · 보호된 생성자, 예: `LightComponent`)를 둘 때 생성된 코드가 컴파일되지 않는다.
 */
SW_TEST_CASE( ReflectionParserTest, AbstractComponentGetsNoFactory )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

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
    SW_EXPECT_TRUE_MSG( generated.find( "addComponentTo<sw::AbstractFactorySampleConcrete>" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "addComponentTo<sw::AbstractFactorySampleBase>" ) == sw::string::npos, generated.c_str() );
}

/**
 * @brief [ReflectionParserTest] REFLECT() 없는 타입의 REFLECT_BODY() 는 빌드를 세운다
 * @details `REFLECT_BODY()` 가 만드는 마커 함수는 매크로 전개 위치에 있다. "주 파일에 있나" 를 `clang_Location_isFromMainFile`
 *          로 물으면 그 함수는 매크로 위치를 늘 "아니다" 로 답해 마커가 검사에 닿지 않는다. 그래서 선언을 매크로를 **쓴** 자리의
 *          파일로 센다.
 */
SW_TEST_CASE( ReflectionParserTest, ReflectBodyWithoutReflectStopsTheBuild )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

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
#endif
}

/**
 * @brief [ReflectionParserTest] 서버 버스 본문 타입(`kBusVersion`)에 모듈 내보내기 매크로가 없으면 빌드를 세운다
 * @details 다른 모듈의 `ServerBusTopic<T>` 가 `T::StaticType()` 을 부르므로, 내보내기가 없으면 서버 키트의 링크에서야 드러났다(C-61).
 *          모듈 매크로(`SW_KIT_API`)를 단 타입과, 타입이 모듈 경계를 넘지 않는 대상(`--module-local`: 실행 파일 · 게임 모듈)은 받는다.
 */
SW_TEST_CASE( ReflectionParserTest, BusBodyWithoutExportStopsTheBuild )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "BusNoExportSample", makeBusBodyHeader( "BusNoExportSampleNotice", "" ) );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    const bool bNamesTheType = run._log.find( "'sw::BusNoExportSampleNotice' declares 'kBusVersion'" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bNamesTheType, run._log.c_str() );

    const ParserRunResult exportedRun = runParserOnTempHeader( parserExe, "BusExportSample", makeBusBodyHeader( "BusExportSampleNotice", "SW_KIT_API" ) );
    SW_EXPECT_TRUE_MSG( exportedRun._exitCode == 0, exportedRun._log.c_str() );

    sw::vector<TempHeader> listModuleLocalHeader;
    listModuleLocalHeader.push_back( TempHeader{ "BusModuleLocalSample", makeBusBodyHeader( "BusModuleLocalSampleNotice", "" ) } );
    const ParserRunResult moduleLocalRun = runParserOnTempHeaders( parserExe, listModuleLocalHeader, "--module-local" );
    SW_EXPECT_TRUE_MSG( moduleLocalRun._exitCode == 0, moduleLocalRun._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] PROPERTY() 의 컴포넌트 핸들(타입 없는 것과 타입 핸들, 단일 값 · 원소 · 맵 키 · 중첩)은 빌드를 세운다
 * @details 저장된 컴포넌트 id 를 다른 실행의 컴포넌트로 옮길 형태가 없다. 로드가 경고와 함께 비우던 폴백을 생성 오류로 바꿨다(docs/09_Decisions.md 5-6 타입 핸들 직렬화).
 *          PROPERTY 가 아닌 핸들 필드는 그대로 받는다(런타임 참조로 쓰는 정상 자리).
 */
SW_TEST_CASE( ReflectionParserTest, ComponentHandlePropertyStopsTheBuild )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "ComponentHandlePropertySample",
                                                       "#pragma once\n"
                                                       "#include \"Core/Container/ComponentHandle.h\"\n"
                                                       "#include \"Core/Container/map.h\"\n"
                                                       "#include \"Core/Container/vector.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct ComponentHandlePropertySampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tComponentHandle _single;\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tvector<ComponentHandle> _listElement;\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tmap<ComponentHandle, int32> _mapKey;\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tvector<vector<ComponentHandle>> _listNested;\n"
                                                       "\t\tComponentHandle _runtimeOnly;\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    const utf8* arrField[] = { "_single'", "_listElement'", "_mapKey'", "_listNested'" };
    for ( const utf8* pField : arrField )
    {
        const sw::string expected = sw::string( "ComponentHandlePropertySampleActor::" ) + pField + " holds a component handle";
        SW_EXPECT_TRUE_MSG( run._log.find( expected ) != sw::string::npos, run._log.c_str() );
    }
    SW_EXPECT_TRUE_MSG( run._log.find( "_runtimeOnly" ) == sw::string::npos, run._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 헤더 여럿을 한 번역 단위로 묶어도, 깨진 헤더 하나가 나머지를 막지 않는다
 * @details 파서는 파싱할 헤더가 둘 이상이면 한 TU 로 묶는다 — 공통 include(CoreMinimal · Windows · D3D)가 비용의 거의 전부라,
 *          헤더마다 TU 를 따로 만들면 같은 것을 헤더 수만큼 다시 파싱한다.
 *          묶으면 새 실패 경로가 둘 생긴다. (1) 한 헤더의 애노테이션 오류 — 순회가 오류에서 멈추면 다른 헤더의
 *          수집까지 끊긴다 → 오류는 헤더 단위로 남기고 계속 돈다. (2) 한 헤더의 C++ 오류 — 묶음 전체가 파싱되지 않는다 →
 *          헤더마다 다시 파싱해 그 헤더의 오류로 알린다. 두 경우 모두 성한 헤더의 산출물은 만들어져야 한다.
 */
SW_TEST_CASE( ReflectionParserTest, OneBrokenHeaderDoesNotBlockTheOthers )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

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
#endif
}

/**
 * @brief [ReflectionParserTest] 파서 자신이 새로워지면 산출물을 다시 만든다
 * @details **도구도 입력이다.** 입력 헤더·템플릿(.tpl)·builtins 의 시간만 보고 그것을 조립하는 실행 파일을
 *          보지 않으면 `CodeGenerator` 나 `AstVisitor` 를 고쳐 다시 빌드해도 산출물이 낡은 모양 그대로 남는다
 *          (CMake 는 exe 를 DEPENDS 에 걸어 파서를 다시 부르지만, 파서가 스스로 "최신" 이라며 건너뛴다). 그 상태에서
 *          일부 파일만 다른 이유로 다시 만들어지면 **두 모양이 섞인다.**
 *
 *          검사는 그 상황을 그대로 만든다: 한 번 생성한 뒤 산출물에 표식을 심고 그 파일을 파서보다
 *          **과거로** 돌린다. 다시 돌렸을 때 표식이 사라져 있으면 다시 만든 것이다.
 */
SW_TEST_CASE( ReflectionParserTest, RegeneratesWhenTheParserItselfIsNewer )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );

    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string projectRoot = sw::ResourcePaths::getProjectFolderPath();
    const sw::string caseRoot    = test::makeTempPath( "staleness" );
    const sw::string headerPath  = sw::FileUtil::joinPath( caseRoot, "StalenessProbeSample.h" );
    const sw::string outGenDir   = sw::FileUtil::joinPath( caseRoot, "gen" );
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
    SW_ASSERT_TRUE( sw::FileUtil::exists( genPath ) );

    // 표식을 심고, 산출물을 파서보다 한 시간 과거로 돌린다.
    sw::string generatedText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genPath, generatedText ) );
    generatedText += "\n// SW_STALE_PROBE\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( genPath, generatedText ) );

    // 시간을 셋으로 벌린다: 입력(2시간 전) < 산출물(1시간 전) < 파서(지금).
    // **입력을 같이 과거로 보내는 것이 핵심이다** — 안 그러면 "입력이 더 새롭다" 는 이유로 다시
    // 만들어져, 이 검사가 파서 시간을 보는지 아닌지를 구분하지 못한다.
    int64 parserTime{ 0 };
    SW_ASSERT_TRUE( sw::FileUtil::getFileWriteTime( parserExe, parserTime ) );
    SW_ASSERT_TRUE( sw::FileUtil::setFileWriteTime( headerPath, parserTime - 2 * kHourTicks ) );
    SW_ASSERT_TRUE( sw::FileUtil::setFileWriteTime( genPath, parserTime - kHourTicks ) );
    const sw::string genHeaderPath = sw::FileUtil::joinPath( outGenDir, "StalenessProbeSample.gen.h" );
    if ( sw::FileUtil::exists( genHeaderPath ) )
        SW_ASSERT_TRUE( sw::FileUtil::setFileWriteTime( genHeaderPath, parserTime - kHourTicks ) );
    // 최신 판정의 기준은 산출물이 아니라 스탬프(마지막으로 성공한 생성)다 — 같이 과거로 보낸다.
    const sw::string stampPath = genPath + ".stamp";
    SW_ASSERT_TRUE( sw::FileUtil::exists( stampPath ) );
    SW_ASSERT_TRUE( sw::FileUtil::setFileWriteTime( stampPath, parserTime - kHourTicks ) );

    SW_ASSERT_EQUAL( 0, sw::Process::execute( command, options, {} ) );

    sw::string regeneratedText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genPath, regeneratedText ) );
    SW_EXPECT_TRUE_MSG( regeneratedText.find( "SW_STALE_PROBE" ) == sw::string::npos,
                        "파서가 자기보다 오래된 산출물을 그대로 두었습니다 — 도구를 고쳐도 옛 모양이 남습니다" );
}

/**
 * @brief [ReflectionParserTest] 같은 이름의 헤더 둘이 같은 산출물을 노리면 **조용히 덮지 않는다**
 * @details 생성 파일 이름은 소스의 **파일 이름만** 으로 짓는다(`makeGeneratedPath`). 그래서 한 모듈
 *          안에 같은 이름의 헤더가 둘 있으면 나중에 도는 쪽이 앞의 것을 덮고, **앞 헤더의 타입들은
 *          아무 말 없이 등록되지 않는다** — 증상은 한참 뒤 "씬이 그 컴포넌트를 못 찾는다" 로 나타나
 *          원인이 코드젠이라는 것을 짚기 어렵다. 그래서 그 자리에서 빌드를 세우고 두 경로를 다 적는다.
 *
 *          헤더를 **옮긴** 경우(옛 경로가 더는 없다)는 정상이므로 조용히 덮어써야 한다 — 이 케이스는
 *          그것도 함께 본다. 그러지 않으면 파일을 옮길 때마다 빌드가 막힌다.
 */
SW_TEST_CASE( ReflectionParserTest, SameFileNameInOneOutputDirIsRejected )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );

    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string projectRoot = sw::ResourcePaths::getProjectFolderPath();

    const sw::string caseRoot  = test::makeTempPath( "collide" );
    const sw::string dirA      = sw::FileUtil::joinPath( caseRoot, "A" );
    const sw::string dirB      = sw::FileUtil::joinPath( caseRoot, "B" );
    const sw::string outGenDir = sw::FileUtil::joinPath( caseRoot, "gen" );
    sw::FileUtil::ensureDirectoryExists( dirA );
    sw::FileUtil::ensureDirectoryExists( dirB );
    sw::FileUtil::ensureDirectoryExists( outGenDir );

    const sw::string headerA = sw::FileUtil::joinPath( dirA, "CollidingSample.h" );
    const sw::string headerB = sw::FileUtil::joinPath( dirB, "CollidingSample.h" );

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

    // 산출물 폴더는 이 케이스의 임시 경로라 처음엔 비어 있다 — 파서는 "이미 최신" 이면 통째로 건너뛰므로,
    // `Bin` 에 두면 앞선 실행이 남긴 파일 때문에 첫 단계가 아무것도 만들지 않고 지나갈 수 있다.
    const sw::string genCppPath = sw::FileUtil::joinPath( outGenDir, "CollidingSample.gen.cpp" );

    // 첫 헤더는 정상적으로 산출물을 만든다.
    const int32 firstExit = runParser( headerA );
    SW_EXPECT_TRUE_MSG( firstExit == 0, outLog.c_str() );
    SW_ASSERT_EQUAL( 0, firstExit );
    SW_ASSERT_TRUE( sw::FileUtil::exists( genCppPath ) );

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

#if defined( SW_PLATFORM_WINDOWS )
    // **폴더 이름의 대소문자만 바꾼 것도 같은 파일이다**(`Gui` → `GUI`). Windows 에서는 옛 철자 경로로도 exists 가 참이라
    // 경로 글자만 비교하면 "다른 헤더" 로 읽혀 빌드가 선다.
    // 내용을 바꿔 "이미 최신" 으로 건너뛰지 않게 한다.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerB, makeHeader( "CollidingSampleRenamedCase" ) ) );
    const sw::string headerBOtherCase = sw::FileUtil::joinPath( sw::FileUtil::joinPath( caseRoot, "b" ), "CollidingSample.h" );
    SW_EXPECT_TRUE_MSG( runParser( headerBOtherCase ) == 0, outLog.c_str() );
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genCppPath, generated ) );
    SW_EXPECT_TRUE( generated.find( "CollidingSampleRenamedCase" ) != sw::string::npos );
#endif
}

/**
 * @brief [ReflectionParserTest] 컨테이너는 바깥 템플릿 이름으로만 알아본다 — 이름에 set · map · list 가 든 타입은 컨테이너가 아니다
 * @details 표기 어디에든 규칙 이름이 있으면 컨테이너로 보면 `TextureAsset` · `Offset2D` 는 set, `Bitmap` 은 map, `Playlist` 는 list 로
 *          나가 생성 파일 안에서 빌드가 깨진다.
 */
SW_TEST_CASE( ReflectionParserTest, ContainerIsRecognizedByItsOuterTemplateNameOnly )
{
    SW_EXPECT_STREQ( "vector", sw::string( sw::ParserUtil::outerTemplateName( "sw::vector<int32>" ) ).c_str() );
    SW_EXPECT_STREQ( "unordered_map", sw::string( sw::ParserUtil::outerTemplateName( "const sw::unordered_map<sw::string, sw::vector<int32>>" ) ).c_str() );
    SW_EXPECT_STREQ( "set", sw::string( sw::ParserUtil::outerTemplateName( "std::set<int>" ) ).c_str() );
    SW_EXPECT_STREQ( "TArray", sw::string( sw::ParserUtil::outerTemplateName( "TArray<float>" ) ).c_str() );

    // 템플릿이 아니면 컨테이너가 아니다 — 이름에 규칙 철자가 들어 있어도.
    for ( const utf8* pPlain : { "sw::TextureAsset", "Offset2D", "game::Bitmap", "Playlist", "sw::settings::Window" } )
    {
        SW_EXPECT_TRUE_MSG( sw::ParserUtil::outerTemplateName( pPlain ).empty(), pPlain );
    }
    // 템플릿이어도 이름이 같아야 한다.
    SW_EXPECT_STREQ( "TextureAssetRef", sw::string( sw::ParserUtil::outerTemplateName( "sw::TextureAssetRef<sw::Texture>" ) ).c_str() );
}

/**
 * @brief [ReflectionParserTest] `--dump` 는 헤더마다 뽑은 것(타입 · 부모 · 프로퍼티의 값 자리 · 범위 · 플래그 · enum 값)을 찍고, `--help` 는 성공이다
 * @details "왜 이 프로퍼티가 인스펙터에 없나" 를 묻는 창구다 — 파서가 무엇을 봤는지 헤더마다 찍는다.
 */
SW_TEST_CASE( ReflectionParserTest, DumpShowsWhatWasExtracted )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    sw::vector<TempHeader> listHeader;
    listHeader.push_back( TempHeader{ "DumpSample",
                                      "#pragma once\n"
                                      "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                      "namespace sw\n"
                                      "{\n"
                                      "\tENUM()\n"
                                      "\tenum class DumpSampleMode : uint8\n"
                                      "\t{\n"
                                      "\t\tIdle = 0,\n"
                                      "\t\tBusy = 200\n"
                                      "\t};\n"
                                      "\tREFLECT()\n"
                                      "\tstruct DumpSampleActor\n"
                                      "\t{\n"
                                      "\t\tREFLECT_BODY();\n"
                                      "\t\tPROPERTY( Min = 0.0, Transient )\n"
                                      "\t\tfloat32 _speed{ 0.0f };\n"
                                      "\t\tPROPERTY()\n"
                                      "\t\tuint8 _bFlag : 1;\n"
                                      "\t\tPROPERTY()\n"
                                      "\t\tDumpSampleMode _mode{ DumpSampleMode::Idle };\n"
                                      "\t};\n"
                                      "}\n" } );
    const ParserRunResult run = runParserOnTempHeaders( parserExe, listHeader, "--dump" );
    SW_EXPECT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );

    for ( const utf8* pExpected : { "REFLECT sw::DumpSampleActor", "PROPERTY _speed : float32", "Min=0", "[Transient]",
                                    "PROPERTY _bFlag : uint8  [bit field - located at runtime]", "ENUM sw::DumpSampleMode", "Busy = 200" } )
    {
        SW_EXPECT_TRUE_MSG( run._log.find( pExpected ) != sw::string::npos, ( sw::string( "missing: " ) + pExpected + "\n" + run._log ).c_str() );
    }
    // 한쪽 범위는 한쪽만 찍는다.
    SW_EXPECT_TRUE_MSG( run._log.find( "Max=" ) == sw::string::npos, run._log.c_str() );

    // `--help` 는 오류가 아니다.
    sw::ProcessOptions options;
    options._workingDirectory = sw::ResourcePaths::getProjectFolderPath();
    sw::string  helpLog;
    const int32 helpExit = sw::Process::execute( "\"" + parserExe + "\" --help", options,
                                                 SW_DELEGATE_LAMBDA( sw::ProcessOutputDelegate, [&helpLog]( sw::string_view line )
    {
        helpLog.append( line.data(), line.size() );
        helpLog.push_back( '\n' );
    } ) );
    SW_EXPECT_TRUE_MSG( helpExit == 0, helpLog.c_str() );
    SW_EXPECT_TRUE_MSG( helpLog.find( "--dump" ) != sw::string::npos, helpLog.c_str() );
}

/**
 * @brief [ReflectionParserTest] 애노테이션 문자열 안의 이스케이프(`\"` · `\\`)와 쉼표를 그대로 읽는다
 * @details 첫 안쪽 따옴표에서 값을 끝내면 `Tooltip = "Say \"hi\""` 가 `Say \` 로 조용히 잘리고, 이스케이프한 따옴표 뒤의 쉼표에서 토큰이 갈라지면
 *          뒷조각이 "모르는 토큰" 으로 빌드를 세운다.
 */
SW_TEST_CASE( ReflectionParserTest, AnnotationStringKeepsEscapesAndCommas )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "EscapedAnnotationSample",
                                                       "#pragma once\n"
                                                       "#include \"Core/Common/Types.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct EscapedAnnotationSampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( Tooltip = \"Say \\\"hi, then go \\\\ home\", Category = \"Sample\" )\n"
                                                       "\t\tint32 _value{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), run._listGeneratedCpp.size() );
    const sw::string& generated = run._listGeneratedCpp[0];
    // 값은 `Say "hi, then go \ home` 이다 — 짝 없는 `\"` 뒤의 쉼표가 토큰을 가르지 않는다. 생성 코드에는 다시 C++ 이스케이프된 꼴로 들어간다.
    SW_EXPECT_TRUE_MSG( generated.find( "\"Say \\\"hi, then go \\\\ home\"" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "\"Sample\"" ) != sw::string::npos, generated.c_str() );
}

/**
 * @brief [ReflectionParserTest] 애노테이션 문자열 안의 `;` `{` `}` `)` 는 선언 경계가 아니다 — 그 타입의 REFLECT() 를 잃지 않는다
 * @details `class` 앞의 속성은 clang 이 버려 파서가 소스를 되읽는다. 매크로와 선언 사이의 `;` `{` `}` 를 "앞 선언의 매크로" 표시로 보는데
 *          따옴표 안까지 세면 `Tooltip = "…; …"` 하나로 그 타입의 REFLECT() 가 사라져, 멤버마다 "REFLECT() 가 없다" 는 엉뚱한 오류로
 *          빌드가 선다. 인자를 읽는 괄호 세기도 따옴표 안의 `\"` 뒤에서 어긋나면 안 된다.
 */
SW_TEST_CASE( ReflectionParserTest, PunctuationInsideAnnotationStringIsNotADeclarationBoundary )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "PunctuatedAnnotationSample",
                                                       "#pragma once\n"
                                                       "#include \"Core/Common/Types.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tREFLECT( Tooltip = \"Kept; {braced} \\\"(\\\" stays\" )\n"
                                                       "\tstruct PunctuatedAnnotationSampleActor\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY( Tooltip = \"Also; here\" )\n"
                                                       "\t\tint32 _value{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), run._listGeneratedCpp.size() );
    const sw::string& generated = run._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "Kept; {braced} \\\"(\\\" stays" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "\"Also; here\"" ) != sw::string::npos, generated.c_str() );
}

/**
 * @brief [ReflectionParserTest] `sw` 하위 네임스페이스의 구조체를 담은 PROPERTY 의 타입 이름은 잎 이름이다 — 런타임이 찾는 이름(짧은 이름 · FQN)과 맞는다
 * @details libclang 16 미만(리눅스 CI)은 한정 없이 적은 타입도 FQN(`sw::sub::Inner`)으로 내보낸다. `sw::` 만 떼면 `sub::Inner` 가 남아
 *          런타임이 그 타입을 못 찾고, 설정 읽기와 직렬화가 그 칸을 버린다. 여기서는 FQN 으로 적어 Windows 에서도 같은 철자를 만든다.
 */
SW_TEST_CASE( ReflectionParserTest, NestedNamespaceStructPropertyUsesTheLeafName )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "NestedNamespaceSample",
                                                       "#pragma once\n"
                                                       "#include \"Core/Container/vector.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw::nestedsample\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct NestedSampleInner\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tint32 _value{ 0 };\n"
                                                       "\t};\n"
                                                       "}\n"
                                                       "namespace sw::nestedsample\n"
                                                       "{\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct NestedSampleOuter\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tsw::nestedsample::NestedSampleInner _inner{};\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tsw::vector<sw::nestedsample::NestedSampleInner> _listInner{};\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), run._listGeneratedCpp.size() );
    const sw::string& generated = run._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "::sw::hashed_string( \"NestedSampleInner\" )," ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "\"nestedsample::NestedSampleInner\"" ) == sw::string::npos, generated.c_str() );
}

/**
 * @brief [ReflectionParserTest] 별칭(`using`)으로 적은 기반도 실제 클래스로 읽는다 — 부모 FQN 과 컴포넌트 팩토리를 잃지 않는다
 * @details 베이스 지정자의 선언을 그대로 물으면 별칭 선언이 나온다. 그러면 부모 FQN 이 별칭 이름이 되어 실행 중에 부모를 못 찾고, 컴포넌트 판별이 별칭에서
 *          멈춰 팩토리가 생기지 않는다(씬에서 그 컴포넌트를 만들 수 없다).
 */
SW_TEST_CASE( ReflectionParserTest, AliasedBaseClassKeepsParentAndFactory )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "AliasedBaseSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Object/Component/Component.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tusing AliasedComponentBase = Component;\n"
                                                       "\tREFLECT()\n"
                                                       "\tclass AliasedBaseSampleComponent : public AliasedComponentBase\n"
                                                       "\t{\n"
                                                       "\tpublic:\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tint32 _value{ 0 };\n"
                                                       "\t};\n"
                                                       "\ttypedef AliasedBaseSampleComponent AliasedBaseSampleParent;\n"
                                                       "\tREFLECT()\n"
                                                       "\tclass AliasedBaseSampleChild : public AliasedBaseSampleParent\n"
                                                       "\t{\n"
                                                       "\tpublic:\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( size_t( 1 ), run._listGeneratedCpp.size() );
    const sw::string& generated = run._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "addComponentTo<sw::AliasedBaseSampleComponent>" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "addComponentTo<sw::AliasedBaseSampleChild>" ) != sw::string::npos, generated.c_str() );
    // 부모는 별칭이 아니라 실제 클래스 이름이다.
    SW_EXPECT_TRUE_MSG( generated.find( "_parentFQN          = ::sw::hashed_string( \"sw::AliasedBaseSampleComponent\" )" ) != sw::string::npos,
                        generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "_parentFQN          = ::sw::hashed_string( \"sw::Component\" )" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "AliasedComponentBase" ) == sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "AliasedBaseSampleParent" ) == sw::string::npos, generated.c_str() );
}

/**
 * @brief [ReflectionParserTest] REFLECT 타입의 둘째 베이스와 가상 베이스는 빌드를 세운다
 * @details `castTo` 는 리플렉션 부모 사슬로 파생을 판단하고 C++ 상속이 보이지 않으면 `reinterpret_cast` 로 내려갑니다. 부모가 offset 0 에 있다는
 *          전제가 깨지는 베이스(둘째 이후, 가상)를 거치면 포인터 보정 없이 잘못된 주소가 됩니다. 둘째 베이스가 `REFLECT()` 없는 인터페이스여도 막습니다.
 *          메시지에는 헤더 · 줄 · 타입 · 베이스 이름이 있어야 합니다.
 */
SW_TEST_CASE( ReflectionParserTest, MultipleOrVirtualBaseStopsTheBuild )
{
    SW_TEST_REQUIRES_FEATURE( DebugLog );
#if defined( SW_DEBUG )
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "MultiBaseSample",
                                                       "#pragma once\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tstruct MultiBaseSampleFirst\n"
                                                       "\t{\n"
                                                       "\t\tint32 _first{ 0 };\n"
                                                       "\t};\n"
                                                       "\tstruct MultiBaseSampleListener\n"
                                                       "\t{\n"
                                                       "\t\tvirtual ~MultiBaseSampleListener() = default;\n"
                                                       "\t};\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct MultiBaseSampleActor : public MultiBaseSampleFirst, public MultiBaseSampleListener\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t\tPROPERTY()\n"
                                                       "\t\tint32 _value{ 0 };\n"
                                                       "\t};\n"
                                                       "\tREFLECT()\n"
                                                       "\tstruct VirtualBaseSampleActor : virtual public MultiBaseSampleFirst\n"
                                                       "\t{\n"
                                                       "\t\tREFLECT_BODY();\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    const bool bNamesTheSecondBase = run._log.find( "MultiBaseSample.h(14): REFLECT type 'sw::MultiBaseSampleActor' has a second base "
                                                    "'sw::MultiBaseSampleListener'" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bNamesTheSecondBase, run._log.c_str() );
    const bool bNamesTheVirtualBase = run._log.find( "MultiBaseSample.h(21): REFLECT type 'sw::VirtualBaseSampleActor' has a virtual base "
                                                     "'sw::MultiBaseSampleFirst'" ) != sw::string::npos;
    SW_EXPECT_TRUE_MSG( bNamesTheVirtualBase, run._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 파서 실행 파일이 있다 — 없으면 이 스위트의 다른 케이스는 모두 건너뛰므로, 그 전제를 여기서 단언한다
 * @details 파서를 못 찾은 케이스는 `SW_TEST_SKIP` 한다. "스위트 전체가 건너뜀" 은 실행 끝의 검사가 잡았지만, ReflectionTest 를 샤드로 나눈 뒤로는
 *          갈린 스위트를 한 샤드가 판단할 수 없다 — 그래서 전제를 케이스로 둔다.
 */
SW_TEST_CASE( ReflectionParserTest, ParserExecutableIsBuilt )
{
    SW_EXPECT_TRUE_MSG( findReflectionParserExecutable().empty() == false,
                        "ReflectionParser executable not found next to the tests (Bin/ · BuildTools/) - the other parser cases skip" );
}

/**
 * @brief [ReflectionParserTest] 파싱하는 동안 저장한 편집도 다음 실행이 다시 파싱한다 — 스탬프보다 오래된 시각이어도
 * @details 스탬프에 읽기 전에 본 시각을 적고 같음으로 본다. 스탬프 파일의 시각(= 다 쓴 때)이 입력보다 새로운지만 보면, 파서가 헤더를 읽은 뒤 · 스탬프를
 *          쓰기 전에 저장한 편집이 스탬프보다 오래된 시각을 가져 다음 실행에서도 "최신" 이 된다 — 그 헤더를 다시 저장할 때까지 생성 코드에 들어가지 않는다.
 */
SW_TEST_CASE( ReflectionParserTest, EditSavedWhileParsingIsNotHiddenByTheStamp )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const sw::string projectRoot = sw::ResourcePaths::getProjectFolderPath();
    const sw::string caseRoot    = test::makeTempPath( "midparse_edit" );
    const sw::string headerPath  = sw::FileUtil::joinPath( caseRoot, "MidParseEditSample.h" );
    const sw::string outGenDir   = sw::FileUtil::joinPath( caseRoot, "gen" );
    sw::FileUtil::ensureDirectoryExists( outGenDir );

    const auto makeHeader = []( const utf8* pExtraProperty )
    {
        return sw::string( "#pragma once\n"
                           "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                           "namespace sw\n"
                           "{\n"
                           "    REFLECT()\n"
                           "    struct MidParseEditSampleActor\n"
                           "    {\n"
                           "        REFLECT_BODY();\n"
                           "        PROPERTY()\n"
                           "        int32 _value;\n" ) +
               pExtraProperty + "    };\n}\n";
    };
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerPath, makeHeader( "" ) ) );
    // 처음 읽을 때의 헤더 시각을 두 시간 전으로 — 아래의 "편집" 시각을 그 뒤 · 스탬프 앞에 둘 자리를 만든다.
    SW_ASSERT_TRUE( sw::FileUtil::setFileWriteTime( headerPath, sw::FileUtil::getCurrentFileWriteTime() - 2 * kHourTicks ) );

    const sw::string   command = makeParserCommand( parserExe, headerPath, outGenDir, projectRoot );
    sw::ProcessOptions options;
    options._workingDirectory = projectRoot;
    SW_ASSERT_EQUAL( 0, sw::Process::execute( command, options, {} ) );

    const sw::string genPath   = sw::FileUtil::joinPath( outGenDir, "MidParseEditSample.gen.cpp" );
    const sw::string stampPath = genPath + ".stamp";
    SW_ASSERT_TRUE( sw::FileUtil::exists( stampPath ) );

    // 파싱 도중에 저장한 편집: 내용이 바뀌고 시각은 처음 읽은 때보다 뒤, 스탬프보다는 앞.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerPath, makeHeader( "        PROPERTY()\n        int32 _addedWhileParsing;\n" ) ) );
    int64 stampTime{ 0 };
    SW_ASSERT_TRUE( sw::FileUtil::getFileWriteTime( stampPath, stampTime ) );
    SW_ASSERT_TRUE( sw::FileUtil::setFileWriteTime( headerPath, stampTime - sw::FileUtil::kFileTimeTicksPerSecond ) );

    SW_ASSERT_EQUAL( 0, sw::Process::execute( command, options, {} ) );
    sw::string generated;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genPath, generated ) );
    SW_EXPECT_TRUE_MSG( generated.find( "_addedWhileParsing" ) != sw::string::npos,
                        "파싱하는 동안 저장한 편집을 스탬프가 가렸습니다 — 그 헤더를 다시 저장할 때까지 생성 코드에 들어가지 않습니다" );

    // 그대로 다시 돌리면 최신이다(다시 파싱하지 않는다 — 산출물을 건드리지 않는다).
    int64 stampBefore{ 0 };
    SW_ASSERT_TRUE( sw::FileUtil::getFileWriteTime( stampPath, stampBefore ) );
    SW_ASSERT_EQUAL( 0, sw::Process::execute( command, options, {} ) );
    int64 stampAfter{ 0 };
    SW_ASSERT_TRUE( sw::FileUtil::getFileWriteTime( stampPath, stampAfter ) );
    SW_EXPECT_EQUAL( stampBefore, stampAfter );
}

/**
 * @brief [ReflectionParserTest] 반사된 헤더가 include 한 **다른** 헤더가 바뀌면 다시 파싱하고, depfile 에 그 헤더를 적는다
 * @details 생성 코드는 include 한 헤더에도 기댄다 — 반사되지 않은 기반 클래스에 순수 가상 함수가 생기면 파생 타입은 추상이 되고(`_bAbstract`),
 *          낡은 생성 코드는 그 타입을 만드는 생성자를 들고 있어 컴파일이 깨진다. 그래서 스탬프가 include 한 헤더를 적고, `--depfile` 이 ninja 에 같은
 *          목록을 준다 — 생성 단계의 의존이 반사된 헤더뿐이면 그 헤더가 바뀌어도 ninja 도 파서도 다시 돌지 않는다.
 */
SW_TEST_CASE( ReflectionParserTest, IncludedHeaderChangeRegeneratesAndIsInTheDepfile )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const sw::string projectRoot = sw::ResourcePaths::getProjectFolderPath();
    const sw::string caseRoot    = test::makeTempPath( "include_dependency" );
    const sw::string basePath    = sw::FileUtil::joinPath( caseRoot, "DependencyBaseSample.h" );
    const sw::string headerPath  = sw::FileUtil::joinPath( caseRoot, "DependencyHolderSample.h" );
    const sw::string outGenDir   = sw::FileUtil::joinPath( caseRoot, "gen" );
    const sw::string depfilePath = sw::FileUtil::joinPath( outGenDir, "ReflectionParser.d" );
    sw::FileUtil::ensureDirectoryExists( outGenDir );

    const auto writeBase = [&basePath]( const utf8* pExtraMember )
    {
        return sw::FileUtil::writeTextFile( basePath, sw::string( "#pragma once\n"
                                                                  "namespace sw\n{\n    struct DependencyBaseSample\n    {\n"
                                                                  "        virtual ~DependencyBaseSample() = default;\n" ) +
                                                          pExtraMember + "    };\n}\n" );
    };
    SW_ASSERT_TRUE( writeBase( "" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerPath, "#pragma once\n"
                                                             "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                             "#include \"DependencyBaseSample.h\"\n"
                                                             "namespace sw\n"
                                                             "{\n"
                                                             "    REFLECT()\n"
                                                             "    struct DependencyHolderSampleActor : public DependencyBaseSample\n"
                                                             "    {\n"
                                                             "        REFLECT_BODY();\n"
                                                             "        PROPERTY()\n"
                                                             "        int32 _value{ 0 };\n"
                                                             "    };\n"
                                                             "}\n" ) );

    const sw::string   command = makeParserCommand( parserExe, headerPath, outGenDir, projectRoot ) + " --depfile \"" + depfilePath + "\"";
    sw::ProcessOptions options;
    options._workingDirectory = projectRoot;
    SW_ASSERT_EQUAL( 0, sw::Process::execute( command, options, {} ) );

    const sw::string genPath = sw::FileUtil::joinPath( outGenDir, "DependencyHolderSample.gen.cpp" );
    sw::string       generated;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genPath, generated ) );
    SW_ASSERT_TRUE_MSG( generated.find( "_bAbstract = 1" ) == sw::string::npos, generated.c_str() );

    // depfile: 목표에 산출물이, 의존에 include 한 헤더가(슬래시로) — 출력 폴더 · 묶음 원본은 없다.
    sw::string depfile;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( depfilePath, depfile ) );
    SW_EXPECT_TRUE_MSG( depfile.find( "DependencyHolderSample.gen.cpp" ) != sw::string::npos, depfile.c_str() );
    // 경로는 실제 경로로 적힌다(Windows CI 의 TEMP 는 8.3 짧은 이름 `RUNNER~1` 이라 받은 경로와 글자가 다르다) — 파일 이름으로 본다.
    SW_EXPECT_TRUE_MSG( depfile.find( "/DependencyBaseSample.h" ) != sw::string::npos, depfile.c_str() );
    SW_EXPECT_TRUE_MSG( depfile.find( "batch.cpp" ) == sw::string::npos, depfile.c_str() );
    SW_EXPECT_TRUE_MSG( depfile.find( ":\\" ) == sw::string::npos, depfile.c_str() ); // 드라이브 뒤 역슬래시가 없다(Makefile 이스케이프와 섞인다)

    // 반사되지 않은 기반에만 순수 가상 함수를 더한다(반사된 헤더는 그대로). 시각은 한 시간 뒤로 — 같은 초 안의 변화도 확실히.
    SW_ASSERT_TRUE( writeBase( "        virtual void mustImplement() = 0;\n" ) );
    int64 baseTime{ 0 };
    SW_ASSERT_TRUE( sw::FileUtil::getFileWriteTime( basePath, baseTime ) );
    SW_ASSERT_TRUE( sw::FileUtil::setFileWriteTime( basePath, baseTime + kHourTicks ) );
    SW_ASSERT_EQUAL( 0, sw::Process::execute( command, options, {} ) );
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( genPath, generated ) );
    SW_EXPECT_TRUE_MSG( generated.find( "_bAbstract = 1" ) != sw::string::npos,
                        "include 한 기반이 추상이 됐는데 생성 코드가 옛것(만들 수 있는 타입)으로 남았습니다" );
}

/**
 * @brief [ReflectionParserTest] ENUM(Flags) 의 .gen.h 는 트레이트 헤더 하나만 include 하고, 그 트레이트 헤더는 `<type_traits>` 만 든다
 * @details `.gen.h` 를 모은 `FlagOps.gen.h` 는 타깃의 모든 TU 에 강제 include 된다. 여기서 엔진 헤더(`EnumUtil.h` → `Macros.h`)를 들이면
 *          그 이름이 어디에나 있는 이름이 되어, `SW_API` 를 쓰면서 `Macros.h` 를 include 하지 않은 헤더도 빌드와 `RunHeaderSelfContained` 를 통과한다.
 */
SW_TEST_CASE( ReflectionParserTest, FlagTraitHeaderIncludesOnlyTheTrait )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const ParserRunResult run = runParserOnTempHeader( parserExe, "FlagTraitSample",
                                                       "#pragma once\n"
                                                       "#include \"Core/Common/Types.h\"\n"
                                                       "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                       "namespace sw\n"
                                                       "{\n"
                                                       "\tENUM( Flags )\n"
                                                       "\tenum class FlagTraitSample : uint8\n"
                                                       "\t{\n"
                                                       "\t\tNone = 0,\n"
                                                       "\t\tFirst = 1,\n"
                                                       "\t};\n"
                                                       "}\n" );
    SW_ASSERT_TRUE_MSG( run._exitCode == 0, run._log.c_str() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), run._listGeneratedHeader.size() );
    const sw::string& generated = run._listGeneratedHeader[0];
    SW_ASSERT_TRUE_MSG( generated.find( "IsBitFlagEnum<sw::FlagTraitSample>" ) != sw::string::npos, generated.c_str() );

    const auto collectIncludes = []( const sw::string& text )
    {
        sw::vector<sw::string> listInclude;
        size_t                 begin = 0;
        while ( begin < text.size() )
        {
            size_t end = text.find( '\n', begin );
            if ( end == sw::string::npos )
                end = text.size();
            // 체크아웃이 CRLF 일 수 있다(core.autocrlf) — 줄 끝의 \r 은 뺀다.
            const size_t     lineEnd = ( end > begin && text[end - 1] == '\r' ) ? end - 1 : end;
            const sw::string line    = text.substr( begin, lineEnd - begin );
            if ( sw::StringUtil::startsWith( line, "#include" ) )
                listInclude.push_back( line );
            begin = end + 1;
        }
        return listInclude;
    };

    const sw::vector<sw::string> listGeneratedInclude = collectIncludes( generated );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listGeneratedInclude.size() );
    SW_EXPECT_TRUE_MSG( listGeneratedInclude[0] == "#include \"Core/Common/BitFlagTrait.h\"", generated.c_str() );

    sw::string traitText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile(
        sw::FileUtil::joinPath( sw::ResourcePaths::getProjectFolderPath(), "Source/Core/Common/BitFlagTrait.h" ), traitText ) );
    const sw::vector<sw::string> listTraitInclude = collectIncludes( traitText );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listTraitInclude.size() );
    SW_EXPECT_TRUE_MSG( listTraitInclude[0] == "#include <type_traits>", traitText.c_str() );
}

/**
 * @brief [ReflectionParserTest] 로컬 parser_config.json 은 커밋된 기본값을 덮지 못한다 — 이 기계에 딸린 키만 받고, 나머지는 경고하고 버린다
 * @details 로컬이 기본값의 **전체 사본**을 들고 키마다 이기면 커밋된 기본값을 고쳐도 그 기계는 옛 값을 계속 쓴다 — `flag_ops_marker` 가 바뀌면
 *          옛 로컬 값 때문에 FlagOps 우산이 비어 Engine 빌드가 깨진다. 여기서는 옛 값을 든 로컬 파일로 돌려, 우산이 여전히
 *          ENUM(Flags) 헤더를 담고 로그가 무시한 키를 말하는지 본다.
 */
SW_TEST_CASE( ReflectionParserTest, LocalConfigCannotOverrideCommittedDefaults )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const sw::string projectRoot = sw::ResourcePaths::getProjectFolderPath();
    const sw::string caseRoot    = test::makeTempPath( "local_config" );
    const sw::string envDir      = sw::FileUtil::joinPath( caseRoot, "Config/Environment" );
    const sw::string outGenDir   = sw::FileUtil::joinPath( caseRoot, "gen" );
    sw::FileUtil::ensureDirectoryExists( envDir );
    sw::FileUtil::ensureDirectoryExists( outGenDir );
    // 파서는 작업 폴더에서 위로 올라가며 설정을 찾는다 — 이 케이스 폴더의 사본이 먼저 잡힌다.
    for ( const utf8* pName : { "parser_config.defaults.json", "toolchain_config.json" } )
    {
        const sw::string source = sw::FileUtil::joinPath( sw::FileUtil::joinPath( projectRoot, "Config/Environment" ), pName );
        if ( sw::FileUtil::exists( source ) == false )
            SW_TEST_SKIP( "Config/Environment is not set up on this machine (run Scripts/setup/SetupEnvironment.py)" );
        SW_ASSERT_TRUE( sw::FileUtil::copyFile( source, sw::FileUtil::joinPath( envDir, pName ) ) );
    }
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( envDir, "parser_config.json" ),
                                                 "{ \"emit\": { \"flag_ops_marker\": \"operator|\" }, \"default_parser_args\": [] }\n" ) );

    const sw::string headerPath = sw::FileUtil::joinPath( caseRoot, "LocalConfigFlagsSample.h" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerPath, "#pragma once\n"
                                                             "#include \"Core/Common/Types.h\"\n"
                                                             "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                             "namespace sw\n"
                                                             "{\n"
                                                             "    ENUM( Flags )\n"
                                                             "    enum class LocalConfigFlagsSample : uint8\n"
                                                             "    {\n"
                                                             "        None = 0,\n"
                                                             "        First = 1,\n"
                                                             "    };\n"
                                                             "}\n" ) );

    sw::ProcessOptions options;
    options._workingDirectory = caseRoot;
    sw::string  log;
    const int32 exitCode = sw::Process::execute( makeParserCommand( parserExe, headerPath, outGenDir, projectRoot ), options,
                                                 SW_DELEGATE_LAMBDA( sw::ProcessOutputDelegate, [&log]( sw::string_view line )
    {
        log.append( line.data(), line.size() );
        log.push_back( '\n' );
    } ) );
    SW_ASSERT_TRUE_MSG( exitCode == 0, log.c_str() );

    sw::string umbrella;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( outGenDir, "FlagOps.gen.h" ), umbrella ) );
    SW_EXPECT_TRUE_MSG( umbrella.find( "LocalConfigFlagsSample.gen.h" ) != sw::string::npos,
                        "로컬의 옛 flag_ops_marker 가 기본값을 덮어 FlagOps 우산이 비었습니다" );
    SW_EXPECT_TRUE_MSG( log.find( "emit.flag_ops_marker" ) != sw::string::npos, log.c_str() );
    SW_EXPECT_TRUE_MSG( log.find( "default_parser_args" ) != sw::string::npos, log.c_str() );
}

/**
 * @brief [ReflectionParserTest] 깨진 로컬 parser_config.json 은 파서를 세운다 — 기본값으로 대신해 조용히 돌지 않는다
 * @details 깨진 설정을 빈 객체로 바꾸면 LLVM · SDK 경로가 비어 libclang 오류가 엉뚱한 자리에서 난다. 없는 파일만 선택이다.
 */
SW_TEST_CASE( ReflectionParserTest, BrokenLocalConfigStopsTheParser )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const sw::string projectRoot = sw::ResourcePaths::getProjectFolderPath();
    const sw::string caseRoot    = test::makeTempPath( "broken_local_config" );
    const sw::string envDir      = sw::FileUtil::joinPath( caseRoot, "Config/Environment" );
    const sw::string outGenDir   = sw::FileUtil::joinPath( caseRoot, "gen" );
    sw::FileUtil::ensureDirectoryExists( envDir );
    sw::FileUtil::ensureDirectoryExists( outGenDir );
    for ( const utf8* pName : { "parser_config.defaults.json", "toolchain_config.json" } )
    {
        const sw::string source = sw::FileUtil::joinPath( sw::FileUtil::joinPath( projectRoot, "Config/Environment" ), pName );
        if ( sw::FileUtil::exists( source ) == false )
            SW_TEST_SKIP( "Config/Environment is not set up on this machine (run Scripts/setup/SetupEnvironment.py)" );
        SW_ASSERT_TRUE( sw::FileUtil::copyFile( source, sw::FileUtil::joinPath( envDir, pName ) ) );
    }
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( envDir, "parser_config.json" ), "{ \"paths\": { \"llvm\": \"x\", } \n" ) );

    const sw::string headerPath = sw::FileUtil::joinPath( caseRoot, "BrokenConfigSample.h" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerPath, "#pragma once\n"
                                                             "#include \"Core/Common/Types.h\"\n"
                                                             "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                             "namespace sw\n"
                                                             "{\n"
                                                             "    ENUM()\n"
                                                             "    enum class BrokenConfigSample : uint8\n"
                                                             "    {\n"
                                                             "        First,\n"
                                                             "    };\n"
                                                             "}\n" ) );

    sw::ProcessOptions options;
    options._workingDirectory = caseRoot;
    sw::string  log;
    const int32 exitCode = sw::Process::execute( makeParserCommand( parserExe, headerPath, outGenDir, projectRoot ), options,
                                                 SW_DELEGATE_LAMBDA( sw::ProcessOutputDelegate, [&log]( sw::string_view line )
    {
        log.append( line.data(), line.size() );
        log.push_back( '\n' );
    } ) );
    SW_EXPECT_TRUE_MSG( exitCode != 0, log.c_str() );
    SW_EXPECT_TRUE_MSG( log.find( "is not valid JSON" ) != sw::string::npos, log.c_str() );
}

/**
 * @brief [ReflectionParserTest] 함수 인자의 이름 · 기본 인자, 이벤트(멀티캐스트 델리게이트 PROPERTY)의 인자 이름을 코드젠한다 — 이벤트에 맞지 않는 애노테이션은 멈춘다
 * @details 인자 이름 · 기본 인자는 타입에 남지 않아 소스 토큰에서 읽는다. 이벤트 인자 이름은 별칭(`using ScoreEvent = …`)으로 적어도
 *          그 별칭 선언에서 읽는다. 한 번의 실행에 성한 헤더 하나와 깨진 헤더 둘을 넣는다(깨진 것은 그 헤더만 실패).
 */
SW_TEST_CASE( ReflectionParserTest, FunctionParametersAndEventsAreGenerated )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const sw::string       kHeaderHead = "#pragma once\n"
                                         "#include \"Core/Common/Types.h\"\n"
                                         "#include \"Core/Delegate/Delegate.h\"\n"
                                         "#include \"Engine/Reflection/ReflectionMacros.h\"\n";
    sw::vector<TempHeader> listHeader;
    listHeader.push_back( TempHeader{ "ParamEventSample", kHeaderHead +
                                                              "namespace sw\n"
                                                              "{\n"
                                                              "\tusing ScoreEvent = MulticastDelegate<void( int32 score, bool bBest )>;\n"
                                                              "\tREFLECT()\n"
                                                              "\tstruct ParamEventSampleActor\n"
                                                              "\t{\n"
                                                              "\t\tREFLECT_BODY();\n"
                                                              "\t\tPROPERTY( Category = \"Signals\" )\n"
                                                              "\t\tMulticastDelegate<void( const string& label, uint8 )> _onLabel;\n"
                                                              "\t\tPROPERTY()\n"
                                                              "\t\tScoreEvent _onScored;\n"
                                                              "\t\tFUNCTION()\n"
                                                              "\t\tvoid configure( int32 count, float32 scale = 2.0f, const string& label = \"a, b\" ) {}\n"
                                                              "\t};\n"
                                                              "}\n" } );
    listHeader.push_back( TempHeader{ "EventFlagSample", kHeaderHead +
                                                             "namespace sw\n"
                                                             "{\n"
                                                             "\tREFLECT()\n"
                                                             "\tstruct EventFlagSampleActor\n"
                                                             "\t{\n"
                                                             "\t\tREFLECT_BODY();\n"
                                                             "\t\tPROPERTY( Default = \"1\" )\n"
                                                             "\t\tMulticastDelegate<void()> _onFlagged;\n"
                                                             "\t};\n"
                                                             "}\n" } );
    listHeader.push_back( TempHeader{ "EventReturnSample", kHeaderHead +
                                                               "namespace sw\n"
                                                               "{\n"
                                                               "\tREFLECT()\n"
                                                               "\tstruct EventReturnSampleActor\n"
                                                               "\t{\n"
                                                               "\t\tREFLECT_BODY();\n"
                                                               "\t\tPROPERTY()\n"
                                                               "\t\tMulticastDelegate<int32()> _onAsked;\n"
                                                               "\t};\n"
                                                               "}\n" } );
    const ParserRunResult run = runParserOnTempHeaders( parserExe, listHeader, "--dump" );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), run._listGeneratedCpp.size() );
    const sw::string& generated = run._listGeneratedCpp[0];

    // 함수 인자: 이름 · 정규 타입 · 기본 인자 글(쉼표가 든 문자열 그대로) · 변환 표
    SW_EXPECT_TRUE_MSG( generated.find( "::sw::FunctionParameterInfo( \"count\", \"int32\", \"\", &::sw::ReflectTypeOpsOf<int32>::kOps )" ) != sw::string::npos,
                        generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "::sw::FunctionParameterInfo( \"scale\", \"float32\", \"2.0f\", &::sw::ReflectTypeOpsOf<float32>::kOps )" ) != sw::string::npos,
                        generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "\"label\", \"string\", \"\\\"a, b\\\"\"" ) != sw::string::npos, generated.c_str() );

    // 이벤트: 프로퍼티 목록이 아니라 이벤트 목록, 인자 이름은 필드 선언 · 별칭 선언에서, 이름 없는 인자는 빈 이름
    SW_EXPECT_TRUE_MSG( generated.find( "ReflectEventOpsOf<decltype(sw::ParamEventSampleActor::_onLabel)>" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "::sw::FunctionParameterInfo( \"label\", \"string\", \"\", nullptr )" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "::sw::FunctionParameterInfo( \"\", \"uint8\", \"\", nullptr )" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "::sw::FunctionParameterInfo( \"score\", \"int32\", \"\", nullptr )" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "::sw::FunctionParameterInfo( \"bBest\", \"bool\", \"\", nullptr )" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "e._offset = offsetof(sw::ParamEventSampleActor, _onLabel);" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "info._listProperty" ) == sw::string::npos, generated.c_str() );

    // 깨진 헤더 둘은 그 헤더만 실패한다
    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._listGeneratedCpp[1].find( "EventFlagSampleActor" ) == sw::string::npos, run._listGeneratedCpp[1].c_str() );
    SW_EXPECT_TRUE_MSG( run._listGeneratedCpp[2].find( "EventReturnSampleActor" ) == sw::string::npos, run._listGeneratedCpp[2].c_str() );
#if defined( SW_DEBUG )
    SW_EXPECT_TRUE_MSG( run._log.find( "takes display metadata only" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "must be MulticastDelegate<void( ... )>" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "FUNCTION configure(int32 count, float32 scale = 2.0f, string label = \"a, b\")" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "EVENT _onScored(int32 score, bool bBest)" ) != sw::string::npos, run._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 역할 애노테이션 — `RepNotify` 는 같은 타입의 `void fn()` · `void fn( const T& )` 만, `Interp` 는 섞을 수 있는 타입만. 다른 모양은 그 헤더만 실패한다
 */
SW_TEST_CASE( ReflectionParserTest, PropertyRoleAnnotationsAreValidated )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    // 성한 헤더 하나(RepNotify 두 모양 · SaveGame · Config) + 깨진 헤더 셋(없는 함수 · 다른 인자 타입 · 섞을 수 없는 Interp)을 한 번에 돌린다.
    const sw::string kHeaderHead = "#pragma once\n"
                                   "#include \"Core/Common/Types.h\"\n"
                                   "#include \"Core/Container/string.h\"\n"
                                   "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                   "namespace sw\n"
                                   "{\n"
                                   "\tREFLECT()\n";
    const auto       makeHeader  = [&kHeaderHead]( const sw::string& typeName, const sw::string& body )
    {
        return kHeaderHead + "\tstruct " + typeName + "\n\t{\n\t\tREFLECT_BODY();\n" + body + "\t};\n}\n";
    };
    sw::vector<TempHeader> listHeader;
    listHeader.push_back( TempHeader{ "RoleGoodSample", makeHeader( "RoleGoodSampleActor", "\t\tPROPERTY( RepNotify = onHp, SaveGame )\n\t\tint32 _hp;\n"
                                                                                           "\t\tPROPERTY( RepNotify = onMp )\n\t\tint32 _mp;\n"
                                                                                           "\t\tvoid onHp( const int32& oldValue ) {}\n"
                                                                                           "\t\tvoid onMp() {}\n" ) } );
    listHeader.push_back( TempHeader{ "RoleMissingSample", makeHeader( "RoleMissingSampleActor", "\t\tPROPERTY( RepNotify = onNothing )\n\t\tint32 _hp;\n" ) } );
    listHeader.push_back( TempHeader{ "RoleShapeSample", makeHeader( "RoleShapeSampleActor", "\t\tPROPERTY( RepNotify = onHp )\n\t\tint32 _hp;\n"
                                                                                             "\t\tvoid onHp( float32 oldValue ) {}\n" ) } );
    listHeader.push_back( TempHeader{ "RoleInterpSample", makeHeader( "RoleInterpSampleActor", "\t\tPROPERTY( Interp )\n\t\tstring _label;\n" ) } );
    const ParserRunResult run = runParserOnTempHeaders( parserExe, listHeader );
    SW_ASSERT_EQUAL( static_cast<size_t>( 4 ), run._listGeneratedCpp.size() );

    const sw::string& generated = run._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "->onHp( *static_cast<const PropDecl*>( pOldValue ) );" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "->onMp(); };" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_bReplicated = SW_TRUE;" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_bSaveGame = SW_TRUE;" ) != sw::string::npos, generated.c_str() );

    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    for ( size_t brokenIndex = 1; brokenIndex < 4; ++brokenIndex )
    {
        SW_EXPECT_TRUE_MSG( run._listGeneratedCpp[brokenIndex].find( "struct Registrar" ) == sw::string::npos, run._listGeneratedCpp[brokenIndex].c_str() );
    }
#if defined( SW_DEBUG )
    SW_EXPECT_TRUE_MSG( run._log.find( "RoleMissingSampleActor::_hp" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "RoleShapeSampleActor::_hp" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "PROPERTY( Interp ) on 'sw::RoleInterpSampleActor::_label'" ) != sw::string::npos, run._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 표시 메타 — `Units` 는 단위 표에 있어야 하고(커스텀 메타 `Units` 로 실린다), 표에 있는 단위를 `Meta = "Units=…"` 로 적으면
 *        거절하고(표에 없는 `HP` 만 `Meta` 로 받는다), `EditCondition` 은 네 꼴 중 하나, C 고정 배열의 원소는 컨테이너가 아니다
 */
SW_TEST_CASE( ReflectionParserTest, DisplayMetadataIsValidated )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    sw::vector<TempHeader> listHeader;
    listHeader.push_back( TempHeader{ "DisplayGoodSample", makeReflectedHeader( "DisplayGoodSampleActor", "Units = \"m/s\", UIMin = 0, UIMax = 30, EditCondition = \"!_bLocked\"", "float32" ) } );
    listHeader.push_back( TempHeader{ "DisplayLabelSample", makeReflectedHeader( "DisplayLabelSampleActor", "Meta = \"Units=HP\"", "float32" ) } );
    listHeader.push_back( TempHeader{ "DisplayUnitSample", makeReflectedHeader( "DisplayUnitSampleActor", "Units = furlong", "float32" ) } );
    listHeader.push_back( TempHeader{ "DisplayMetaUnitSample", makeReflectedHeader( "DisplayMetaUnitSampleActor", "Meta = \"Units=m\"", "float32" ) } );
    listHeader.push_back( TempHeader{ "DisplaySpacedUnitSample", makeReflectedHeader( "DisplaySpacedUnitSampleActor", "Units = m / s", "float32" ) } );
    listHeader.push_back( TempHeader{ "DisplayConditionSample", makeReflectedHeader( "DisplayConditionSampleActor", "EditCondition = \"a && b\"", "float32" ) } );
    listHeader.push_back( TempHeader{ "DisplayArraySample", "#pragma once\n"
                                                            "#include \"Core/Common/Types.h\"\n"
                                                            "#include \"Core/Container/vector.h\"\n"
                                                            "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                                                            "namespace sw\n"
                                                            "{\n"
                                                            "\tREFLECT()\n"
                                                            "\tstruct DisplayArraySampleActor\n"
                                                            "\t{\n"
                                                            "\t\tREFLECT_BODY();\n"
                                                            "\t\tPROPERTY()\n"
                                                            "\t\tint32 _arrGood[4];\n"
                                                            "\t\tPROPERTY()\n"
                                                            "\t\tvector<int32> _arrBad[2];\n"
                                                            "\t};\n"
                                                            "}\n" } );
    const ParserRunResult run = runParserOnTempHeaders( parserExe, listHeader );
    SW_ASSERT_EQUAL( static_cast<size_t>( 7 ), run._listGeneratedCpp.size() );

    const sw::string& generated = run._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "{ ::sw::hashed_string( \"Units\" ), \"m/s\" }," ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_uiMaxRange   = 30.000000f;" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_editCondition = \"!_bLocked\";" ) != sw::string::npos, generated.c_str() );
    const sw::string& generatedLabel = run._listGeneratedCpp[1];
    SW_EXPECT_TRUE_MSG( generatedLabel.find( "{ ::sw::hashed_string( \"Units\" ), \"HP\" }," ) != sw::string::npos, generatedLabel.c_str() );

    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    for ( size_t brokenIndex = 2; brokenIndex < 7; ++brokenIndex )
    {
        SW_EXPECT_TRUE_MSG( run._listGeneratedCpp[brokenIndex].find( "struct Registrar" ) == sw::string::npos, run._listGeneratedCpp[brokenIndex].c_str() );
    }
#if defined( SW_DEBUG )
    SW_EXPECT_TRUE_MSG( run._log.find( "Units = furlong" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "DisplayMetaUnitSampleActor::_value' writes Meta = \"Units=m\"" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "Units = m / s  (value has spaces - quote it)" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "EditCondition = \"a && b\"" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "DisplayArraySampleActor::_arrBad" ) != sw::string::npos, run._log.c_str() );
#endif
}

/**
 * @brief [ReflectionParserTest] 검증 함수 — `Validate = fn` 은 같은 타입의 `void fn( ValidationContext& )` 여야 한다(없거나 모양이 다르면 그 헤더만 실패)
 */
SW_TEST_CASE( ReflectionParserTest, ValidateFunctionsAreChecked )
{
    const sw::string parserExe = findReflectionParserExecutable();
    if ( parserExe.empty() )
        SW_TEST_SKIP( "ReflectionParser executable not found (Bin/ · BuildTools/)" );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    const auto makeHeader = []( const sw::string& typeName, const sw::string& reflectArgs, const sw::string& body )
    {
        return sw::string( "#pragma once\n"
                           "#include \"Core/Common/Types.h\"\n"
                           "#include \"Engine/Reflection/ReflectionMacros.h\"\n"
                           "namespace sw\n"
                           "{\n"
                           "\tclass ValidationContext;\n"
                           "\tREFLECT( " ) +
               reflectArgs + " )\n\tstruct " + typeName + "\n\t{\n\t\tREFLECT_BODY();\n" + body + "\t};\n}\n";
    };
    sw::vector<TempHeader> listHeader;
    listHeader.push_back( TempHeader{ "ValidateGoodSample", makeHeader( "ValidateGoodSampleActor", "Validate = checkAll",
                                                                        "\t\tPROPERTY( Validate = checkHp )\n\t\tint32 _hp;\n"
                                                                        "\t\tvoid checkHp( ValidationContext& context ) const;\n"
                                                                        "\t\tvoid checkAll( ValidationContext& context );\n" ) } );
    listHeader.push_back( TempHeader{ "ValidateMissingSample", makeHeader( "ValidateMissingSampleActor", "Validate = noSuchCheck", "\t\tPROPERTY()\n\t\tint32 _hp;\n" ) } );
    listHeader.push_back( TempHeader{ "ValidateShapeSample", makeHeader( "ValidateShapeSampleActor", "",
                                                                         "\t\tPROPERTY( Validate = checkHp )\n\t\tint32 _hp;\n"
                                                                         "\t\tbool checkHp() const;\n" ) } );
    const ParserRunResult run = runParserOnTempHeaders( parserExe, listHeader );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), run._listGeneratedCpp.size() );

    const sw::string& generated = run._listGeneratedCpp[0];
    SW_EXPECT_TRUE_MSG( generated.find( "info._pValidate = []( const void* pInstance, ::sw::ValidationContext& context )" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "->checkAll( context ); };" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._pValidate = []( const void* pInstance, ::sw::ValidationContext& context )" ) != sw::string::npos, generated.c_str() );
    SW_EXPECT_TRUE_MSG( generated.find( "p._metadata->_validate = \"checkHp\";" ) != sw::string::npos, generated.c_str() );

    SW_EXPECT_TRUE_MSG( run._exitCode != 0, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._listGeneratedCpp[1].find( "struct Registrar" ) == sw::string::npos, run._listGeneratedCpp[1].c_str() );
    SW_EXPECT_TRUE_MSG( run._listGeneratedCpp[2].find( "struct Registrar" ) == sw::string::npos, run._listGeneratedCpp[2].c_str() );
#if defined( SW_DEBUG )
    SW_EXPECT_TRUE_MSG( run._log.find( "REFLECT( Validate = noSuchCheck )" ) != sw::string::npos, run._log.c_str() );
    SW_EXPECT_TRUE_MSG( run._log.find( "ValidateShapeSampleActor::_hp" ) != sw::string::npos, run._log.c_str() );
#endif
}
