#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectAny.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionEnumNames.h"
#include "Engine/Reflection/Rpc/ReflectionRpc.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/Serializer.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Utility/Resource/ResourceUtil.h"

#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/ParserUtil.h"

#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 0) 헬퍼 — DummyActor / DummyType 수동 TypeInfo
	// ------------------------------------------------------------------------------
	struct DummyBase
	{
	};

	struct DummyActor
	{
		int32	_hp{ 0 };
		string	_name = "";
		float32 _speed{ 0.0f };
	};

	enum class DummyType : int64
	{
		None  = 0,
		TypeA = 1,
		TypeB = 2,
	};

	enum class DummyBitFlag : int64
	{
		None	= 0,
		OptionA = 1 << 0,
		OptionB = 1 << 1,
		OptionC = 1 << 2,
	};

	struct ComplexData
	{
		int32			   _id	  = 101;
		string			   _title = "HeroData";
		int64			   _flags = static_cast<int64>( DummyBitFlag::OptionA ) | static_cast<int64>( DummyBitFlag::OptionC );
		vector<int32>	   _listScore;
		map<string, int32> _mapStat;
	};
} // namespace sw

/** @brief DummyActor 등 수동 TypeInfo 를 레지스트리에 등록합니다. */
static void RegisterTypes( sw::TypeRegistry& registry )
{
	{
		sw::TypeInfo info;
		info._name				 = sw::hashed_string( "DummyBase" );
		info._fullyQualifiedName = sw::hashed_string( "sw::DummyBase" );
		info._parentFQN			 = sw::hashed_string( "" );
		info._size				 = sizeof( sw::DummyBase );
		registry.registerClass( info );
	}

	{
		sw::TypeInfo info;
		info._name				 = sw::hashed_string( "DummyActor" );
		info._fullyQualifiedName = sw::hashed_string( "sw::DummyActor" );
		info._parentFQN			 = sw::hashed_string( "sw::DummyBase" );
		info._size				 = sizeof( sw::DummyActor );
		info._listProperty =
			{
				{	  sw::hashed_string( "_hp" ),	  sw::hashed_string( "int32" ),
				  SW_OFFSET_OF( sw::DummyActor,	_hp ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
				{ sw::hashed_string( "_name" ),	sw::hashed_string( "string" ),
				  SW_OFFSET_OF( sw::DummyActor,	_name ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
				{sw::hashed_string( "_speed" ), sw::hashed_string( "float32" ),
				  SW_OFFSET_OF( sw::DummyActor, _speed ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		};
		registry.registerClass( info );
	}

	{
		sw::TypeInfo info;
		info._name				 = sw::hashed_string( "ComplexData" );
		info._fullyQualifiedName = sw::hashed_string( "sw::ComplexData" );
		info._parentFQN			 = sw::hashed_string( "" );
		info._size				 = sizeof( sw::ComplexData );
		info._listProperty =
			{
				{ sw::hashed_string( "_id" ), sw::hashed_string( "int32" ),
				  SW_OFFSET_OF( sw::ComplexData, _id ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
				{ sw::hashed_string( "_title" ), sw::hashed_string( "string" ),
				  SW_OFFSET_OF( sw::ComplexData, _title ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
				{ sw::hashed_string( "_flags" ), sw::hashed_string( "sw::DummyBitFlag" ),
				  SW_OFFSET_OF( sw::ComplexData, _flags ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
				{ sw::hashed_string( "_listScore" ), sw::hashed_string( "vector" ),
				  SW_OFFSET_OF( sw::ComplexData, _listScore ), true, sw::ContainerKind::Sequence, sw::hashed_string( "int32" ), sw::hashed_string(),
				  sw::make_shared<sw::VectorWrapper<sw::vector<int32>>>() },
				{ sw::hashed_string( "_mapStat" ), sw::hashed_string( "map" ),
				  SW_OFFSET_OF( sw::ComplexData, _mapStat ), true, sw::ContainerKind::Map, sw::hashed_string( "int32" ), sw::hashed_string( "string" ),
				  sw::make_shared<sw::MapWrapper<sw::map<sw::string, int32>>>() },
		};
		registry.registerClass( info );
	}
}

/** @brief DummyType / DummyBitFlag 를 레지스트리에 등록합니다. */
static void RegisterEnums( sw::TypeRegistry& registry )
{

	{
		sw::EnumInfo info;
		info._name				 = sw::hashed_string( "DummyType" );
		info._fullyQualifiedName = sw::hashed_string( "sw::DummyType" );
		info._bIsBitFlag		 = SW_FALSE;
		info._mapNameToValue =
			{
				{ sw::hashed_string( "None" ), 0},
				{sw::hashed_string( "TypeA" ), 1},
				{sw::hashed_string( "TypeB" ), 2},
		};
		info._mapValueToName =
			{
				{0,	 sw::hashed_string( "None" )},
				{1, sw::hashed_string( "TypeA" )},
				{2, sw::hashed_string( "TypeB" )},
		};
		registry.registerEnum( info );
	}

	{
		sw::EnumInfo info;
		info._name				 = sw::hashed_string( "DummyBitFlag" );
		info._fullyQualifiedName = sw::hashed_string( "sw::DummyBitFlag" );
		info._bIsBitFlag		 = SW_TRUE;
		info._mapNameToValue =
			{
				{	  sw::hashed_string( "None" ), 0},
				{sw::hashed_string( "OptionA" ), 1},
				{sw::hashed_string( "OptionB" ), 2},
				{sw::hashed_string( "OptionC" ), 4},
		};
		info._mapValueToName =
			{
				{0,	 sw::hashed_string( "None" )},
				{1, sw::hashed_string( "OptionA" )},
				{2, sw::hashed_string( "OptionB" )},
				{4, sw::hashed_string( "OptionC" )},
		};
		registry.registerEnum( info );
	}
}

struct RegistrarInit
{
	/** @brief 정적 초기화에서 Type/Enum registrar 를 연결합니다. */
	RegistrarInit()
	{
		static sw::TypeRegistrar s_regType( &RegisterTypes );
		static sw::EnumRegistrar s_regEnum( &RegisterEnums );
	}
};

static RegistrarInit s_RegistrarInit;

// ------------------------------------------------------------------------------
// 1) Reflection_TypeRegistry — 등록·조회·별칭·builtins
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_TypeRegistry] 등록된 클래스 조회
 */
SW_TEST_CASE( Reflection_TypeRegistry, FindRegisteredClass )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );

	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_EQUAL( sw::string( "DummyActor" ), sw::string( info->_name.c_str() ) );
	SW_EXPECT_EQUAL( sizeof( sw::DummyActor ), info->_size );
}

/**
 * @brief [Reflection_TypeRegistry] 없는 클래스는 null
 */
SW_TEST_CASE( Reflection_TypeRegistry, FindNonExistentClass )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::NotExist" ) );

	SW_EXPECT_TRUE( info == nullptr );
}

/**
 * @brief ReflectBuiltins.gen.cpp 의 primitive TypeInfo (canonical 이름)
 */
SW_TEST_CASE( Reflection_TypeRegistry, PrimitiveBuiltins )
{
	const sw::TypeInfo* i32 = sw::engine::getTypeRegistry().findType( sw::hashed_string( "int32" ) );
	SW_ASSERT_NOT_NULL( i32 );
	SW_EXPECT_TRUE( i32->isPrimitive() );
	SW_EXPECT_EQUAL( sizeof( int32 ), i32->_size );
	SW_EXPECT_FALSE( i32->canConstruct() );

	const sw::TypeInfo* f32 = sw::engine::getTypeRegistry().findType( sw::hashed_string( "float32" ) );
	SW_ASSERT_NOT_NULL( f32 );
	SW_EXPECT_TRUE( f32->isPrimitive() );

	const sw::TypeInfo* str = sw::engine::getTypeRegistry().findType( sw::hashed_string( "string" ) );
	SW_ASSERT_NOT_NULL( str );
	SW_EXPECT_TRUE( str->isPrimitive() );
	SW_EXPECT_EQUAL( sizeof( std::string ), str->_size );

	// ReflectBuiltins 별칭 → canonical TypeInfo (직렬화 핸들러 resolve 용).
	const sw::TypeInfo* viaInt = sw::engine::getTypeRegistry().findType( sw::hashed_string( "int32" ) );
	SW_ASSERT_NOT_NULL( viaInt );
	SW_EXPECT_EQUAL( i32->_typeId, viaInt->_typeId );
	SW_EXPECT_TRUE( viaInt->_name == sw::hashed_string( "int32" ) );
}

// ------------------------------------------------------------------------------
// 2) Reflection_TypeInfo — 프로퍼티·isA·생성자
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_TypeInfo] 프로퍼티 개수
 */
SW_TEST_CASE( Reflection_TypeInfo, PropertyCount )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( info->_listProperty.size() ) );
}

/**
 * @brief [Reflection_TypeInfo] 존재하는 프로퍼티 조회
 */
SW_TEST_CASE( Reflection_TypeInfo, FindExistingProperty )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	const sw::PropertyInfo* prop = info->findProperty( sw::hashed_string( "_hp" ) );
	SW_EXPECT_TRUE( prop != nullptr );
	if ( prop == nullptr )
		return;

	SW_EXPECT_EQUAL( sw::string( "_hp" ), sw::string( prop->_name.c_str() ) );
	SW_EXPECT_EQUAL( sw::string( "int32" ), sw::string( prop->_typeName.c_str() ) );
	SW_EXPECT_FALSE( prop->_bIsContainer );
}

/**
 * @brief [ReflectionParser] 주석 내에 있는 매크로 문자열은 파싱되지 않아야 함
 */
SW_TEST_CASE( ReflectionParser, FallbackCommentTest )
{
	const sw::TypeInfo* info = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::TestScriptComponent" ) );
	SW_ASSERT_NOT_NULL( info );

	// _shouldNotBeParsed 프로퍼티는 등록되지 않아야 함 (주석 안에 PROPERTY()가 있으므로 무시되어야 함)
	const sw::PropertyInfo* badProp = info->findProperty( sw::hashed_string( "_shouldNotBeParsed" ) );
	SW_EXPECT_TRUE( badProp == nullptr );
}

/**
 * @brief [ReflectionParser] ParserUtil 경로 조합 및 Include 경로 추출 검증
 */
SW_TEST_CASE( ReflectionParser, ParserUtilPathAndIncludeGeneration )
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
 * @brief [ReflectionParser] ParserUtil splitCommaRespectingAngles 템플릿 중첩 쉼표 분할 검증
 */
SW_TEST_CASE( ReflectionParser, ParserUtilSplitCommaRespectingAngles )
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
 * @brief [ReflectionParser] AnnotationMeta tryParseAnnotationKind 파싱 검증
 */
SW_TEST_CASE( ReflectionParser, AnnotationKindParsing )
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
 * @brief [ReflectionParser] ReflectionParser 코드젠 출력 메타데이터 및 Static/Ctor 심볼 검증
 */
SW_TEST_CASE( ReflectionParser, CodegenStaticLibraryAndCtorMetadata )
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
 * @brief [ReflectionParser] 프로퍼티 다중 별칭, 타입 개명 호환 및 저작 기본값 코드젠 검증
 */
SW_TEST_CASE( ReflectionParser, MultiplePropertyAliasesAndRenameCompat )
{
	// 1) 프로퍼티 다중 별칭 (Alias = "hp, HitPoints")
	const sw::TypeInfo* pAliasType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AliasAndReorderTestActor" ) );
	SW_ASSERT_NOT_NULL( pAliasType );

	const sw::PropertyInfo* pMainProp = pAliasType->findProperty( sw::hashed_string( "_currentHp" ) );
	const sw::PropertyInfo* pAlias1	  = pAliasType->findProperty( sw::hashed_string( "hp" ) );
	const sw::PropertyInfo* pAlias2	  = pAliasType->findProperty( sw::hashed_string( "HitPoints" ) );

	SW_ASSERT_NOT_NULL( pMainProp );
	SW_EXPECT_EQUAL( pMainProp, pAlias1 );
	SW_EXPECT_EQUAL( pMainProp, pAlias2 );

	// 2) 타입 개명 호환 (Alias = LegacyRenameActor)
	const sw::TypeInfo* pTypeCurrent = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::RenameCompatActor" ) );
	const sw::TypeInfo* pTypeLegacy	 = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::LegacyRenameActor" ) );
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
 * @brief [ReflectionParser] ParserUtil 극단적 템플릿 중첩 및 경로 불일치 경계조건 검증
 */
SW_TEST_CASE( ReflectionParser, ParserUtilExtremeEdgeCases )
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
 * @brief [ReflectionParser] RpcDemoActor 메타데이터 및 Invoker 실행 검증
 */
SW_TEST_CASE( ReflectionParser, RpcMethodMetadataAndInvokerExecution )
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
 * @brief [ReflectionParser] :2 이상 다중 비트 비트필드에 PROPERTY() 선언 시 빌드타임 컴파일 에러 진단 검증
 */
SW_TEST_CASE( ReflectionParser, MultiBitBitfieldCompilationErrorDiagnosis )
{
#if defined( SW_DEBUG )
	const sw::string binDir	   = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
	const sw::string parserExe = sw::FileUtil::joinPath( binDir, "ReflectionParser" );

	if ( sw::FileUtil::fileExists( parserExe ) == false &&
		 sw::FileUtil::fileExists( parserExe + ".exe" ) == false )
	{
		SW_TEST_SKIP( "ReflectionParser executable not found in binary directory" );
	}

	const sw::string projectRoot	= sw::ResourceUtil::getProjectFolderPath();
	const sw::string tempHeaderPath = sw::FileUtil::joinPath( binDir, "InvalidBitfieldSample.h" );
	const sw::string outGenDir		= sw::FileUtil::joinPath( binDir, "temp_gen" );
	sw::FileUtil::createDirectory( outGenDir );

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

	sw::string				  capturedLog;
	sw::ProcessOutputDelegate outputCb = SW_DELEGATE_LAMBDA(
		sw::ProcessOutputDelegate,
		[&capturedLog]( string_view line, bool /*bIsStdErr*/ )
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
 * @brief [Reflection_TypeInfo] 프로퍼티 메타데이터
 */
SW_TEST_CASE( Reflection_TypeInfo, PropertyMetadataSupport )
{
	sw::PropertyInfo prop;
#if !defined( SW_SHIPPING )
	prop._metadata._category	= "Rendering";
	prop._metadata._displayName = "Light Intensity";
	prop._metadata._tooltip		= "Controls light intensity";
#endif
	prop._metadata._minRange  = 0.0f;
	prop._metadata._maxRange  = 100.0f;
	prop._metadata._bHasRange = SW_TRUE;
	prop._metadata._bReadOnly = SW_TRUE;

#if !defined( SW_SHIPPING )
	SW_EXPECT_EQUAL( sw::string( "Rendering" ), prop._metadata._category );
	SW_EXPECT_EQUAL( sw::string( "Light Intensity" ), prop._metadata._displayName );
	SW_EXPECT_EQUAL( sw::string( "Controls light intensity" ), prop._metadata._tooltip );
#endif
	SW_EXPECT_NEAR_EQUAL( 0.0f, prop._metadata._minRange, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 100.0f, prop._metadata._maxRange, 1e-4f );
	SW_EXPECT_TRUE( prop._metadata._bHasRange );
	SW_EXPECT_TRUE( prop._metadata._bReadOnly );
}

// ------------------------------------------------------------------------------
// 3) Reflection_Serialization — Binary/JSON/XML·버전
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Serialization] 오브젝트 diff 직렬화 델타
 */
SW_TEST_CASE( Reflection_Serialization, ObjectDiffSerializationDelta )
{
	const sw::TypeInfo* info = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info != nullptr )
	{
		sw::DummyActor cdoActor;
		sw::DummyActor modActor;
		modActor._hp = 999;

		sw::vector<uint8> diffBuf;
		bool			  diffOk = sw::ObjectDiffSerializer::serializeDiff( diffBuf, &cdoActor, &modActor, *info );
		SW_EXPECT_TRUE( diffOk );
		SW_EXPECT_FALSE( diffBuf.empty() );

		sw::DummyActor restored = cdoActor;
		SW_EXPECT_TRUE( sw::ObjectDiffSerializer::deserializeDiff( &restored, *info, diffBuf.data(), diffBuf.size() ) );
		SW_EXPECT_EQUAL( 999, restored._hp );
	}

	// Alias 해시로도 diff 적용
	struct DiffAliasActor
	{
		int32 _currentHp{ 0 };
	};
	sw::TypeInfo aliasInfo;
	aliasInfo._name				  = sw::hashed_string( "DiffAliasActor" );
	aliasInfo._fullyQualifiedName = sw::hashed_string( "sw::DiffAliasActor" );
	aliasInfo._size				  = sizeof( DiffAliasActor );
	sw::PropertyInfo hpProp( sw::hashed_string( "_currentHp" ), sw::hashed_string( "int32" ),
							 SW_OFFSET_OF( DiffAliasActor, _currentHp ) );
	hpProp._listAlias.push_back( sw::hashed_string( "hp" ) );
	aliasInfo._listProperty.push_back( hpProp );

	DiffAliasActor cdo{};
	DiffAliasActor mod{};
	mod._currentHp = 42;
	sw::vector<uint8> aliasDiff;
	SW_EXPECT_TRUE( sw::ObjectDiffSerializer::serializeDiff( aliasDiff, &cdo, &mod, aliasInfo ) );

	// 직렬화 페이로드 상의 이름을 alias 해시로 위조해 matchesNameHash 경로를 검증합니다.
	if ( aliasDiff.size() >= sizeof( uint32 ) )
	{
		const uint32 aliasHash = sw::hashed_string( "hp" ).getHash();
		sw::Memory::copy( aliasDiff.data(), &aliasHash, sizeof( uint32 ) );
	}
	DiffAliasActor viaAlias{};
	SW_EXPECT_TRUE( sw::ObjectDiffSerializer::deserializeDiff( &viaAlias, aliasInfo, aliasDiff.data(), aliasDiff.size() ) );
	SW_EXPECT_EQUAL( 42, viaAlias._currentHp );

	{
		SW_TEST_DEFENSIVE_SCOPE( "Testing unknown property hash in ObjectDiff" );
		uint32			  unknownHash = 0xDEADBEEFu;
		uint32			  zeroPayload{ 0 };
		sw::vector<uint8> unknownDiff( sizeof( uint32 ) * 2 );
		sw::Memory::copy( unknownDiff.data(), &unknownHash, sizeof( uint32 ) );
		sw::Memory::copy( unknownDiff.data() + sizeof( uint32 ), &zeroPayload, sizeof( uint32 ) );
		SW_EXPECT_FALSE( sw::ObjectDiffSerializer::deserializeDiff( &viaAlias, aliasInfo, unknownDiff.data(), unknownDiff.size() ) );
	}
}

// ------------------------------------------------------------------------------
// 4) Reflection_TypeInfo — 프로퍼티·isA·생성자
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_TypeInfo] 없는 프로퍼티는 null
 */
SW_TEST_CASE( Reflection_TypeInfo, FindNonExistentProperty )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	const sw::PropertyInfo* prop = info->findProperty( sw::hashed_string( "_notExist" ) );
	SW_EXPECT_TRUE( prop == nullptr );
}

/**
 * @brief [Reflection_TypeInfo] isA 동일 타입
 */
SW_TEST_CASE( Reflection_TypeInfo, IsA_SameType )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_TRUE( info->isDerivedFrom( sw::hashed_string( "sw::DummyActor" ) ) );
}

/**
 * @brief [Reflection_TypeInfo] isA 부모 타입
 */
SW_TEST_CASE( Reflection_TypeInfo, IsA_ParentType )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_TRUE( info->isDerivedFrom( sw::hashed_string( "sw::DummyBase" ) ) );
}

/**
 * @brief [Reflection_TypeInfo] isA 무관 타입
 */
SW_TEST_CASE( Reflection_TypeInfo, IsA_UnrelatedType )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_FALSE( info->isDerivedFrom( sw::hashed_string( "sw::UnrelatedType" ) ) );
}

// ------------------------------------------------------------------------------
// 5) Reflection_PropertyInfo — 오프셋·get/set
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_PropertyInfo] 오프셋 정확성
 */
SW_TEST_CASE( Reflection_PropertyInfo, OffsetCorrectness )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	const sw::PropertyInfo* hpProp	  = info->findProperty( sw::hashed_string( "_hp" ) );
	const sw::PropertyInfo* nameProp  = info->findProperty( sw::hashed_string( "_name" ) );
	const sw::PropertyInfo* speedProp = info->findProperty( sw::hashed_string( "_speed" ) );

	SW_EXPECT_TRUE( hpProp != nullptr );
	SW_EXPECT_TRUE( nameProp != nullptr );
	SW_EXPECT_TRUE( speedProp != nullptr );

	if ( hpProp != nullptr )
		SW_EXPECT_EQUAL( SW_OFFSET_OF( sw::DummyActor, _hp ), hpProp->_offset );

	if ( nameProp != nullptr )
		SW_EXPECT_EQUAL( SW_OFFSET_OF( sw::DummyActor, _name ), nameProp->_offset );

	if ( speedProp != nullptr )
		SW_EXPECT_EQUAL( SW_OFFSET_OF( sw::DummyActor, _speed ), speedProp->_offset );
}

/**
 * @brief [Reflection_PropertyInfo] getValuePtr
 */
SW_TEST_CASE( Reflection_PropertyInfo, GetValuePtr )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	sw::DummyActor actor;
	actor._hp = 42;

	const sw::PropertyInfo* hpProp = info->findProperty( sw::hashed_string( "_hp" ) );
	SW_EXPECT_TRUE( hpProp != nullptr );
	if ( hpProp == nullptr )
		return;

	const int32* hpPtr = hpProp->getValuePtr<int32>( &actor );
	SW_EXPECT_TRUE( hpPtr != nullptr );
	SW_EXPECT_EQUAL( 42, *hpPtr );
}

/**
 * @brief [Reflection_PropertyInfo] setValue
 */
SW_TEST_CASE( Reflection_PropertyInfo, SetValue )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	sw::DummyActor actor;
	actor._hp = 0;

	const sw::PropertyInfo* hpProp = info->findProperty( sw::hashed_string( "_hp" ) );
	SW_EXPECT_TRUE( hpProp != nullptr );
	if ( hpProp == nullptr )
		return;

	hpProp->setValue<int32>( &actor, 999 );
	SW_EXPECT_EQUAL( 999, actor._hp );
}

/**
 * @brief [Reflection_PropertyInfo] setValue 중복 쓰기 없음
 */
SW_TEST_CASE( Reflection_PropertyInfo, SetValue_NoDuplicateWrite )
{
	const sw::TypeInfo* info =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	sw::DummyActor actor;
	actor._hp = 100;

	const sw::PropertyInfo* hpProp = info->findProperty( sw::hashed_string( "_hp" ) );
	SW_EXPECT_TRUE( hpProp != nullptr );
	if ( hpProp == nullptr )
		return;

	hpProp->setValue<int32>( &actor, 100 );
	SW_EXPECT_EQUAL( 100, actor._hp );
}

// ------------------------------------------------------------------------------
// 6) Reflection_Containers — 래퍼 시퀀스·맵
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Containers] Vector 래퍼
 */
SW_TEST_CASE( Reflection_Containers, VectorWrapper )
{
	sw::vector<int32>					 vec = { 10, 20, 30 };
	sw::VectorWrapper<sw::vector<int32>> wrapper;

	SW_EXPECT_EQUAL( 3u, wrapper.getSize( &vec ) );
	SW_EXPECT_EQUAL( 10, *static_cast<int32*>( wrapper.getElement( &vec, 0 ) ) );
	SW_EXPECT_EQUAL( 30, *static_cast<int32*>( wrapper.getElement( &vec, 2 ) ) );

	wrapper.addElementDefault( &vec );
	SW_EXPECT_EQUAL( 4u, wrapper.getSize( &vec ) );
	SW_EXPECT_EQUAL( 0, *static_cast<int32*>( wrapper.getElement( &vec, 3 ) ) );

	wrapper.clear( &vec );
	SW_EXPECT_EQUAL( 0u, wrapper.getSize( &vec ) );
}

/**
 * @brief [Reflection_Containers] List 래퍼
 */
SW_TEST_CASE( Reflection_Containers, ListWrapper )
{
	sw::list<sw::string>				  lst = { "alpha", "beta" };
	sw::ListWrapper<sw::list<sw::string>> wrapper;

	SW_EXPECT_EQUAL( 2u, wrapper.getSize( &lst ) );
	SW_EXPECT_EQUAL( sw::string( "alpha" ), *static_cast<sw::string*>( wrapper.getElement( &lst, 0 ) ) );

	wrapper.clear( &lst );
	SW_EXPECT_EQUAL( 0u, wrapper.getSize( &lst ) );
}

/**
 * @brief [Reflection_Containers] Deque 래퍼
 */
SW_TEST_CASE( Reflection_Containers, DequeWrapper )
{
	sw::deque<float32>					 dq = { 1.5f, 2.5f, 3.5f };
	sw::DequeWrapper<sw::deque<float32>> wrapper;

	SW_EXPECT_EQUAL( 3u, wrapper.getSize( &dq ) );
	SW_EXPECT_NEAR_EQUAL( 2.5f, *static_cast<float32*>( wrapper.getElement( &dq, 1 ) ), 0.001f );
}

/**
 * @brief [Reflection_Containers] Set 래퍼
 */
SW_TEST_CASE( Reflection_Containers, SetWrapper )
{
	sw::set<int32>				   st = { 100, 200, 300 };
	sw::SetWrapper<sw::set<int32>> wrapper;

	SW_EXPECT_EQUAL( 3u, wrapper.getSize( &st ) );
	SW_EXPECT_EQUAL( 100, *static_cast<const int32*>( wrapper.getElementConst( &st, 0 ) ) );
}

/**
 * @brief [Reflection_Containers] Map 래퍼
 */
SW_TEST_CASE( Reflection_Containers, MapWrapper )
{
	sw::map<sw::string, int32> mp = {
		{"Atk", 50},
		{"Def", 20}
	  };
	sw::MapWrapper<sw::map<sw::string, int32>> wrapper;

	SW_EXPECT_EQUAL( 2u, wrapper.getSize( &mp ) );

	int32 elementCount{ 0 };
	wrapper.forEach( &mp, [&]( const void* pKPtr, const void* pVPtr )
	{
		elementCount++;
		const sw::string* key = static_cast<const sw::string*>( pKPtr );
		const int32*	  val = static_cast<const int32*>( pVPtr );
		if ( *key == "Atk" )
		{
			SW_EXPECT_EQUAL( 50, *val );
		}
		if ( *key == "Def" )
		{
			SW_EXPECT_EQUAL( 20, *val );
		}
	} );

	SW_EXPECT_EQUAL( 2, elementCount );

	sw::string newKey = "Speed";
	int32	   newVal = 10;
	wrapper.insertKeyValue( &mp, &newKey, &newVal );
	SW_EXPECT_EQUAL( 3u, wrapper.getSize( &mp ) );
	SW_EXPECT_EQUAL( 10, mp["Speed"] );
}

/**
 * @brief [Reflection_Containers] 추가 컨테이너 래퍼
 */
SW_TEST_CASE( Reflection_Containers, AdditionalContainerWrappers )
{

	std::array<int32, 4>				   arr = { 1, 2, 3, 4 };
	sw::ArrayWrapper<std::array<int32, 4>> arrWrapper;
	SW_EXPECT_EQUAL( 4u, arrWrapper.getSize( &arr ) );
	SW_EXPECT_EQUAL( 3, *static_cast<int32*>( arrWrapper.getElement( &arr, 2 ) ) );

	sw::unordered_set<sw::string>						   uSet = { "alpha", "beta" };
	sw::UnorderedSetWrapper<sw::unordered_set<sw::string>> uSetWrapper;
	SW_EXPECT_EQUAL( 2u, uSetWrapper.getSize( &uSet ) );

	sw::unordered_map<sw::string, int32> uMap = {
		{ "Hp", 100 }
	  };
	sw::UnorderedMapWrapper<sw::unordered_map<sw::string, int32>> uMapWrapper;
	SW_EXPECT_EQUAL( 1u, uMapWrapper.getSize( &uMap ) );
	SW_EXPECT_EQUAL( sizeof( sw::string ), uMapWrapper.getKeySize() );
	SW_EXPECT_EQUAL( sizeof( int32 ), uMapWrapper.getValueSize() );
}

// ------------------------------------------------------------------------------
// 7) Reflection_EnumBitFlag — 비트플래그 문자열
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_EnumBitFlag] 비트플래그 감지와 ToString
 */
SW_TEST_CASE( Reflection_EnumBitFlag, BitFlagDetectionAndToString )
{
	const sw::EnumInfo* info =
		sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyBitFlag" ) );

	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_TRUE( info->_bIsBitFlag );

	int64			  flagVal = static_cast<int64>( sw::DummyBitFlag::OptionA ) | static_cast<int64>( sw::DummyBitFlag::OptionC );
	sw::hashed_string flagStr = info->toStringFlags( flagVal );

	SW_EXPECT_EQUAL( sw::string( "OptionA | OptionC" ), sw::string( flagStr.c_str() ) );
}

/**
 * @brief [Reflection_EnumBitFlag] 문자열 플래그 → 값
 */
SW_TEST_CASE( Reflection_EnumBitFlag, StringFlagsToValue )
{
	const sw::EnumInfo* info =
		sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyBitFlag" ) );

	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	int64 val	   = info->stringFlagsToValue( "OptionB | OptionC" );
	int64 expected = static_cast<int64>( sw::DummyBitFlag::OptionB ) | static_cast<int64>( sw::DummyBitFlag::OptionC );

	SW_EXPECT_EQUAL( expected, val );
}

// ------------------------------------------------------------------------------
// 8) Reflection_Serialization — Binary/JSON/XML·버전
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Serialization] 바이너리 라운드트립
 */
SW_TEST_CASE( Reflection_Serialization, BinaryRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id		   = 999;
	src._title	   = "BinaryTest";
	src._flags	   = static_cast<int64>( sw::DummyBitFlag::OptionB );
	src._listScore = { 100, 200, 300 };

	sw::vector<uint8> buffer;
	sw::BinarySerializer::serialize( &src, *typeInfo, buffer );
	SW_EXPECT_TRUE( buffer.empty() == false );

	sw::ComplexData dst;
	bool			success = sw::BinarySerializer::deserialize( &dst, *typeInfo, buffer.data(), buffer.size() );

	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 999, dst._id );
	SW_EXPECT_EQUAL( sw::string( "BinaryTest" ), dst._title );
	SW_EXPECT_EQUAL( static_cast<int64>( sw::DummyBitFlag::OptionB ), dst._flags );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( dst._listScore.size() ) );
	if ( dst._listScore.size() == 3 )
	{
		SW_EXPECT_EQUAL( 100, dst._listScore[0] );
		SW_EXPECT_EQUAL( 300, dst._listScore[2] );
	}
}

/**
 * @brief [Reflection_Serialization] 압축 바이너리(CompressedBinarySerializer) 라운드트립
 */
SW_TEST_CASE( Reflection_Serialization, CompressedBinaryRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id		   = 888;
	src._title	   = "CompressedBinaryTest";
	src._flags	   = static_cast<int64>( sw::DummyBitFlag::OptionA ) | static_cast<int64>( sw::DummyBitFlag::OptionC );
	src._listScore = { 10, 20, 30, 40, 50 };

	// 1) RLE 압축 직렬화
	sw::vector<uint8> compBuffer;
	const bool		  bSerializeOk = sw::BinarySerializer::serializeCompressed( &src, *typeInfo, compBuffer, sw::CompressionCodecType::RLE );
	SW_EXPECT_TRUE( bSerializeOk );
	SW_EXPECT_TRUE( compBuffer.empty() == false );

	// 2) 압축 해제 및 역직렬화
	sw::ComplexData dst;
	const bool		bDeserializeOk = sw::BinarySerializer::deserializeCompressed( &dst, *typeInfo, compBuffer.data(), compBuffer.size() );
	SW_EXPECT_TRUE( bDeserializeOk );
	SW_EXPECT_EQUAL( 888, dst._id );
	SW_EXPECT_EQUAL( sw::string( "CompressedBinaryTest" ), dst._title );
	SW_EXPECT_EQUAL( src._flags, dst._flags );
	SW_EXPECT_EQUAL( 5u, static_cast<uint32>( dst._listScore.size() ) );
	if ( dst._listScore.size() == 5 )
	{
		SW_EXPECT_EQUAL( 10, dst._listScore[0] );
		SW_EXPECT_EQUAL( 50, dst._listScore[4] );
	}
}

/**
 * @brief [Reflection_Serialization] JSON 라운드트립
 */
SW_TEST_CASE( Reflection_Serialization, JsonRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id		   = 777;
	src._title	   = "JsonTest";
	src._listScore = { 5, 10, 15 };

	sw::string json = sw::JsonSerializer::serialize( &src, *typeInfo );
	SW_EXPECT_TRUE( json.empty() == false );

	sw::ComplexData dst;
	bool			success = sw::JsonSerializer::deserialize( &dst, *typeInfo, json );

	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 777, dst._id );
	SW_EXPECT_EQUAL( sw::string( "JsonTest" ), dst._title );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( dst._listScore.size() ) );
}

/**
 * @brief [Reflection_Serialization] XML 라운드트립
 */
SW_TEST_CASE( Reflection_Serialization, XmlRapidXmlRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id		   = 888;
	src._title	   = "XmlTest";
	src._listScore = { 1, 2, 3, 4 };

	sw::string xml = sw::XmlSerializer::serialize( &src, *typeInfo );
	SW_EXPECT_TRUE( xml.empty() == false );

	sw::ComplexData dst;
	bool			success = sw::XmlSerializer::deserialize( &dst, *typeInfo, xml );

	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 888, dst._id );
	SW_EXPECT_EQUAL( sw::string( "XmlTest" ), dst._title );
	SW_EXPECT_EQUAL( 4u, static_cast<uint32>( dst._listScore.size() ) );
}

struct SimpleXmlBackend : public sw::IXmlBackend
{
	sw::string								  _result;
	sw::string								  _rootTagName;
	sw::vector<sw::string>					  _listOpenTag;
	sw::unordered_map<sw::string, sw::string> _mapKv;
	bool									  _bOpenTagPending{ false };

	void closeOpenTag()
	{
		if ( _bOpenTagPending == false )
			return;
		_result += ">";
		_bOpenTagPending = false;
	}

	void beginNamedElement( const utf8* pTag )
	{
		closeOpenTag();
		sw::string tag( pTag != nullptr ? pTag : "" );
		_result += "<" + tag;
		_bOpenTagPending = true;
		_listOpenTag.push_back( tag );
	}

	void endNamedElement()
	{
		closeOpenTag();
		if ( _listOpenTag.empty() )
			return;
		_result += "</" + _listOpenTag.back() + ">";
		_listOpenTag.pop_back();
	}

	void initXmlSerialization( const utf8* pRootTag ) override
	{
		_rootTagName	 = pRootTag != nullptr ? pRootTag : "";
		_result			 = "<" + _rootTagName;
		_bOpenTagPending = true;
	}
	void writeValue( const utf8* pTag, const utf8* pValue ) override
	{
		closeOpenTag();
		_result += "<" + sw::string( pTag ) + ">" + sw::string( pValue ) + "</" + sw::string( pTag ) + ">";
		_mapKv[sw::string( pTag )] = pValue != nullptr ? pValue : "";
	}
	void writeAttribute( const utf8* pAttr, const utf8* pValue ) override
	{
		_result += " " + sw::string( pAttr ) + "=\"" + sw::string( pValue ) + "\"";
		_mapKv[sw::string( pAttr )] = pValue != nullptr ? pValue : "";
	}
	void beginArray( const utf8* pTag ) override
	{
		beginNamedElement( pTag );
	}
	void writeArrayItem( const utf8* pValue ) override
	{
		closeOpenTag();
		_result += "<item>" + sw::string( pValue ) + "</item>";
	}
	void endArray() override
	{
		endNamedElement();
	}
	void beginMap( const utf8* pTag ) override
	{
		beginNamedElement( pTag );
	}
	void beginMapEntry() override
	{
		beginNamedElement( "entry" );
	}
	void writeMapKey( const utf8* pKey ) override
	{
		closeOpenTag();
		_result += "<key>" + sw::string( pKey ) + "</key>";
	}
	void writeMapValue( const utf8* pValue ) override
	{
		closeOpenTag();
		_result += "<value>" + sw::string( pValue ) + "</value>";
	}
	void endMapEntry() override
	{
		endNamedElement();
	}
	void endMap() override
	{
		endNamedElement();
	}
	sw::string endSerialize() override
	{
		closeOpenTag();
		if ( _rootTagName.empty() == false )
			_result += "</" + _rootTagName + ">";
		return _result;
	}

	bool initXmlDeserialization( const utf8* pXmlStr, const utf8* pRootTag ) override
	{
		(void)pRootTag;
		sw::string str( pXmlStr );
		size_t	   pos{ 0 };
		while ( ( pos = str.find( '<', pos ) ) != sw::string::npos )
		{
			size_t closeTag = str.find( '>', pos );
			if ( closeTag == sw::string::npos )
				break;
			sw::string tag = str.substr( pos + 1, closeTag - pos - 1 );
			if ( tag.empty() == false && tag[0] != '/' )
			{
				if ( tag.back() == '/' )
					tag.pop_back();
				while ( tag.empty() == false && tag.back() == ' ' )
					tag.pop_back();

				size_t spacePos = tag.find( ' ' );
				if ( spacePos != sw::string::npos )
				{
					sw::string attrs = tag.substr( spacePos + 1 );
					tag				 = tag.substr( 0, spacePos );
					size_t attrPos{ 0 };
					while ( attrPos < attrs.size() )
					{
						while ( attrPos < attrs.size() && attrs[attrPos] == ' ' )
							++attrPos;
						size_t eqPos = attrs.find( '=', attrPos );
						if ( eqPos == sw::string::npos )
							break;
						sw::string attrName = attrs.substr( attrPos, eqPos - attrPos );
						size_t	   valBegin = eqPos + 1;
						if ( valBegin < attrs.size() && attrs[valBegin] == '"' )
						{
							++valBegin;
							size_t valEnd = attrs.find( '"', valBegin );
							if ( valEnd == sw::string::npos )
								break;
							_mapKv[attrName] = attrs.substr( valBegin, valEnd - valBegin );
							attrPos			 = valEnd + 1;
						}
						else
							break;
					}
				}

				size_t endTagPos = str.find( "</" + tag + ">", closeTag );
				if ( endTagPos != sw::string::npos )
				{
					sw::string val = str.substr( closeTag + 1, endTagPos - closeTag - 1 );
					_mapKv[tag]	   = val;
				}
			}
			pos = closeTag + 1;
		}
		return true;
	}
	bool readValue( const utf8* pTag, sw::string& outValue ) override
	{
		auto it = _mapKv.find( sw::string( pTag ) );
		if ( it != _mapKv.end() )
		{
			outValue = it->second;
			return true;
		}
		return false;
	}
	bool readAttribute( const utf8* pAttr, sw::string& outValue ) override
	{
		return readValue( pAttr, outValue );
	}
	bool iterateArray( const utf8*, const sw::XmlArrayItemDelegate& ) override
	{
		return false;
	}
	bool iterateMap( const utf8*, const sw::XmlMapItemDelegate& ) override
	{
		return false;
	}
};

/**
 * @brief [Reflection_Serialization] XML 어트리뷰트 라운드트립
 */
SW_TEST_CASE( Reflection_Serialization, XmlAttributeRoundtrip )
{
	sw::XmlDocumentBackend backend;
	backend.initXmlSerialization( "AttrRoot" );
	backend.writeAttribute( "_id", "42" );
	backend.writeAttribute( "_title", "AttrTitle" );
	backend.writeValue( "_note", "child-element" );
	const sw::string xml = backend.endSerialize();
	SW_EXPECT_TRUE( xml.find( "_id=\"42\"" ) != sw::string::npos );
	SW_EXPECT_TRUE( xml.find( "_title=\"AttrTitle\"" ) != sw::string::npos );
	SW_EXPECT_TRUE( xml.find( "<_note>" ) != sw::string::npos );

	sw::XmlDocumentBackend reader;
	SW_EXPECT_TRUE( reader.initXmlDeserialization( xml.c_str(), "AttrRoot" ) );
	sw::string id, title, note;
	SW_EXPECT_TRUE( reader.readAttribute( "_id", id ) );
	SW_EXPECT_TRUE( reader.readAttribute( "_title", title ) );
	SW_EXPECT_TRUE( reader.readValue( "_note", note ) );
	SW_EXPECT_EQUAL( sw::string( "42" ), id );
	SW_EXPECT_EQUAL( sw::string( "AttrTitle" ), title );
	SW_EXPECT_EQUAL( sw::string( "child-element" ), note );
	SW_EXPECT_TRUE( reader.readValueOrAttribute( "_id", id ) );
};

/**
 * @brief [Reflection_Serialization] XML/JSON 키 대소문자 무시, 값은 유지
 */
SW_TEST_CASE( Reflection_Serialization, XmlJsonKeysIgnoreCaseValuesPreserveCase )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	// 프로퍼티 키/태그는 대소문자가 달라도 되고, 문자열 값은 대소문자를 유지해야 한다.
	const utf8* json =
		R"({"_ID":77,"_TITLE":"CaseSensitiveValue","vector":[{"_name":"_listScore","item":[1,2]}]})";
	sw::ComplexData fromJson;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &fromJson, *typeInfo, json ) );
	SW_EXPECT_EQUAL( 77, fromJson._id );
	SW_EXPECT_EQUAL( sw::string( "CaseSensitiveValue" ), fromJson._title );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( fromJson._listScore.size() ) );

	const utf8* xml =
		R"(<sw__ComplexData _ID="88" _TITLE="XmlCaseValue"><vector _name="_listScore"><item>3</item><ITEM>4</ITEM></vector></sw__ComplexData>)";
	sw::ComplexData fromXml;
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &fromXml, *typeInfo, xml ) );
	SW_EXPECT_EQUAL( 88, fromXml._id );
	SW_EXPECT_EQUAL( sw::string( "XmlCaseValue" ), fromXml._title );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( fromXml._listScore.size() ) );
	SW_EXPECT_EQUAL( 3, fromXml._listScore[0] );
	SW_EXPECT_EQUAL( 4, fromXml._listScore[1] );

	SW_EXPECT_EQUAL( sw::string( "CaseSensitiveValue" ),
					 sw::JsonSerializer::extractStringField( json, "_title" ) );
	SW_EXPECT_TRUE( sw::JsonSerializer::extractStringField( json, "_title", false ).empty() );

	// 옵트아웃: 대소문자 구분 키 조회는 다른 대소문자를 바인딩하면 안 된다.
	sw::SerializeContext strictCtx = sw::SerializeContext::getDefault();
	strictCtx.setIgnoreCaseKeys( false );

	sw::ComplexData strictJson;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &strictJson, *typeInfo, json, strictCtx ) );
	SW_EXPECT_EQUAL( 101, strictJson._id ); // ComplexData 기본값, 77 아님
	SW_EXPECT_EQUAL( sw::string( "HeroData" ), strictJson._title );

	sw::ComplexData strictXml;
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &strictXml, *typeInfo, xml, strictCtx ) );
	SW_EXPECT_EQUAL( 101, strictXml._id );
	SW_EXPECT_EQUAL( sw::string( "HeroData" ), strictXml._title );
	SW_EXPECT_EQUAL( 1u, static_cast<uint32>( strictXml._listScore.size() ) ); // 정확 일치 "item"만, "ITEM" 제외
	SW_EXPECT_EQUAL( 3, strictXml._listScore[0] );
};

/**
 * @brief [Reflection_Serialization] JSON 시퀀스 컨테이너를 평범한 배열 표현으로도 읽는다(손으로 쓴 Config 등).
 */
SW_TEST_CASE( Reflection_Serialization, JsonSequenceAcceptsPlainArray )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_ASSERT_TRUE( typeInfo != nullptr );

	// 자연스러운 형식: "_listScore": [10, 20, 30] (직렬화기가 쓰는 {"vector":[{...}]} 래핑이 아님)
	sw::ComplexData plain;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &plain, *typeInfo, R"({"_id":9,"_listScore":[10,20,30]})" ) );
	SW_EXPECT_EQUAL( 9, plain._id );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( plain._listScore.size() ) );
	SW_EXPECT_EQUAL( 10, plain._listScore[0] );
	SW_EXPECT_EQUAL( 30, plain._listScore[2] );

	// 빈 배열도 유효하다.
	sw::ComplexData empty;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &empty, *typeInfo, R"({"_listScore":[]})" ) );
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( empty._listScore.size() ) );

	// 잘못된 원소 타입은 여전히 실패한다.
	sw::ComplexData bad;
	SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &bad, *typeInfo, R"({"_listScore":[1,"nope"]})" ) );
};

/**
 * @brief [Reflection_Serialization] JSON/XML 엄격 역직렬화가 잘못된 컨테이너·필드 coerce 에서 실패
 */
SW_TEST_CASE( Reflection_Serialization, StrictDeserializeFailsOnBadContainerAndField )
{
	SW_TEST_DEFENSIVE_SCOPE( "Testing strict deserialization failure on bad containers and fields" );
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_ASSERT_TRUE( typeInfo != nullptr );

	sw::ComplexData jsonContainer;
	SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &jsonContainer, *typeInfo, R"({"vector":[{"_name":"_listScore","item":[1,"not_an_int"]}]})" ) );

	sw::ComplexData jsonField;
	SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &jsonField, *typeInfo, R"({"_id":"not_an_int"})" ) );

	sw::ComplexData xmlField;
	SW_EXPECT_FALSE( sw::XmlSerializer::deserialize(
		&xmlField, *typeInfo, R"(<sw__ComplexData _id="not_an_int"/>)" ) );

	sw::ComplexData xmlContainer;
	SW_EXPECT_FALSE( sw::XmlSerializer::deserialize(
		&xmlContainer, *typeInfo,
		R"(<sw__ComplexData><vector _name="_listScore"><item>1</item><item>not_an_int</item></vector></sw__ComplexData>)" ) );

	sw::ComplexData jsonOk;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &jsonOk, *typeInfo, R"({"_id":7,"vector":[{"_name":"_listScore","item":[1,2]}]})" ) );
	SW_EXPECT_EQUAL( 7, jsonOk._id );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( jsonOk._listScore.size() ) );

	sw::ComplexData jsonMalformed;
	SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &jsonMalformed, *typeInfo, R"({"not_a_pair","_id":1})" ) );

	sw::ComplexData jsonUnknown;
	SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &jsonUnknown, *typeInfo, R"({"_id":1,"NotARealField":2})" ) );

	sw::ComplexData xmlUnknown;
	SW_EXPECT_FALSE( sw::XmlSerializer::deserialize(
		&xmlUnknown, *typeInfo,
		R"(<sw__ComplexData _id="1" NotARealField="x"/>)" ) );

	const utf8* xmlUnknownStr =
		R"(<sw__ComplexData _id="1" NotARealField="x"/>)";
	sw::XmlDocumentBackend xmlBackend;
	sw::ComplexData		   xmlBackendUnknown;
	SW_EXPECT_FALSE( sw::XmlSerializer::deserialize( &xmlBackendUnknown, *typeInfo, xmlBackend, xmlUnknownStr ) );

	sw::ComplexData binSrc;
	binSrc._id = 3;
	sw::vector<uint8> binBuf;
	sw::BinarySerializer::serialize( &binSrc, *typeInfo, binBuf );
	SW_ASSERT_TRUE( binBuf.size() >= sizeof( uint32 ) );
	uint32 propCount{ 0 };
	sw::Memory::copy( &propCount, binBuf.data(), sizeof( uint32 ) );
	propCount += 1;
	sw::Memory::copy( binBuf.data(), &propCount, sizeof( uint32 ) );
	const uint32 unknownTag	 = 0xDEADBEEFu;
	const uint32 unknownWire = 0;
	const uint32 unknownSize = 0;
	const uint8* tagBytes	 = reinterpret_cast<const uint8*>( &unknownTag );
	const uint8* wireBytes	 = reinterpret_cast<const uint8*>( &unknownWire );
	const uint8* sizeBytes	 = reinterpret_cast<const uint8*>( &unknownSize );
	binBuf.insert( binBuf.end(), tagBytes, tagBytes + sizeof( uint32 ) );
	binBuf.insert( binBuf.end(), wireBytes, wireBytes + sizeof( uint32 ) );
	binBuf.insert( binBuf.end(), sizeBytes, sizeBytes + sizeof( uint32 ) );
	sw::ComplexData binUnknown;
	SW_EXPECT_FALSE( sw::BinarySerializer::deserialize( &binUnknown, *typeInfo, binBuf.data(), binBuf.size() ) );
	sw::vector<sw::SchemaOrphanValue> binOrphans;
	SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &binUnknown, *typeInfo, binBuf.data(), binBuf.size(),
														   &binOrphans ) );
	SW_EXPECT_TRUE( binOrphans.empty() == false );
}

/**
 * @brief [Reflection_Serialization] 커스텀 XML 백엔드
 */
SW_TEST_CASE( Reflection_Serialization, CustomXmlBackend )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id	   = 999;
	src._title = "CustomBackend";

	SimpleXmlBackend backend;
	sw::string		 xml = sw::XmlSerializer::serialize( &src, *typeInfo, backend );
	SW_EXPECT_TRUE( xml.empty() == false );

	sw::ComplexData	 dst;
	SimpleXmlBackend readBackend;
	bool			 success = sw::XmlSerializer::deserialize( &dst, *typeInfo, readBackend, xml );
	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 999, dst._id );
	SW_EXPECT_EQUAL( sw::string( "CustomBackend" ), dst._title );
}

/**
 * @brief [Reflection_Serialization] 커스텀 SerializeContext
 */
SW_TEST_CASE( Reflection_Serialization, CustomSerializeContext )
{
	sw::SerializeContext customCtx = sw::SerializeContext::getDefault();

	customCtx.registerTextHandler(
		sw::hashed_string( "int32" ),
		[]( const void* pPtr )
	{ return sw::to_string( ( *static_cast<const int32*>( pPtr ) ) * 10 ); },
		[]( void* pPtr, std::string_view s )
	{
		int32 val{ 0 };
		sw::StringUtil::parseInt( s, val );
		*static_cast<int32*>( pPtr ) = val / 10;
		return true;
	} );

	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id = 50;

	sw::string json = sw::JsonSerializer::serialize( &src, *typeInfo, customCtx );
	SW_EXPECT_TRUE( json.find( "\"_id\":500" ) != sw::string::npos );

	sw::ComplexData dst;
	bool			success = sw::JsonSerializer::deserialize( &dst, *typeInfo, json, customCtx );
	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 50, dst._id );
}

// ------------------------------------------------------------------------------
// 9) Reflection_EnumInfo — 등록·값↔문자열
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_EnumInfo] 등록된 enum 조회
 */
SW_TEST_CASE( Reflection_EnumInfo, FindRegisteredEnum )
{
	const sw::EnumInfo* info =
		sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyType" ) );

	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_EQUAL( sw::string( "DummyType" ), sw::string( info->_name.c_str() ) );
}

/**
 * @brief [Reflection_EnumInfo] 값 → 문자열
 */
SW_TEST_CASE( Reflection_EnumInfo, ValueToString )
{
	const sw::EnumInfo* info =
		sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyType" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
	{
		return;
	}

	SW_EXPECT_EQUAL( sw::string( "None" ), sw::string( info->toString( 0 ).c_str() ) );
	SW_EXPECT_EQUAL( sw::string( "TypeA" ), sw::string( info->toString( 1 ).c_str() ) );
	SW_EXPECT_EQUAL( sw::string( "TypeB" ), sw::string( info->toString( 2 ).c_str() ) );
}

/**
 * @brief [Reflection_EnumInfo] 잘못된 값은 기본값
 */
SW_TEST_CASE( Reflection_EnumInfo, InvalidValueReturnsDefault )
{
	const sw::EnumInfo* info =
		sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyType" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
	{
		return;
	}

	sw::hashed_string name = info->toString( 999 );
	SW_EXPECT_TRUE( name == sw::hashed_string() );
}

// ------------------------------------------------------------------------------
// 10) Reflection_InnerTypes — 중첩 struct/class/enum
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_InnerTypes] 외부 구조체 조회
 */
SW_TEST_CASE( Reflection_InnerTypes, FindOuterStruct )
{
	const sw::TypeInfo* typeInfoFqn =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct" ) );
	SW_EXPECT_TRUE( typeInfoFqn != nullptr );

	const sw::TypeInfo* typeInfoShort =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "OuterStruct" ) );
	SW_EXPECT_TRUE( typeInfoShort != nullptr );

	if ( typeInfoFqn == nullptr )
	{
		return;
	}

	sw::InnerNamespaceForTest::OuterStruct instance;
	instance._outerValue = 123;

	const sw::PropertyInfo* prop = typeInfoFqn->findProperty( sw::hashed_string( "_outerValue" ) );
	SW_EXPECT_TRUE( prop != nullptr );
	if ( prop != nullptr )
	{
		SW_EXPECT_EQUAL( 123, *prop->getValuePtr<int32>( &instance ) );
	}
}

/**
 * @brief [Reflection_InnerTypes] 내부 구조체 조회
 */
SW_TEST_CASE( Reflection_InnerTypes, FindInnerStruct )
{
	const sw::TypeInfo* typeInfoFqn =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerStruct" ) );
	SW_EXPECT_TRUE( typeInfoFqn != nullptr );

	const sw::TypeInfo* typeInfoShort =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "InnerStruct" ) );
	SW_EXPECT_TRUE( typeInfoShort != nullptr );

	if ( typeInfoFqn == nullptr )
	{
		return;
	}

	sw::InnerNamespaceForTest::OuterStruct::InnerStruct instance;
	instance._innerData = "TestNested";
	instance._score		= 9.5f;

	const sw::PropertyInfo* propData  = typeInfoFqn->findProperty( sw::hashed_string( "_innerData" ) );
	const sw::PropertyInfo* propScore = typeInfoFqn->findProperty( sw::hashed_string( "_score" ) );
	SW_EXPECT_TRUE( propData != nullptr );
	SW_EXPECT_TRUE( propScore != nullptr );

	if ( propData && propScore )
	{
		SW_EXPECT_EQUAL( sw::string( "TestNested" ), *propData->getValuePtr<sw::string>( &instance ) );
		SW_EXPECT_NEAR_EQUAL( 9.5f, *propScore->getValuePtr<float32>( &instance ), 0.001f );
	}
}

/**
 * @brief [Reflection_InnerTypes] 내부 클래스 조회
 */
SW_TEST_CASE( Reflection_InnerTypes, FindInnerClass )
{
	const sw::TypeInfo* typeInfoFqn =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerClass" ) );
	SW_EXPECT_TRUE( typeInfoFqn != nullptr );

	const sw::TypeInfo* typeInfoShort =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "InnerClass" ) );
	SW_EXPECT_TRUE( typeInfoShort != nullptr );

	if ( typeInfoFqn == nullptr )
		return;

	sw::InnerNamespaceForTest::OuterStruct::InnerClass instance;
	const sw::PropertyInfo*							   propId = typeInfoFqn->findProperty( sw::hashed_string( "_id" ) );
	SW_EXPECT_TRUE( propId != nullptr );
	if ( propId != nullptr )
	{
		int64 newId = 8888;
		propId->setValue( &instance, newId );
		SW_EXPECT_EQUAL( 8888, *propId->getValuePtr<int64>( &instance ) );
	}
}

/**
 * @brief [Reflection_InnerTypes] 내부 enum 조회
 */
SW_TEST_CASE( Reflection_InnerTypes, FindInnerEnum )
{
	const sw::EnumInfo* enumInfoFqn =
		sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerEnum" ) );
	SW_EXPECT_TRUE( enumInfoFqn != nullptr );

	const sw::EnumInfo* enumInfoShort =
		sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "InnerEnum" ) );
	SW_EXPECT_TRUE( enumInfoShort != nullptr );

	if ( enumInfoFqn == nullptr )
		return;

	SW_EXPECT_TRUE( enumInfoFqn->_bIsBitFlag );
	uint32	   combined = 1 | 4;
	sw::string flagStr	= enumInfoFqn->toStringFlags( combined ).c_str();
	SW_EXPECT_TRUE( flagStr.find( "OptionA" ) != sw::string::npos );
	SW_EXPECT_TRUE( flagStr.find( "OptionC" ) != sw::string::npos );
}

/**
 * @brief [Reflection_InnerTypes] 내부 구조체 직렬화 라운드트립
 */
SW_TEST_CASE( Reflection_InnerTypes, InnerStructSerializationRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerStruct" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::InnerNamespaceForTest::OuterStruct::InnerStruct src;
	src._innerData = "SerializationTest";
	src._score	   = 12.34f;

	sw::string json = sw::JsonSerializer::serialize( &src, *typeInfo );
	SW_EXPECT_TRUE( json.empty() == false );

	sw::InnerNamespaceForTest::OuterStruct::InnerStruct dstJson;
	bool												jsonOk = sw::JsonSerializer::deserialize( &dstJson, *typeInfo, json );
	SW_EXPECT_TRUE( jsonOk );
	SW_EXPECT_EQUAL( sw::string( "SerializationTest" ), dstJson._innerData );
	SW_EXPECT_NEAR_EQUAL( 12.34f, dstJson._score, 0.01f );

	sw::string xml = sw::XmlSerializer::serialize( &src, *typeInfo );
	SW_EXPECT_TRUE( xml.empty() == false );

	sw::InnerNamespaceForTest::OuterStruct::InnerStruct dstXml;
	bool												xmlOk = sw::XmlSerializer::deserialize( &dstXml, *typeInfo, xml );
	SW_EXPECT_TRUE( xmlOk );
	SW_EXPECT_EQUAL( sw::string( "SerializationTest" ), dstXml._innerData );
	SW_EXPECT_NEAR_EQUAL( 12.34f, dstXml._score, 0.01f );
}

// ------------------------------------------------------------------------------
// 11) Reflection_Serialization — Binary/JSON/XML·버전
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Serialization] 누락 필드의 PROPERTY Default
 */
SW_TEST_CASE( Reflection_Serialization, PropertyDefaultOnMissing )
{
	struct DefaultActor
	{
		int32	   _mana{ 0 };
		sw::string _title = "unset";
	};

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "DefaultActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::DefaultActor" );
	info._size				 = sizeof( DefaultActor );

	sw::PropertyInfo manaProp( sw::hashed_string( "_mana" ), sw::hashed_string( "int32" ),
							   SW_OFFSET_OF( DefaultActor, _mana ) );
	manaProp._metadata._defaultValue = "75";
	sw::PropertyInfo titleProp( sw::hashed_string( "_title" ), sw::hashed_string( "string" ),
								SW_OFFSET_OF( DefaultActor, _title ) );
	titleProp._metadata._defaultValue  = "Apprentice";
	titleProp._metadata._bXmlAttribute = 1;
	info._listProperty				   = { manaProp, titleProp };

	DefaultActor actor;
	const utf8*	 emptyXml = R"(<?xml version="1.0"?><DefaultActor></DefaultActor>)";
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &actor, info, emptyXml ) );
	SW_EXPECT_EQUAL( 75, actor._mana );
	SW_EXPECT_EQUAL( sw::string( "Apprentice" ), actor._title );

	DefaultActor jsonActor;
	jsonActor._mana	 = 1;
	jsonActor._title = "x";
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &jsonActor, info, "{}" ) );
	SW_EXPECT_EQUAL( 75, jsonActor._mana );
	SW_EXPECT_EQUAL( sw::string( "Apprentice" ), jsonActor._title );

	// 에셋의 명시 값이 Default 메타데이터보다 우선한다.
	DefaultActor overridden;
	const utf8*	 filledXml =
		R"(<?xml version="1.0"?><DefaultActor _title="Mage" _mana="10"/>)";
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &overridden, info, filledXml ) );
	SW_EXPECT_EQUAL( 10, overridden._mana );
	SW_EXPECT_EQUAL( sw::string( "Mage" ), overridden._title );
}

/**
 * @brief [Reflection_Serialization] 프로퍼티 Alias 와 재정렬
 */
SW_TEST_CASE( Reflection_Serialization, PropertyAliasAndReorderingTest )
{
	struct AliasTestActor
	{
		int32 _currentHp = 100;
	};

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "AliasTestActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::AliasTestActor" );
	info._size				 = sizeof( AliasTestActor );
	info._listProperty		 = {
		{ sw::hashed_string( "_currentHp" ), sw::hashed_string( "int32" ),
		  SW_OFFSET_OF( AliasTestActor, _currentHp ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr, sw::hashed_string( "hp" ) }
	 };

	sw::string	   oldJson = "{\"hp\": 250}";
	AliasTestActor actor;
	bool		   jsonOk = sw::JsonSerializer::deserialize( &actor, info, oldJson );
	SW_EXPECT_TRUE( jsonOk );
	SW_EXPECT_EQUAL( 250, actor._currentHp );

	AliasTestActor xmlActor;
	xmlActor._currentHp = 100;
	const utf8* oldXml	= R"(<?xml version="1.0"?><AliasTestActor hp="250"/>)";
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &xmlActor, info, oldXml ) );
	SW_EXPECT_EQUAL( 250, xmlActor._currentHp );

	// 복수 Alias (codegen AliasAndReorderTestActor: hp + HitPoints)
	const sw::TypeInfo* multiAliasInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AliasAndReorderTestActor" ) );
	SW_ASSERT_NOT_NULL( multiAliasInfo );
	sw::AliasAndReorderTestActor multi{};
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &multi, *multiAliasInfo, R"({"HitPoints":33,"_score":1})" ) );
	SW_EXPECT_EQUAL( 33, multi._currentHp );

	// PROPERTY(Default) 없는 누락 필드는 생성 시 값을 유지한다.
	AliasTestActor missingActor;
	missingActor._currentHp = 42;
	const utf8* emptyXml	= R"(<?xml version="1.0"?><AliasTestActor></AliasTestActor>)";
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &missingActor, info, emptyXml ) );
	SW_EXPECT_EQUAL( 42, missingActor._currentHp );

	struct ReorderActor1
	{
		int32 _fieldA = 10;
		int32 _fieldB = 20;
	};

	sw::TypeInfo info1;
	info1._name				  = sw::hashed_string( "ReorderActor" );
	info1._fullyQualifiedName = sw::hashed_string( "sw::ReorderActor" );
	info1._size				  = sizeof( ReorderActor1 );
	info1._listProperty		  = {
		{sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ),
		  SW_OFFSET_OF( ReorderActor1, _fieldA ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		{sw::hashed_string( "_fieldB" ), sw::hashed_string( "int32" ),
		  SW_OFFSET_OF( ReorderActor1, _fieldB ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
	  };

	ReorderActor1	  src;
	sw::vector<uint8> binBuf;
	sw::BinarySerializer::serialize( &src, info1, binBuf );

	sw::TypeInfo infoReordered = info1;
	std::swap( infoReordered._listProperty[0], infoReordered._listProperty[1] );

	ReorderActor1 dst;
	dst._fieldA = 0;
	dst._fieldB = 0;
	bool binOk	= sw::BinarySerializer::deserialize( &dst, infoReordered, binBuf.data(), binBuf.size() );
	SW_EXPECT_TRUE( binOk );
	SW_EXPECT_EQUAL( 10, dst._fieldA );
	SW_EXPECT_EQUAL( 20, dst._fieldB );
}

// ------------------------------------------------------------------------------
// 12) Reflection_TypeRegistry — 등록·조회·별칭·builtins
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_TypeRegistry] REFLECT(Alias) / ENUM(Alias) codegen 등록
 */
SW_TEST_CASE( Reflection_TypeRegistry, TypeAndEnumAliasLookup )
{
	const sw::TypeInfo* canonical =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::RenameCompatActor" ) );
	SW_ASSERT_NOT_NULL( canonical );

	const sw::TypeInfo* viaAlias =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::LegacyRenameActor" ) );
	SW_ASSERT_NOT_NULL( viaAlias );
	SW_EXPECT_EQUAL( canonical->_typeId, viaAlias->_typeId );
	SW_EXPECT_TRUE( viaAlias->_fullyQualifiedName == sw::hashed_string( "sw::RenameCompatActor" ) );

	const sw::EnumInfo* enumCanonical =
		sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::SampleStatus" ) );
	SW_ASSERT_NOT_NULL( enumCanonical );
	const sw::EnumInfo* enumAlias =
		sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::LegacySampleStatus" ) );
	SW_ASSERT_NOT_NULL( enumAlias );
	SW_EXPECT_TRUE( enumAlias->_fullyQualifiedName == sw::hashed_string( "sw::SampleStatus" ) );

	// enumerator ValueAlias: OldIdle → Idle 값
	SW_EXPECT_EQUAL( enumCanonical->stringFlagsToValue( "Idle" ), enumCanonical->stringFlagsToValue( "OldIdle" ) );
	SW_EXPECT_EQUAL( enumCanonical->stringFlagsToValue( "Moving" ),
					 enumCanonical->stringFlagsToValue( "OldMoving" ) );
}

// ------------------------------------------------------------------------------
// 13) Reflection_Serialization — Binary/JSON/XML·버전
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Serialization] 타입 Alias + 옛 XML 루트 태그로 로드
 */
SW_TEST_CASE( Reflection_Serialization, TypeAliasXmlLoad )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::LegacyRenameActor" ) );
	SW_ASSERT_NOT_NULL( typeInfo );

	sw::RenameCompatActor actor;
	actor._hp		   = 1;
	const utf8* oldXml = R"(<?xml version="1.0"?><LegacyRenameActor _hp="77"/>)";
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &actor, *typeInfo, oldXml ) );
	SW_EXPECT_EQUAL( 77, actor._hp );
}

/**
 * @brief [Reflection_Serialization] 필드 추가/삭제/개명 레이아웃 진화
 */
SW_TEST_CASE( Reflection_Serialization, LayoutEvolveAddRemoveRename )
{
	struct LayoutV1
	{
		int32 _hp{ 0 };
		int32 _score{ 0 };
	};

	struct LayoutV2
	{
		int32 _hp{ 0 };
		int32 _mana{ 0 }; ///< V1에 없음 — Default 적용
	};

	sw::TypeInfo infoV1;
	infoV1._name			   = sw::hashed_string( "LayoutActor" );
	infoV1._fullyQualifiedName = sw::hashed_string( "sw::LayoutActor" );
	infoV1._size			   = sizeof( LayoutV1 );
	infoV1._listProperty	   = {
		{	  sw::hashed_string( "_hp" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( LayoutV1,	_hp )},
		{sw::hashed_string( "_score" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( LayoutV1, _score )},
	};

	sw::PropertyInfo hpV2( sw::hashed_string( "_hp" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( LayoutV2, _hp ),
						   false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr,
						   sw::hashed_string( "health" ) ); ///< 개명: 옛 키 health
	sw::PropertyInfo manaV2( sw::hashed_string( "_mana" ), sw::hashed_string( "int32" ),
							 SW_OFFSET_OF( LayoutV2, _mana ) );
	manaV2._metadata._defaultValue = "9";

	sw::TypeInfo infoV2;
	infoV2._name			   = sw::hashed_string( "LayoutActor" );
	infoV2._fullyQualifiedName = sw::hashed_string( "sw::LayoutActor" );
	infoV2._size			   = sizeof( LayoutV2 );
	infoV2._listProperty	   = { hpV2, manaV2 };

	// --- Binary: V1 blob → V2 (score 스킵, mana Default, hp 매칭) ---
	LayoutV1		  v1{ 40, 99 };
	sw::vector<uint8> bin;
	sw::BinarySerializer::serialize( &v1, infoV1, bin );

	LayoutV2 fromBin{};
	fromBin._hp	  = -1;
	fromBin._mana = -1;
	sw::vector<sw::SchemaOrphanValue> binOrphans;
	SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &fromBin, infoV2, bin.data(), bin.size(), &binOrphans ) );
	SW_EXPECT_EQUAL( 40, fromBin._hp );
	SW_EXPECT_EQUAL( 9, fromBin._mana );
	SW_EXPECT_TRUE( binOrphans.empty() == false );

	// --- XML: 옛 필드명 health + 제거된 score + 신규 mana 누락 ---
	LayoutV2 fromXml{};
	fromXml._hp	  = -1;
	fromXml._mana = -1;
	const utf8* oldXml =
		R"(<?xml version="1.0"?><LayoutActor health="55" _score="1"/>)";
	sw::vector<sw::SchemaOrphanValue> xmlOrphans;
	SW_EXPECT_TRUE( sw::XmlSerializer::deserializeSoft( &fromXml, infoV2, oldXml, &xmlOrphans ) );
	SW_EXPECT_EQUAL( 55, fromXml._hp );
	SW_EXPECT_EQUAL( 9, fromXml._mana );
	SW_EXPECT_TRUE( xmlOrphans.empty() == false );

	// --- JSON: 동일 ---
	LayoutV2 fromJson{};
	fromJson._hp   = -1;
	fromJson._mana = -1;
	sw::vector<sw::SchemaOrphanValue> jsonOrphans;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserializeSoft( &fromJson, infoV2, R"({"health":66,"_score":2})", &jsonOrphans ) );
	SW_EXPECT_EQUAL( 66, fromJson._hp );
	SW_EXPECT_EQUAL( 9, fromJson._mana );
	SW_EXPECT_TRUE( jsonOrphans.empty() == false );

	// --- 필드 타입명 별칭(int32 → int32)으로 텍스트 파싱 ---
	struct IntAliasHolder
	{
		int32 _v{ 0 };
	};
	sw::TypeInfo intAliasInfo;
	intAliasInfo._name				 = sw::hashed_string( "IntAliasHolder" );
	intAliasInfo._fullyQualifiedName = sw::hashed_string( "sw::IntAliasHolder" );
	intAliasInfo._size				 = sizeof( IntAliasHolder );
	intAliasInfo._listProperty		 = {
		{ sw::hashed_string( "_v" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( IntAliasHolder, _v ) }
	};
	IntAliasHolder holder{};
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize(
		&holder, intAliasInfo, R"(<?xml version="1.0"?><IntAliasHolder _v="123"/>)" ) );
	SW_EXPECT_EQUAL( 123, holder._v );
}

// ------------------------------------------------------------------------------
// 14) Reflection_TypeInfo — 프로퍼티·isA·생성자
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_TypeInfo] PropertyInfo 이름 매칭
 */
SW_TEST_CASE( Reflection_TypeInfo, PropertyInfoMatchesName )
{
	sw::PropertyInfo prop;
	prop._name		= sw::hashed_string( "_currentHp" );
	prop._listAlias = { sw::hashed_string( "hp" ), sw::hashed_string( "HitPoints" ) };

	SW_EXPECT_TRUE( prop.matchesName( sw::hashed_string( "_currentHp" ) ) );
	SW_EXPECT_TRUE( prop.matchesName( sw::hashed_string( "hp" ) ) );
	SW_EXPECT_TRUE( prop.matchesName( sw::hashed_string( "HitPoints" ) ) );
	SW_EXPECT_FALSE( prop.matchesName( sw::hashed_string( "mana" ) ) );
}

// ------------------------------------------------------------------------------
// 15) Reflection_EnumInfo — 등록·값↔문자열
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_EnumInfo] EnumInfo 플래그 문자열 변환
 */
SW_TEST_CASE( Reflection_EnumInfo, EnumInfoFlagsStringConversion )
{
	sw::EnumInfo info;
	info._name			 = sw::hashed_string( "ESampleFlags" );
	info._bIsBitFlag	 = SW_TRUE;
	info._mapNameToValue = {
		{ sw::hashed_string( "None" ), 0},
		{sw::hashed_string( "FlagA" ), 1},
		{sw::hashed_string( "FlagB" ), 2},
		{sw::hashed_string( "FlagC" ), 4}
	};
	info._mapValueToName = {
		{0,	 sw::hashed_string( "None" )},
		{1, sw::hashed_string( "FlagA" )},
		{2, sw::hashed_string( "FlagB" )},
		{4, sw::hashed_string( "FlagC" )}
	};

	int64 val = info.stringFlagsToValue( "FlagA | FlagC" );
	SW_EXPECT_EQUAL( 5, val );

	sw::hashed_string flagsStr = info.toStringFlags( 5 );
	SW_EXPECT_TRUE( flagsStr.empty() == false );
}

struct TestBindingActor
{
	uint32 _score{ 0 };
};

// ------------------------------------------------------------------------------
// 16) Reflection_Binding — 양방향 프로퍼티 바인딩
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Binding] 양방향 프로퍼티 바인딩
 */
SW_TEST_CASE( Reflection_Binding, BiDirectionalPropertyBinding )
{
	sw::PropertyInfo prop( sw::hashed_string( "score" ), sw::hashed_string( "uint32" ), SW_OFFSET_OF( TestBindingActor, _score ) );

	bool bCalled{ false };
	prop.bindOnChanged( SW_DELEGATE_LAMBDA( sw::PropertyInfo::PropertyBindingDelegate, [&bCalled]( const sw::PropertyInfo& p, const void* pInst )
	{
		(void)p;
		(void)pInst;
		bCalled = true;
	} ) );

	TestBindingActor actor{};
	prop.setValue( &actor, 500u );
	SW_EXPECT_EQUAL( 500u, actor._score );
	SW_EXPECT_TRUE( bCalled );
}

// ------------------------------------------------------------------------------
// 17) Reflection_Serialization — Binary/JSON/XML·버전
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Serialization] 바이너리 버전 헤더
 */
SW_TEST_CASE( Reflection_Serialization, BinaryVersionHeaderTest )
{
	struct VersionedActor
	{
		int32 _fieldA = 777;
		int32 _fieldB = 888;
	};

	VersionedActor actor;

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "VersionedActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::VersionedActor" );
	info._size				 = sizeof( VersionedActor );
	info._listProperty		 = {
		{sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ),
		  SW_OFFSET_OF( VersionedActor, _fieldA ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		{sw::hashed_string( "_fieldB" ), sw::hashed_string( "int32" ),
		  SW_OFFSET_OF( VersionedActor, _fieldB ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
	   };

	sw::vector<uint8> buffer;
	sw::BinarySerializer::serializeVersioned( 102, &actor, info, buffer );
	SW_EXPECT_TRUE( buffer.size() > sizeof( uint32 ) );

	VersionedActor restored;
	restored._fieldA = 0;
	restored._fieldB = 0;
	uint32 readVersion{ 0 };
	bool   ok = sw::BinarySerializer::deserializeVersioned( readVersion, &restored, info, buffer.data(), buffer.size(),
															102u );
	SW_EXPECT_TRUE( ok );
	SW_EXPECT_EQUAL( 102u, readVersion );
	SW_EXPECT_EQUAL( 777, restored._fieldA );
	SW_EXPECT_EQUAL( 888, restored._fieldB );

	// fromVersion != currentVersion → migrate 콜백
	static bool s_migrateCalled{ false };
	s_migrateCalled = false;
	auto migrateFn	= []( const sw::SchemaMigrateContext& ctx ) -> bool
	{
		s_migrateCalled = true;
		SW_EXPECT_EQUAL( 102u, ctx._fromVersion );
		SW_EXPECT_EQUAL( 103u, ctx._toVersion );
		static_cast<VersionedActor*>( ctx._pInstance )->_fieldA += 1;
		return true;
	};
	restored._fieldA = 0;
	restored._fieldB = 0;
	readVersion		 = 0;
	ok				 = sw::BinarySerializer::deserializeVersioned( readVersion, &restored, info, buffer.data(), buffer.size(), 103u,
																   +migrateFn );
	SW_EXPECT_TRUE( ok );
	SW_EXPECT_TRUE( s_migrateCalled );
	SW_EXPECT_EQUAL( 778, restored._fieldA );
}

/**
 * @brief [Reflection_Serialization] 필드 타입 변경 (int32→string) binary coerce + Json/Xml versioned
 */
SW_TEST_CASE( Reflection_Serialization, FieldTypeChangeAndTextVersioned )
{
	struct IntHp
	{
		int32 _hp{ 0 };
	};
	struct StrHp
	{
		sw::string _hp;
	};

	sw::TypeInfo intInfo;
	intInfo._name				= sw::hashed_string( "IntHp" );
	intInfo._fullyQualifiedName = sw::hashed_string( "sw::IntHp" );
	intInfo._size				= sizeof( IntHp );
	intInfo._listProperty		= {
		{ sw::hashed_string( "_hp" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( IntHp, _hp ) }
	 };

	sw::TypeInfo strInfo;
	strInfo._name				= sw::hashed_string( "StrHp" );
	strInfo._fullyQualifiedName = sw::hashed_string( "sw::StrHp" );
	strInfo._size				= sizeof( StrHp );
	strInfo._listProperty		= {
		{ sw::hashed_string( "_hp" ), sw::hashed_string( "string" ), SW_OFFSET_OF( StrHp, _hp ) }
	  };

	IntHp			  src{ 42 };
	sw::vector<uint8> bin;
	sw::BinarySerializer::serializeVersioned( 1, &src, intInfo, bin );

	StrHp  dst;
	uint32 ver{ 0 };
	SW_EXPECT_TRUE( sw::BinarySerializer::deserializeVersioned( ver, &dst, strInfo, bin.data(), bin.size(), 1u ) );
	SW_EXPECT_EQUAL( 1u, ver );
	SW_EXPECT_TRUE( dst._hp == "42" );

	// string → int32 (quoted JSON)
	StrHp	   strSrc{ "99" };
	sw::string json = sw::JsonSerializer::serializeVersioned( 3, &strSrc, strInfo );
	SW_EXPECT_TRUE( json.find( "\"_schemaVersion\":3" ) != sw::string::npos );

	IntHp fromJson{};
	ver = 0;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserializeVersioned( ver, &fromJson, intInfo, json, 3u ) );
	SW_EXPECT_EQUAL( 3u, ver );
	SW_EXPECT_EQUAL( 99, fromJson._hp );

	// Xml versioned + int32→string coerce
	sw::string xml = sw::XmlSerializer::serializeVersioned( 4, &src, intInfo );
	SW_EXPECT_TRUE( xml.find( "_schemaVersion" ) != sw::string::npos );
	StrHp fromXml;
	ver = 0;
	SW_EXPECT_TRUE( sw::XmlSerializer::deserializeVersioned( ver, &fromXml, strInfo, xml, 4u ) );
	SW_EXPECT_EQUAL( 4u, ver );
	SW_EXPECT_TRUE( fromXml._hp == "42" );
}

/**
 * @brief [Reflection_Serialization] migrate 없이 스키마 버전이 다르면 실패
 */
SW_TEST_CASE( Reflection_Serialization, VersionedDeserializeFailsWithoutMigrate )
{
	SW_TEST_DEFENSIVE_SCOPE( "Testing schema version mismatch without migration callback" );
	struct VersionedActor
	{
		int32 _fieldA{ 0 };
	};
	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "VersionMismatchActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::VersionMismatchActor" );
	info._size				 = sizeof( VersionedActor );
	info._listProperty		 = {
		{ sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( VersionedActor, _fieldA ) }
	  };

	VersionedActor	  actor{ 7 };
	sw::vector<uint8> bin;
	sw::BinarySerializer::serializeVersioned( 1, &actor, info, bin );

	VersionedActor restored{ 0 };
	uint32		   ver{ 0 };
	SW_EXPECT_FALSE( sw::BinarySerializer::deserializeVersioned( ver, &restored, info, bin.data(), bin.size(), 2u ) );
	SW_EXPECT_EQUAL( 1u, ver );

	sw::string json = sw::JsonSerializer::serializeVersioned( 1, &actor, info );
	ver				= 0;
	SW_EXPECT_FALSE( sw::JsonSerializer::deserializeVersioned( ver, &restored, info, json, 2u ) );

	sw::string xml = sw::XmlSerializer::serializeVersioned( 1, &actor, info );
	ver			   = 0;
	SW_EXPECT_FALSE( sw::XmlSerializer::deserializeVersioned( ver, &restored, info, xml, 2u ) );
}

/**
 * @brief [Reflection_Serialization] orphan 구조 이동 + PROPERTY Alias 개명
 */
SW_TEST_CASE( Reflection_Serialization, StructuralMoveAndPropertyAlias )
{
	struct NestedStats
	{
		int32 _hp{ 0 };
	};
	struct NestedActor
	{
		NestedStats _stats;
	};
	struct RenamedActor
	{
		int32 _hitPoints{ 0 };
	};

	sw::TypeInfo nestedStatsInfo;
	nestedStatsInfo._name				= sw::hashed_string( "NestedStats" );
	nestedStatsInfo._fullyQualifiedName = sw::hashed_string( "sw::NestedStats" );
	nestedStatsInfo._size				= sizeof( NestedStats );
	nestedStatsInfo._listProperty		= {
		{ sw::hashed_string( "_hp" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( NestedStats, _hp ) }
	   };
	sw::engine::getTypeRegistry().registerClass( nestedStatsInfo );

	sw::TypeInfo nestedActorInfo;
	nestedActorInfo._name				= sw::hashed_string( "NestedActor" );
	nestedActorInfo._fullyQualifiedName = sw::hashed_string( "sw::NestedActor" );
	nestedActorInfo._size				= sizeof( NestedActor );
	nestedActorInfo._listProperty		= {
		{ sw::hashed_string( "_stats" ), sw::hashed_string( "sw::NestedStats" ), SW_OFFSET_OF( NestedActor, _stats ) }
	   };

	// JSON orphan `_hp` → `_stats._hp`
	NestedActor nested{};
	uint32		ver{ 0 };
	auto		moveOrphan = []( const sw::SchemaMigrateContext& ctx ) -> bool
	{
		return ctx.applyOrphanToPath( "_stats._hp" );
	};
	SW_EXPECT_TRUE( sw::JsonSerializer::deserializeVersioned( ver, &nested, nestedActorInfo, R"({"_schemaVersion":1,"_hp":77})",
															  2u, +moveOrphan ) );
	SW_EXPECT_EQUAL( 77, nested._stats._hp );

	// 필드 개명: Alias 로 옛 키 로드 (legacyTypeInfo 스테이징 불필요)
	sw::TypeInfo renamedInfo;
	renamedInfo._name				= sw::hashed_string( "RenamedActor" );
	renamedInfo._fullyQualifiedName = sw::hashed_string( "sw::RenamedActor" );
	renamedInfo._size				= sizeof( RenamedActor );
	sw::PropertyInfo hpProp( sw::hashed_string( "_hitPoints" ), sw::hashed_string( "int32" ),
							 SW_OFFSET_OF( RenamedActor, _hitPoints ) );
	hpProp._listAlias.push_back( sw::hashed_string( "_hp" ) );
	renamedInfo._listProperty.push_back( hpProp );

	RenamedActor renamed{};
	ver = 0;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserializeVersioned( ver, &renamed, renamedInfo,
															  R"({"_schemaVersion":1,"_hp":55})", 1u ) );
	SW_EXPECT_EQUAL( 55, renamed._hitPoints );
}

/**
 * @brief [Reflection_Serialization] JSON pretty print
 */
SW_TEST_CASE( Reflection_Serialization, JsonPrettyPrint )
{
	struct SimpleJsonActor
	{
		int32 _val = 42;
	} actor;

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "SimpleJsonActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::SimpleJsonActor" );
	info._size				 = sizeof( SimpleJsonActor );
	info._listProperty		 = {
		{ sw::hashed_string( "_val" ), sw::hashed_string( "int32" ),
		  SW_OFFSET_OF( SimpleJsonActor, _val ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr }
	 };

	sw::string prettyStr = sw::JsonSerializer::serializePretty( &actor, info, 4 );
	SW_EXPECT_TRUE( prettyStr.find( '\n' ) != sw::string::npos );
	SW_EXPECT_TRUE( prettyStr.find( "    \"_val\": 42" ) != sw::string::npos );
}

// ------------------------------------------------------------------------------
// 18) Reflection_TypeInfo — 프로퍼티·isA·생성자
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_TypeInfo] 동적 메서드 호출
 */
SW_TEST_CASE( Reflection_TypeInfo, DynamicMethodInvoke )
{
	struct InvokableTestActor
	{
		int32 _score{ 0 };
		void  addScore( int32 delta )
		{
			_score += delta;
		}
	} actor;

	sw::FunctionInfo funcInfo;
	funcInfo._name	   = "addScore";
	funcInfo._hashName = sw::hashed_string( "addScore" );
	funcInfo._invoker  = SW_DELEGATE_LAMBDA( sw::Delegate<sw::TaskValue( void*, const sw::TaskArgs& )>, []( void* pObjPtr, const sw::TaskArgs& args ) -> sw::TaskValue
	{
		static_cast<InvokableTestActor*>( pObjPtr )->addScore( args.get<int32>( 0 ) );
		return sw::TaskValue{};
	} );

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "InvokableTestActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::InvokableTestActor" );
	info._size				 = sizeof( InvokableTestActor );
	info._listMethod.push_back( funcInfo );

	sw::engine::getTypeRegistry().registerClass( info );

	sw::TaskArgs args;
	args.add( int32{ 50 } );
	sw::engine::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::InvokableTestActor" ), sw::hashed_string( "addScore" ), args );

	SW_EXPECT_EQUAL( 50, actor._score );
}

// ------------------------------------------------------------------------------
// 19) Reflection_EnumFlag — 플래그 연산자
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_EnumFlag] enum 플래그 연산자
 */
SW_TEST_CASE( Reflection_EnumFlag, EnumFlagOperators )
{
	TestFlag flag = TestFlag::Read | TestFlag::Write;
	SW_EXPECT_TRUE( sw::engine::getTypeRegistry().hasFlag( flag, TestFlag::Read ) );
	SW_EXPECT_TRUE( sw::engine::getTypeRegistry().hasFlag( flag, TestFlag::Write ) );
	SW_EXPECT_FALSE( sw::engine::getTypeRegistry().hasFlag( flag, TestFlag::Execute ) );

	flag |= TestFlag::Execute;
	SW_EXPECT_TRUE( sw::engine::getTypeRegistry().hasFlag( flag, TestFlag::Execute ) );
}

// ------------------------------------------------------------------------------
// 20) Reflection_Cloning — 딥카피
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Cloning] 오브젝트 딥카피
 */
SW_TEST_CASE( Reflection_Cloning, ObjectDeepCopyClone )
{
	struct CloneableActor
	{
		int32	_health = 100;
		float32 _speed	= 5.5f;
	} srcActor, dstActor;

	srcActor._health = 250;
	srcActor._speed	 = 12.0f;

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "CloneableActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::CloneableActor" );
	info._size				 = sizeof( CloneableActor );
	info._listProperty		 = {
		{sw::hashed_string( "_health" ),	  sw::hashed_string( "int32" ), SW_OFFSET_OF( CloneableActor, _health ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		{ sw::hashed_string( "_speed" ), sw::hashed_string( "float32" ), SW_OFFSET_OF( CloneableActor,  _speed ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
	 };

	bool cloneOk = sw::BinarySerializer::cloneObject( &dstActor, &srcActor, info );
	SW_EXPECT_TRUE( cloneOk );
	SW_EXPECT_EQUAL( 250, dstActor._health );
	SW_EXPECT_NEAR_EQUAL( 12.0f, dstActor._speed, 1e-4f );
}

// ------------------------------------------------------------------------------
// 21) Reflection_FunctionMacro — FUNCTION 인보크
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_FunctionMacro] 어노테이션 메서드 호출
 */
SW_TEST_CASE( Reflection_FunctionMacro, AnnotatedMethodInvoke )
{
	// 수동 연결 invoker (레거시 경로도 여전히 지원).
	REFLECT()
	struct FunctionAnnotatedActor
	{
		PROPERTY()
		int32 _health = 100;

		FUNCTION()
		void takeDamage( int32 damage )
		{
			_health -= damage;
		}
	} actor;

	sw::FunctionInfo funcInfo;
	funcInfo._name					= "takeDamage";
	funcInfo._hashName				= sw::hashed_string( "takeDamage" );
	funcInfo._returnTypeName		= "void";
	funcInfo._listParameterTypeName = { "sw::int32" };
	funcInfo._invoker				= SW_DELEGATE_LAMBDA( sw::Delegate<sw::TaskValue( void*, const sw::TaskArgs& )>, []( void* pObjPtr, const sw::TaskArgs& args ) -> sw::TaskValue
	{
		static_cast<FunctionAnnotatedActor*>( pObjPtr )->takeDamage( args.get<int32>( 0 ) );
		return sw::TaskValue{};
	} );

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "FunctionAnnotatedActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::FunctionAnnotatedActor" );
	info._size				 = sizeof( FunctionAnnotatedActor );
	info._listMethod.push_back( funcInfo );

	sw::engine::getTypeRegistry().registerClass( info );

	sw::TaskArgs args;
	args.add( int32{ 35 } );
	sw::engine::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::FunctionAnnotatedActor" ), sw::hashed_string( "takeDamage" ), args );

	SW_EXPECT_EQUAL( 65, actor._health );
}

/**
 * @brief [Reflection_FunctionMacro] 코드젠 메서드 호출
 */
SW_TEST_CASE( Reflection_FunctionMacro, CodegenMethodInvoke )
{
	// SampleTestActor::takeDamage / getHp 는 ReflectionParser 코드젠이 출력한다.
	sw::SampleTestActor actor;
	SW_ASSERT_NOT_NULL( sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::SampleTestActor" ) ) );

	const sw::TypeInfo* typeInfo = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::SampleTestActor" ) );
	SW_ASSERT_NOT_NULL( typeInfo );
	const sw::PropertyInfo* nameProp = typeInfo->findProperty( sw::hashed_string( "_name" ) );
	SW_ASSERT_NOT_NULL( nameProp );
	SW_EXPECT_EQUAL( sw::string( "string" ), sw::string( nameProp->_typeName.c_str() ) );
	const sw::FunctionInfo* takeDamageFn = typeInfo->findMethod( sw::hashed_string( "takeDamage" ) );
	const sw::FunctionInfo* getHpFn		 = typeInfo->findMethod( sw::hashed_string( "getHp" ) );
	SW_ASSERT_NOT_NULL( takeDamageFn );
	SW_ASSERT_NOT_NULL( getHpFn );
	SW_EXPECT_EQUAL( 0, static_cast<int32>( takeDamageFn->_metadata._bConst ) );
	SW_EXPECT_EQUAL( 1, static_cast<int32>( getHpFn->_metadata._bConst ) );

	sw::TaskArgs damageArgs;
	damageArgs.add( int32{ 40 } );
	sw::engine::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::SampleTestActor" ),
												sw::hashed_string( "takeDamage" ), damageArgs );
	SW_EXPECT_EQUAL( 60, actor._hp );

	sw::TaskValue hp = sw::engine::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::SampleTestActor" ),
																   sw::hashed_string( "getHp" ) );
	SW_EXPECT_TRUE( hp.hasValue() );
	SW_EXPECT_EQUAL( 60, hp.getValue<int32>() );
}

// ------------------------------------------------------------------------------
// 22) Reflection_Serialization — Binary/JSON/XML·버전
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Serialization] 중첩 구조체·컨테이너 라운드트립
 */
SW_TEST_CASE( Reflection_Serialization, NestedStructAndContainersRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::NestedContainerActor" ) );
	SW_ASSERT_TRUE( typeInfo != nullptr );

	sw::NestedContainerActor src;
	src._grid = {
		{ 1, 2 },
		{ 3, 4, 5 }
	};
	src._namedRows["a"] = { 1.0f, 2.0f };
	src._namedRows["b"] = { 3.5f };
	src._inner._x		= 42;

	sw::vector<uint8> bin;
	sw::BinarySerializer::serialize( &src, *typeInfo, bin );
	sw::NestedContainerActor dstBin;
	SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &dstBin, *typeInfo, bin.data(), bin.size() ) );
	SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), dstBin._grid.size() );
	SW_EXPECT_EQUAL( 5, dstBin._grid[1][2] );
	SW_EXPECT_EQUAL( 42, dstBin._inner._x );
	SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), dstBin._namedRows["a"].size() );

	const sw::string		 json = sw::JsonSerializer::serialize( &src, *typeInfo );
	sw::NestedContainerActor dstJson;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &dstJson, *typeInfo, json ) );
	SW_EXPECT_EQUAL( 4, dstJson._grid[1][1] );
	SW_EXPECT_EQUAL( 42, dstJson._inner._x );
}

namespace
{
	/** @brief 골든 테스트용 고정 상태의 NestedContainerActor 를 만듭니다. */
	sw::NestedContainerActor makeGoldenNestedActor()
	{
		sw::NestedContainerActor src;
		src._grid = {
			{ 1, 2 },
			{ 3, 4, 5 }
		};
		src._namedRows["a"] = { 1.0f, 2.5f };
		src._namedRows["b"] = { -3.25f };
		src._inner._x		= 42;
		return src;
	}

	/** @brief 바이트 버퍼를 소문자 16진 문자열로 인코딩합니다. */
	sw::string toHexString( const sw::vector<uint8>& bytes )
	{
		static constexpr utf8 kDigit[] = "0123456789abcdef";
		sw::string			  out;
		out.reserve( bytes.size() * 2 );
		for ( uint8 byteValue : bytes )
		{
			out.push_back( kDigit[byteValue >> 4] );
			out.push_back( kDigit[byteValue & 0x0F] );
		}
		return out;
	}

	/** @brief 골든 문자열 비교 — 불일치 시 양쪽을 stdout 에 찍고 실패로 이어지도록 false 반환. */
	bool goldenEq( const utf8* pLabel, const sw::string& actual, const utf8* pExpected )
	{
		if ( actual == pExpected )
			return true;
		std::fprintf( stdout, "[golden %s]\n---expected---\n%s\n---actual---\n%s\n", pLabel, pExpected, actual.c_str() );
		std::fflush( stdout );
		return false;
	}

	/** @brief 한 인스턴스의 5개 골든(Json/Xml/Bin/JsonVer/BinVer)을 검증합니다. */
	bool checkGoldenSet( const utf8* pLabel, const void* pInstance, const sw::TypeInfo& typeInfo,
						 const utf8* pJson, const utf8* pXml, const utf8* pBinHex, const utf8* pJsonVer, const utf8* pBinVerHex )
	{
		sw::vector<uint8> bin;
		sw::BinarySerializer::serialize( pInstance, typeInfo, bin );
		sw::vector<uint8> binVer;
		sw::BinarySerializer::serializeVersioned( 7, pInstance, typeInfo, binVer );

		bool bOk = true;
		bOk &= goldenEq( pLabel, sw::JsonSerializer::serialize( pInstance, typeInfo ), pJson );
		bOk &= goldenEq( pLabel, sw::XmlSerializer::serialize( pInstance, typeInfo ), pXml );
		bOk &= goldenEq( pLabel, toHexString( bin ), pBinHex );
		bOk &= goldenEq( pLabel, sw::JsonSerializer::serializeVersioned( 7, pInstance, typeInfo ), pJsonVer );
		bOk &= goldenEq( pLabel, toHexString( binVer ), pBinVerHex );
		return bOk;
	}
} // namespace

/**
 * @brief [Reflection_Serialization] 직렬화 3포맷의 정확한 출력을 골든으로 고정합니다.
 * @details 라운드트립 테스트는 디스크 포맷이 바뀌어도 통과하므로, 리팩터 시 바이트 호환을
 *          지키는 안전망으로 정확한 출력 문자열/헥스를 비교합니다.
 *          바이너리 헥스에는 타입/프로퍼티 이름의 FNV-1a 해시가 포함되어 있어(결정적),
 *          레이아웃·해시·부동소수 포맷이 바뀌면 이 테스트가 먼저 잡습니다.
 */
SW_TEST_CASE( Reflection_Serialization, GoldenOutputFormatsStable )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::NestedContainerActor" ) );
	SW_ASSERT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	const sw::NestedContainerActor src = makeGoldenNestedActor();

	const sw::string kGoldenJson =
		"{\"vector\":[{\"_name\":\"_grid\",\"item\":[{\"vector\":[{\"_name\":\"item\",\"item\":[1,2]}]},"
		"{\"vector\":[{\"_name\":\"item\",\"item\":[3,4,5]}]}]}],\"map\":[{\"_name\":\"_namedRows\","
		"\"entry\":{\"a\":{\"vector\":[{\"_name\":\"value\",\"item\":[1,2.5]}]},"
		"\"b\":{\"vector\":[{\"_name\":\"value\",\"item\":[-3.25]}]}}}],\"_inner\":{\"_x\":42}}";

	const sw::string kGoldenPretty =
		"{\n"
		"    \"vector\": [\n"
		"        {\n"
		"            \"_name\": \"_grid\",\n"
		"            \"item\": [\n"
		"                {\n"
		"                    \"vector\": [\n"
		"                        {\n"
		"                            \"_name\": \"item\",\n"
		"                            \"item\": [\n"
		"                                1,\n"
		"                                2\n"
		"                            ]\n"
		"                        }\n"
		"                    ]\n"
		"                },\n"
		"                {\n"
		"                    \"vector\": [\n"
		"                        {\n"
		"                            \"_name\": \"item\",\n"
		"                            \"item\": [\n"
		"                                3,\n"
		"                                4,\n"
		"                                5\n"
		"                            ]\n"
		"                        }\n"
		"                    ]\n"
		"                }\n"
		"            ]\n"
		"        }\n"
		"    ],\n"
		"    \"map\": [\n"
		"        {\n"
		"            \"_name\": \"_namedRows\",\n"
		"            \"entry\": {\n"
		"                \"a\": {\n"
		"                    \"vector\": [\n"
		"                        {\n"
		"                            \"_name\": \"value\",\n"
		"                            \"item\": [\n"
		"                                1,\n"
		"                                2.5\n"
		"                            ]\n"
		"                        }\n"
		"                    ]\n"
		"                },\n"
		"                \"b\": {\n"
		"                    \"vector\": [\n"
		"                        {\n"
		"                            \"_name\": \"value\",\n"
		"                            \"item\": [\n"
		"                                -3.25\n"
		"                            ]\n"
		"                        }\n"
		"                    ]\n"
		"                }\n"
		"            }\n"
		"        }\n"
		"    ],\n"
		"    \"_inner\": {\n"
		"        \"_x\": 42\n"
		"    }\n"
		"}";

	const sw::string kGoldenXml =
		"<NestedContainerActor>\n"
		"\t<vector _name=\"_grid\">\n"
		"\t\t<vector _name=\"item\">\n"
		"\t\t\t<item>1</item>\n"
		"\t\t\t<item>2</item>\n"
		"\t\t</vector>\n"
		"\t\t<vector _name=\"item\">\n"
		"\t\t\t<item>3</item>\n"
		"\t\t\t<item>4</item>\n"
		"\t\t\t<item>5</item>\n"
		"\t\t</vector>\n"
		"\t</vector>\n"
		"\t<map _name=\"_namedRows\">\n"
		"\t\t<entry>\n"
		"\t\t\t<key>a</key>\n"
		"\t\t\t<vector _name=\"value\">\n"
		"\t\t\t\t<item>1</item>\n"
		"\t\t\t\t<item>2.5</item>\n"
		"\t\t\t</vector>\n"
		"\t\t</entry>\n"
		"\t\t<entry>\n"
		"\t\t\t<key>b</key>\n"
		"\t\t\t<vector _name=\"value\">\n"
		"\t\t\t\t<item>-3.25</item>\n"
		"\t\t\t</vector>\n"
		"\t\t</entry>\n"
		"\t</map>\n"
		"\t<_inner _x=\"42\"/>\n"
		"</NestedContainerActor>\n"
		"\n";

	const sw::string kGoldenBinHex =
		"030000005614ce66612cdb3e20000000020000000200000001000000020000000300000003000000"
		"040000000500000086cb7acc7e60e28222000000020000000100000061020000000000803f000020"
		"40010000006201000000000050c0be55188f1aa2d1f6180000001400000001000000a27d0b57bfe2"
		"defb040000002a000000";

	const sw::string json = sw::JsonSerializer::serialize( &src, *typeInfo );
	if ( json != kGoldenJson )
		std::fprintf( stdout, "[golden json]\n  expected: %s\n  actual  : %s\n", kGoldenJson.c_str(), json.c_str() );
	SW_EXPECT_TRUE( json == kGoldenJson );

	const sw::string pretty = sw::JsonSerializer::serializePretty( &src, *typeInfo, 4 );
	if ( pretty != kGoldenPretty )
		std::fprintf( stdout, "[golden pretty]\n---expected---\n%s\n---actual---\n%s\n", kGoldenPretty.c_str(), pretty.c_str() );
	SW_EXPECT_TRUE( pretty == kGoldenPretty );

	const sw::string xml = sw::XmlSerializer::serialize( &src, *typeInfo );
	if ( xml != kGoldenXml )
		std::fprintf( stdout, "[golden xml]\n---expected---\n%s\n---actual---\n%s\n", kGoldenXml.c_str(), xml.c_str() );
	SW_EXPECT_TRUE( xml == kGoldenXml );

	sw::vector<uint8> bin;
	sw::BinarySerializer::serialize( &src, *typeInfo, bin );
	const sw::string binHex = toHexString( bin );
	if ( binHex != kGoldenBinHex )
		std::fprintf( stdout, "[golden binhex]\n  expected: %s\n  actual  : %s\n", kGoldenBinHex.c_str(), binHex.c_str() );
	SW_EXPECT_TRUE( binHex == kGoldenBinHex );

	// 역방향: 골든 문자열을 다시 읽어 원본과 같은지 (안전망 자체가 유효한지 확인)
	sw::NestedContainerActor back;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &back, *typeInfo, kGoldenJson ) );
	SW_EXPECT_EQUAL( 5, back._grid[1][2] );
	SW_EXPECT_EQUAL( 42, back._inner._x );
}

/**
 * @brief [Reflection_Serialization] 스칼라 이스케이프·비트필드·XmlAttribute·versioned 헤더 골든.
 * @details GoldenOutputFormatsStable(중첩 컨테이너)를 보완하는 두 번째 안전망.
 */
SW_TEST_CASE( Reflection_Serialization, GoldenOutputFormatsWide )
{
	sw::TypeRegistry& reg = sw::engine::getTypeRegistry();

	const sw::TypeInfo* pScalar = reg.findType( sw::hashed_string( "sw::SampleTestActor" ) );
	const sw::TypeInfo* pBits	= reg.findType( sw::hashed_string( "sw::BitfieldTestActor" ) );
	const sw::TypeInfo* pAttr	= reg.findType( sw::hashed_string( "sw::DefaultValueTestActor" ) );
	SW_ASSERT_TRUE( pScalar != nullptr && pBits != nullptr && pAttr != nullptr );

	// 스칼라 + 문자열 이스케이프(JSON \" \\ \n / XML 단일따옴표 속성 + 원시 개행)
	sw::SampleTestActor scalar;
	scalar._hp	 = -7;
	scalar._name = "a\"b\\c\nd";
	SW_EXPECT_TRUE( checkGoldenSet(
		"scalar", &scalar, *pScalar,
		"{\"_hp\":-7,\"_name\":\"a\\\"b\\\\c\\nd\"}",
		"<SampleTestActor _hp=\"-7\" _name='a\"b\\c\n"
		"d'/>\n"
		"\n",
		"02000000266feed8bfe2defb04000000f9ffffffcd98c89d3865c1170b000000070000006122625c630a64",
		"{\"_schemaVersion\":7,\"_hp\":-7,\"_name\":\"a\\\"b\\\\c\\nd\"}",
		"0700000002000000266feed8bfe2defb04000000f9ffffffcd98c89d3865c1170b000000070000006122625c630a64" ) );

	// 비트필드(: 1) → JSON/XML true|false, Binary 01|00
	sw::BitfieldTestActor bits;
	bits._bActive		= SW_TRUE;
	bits._bInvulnerable = SW_FALSE;
	bits._bCanJump		= SW_TRUE;
	bits._score			= 777;
	SW_EXPECT_TRUE( checkGoldenSet(
		"bits", &bits, *pBits,
		"{\"_bActive\":true,\"_bInvulnerable\":false,\"_bCanJump\":true,\"_score\":777}",
		"<BitfieldTestActor _bActive=\"true\" _bInvulnerable=\"false\" _bCanJump=\"true\" _score=\"777\"/>\n"
		"\n",
		"0400000046dd8356dbf29d1901000000012335d1a0dbf29d1901000000002e8c2fdadbf29d190100000001bc771186bfe2defb0400000009030000",
		"{\"_schemaVersion\":7,\"_bActive\":true,\"_bInvulnerable\":false,\"_bCanJump\":true,\"_score\":777}",
		"070000000400000046dd8356dbf29d1901000000012335d1a0dbf29d1901000000002e8c2fdadbf29d190100000001bc771186bfe2defb0400000009030000" ) );

	// XmlAttribute 플래그 프로퍼티
	sw::DefaultValueTestActor attr;
	attr._mana	= 12;
	attr._title = "Sir";
	SW_EXPECT_TRUE( checkGoldenSet(
		"attr", &attr, *pAttr,
		"{\"_mana\":12,\"_title\":\"Sir\"}",
		"<DefaultValueTestActor _mana=\"12\" _title=\"Sir\"/>\n"
		"\n",
		"0200000089a752a5bfe2defb040000000c00000068fdd9173865c1170700000003000000536972",
		"{\"_schemaVersion\":7,\"_mana\":12,\"_title\":\"Sir\"}",
		"070000000200000089a752a5bfe2defb040000000c00000068fdd9173865c1170700000003000000536972" ) );
}

/**
 * @brief [Reflection_Serialization] Reflection RPC 팩·호출
 */
SW_TEST_CASE( Reflection_Serialization, ReflectionRpcPackInvoke )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::RpcDemoActor" ) );
	SW_ASSERT_TRUE( typeInfo != nullptr );
	const sw::FunctionInfo* fn = typeInfo->findMethod( sw::hashed_string( "applyDamage" ) );
	SW_ASSERT_TRUE( fn != nullptr );
	SW_EXPECT_TRUE( fn->_metadata._netRole == sw::FunctionNetRole::Server );
	SW_EXPECT_EQUAL( 1, static_cast<int32>( fn->_metadata._bReliable ) );
#if !defined( SW_SHIPPING )
	SW_EXPECT_EQUAL( sw::string( "Apply Damage" ), fn->_metadata._displayName );
	SW_EXPECT_EQUAL( sw::string( "Subtracts amount from HP" ), fn->_metadata._tooltip );
	SW_EXPECT_EQUAL( sw::string( "Combat" ), fn->_metadata._category );
#endif

	sw::RpcDemoActor actor;
	actor._hp = 100;
	sw::TaskArgs args;
	args.add( int32{ 25 } );
	sw::TaskValue result;
	SW_EXPECT_TRUE( sw::ReflectionRpc::packAndInvoke( &actor, sw::hashed_string( "sw::RpcDemoActor" ),
													  sw::hashed_string( "applyDamage" ), args, &result ) );
	SW_EXPECT_EQUAL( 75, actor._hp );
}

/**
 * @brief [Reflection_Serialization] REFLECT Abstract/Static
 */
SW_TEST_CASE( Reflection_Serialization, ReflectAbstractAndStatic )
{
	const sw::TypeInfo* abstractInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AbstractDemoBase" ) );
	SW_ASSERT_TRUE( abstractInfo != nullptr );
	SW_EXPECT_EQUAL( 1, static_cast<int32>( abstractInfo->_bAbstract ) );
	SW_EXPECT_TRUE( abstractInfo->canConstruct() == false );
	SW_EXPECT_TRUE( abstractInfo->findMethod( sw::hashed_string( "$ctor" ) ) == nullptr );

	const sw::TypeInfo* staticInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::StaticDemoLibrary" ) );
	SW_ASSERT_TRUE( staticInfo != nullptr );
	SW_EXPECT_EQUAL( 1, static_cast<int32>( staticInfo->_bStatic ) );
	SW_EXPECT_TRUE( staticInfo->canConstruct() == false );

	const sw::FunctionInfo* doubleFn = staticInfo->findMethod( sw::hashed_string( "doubleInt" ) );
	SW_ASSERT_TRUE( doubleFn != nullptr );
	SW_EXPECT_EQUAL( 1, static_cast<int32>( doubleFn->_metadata._bStatic ) );
#if !defined( SW_SHIPPING )
	SW_EXPECT_EQUAL( sw::string( "Double Int" ), doubleFn->_metadata._displayName );
	SW_EXPECT_EQUAL( sw::string( "Returns value * 2" ), doubleFn->_metadata._tooltip );
	SW_EXPECT_EQUAL( sw::string( "Math" ), doubleFn->_metadata._category );
#endif

	sw::TaskArgs args;
	args.add( int32{ 21 } );
	const sw::TaskValue result = doubleFn->_invoker( nullptr, args );
	SW_EXPECT_EQUAL( 42, result.getValue<int32>() );
}

// ------------------------------------------------------------------------------
// 23) Reflection_TypeInfo — 프로퍼티·isA·생성자
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_TypeInfo] REFLECT 생성자 placement new
 */
SW_TEST_CASE( Reflection_TypeInfo, ReflectCtorPlacementNew )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::CtorDemoActor" ) );
	SW_ASSERT_TRUE( typeInfo != nullptr );
	SW_EXPECT_TRUE( typeInfo->canConstruct() );

	const sw::FunctionInfo* defaultCtor = typeInfo->findMethod( sw::hashed_string( "$ctor" ) );
	const sw::FunctionInfo* valueCtor	= typeInfo->findMethod( sw::hashed_string( "$ctor(int32)" ) );
	SW_ASSERT_TRUE( defaultCtor != nullptr );
	SW_ASSERT_TRUE( valueCtor != nullptr );
	SW_EXPECT_EQUAL( 1, static_cast<int32>( defaultCtor->_metadata._bConstructor ) );
	SW_EXPECT_EQUAL( 1, static_cast<int32>( valueCtor->_metadata._bConstructor ) );

	alignas( sw::CtorDemoActor ) uint8 defaultStorage[sizeof( sw::CtorDemoActor )]{};
	sw::CtorDemoActor*				   defaultInstance = reinterpret_cast<sw::CtorDemoActor*>( defaultStorage );
	defaultCtor->_invoker( defaultInstance, sw::TaskArgs{} );
	SW_EXPECT_EQUAL( 0, defaultInstance->_value );
	defaultInstance->~CtorDemoActor();

	alignas( sw::CtorDemoActor ) uint8 valueStorage[sizeof( sw::CtorDemoActor )]{};
	sw::CtorDemoActor*				   valueInstance = reinterpret_cast<sw::CtorDemoActor*>( valueStorage );
	sw::TaskArgs					   valueArgs;
	valueArgs.add( int32{ 77 } );
	valueCtor->_invoker( valueInstance, valueArgs );
	SW_EXPECT_EQUAL( 77, valueInstance->_value );
	valueInstance->~CtorDemoActor();
}

/**
 * @brief [Reflection_TypeInfo] PROPERTY 어노테이션 메타 코드젠
 */
SW_TEST_CASE( Reflection_TypeInfo, PropertyAnnotationMetadataCodegen )
{
	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::MetadataDemoActor" ) );
	SW_ASSERT_TRUE( typeInfo != nullptr );
	const sw::PropertyInfo* hp = typeInfo->findProperty( sw::hashed_string( "_hp" ) );
	SW_ASSERT_TRUE( hp != nullptr );
#if !defined( SW_SHIPPING )
	SW_EXPECT_EQUAL( sw::string( "Stats" ), hp->_metadata._category );
	SW_EXPECT_EQUAL( sw::string( "Hit Points" ), hp->_metadata._displayName );
	SW_EXPECT_EQUAL( sw::string( "Current HP" ), hp->_metadata._tooltip );
#endif
	SW_EXPECT_TRUE( hp->_metadata._bReadOnly );
}

// ------------------------------------------------------------------------------
// 24) Reflection_Serialization — Binary/JSON/XML·버전
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Serialization] JSON 이스케이프 라운드트립
 */
SW_TEST_CASE( Reflection_Serialization, JsonEscapeUnescapeRoundtrip )
{
	const sw::string raw	 = "line\n\t\"quote\"\\slash";
	const sw::string escaped = sw::JsonSerializer::escapeString( raw );
	SW_EXPECT_TRUE( escaped.find( '\n' ) == sw::string::npos );
	SW_EXPECT_TRUE( escaped.find( '\t' ) == sw::string::npos );
	SW_EXPECT_TRUE( escaped.find( "\\\"" ) != sw::string::npos );
	SW_EXPECT_TRUE( escaped.find( "\\\\" ) != sw::string::npos );
	SW_EXPECT_EQUAL( raw, sw::JsonSerializer::unescapeString( escaped ) );

	const sw::string extracted =
		sw::JsonSerializer::extractStringField( R"({"Title":"Hero","HP":"10"})", "title", true );
	SW_EXPECT_EQUAL( sw::string( "Hero" ), extracted );
	const sw::string missing =
		sw::JsonSerializer::extractStringField( R"({"Title":"Hero"})", "title", false );
	SW_EXPECT_TRUE( missing.empty() );
}

/**
 * @brief [Reflection_Serialization] ReflectAny 다형성
 */
SW_TEST_CASE( Reflection_Serialization, ReflectAnyPolymorphic )
{
	const sw::TypeInfo* payloadType =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::PolyPayloadA" ) );
	SW_ASSERT_TRUE( payloadType != nullptr );
	sw::PolyPayloadA payload;
	payload._a		   = 9;
	sw::ReflectAny any = sw::ReflectAny::makeFrom( *payloadType, &payload );
	SW_EXPECT_TRUE( any.empty() == false );

	sw::PolyPayloadA out{};
	SW_EXPECT_TRUE( any.tryGetFrom( *payloadType, &out ) );
	SW_EXPECT_EQUAL( 9, out._a );

	const sw::TypeInfo* typeInfo =
		sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AssetPathActor" ) );
	if ( typeInfo == nullptr )
	{
		SW_TEST_SKIP( "AssetPathActor type not registered" );
	}
	const sw::PropertyInfo* albedo = typeInfo->findProperty( sw::hashed_string( "_albedo" ) );
	SW_ASSERT_TRUE( albedo != nullptr );
	SW_EXPECT_EQUAL( 1, static_cast<int32>( albedo->_metadata._bAssetPath ) );
}

// ------------------------------------------------------------------------------
// 25) Reflection_Component — 생명주기·상속
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Component] Component beginPlay / tick / endPlay 동작 검증
 */
SW_TEST_CASE( Reflection_Component, ComponentLifecycle )
{
	sw::GameObjectManager manager;

	sw::GameObject* obj1 = manager.createGameObject( sw::hashed_string( "TestObj1" ) );
	sw::GameObject* obj2 = manager.createGameObject( sw::hashed_string( "TestObj2" ) );

	sw::TestScriptComponent* pComp1 = obj1->addComponent<sw::TestScriptComponent>();
	sw::TestScriptComponent* pComp2 = obj2->addComponent<sw::TestScriptComponent>();
	SW_ASSERT_NOT_NULL( pComp1 );
	SW_ASSERT_NOT_NULL( pComp2 );

	manager.beginPlay();

	SW_EXPECT_EQUAL( obj1, pComp1->getOwner() );
	SW_EXPECT_EQUAL( obj2, pComp2->getOwner() );
	SW_EXPECT_TRUE( pComp1->_beganPlay );
	SW_EXPECT_TRUE( pComp2->_beganPlay );

	manager.tick( 1.0f );
	manager.tick( 1.0f );

	SW_EXPECT_EQUAL( 2, pComp1->_tickCount );
	SW_EXPECT_EQUAL( 2, pComp2->_tickCount );

	manager.endPlay();

	SW_EXPECT_TRUE( pComp1->_endedPlay );
	SW_EXPECT_TRUE( pComp2->_endedPlay );
}

/**
 * @brief [Reflection_Component] Component 다중 상속 라이프사이클 및 틱 정상 동작 검증
 */
SW_TEST_CASE( Reflection_Component, ComponentInheritanceMultiLevel )
{
	sw::GameObjectManager manager;

	sw::GameObject* baseObj		  = manager.createGameObject( sw::hashed_string( "BaseObj" ) );
	sw::GameObject* derivedObj	  = manager.createGameObject( sw::hashed_string( "DerivedObj" ) );
	sw::GameObject* grandChildObj = manager.createGameObject( sw::hashed_string( "GrandChildObj" ) );

	baseObj->addComponent<sw::TestScriptComponent>();
	derivedObj->addComponent<sw::TestDerivedScriptComponent>();
	grandChildObj->addComponent<sw::TestGrandChildScriptComponent>();

	manager.beginPlay();

	sw::TestScriptComponent*		   baseComp		  = baseObj->getComponent<sw::TestScriptComponent>();
	sw::TestDerivedScriptComponent*	   derivedComp	  = derivedObj->getComponent<sw::TestDerivedScriptComponent>();
	sw::TestGrandChildScriptComponent* grandChildComp = grandChildObj->getComponent<sw::TestGrandChildScriptComponent>();

	SW_ASSERT_NOT_NULL( baseComp );
	SW_ASSERT_NOT_NULL( derivedComp );
	SW_ASSERT_NOT_NULL( grandChildComp );

	SW_EXPECT_EQUAL( baseObj, baseComp->getOwner() );
	SW_EXPECT_EQUAL( derivedObj, derivedComp->getOwner() );
	SW_EXPECT_EQUAL( grandChildObj, grandChildComp->getOwner() );

	SW_EXPECT_TRUE( baseComp->_beganPlay );
	SW_EXPECT_TRUE( derivedComp->_beganPlay );
	SW_EXPECT_TRUE( grandChildComp->_beganPlay );

	manager.tick( 0.016f );
	manager.tick( 0.016f );

	baseComp	   = baseObj->getComponent<sw::TestScriptComponent>();
	derivedComp	   = derivedObj->getComponent<sw::TestDerivedScriptComponent>();
	grandChildComp = grandChildObj->getComponent<sw::TestGrandChildScriptComponent>();

	SW_EXPECT_EQUAL( 2, baseComp->_tickCount );

	SW_EXPECT_EQUAL( 2, derivedComp->_tickCount );
	SW_EXPECT_EQUAL( 4, derivedComp->_derivedTickCount );

	SW_EXPECT_EQUAL( 2, grandChildComp->_tickCount );
	SW_EXPECT_EQUAL( 4, grandChildComp->_derivedTickCount );
	SW_EXPECT_EQUAL( 6, grandChildComp->_grandChildTickCount );

	manager.endPlay();

	baseComp	   = baseObj->getComponent<sw::TestScriptComponent>();
	derivedComp	   = derivedObj->getComponent<sw::TestDerivedScriptComponent>();
	grandChildComp = grandChildObj->getComponent<sw::TestGrandChildScriptComponent>();

	SW_EXPECT_TRUE( baseComp->_endedPlay );
	SW_EXPECT_TRUE( derivedComp->_endedPlay );
	SW_EXPECT_TRUE( grandChildComp->_endedPlay );
}

/**
 * @brief [Reflection_Component] Component에 선언된 PROPERTY가 TypeInfo에 반영되고 직렬화/역직렬화되는지 검증
 */
SW_TEST_CASE( Reflection_Component, ComponentPropertySerialization )
{
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::TestScriptComponent" ) );
	SW_ASSERT_NOT_NULL( pType );

	const sw::PropertyInfo* pSpeedProp = pType->findProperty( sw::hashed_string( "_scriptSpeed" ) );
	SW_ASSERT_NOT_NULL( pSpeedProp );
	SW_EXPECT_TRUE( pSpeedProp->_typeName == sw::hashed_string( "float32" ) );

	sw::TestScriptComponent comp;
	SW_EXPECT_TRUE( sw::MathUtil::nearEqual( comp._scriptSpeed, 1.5f, 0.0001f ) );

	comp._scriptSpeed = 3.14f;

	pSpeedProp->setValue<float32>( &comp, 2.718f );
	SW_EXPECT_TRUE( sw::MathUtil::nearEqual( comp._scriptSpeed, 2.718f, 0.0001f ) );
}

/**
 * @brief [Reflection_GenericQuery] TypeRegistry 템플릿 조회 및 isA 헬퍼 검증
 */
SW_TEST_CASE( Reflection_GenericQuery, FindTypeAndIsA )
{
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::TestDerivedScriptComponent>();
	SW_ASSERT_NOT_NULL( pType );
	SW_EXPECT_TRUE( pType->_name == sw::hashed_string( "TestDerivedScriptComponent" ) );

	sw::TestDerivedScriptComponent comp;
	SW_EXPECT_TRUE( sw::isA<sw::TestDerivedScriptComponent>( &comp ) );
	SW_EXPECT_TRUE( sw::isA<sw::TestScriptComponent>( &comp ) );
	SW_EXPECT_TRUE( sw::isA<sw::Component>( &comp ) );
	SW_EXPECT_TRUE( sw::isA<sw::DummyActor>( &comp ) == false );
}

/**
 * @brief [Reflection_GenericQuery] TypeRegistry forEachType 및 getDerivedTypes 검증
 */
SW_TEST_CASE( Reflection_GenericQuery, ForEachTypeAndDerivedTypes )
{
	uint32 typeCount{ 0 };
	sw::engine::getTypeRegistry().forEachType(
		[&typeCount]( const sw::TypeInfo& )
	{
		++typeCount;
	} );
	SW_EXPECT_TRUE( typeCount > 0 );

	const auto derivedComponents = sw::engine::getTypeRegistry().getDerivedTypes<sw::TestScriptComponent>();
	SW_EXPECT_TRUE( derivedComponents.size() >= 2 ); // TestDerivedScriptComponent, TestGrandChildScriptComponent
}

/**
 * @brief [Reflection_GenericQuery] PropertyInfo getRawPtr 및 findPropertyInHierarchy 검증
 */
SW_TEST_CASE( Reflection_GenericQuery, HierarchyPropertyLookupAndRawPtr )
{
	const sw::TypeInfo* pGrandChildType = sw::engine::getTypeRegistry().findType<sw::TestGrandChildScriptComponent>();
	SW_ASSERT_NOT_NULL( pGrandChildType );

	// 직계 프로퍼티
	const sw::PropertyInfo* pDirectProp = pGrandChildType->findProperty( sw::hashed_string( "_grandChildSpeed" ) );
	SW_ASSERT_NOT_NULL( pDirectProp );

	// 부모 프로퍼티 (findPropertyInHierarchy)
	const sw::PropertyInfo* pInheritedProp = pGrandChildType->findPropertyInHierarchy( sw::hashed_string( "_scriptSpeed" ) );
	SW_ASSERT_NOT_NULL( pInheritedProp );

	sw::TestGrandChildScriptComponent comp;
	comp._scriptSpeed = 42.0f;

	const void* pRaw = pInheritedProp->getRawPtr( &comp );
	SW_ASSERT_NOT_NULL( pRaw );
	const float32 val = *reinterpret_cast<const float32*>( pRaw );
	SW_EXPECT_TRUE( sw::MathUtil::nearEqual( val, 42.0f, 0.0001f ) );
}

// ------------------------------------------------------------------------------
// 6) Reflection_Metadata — Rich Metadata & Transient Serialization 검증
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Metadata] TypeMetadata (Category, DisplayName, Tooltip, HideInMenu, CustomMeta) 검증
 */
SW_TEST_CASE( Reflection_Metadata, TypeMetadataQuery )
{
#if !defined( SW_SHIPPING )
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::MetaTestActor>();
	SW_ASSERT_NOT_NULL( pType );

	SW_EXPECT_TRUE( pType->getCategory() == "Gameplay" );
	SW_EXPECT_TRUE( sw::string( pType->getDisplayName() ) == "Meta Test Actor" );
	SW_EXPECT_TRUE( pType->getTooltip() == "Actor for testing rich metadata" );
	SW_EXPECT_TRUE( pType->isHiddenInMenu() );

	const sw::string* pCustomVal = pType->findCustomMeta( sw::hashed_string( "CustomTag" ) );
	SW_ASSERT_NOT_NULL( pCustomVal );
	SW_EXPECT_TRUE( *pCustomVal == "ActorVal" );

	const sw::string* pPriority = pType->findCustomMeta( sw::hashed_string( "Priority" ) );
	SW_ASSERT_NOT_NULL( pPriority );
	SW_EXPECT_TRUE( *pPriority == "10" );
#else
	SW_TEST_SKIP( "Metadata is omitted in shipping builds" );
#endif
}

/**
 * @brief [Reflection_Metadata] PropertyMetadata (DisplayName, Category, Tooltip, Transient, HideInInspector, CustomMeta) 검증
 */
SW_TEST_CASE( Reflection_Metadata, PropertyMetadataQuery )
{
#if !defined( SW_SHIPPING )
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::MetaTestActor>();
	SW_ASSERT_NOT_NULL( pType );

	const sw::PropertyInfo* pHealth = pType->findProperty( sw::hashed_string( "_health" ) );
	SW_ASSERT_NOT_NULL( pHealth );

	SW_EXPECT_TRUE( pHealth->_metadata._bTransient == SW_TRUE );
	SW_EXPECT_TRUE( pHealth->_metadata._bHideInInspector == SW_TRUE );
	SW_EXPECT_TRUE( pHealth->_metadata._displayName == "Health Points" );
	SW_EXPECT_TRUE( pHealth->_metadata._category == "Stats" );
	SW_EXPECT_TRUE( pHealth->_metadata._tooltip == "Current health" );

	const sw::string* pUnits = pHealth->findCustomMeta( sw::hashed_string( "Units" ) );
	SW_ASSERT_NOT_NULL( pUnits );
	SW_EXPECT_TRUE( *pUnits == "HP" );

	const sw::string* pClamp = pHealth->findCustomMeta( sw::hashed_string( "Clamp" ) );
	SW_ASSERT_NOT_NULL( pClamp );
	SW_EXPECT_TRUE( *pClamp == "True" );

	const sw::PropertyInfo* pArmor = pType->findProperty( sw::hashed_string( "_armor" ) );
	SW_ASSERT_NOT_NULL( pArmor );
	SW_EXPECT_TRUE( pArmor->_metadata._bTransient == SW_FALSE );
	SW_EXPECT_TRUE( pArmor->_metadata._bHideInInspector == SW_FALSE );
#else
	SW_TEST_SKIP( "Metadata is omitted in shipping builds" );
#endif
}

/**
 * @brief [Reflection_Metadata] FunctionMetadata (DisplayName, Category, Tooltip, CallInEditor, CustomMeta) 검증
 */
SW_TEST_CASE( Reflection_Metadata, FunctionMetadataQuery )
{
#if !defined( SW_SHIPPING )
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::MetaTestActor>();
	SW_ASSERT_NOT_NULL( pType );

	const sw::FunctionInfo* pMethod = pType->findMethod( sw::hashed_string( "resetHealth" ) );
	SW_ASSERT_NOT_NULL( pMethod );

	SW_EXPECT_TRUE( pMethod->_metadata._bCallInEditor == SW_TRUE );
	SW_EXPECT_TRUE( pMethod->_metadata._displayName == "Reset Health" );
	SW_EXPECT_TRUE( pMethod->_metadata._category == "Actions" );
	SW_EXPECT_TRUE( pMethod->_metadata._tooltip == "Resets health to 100" );

	const sw::string* pActionType = pMethod->findCustomMeta( sw::hashed_string( "ActionType" ) );
	SW_ASSERT_NOT_NULL( pActionType );
	SW_EXPECT_TRUE( *pActionType == "Reset" );
#else
	SW_TEST_SKIP( "Metadata is omitted in shipping builds" );
#endif
}

/**
 * @brief [Reflection_Metadata] EnumInfo CustomMeta 검증
 */
SW_TEST_CASE( Reflection_Metadata, EnumMetadataQuery )
{
#if !defined( SW_SHIPPING )
	const sw::EnumInfo* pEnumInfo = sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "TestMetaEnum" ) );
	SW_ASSERT_NOT_NULL( pEnumInfo );

	const sw::string* pDoc = pEnumInfo->findCustomMeta( sw::hashed_string( "Doc" ) );
	SW_ASSERT_NOT_NULL( pDoc );
	SW_EXPECT_TRUE( *pDoc == "EnumForTesting" );

	const sw::string* pVersion = pEnumInfo->findCustomMeta( sw::hashed_string( "Version" ) );
	SW_ASSERT_NOT_NULL( pVersion );
	SW_EXPECT_TRUE( *pVersion == "2" );
#else
	SW_TEST_SKIP( "Metadata is omitted in shipping builds" );
#endif
}

/**
 * @brief [Reflection_Metadata] Transient 프로퍼티의 JSON 및 Binary 직렬화 제외 검증
 */
SW_TEST_CASE( Reflection_Metadata, TransientPropertySerialization )
{
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::MetaTestActor>();
	SW_ASSERT_NOT_NULL( pType );

	sw::MetaTestActor sourceActor;
	sourceActor._health = 999;
	sourceActor._armor	= 77;

	// 1) JSON 직렬화 검증: _health는 제외되고 _armor만 직렬화되어야 함
	const sw::string json = sw::JsonSerializer::serialize( &sourceActor, *pType );
	SW_EXPECT_TRUE( json.find( "_armor" ) != sw::string::npos );
	SW_EXPECT_TRUE( json.find( "_health" ) == sw::string::npos );
	SW_EXPECT_TRUE( json.find( "Health Points" ) == sw::string::npos );

	// 2) JSON 역직렬화 검증: targetActor의 _health는 기본값을 유지해야 함
	sw::MetaTestActor targetActor;
	targetActor._health = 50;
	targetActor._armor	= 0;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &targetActor, *pType, json ) );
	SW_EXPECT_EQUAL( 77, targetActor._armor );
	SW_EXPECT_EQUAL( 50, targetActor._health );

	// 3) Binary 직렬화/역직렬화 검증
	sw::vector<uint8> listBin;
	sw::BinarySerializer::serialize( &sourceActor, *pType, listBin );
	sw::MetaTestActor binTarget;
	binTarget._health = 30;
	binTarget._armor  = 0;
	SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &binTarget, *pType, listBin.data(), listBin.size() ) );
	SW_EXPECT_EQUAL( 77, binTarget._armor );
	SW_EXPECT_EQUAL( 30, binTarget._health );
}

// ------------------------------------------------------------------------------
// 22) Reflection_Bitfield — 비트필드(uint8 : 1) 리플렉션, 읽기/쓰기, 직렬화
// ------------------------------------------------------------------------------
/**
 * @brief [Reflection_Bitfield] 비트필드 프로퍼티 메타데이터 및 오프셋/마스크 검증
 */
SW_TEST_CASE( Reflection_Bitfield, BitfieldPropertyMetadata )
{
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::BitfieldTestActor>();
	SW_ASSERT_NOT_NULL( pType );

	const sw::PropertyInfo* pPropActive = pType->findProperty( sw::hashed_string( "_bActive" ) );
	SW_ASSERT_NOT_NULL( pPropActive );
	SW_EXPECT_EQUAL( SW_TRUE, pPropActive->_bIsBitField );
	SW_EXPECT_EQUAL( 0x01, pPropActive->_bitMask );

	const sw::PropertyInfo* pPropInvuln = pType->findProperty( sw::hashed_string( "_bInvulnerable" ) );
	SW_ASSERT_NOT_NULL( pPropInvuln );
	SW_EXPECT_EQUAL( SW_TRUE, pPropInvuln->_bIsBitField );
	SW_EXPECT_EQUAL( 0x02, pPropInvuln->_bitMask );

	const sw::PropertyInfo* pPropCanJump = pType->findProperty( sw::hashed_string( "_bCanJump" ) );
	SW_ASSERT_NOT_NULL( pPropCanJump );
	SW_EXPECT_EQUAL( SW_TRUE, pPropCanJump->_bIsBitField );
	SW_EXPECT_EQUAL( 0x04, pPropCanJump->_bitMask );

	const sw::PropertyInfo* pPropScore = pType->findProperty( sw::hashed_string( "_score" ) );
	SW_ASSERT_NOT_NULL( pPropScore );
	SW_EXPECT_EQUAL( SW_FALSE, pPropScore->_bIsBitField );
	SW_EXPECT_EQUAL( 0xFF, pPropScore->_bitMask );
}

/**
 * @brief [Reflection_Bitfield] 비트필드 getValue/setValue 독립성 검증
 */
SW_TEST_CASE( Reflection_Bitfield, BitfieldGetSetValue )
{
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::BitfieldTestActor>();
	SW_ASSERT_NOT_NULL( pType );

	const sw::PropertyInfo* pPropActive	 = pType->findProperty( sw::hashed_string( "_bActive" ) );
	const sw::PropertyInfo* pPropInvuln	 = pType->findProperty( sw::hashed_string( "_bInvulnerable" ) );
	const sw::PropertyInfo* pPropCanJump = pType->findProperty( sw::hashed_string( "_bCanJump" ) );
	SW_ASSERT_NOT_NULL( pPropActive );
	SW_ASSERT_NOT_NULL( pPropInvuln );
	SW_ASSERT_NOT_NULL( pPropCanJump );

	sw::BitfieldTestActor actor;
	SW_EXPECT_EQUAL( SW_FALSE, actor._bActive );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bInvulnerable );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bCanJump );

	// 1) _bActive = true
	pPropActive->setValue<bool>( &actor, true );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bActive );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bInvulnerable );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bCanJump );
	SW_EXPECT_TRUE( pPropActive->getValue<bool>( &actor ) );
	SW_EXPECT_FALSE( pPropInvuln->getValue<bool>( &actor ) );

	// 2) _bCanJump = true (인접 비트 _bActive 유지 검증)
	pPropCanJump->setValue<bool>( &actor, true );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bActive );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bInvulnerable );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bCanJump );
	SW_EXPECT_TRUE( pPropCanJump->getValue<bool>( &actor ) );

	// 3) _bActive = false
	pPropActive->setValue<bool>( &actor, false );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bActive );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bInvulnerable );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bCanJump );
	SW_EXPECT_FALSE( pPropActive->getValue<bool>( &actor ) );
	SW_EXPECT_TRUE( pPropCanJump->getValue<bool>( &actor ) );
}

/**
 * @brief [Reflection_Bitfield] 비트필드 JSON, XML, Binary 직렬화 라운드트립 검증
 */
SW_TEST_CASE( Reflection_Bitfield, BitfieldSerializationRoundtrip )
{
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::BitfieldTestActor>();
	SW_ASSERT_NOT_NULL( pType );

	sw::BitfieldTestActor source;
	source._bActive		  = SW_TRUE;
	source._bInvulnerable = SW_FALSE;
	source._bCanJump	  = SW_TRUE;
	source._score		  = 777;

	// 1) JSON 직렬화 & 역직렬화
	const sw::string json = sw::JsonSerializer::serialize( &source, *pType );
	SW_EXPECT_TRUE( json.find( "_bActive" ) != sw::string::npos );
	SW_EXPECT_TRUE( json.find( "_bCanJump" ) != sw::string::npos );

	sw::BitfieldTestActor jsonTarget;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &jsonTarget, *pType, json ) );
	SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bActive );
	SW_EXPECT_EQUAL( SW_FALSE, jsonTarget._bInvulnerable );
	SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bCanJump );
	SW_EXPECT_EQUAL( 777, jsonTarget._score );

	// 2) XML 직렬화 & 역직렬화
	const sw::string	  xml = sw::XmlSerializer::serialize( &source, *pType );
	sw::BitfieldTestActor xmlTarget;
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &xmlTarget, *pType, xml ) );
	SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bActive );
	SW_EXPECT_EQUAL( SW_FALSE, xmlTarget._bInvulnerable );
	SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bCanJump );
	SW_EXPECT_EQUAL( 777, xmlTarget._score );

	// 3) Binary 직렬화 & 역직렬화
	sw::vector<uint8> listBin;
	sw::BinarySerializer::serialize( &source, *pType, listBin );
	sw::BitfieldTestActor binTarget;
	SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &binTarget, *pType, listBin.data(), listBin.size() ) );
	SW_EXPECT_EQUAL( SW_TRUE, binTarget._bActive );
	SW_EXPECT_EQUAL( SW_FALSE, binTarget._bInvulnerable );
	SW_EXPECT_EQUAL( SW_TRUE, binTarget._bCanJump );
	SW_EXPECT_EQUAL( 777, binTarget._score );
}

/**
 * @brief [Reflection_Bitfield] uint16, uint32, uint64 비트필드 플래그 getValue/setValue 독립성 검증
 */
SW_TEST_CASE( Reflection_Bitfield, WideBitfieldGetSetValue )
{
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::WideBitfieldTestActor>();
	SW_ASSERT_NOT_NULL( pType );

	const sw::PropertyInfo* pProp16A = pType->findProperty( sw::hashed_string( "_bFlag16_A" ) );
	const sw::PropertyInfo* pProp16B = pType->findProperty( sw::hashed_string( "_bFlag16_B" ) );
	const sw::PropertyInfo* pProp32A = pType->findProperty( sw::hashed_string( "_bFlag32_A" ) );
	const sw::PropertyInfo* pProp32B = pType->findProperty( sw::hashed_string( "_bFlag32_B" ) );
	const sw::PropertyInfo* pProp64A = pType->findProperty( sw::hashed_string( "_bFlag64_A" ) );
	const sw::PropertyInfo* pProp64B = pType->findProperty( sw::hashed_string( "_bFlag64_B" ) );

	SW_ASSERT_NOT_NULL( pProp16A );
	SW_ASSERT_NOT_NULL( pProp16B );
	SW_ASSERT_NOT_NULL( pProp32A );
	SW_ASSERT_NOT_NULL( pProp32B );
	SW_ASSERT_NOT_NULL( pProp64A );
	SW_ASSERT_NOT_NULL( pProp64B );

	SW_EXPECT_EQUAL( SW_TRUE, pProp16A->_bIsBitField );
	SW_EXPECT_EQUAL( SW_TRUE, pProp16B->_bIsBitField );
	SW_EXPECT_EQUAL( SW_TRUE, pProp32A->_bIsBitField );
	SW_EXPECT_EQUAL( SW_TRUE, pProp32B->_bIsBitField );
	SW_EXPECT_EQUAL( SW_TRUE, pProp64A->_bIsBitField );
	SW_EXPECT_EQUAL( SW_TRUE, pProp64B->_bIsBitField );

	sw::WideBitfieldTestActor actor;

	// 1) uint16 비트필드 쓰기 및 읽기
	pProp16A->setValue<bool>( &actor, true );
	pProp16B->setValue<bool>( &actor, false );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag16_A );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bFlag16_B );
	SW_EXPECT_TRUE( pProp16A->getValue<bool>( &actor ) );
	SW_EXPECT_FALSE( pProp16B->getValue<bool>( &actor ) );

	// 2) uint32 비트필드 쓰기 및 읽기
	pProp32A->setValue<bool>( &actor, false );
	pProp32B->setValue<bool>( &actor, true );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bFlag32_A );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag32_B );
	SW_EXPECT_FALSE( pProp32A->getValue<bool>( &actor ) );
	SW_EXPECT_TRUE( pProp32B->getValue<bool>( &actor ) );

	// 3) uint64 비트필드 쓰기 및 읽기
	pProp64A->setValue<bool>( &actor, true );
	pProp64B->setValue<bool>( &actor, true );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag64_A );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag64_B );
	SW_EXPECT_TRUE( pProp64A->getValue<bool>( &actor ) );
	SW_EXPECT_TRUE( pProp64B->getValue<bool>( &actor ) );

	// 4) 독립성 검증: uint16_B를 true로 수정해도 다른 필드 영향 없음
	pProp16B->setValue<bool>( &actor, true );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag16_A );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag16_B );
	SW_EXPECT_EQUAL( SW_FALSE, actor._bFlag32_A );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag32_B );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag64_A );
	SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag64_B );
}

/**
 * @brief [Reflection_Bitfield] uint16, uint32, uint64 비트필드 플래그 JSON, XML, Binary 직렬화 라운드트립 검증
 */
SW_TEST_CASE( Reflection_Bitfield, WideBitfieldSerializationRoundtrip )
{
	const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::WideBitfieldTestActor>();
	SW_ASSERT_NOT_NULL( pType );

	sw::WideBitfieldTestActor source;
	source._bFlag16_A = SW_TRUE;
	source._bFlag16_B = SW_FALSE;
	source._bFlag32_A = SW_FALSE;
	source._bFlag32_B = SW_TRUE;
	source._bFlag64_A = SW_TRUE;
	source._bFlag64_B = SW_TRUE;

	// 1) JSON 직렬화 & 역직렬화
	const sw::string		  json = sw::JsonSerializer::serialize( &source, *pType );
	sw::WideBitfieldTestActor jsonTarget;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &jsonTarget, *pType, json ) );
	SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bFlag16_A );
	SW_EXPECT_EQUAL( SW_FALSE, jsonTarget._bFlag16_B );
	SW_EXPECT_EQUAL( SW_FALSE, jsonTarget._bFlag32_A );
	SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bFlag32_B );
	SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bFlag64_A );
	SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bFlag64_B );

	// 2) XML 직렬화 & 역직렬화
	const sw::string		  xml = sw::XmlSerializer::serialize( &source, *pType );
	sw::WideBitfieldTestActor xmlTarget;
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &xmlTarget, *pType, xml ) );
	SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bFlag16_A );
	SW_EXPECT_EQUAL( SW_FALSE, xmlTarget._bFlag16_B );
	SW_EXPECT_EQUAL( SW_FALSE, xmlTarget._bFlag32_A );
	SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bFlag32_B );
	SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bFlag64_A );
	SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bFlag64_B );

	// 3) Binary 직렬화 & 역직렬화
	sw::vector<uint8> listBin;
	sw::BinarySerializer::serialize( &source, *pType, listBin );
	sw::WideBitfieldTestActor binTarget;
	SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &binTarget, *pType, listBin.data(), listBin.size() ) );
	SW_EXPECT_EQUAL( SW_TRUE, binTarget._bFlag16_A );
	SW_EXPECT_EQUAL( SW_FALSE, binTarget._bFlag16_B );
	SW_EXPECT_EQUAL( SW_FALSE, binTarget._bFlag32_A );
	SW_EXPECT_EQUAL( SW_TRUE, binTarget._bFlag32_B );
	SW_EXPECT_EQUAL( SW_TRUE, binTarget._bFlag64_A );
	SW_EXPECT_EQUAL( SW_TRUE, binTarget._bFlag64_B );
}

/**
 * @brief [Reflection_Cast] ReflectionCast 헬퍼 (HasStaticType, castTo, isA) 다형 상속 계층 검증
 */
SW_TEST_CASE( Reflection_Cast, TypeTraitsAndPolymorphicCast )
{
	// 1) 타입 트레이트 정적 검증
	SW_EXPECT_TRUE( sw::HasGetTypeInfo_v<sw::TestScriptComponent> );
	SW_EXPECT_TRUE( sw::HasOwnReflectBody_v<sw::TestScriptComponent> );
	SW_EXPECT_TRUE( sw::HasStaticType_v<sw::TestScriptComponent> );
	SW_EXPECT_TRUE( sw::HasStaticType_v<sw::TestDerivedScriptComponent> );
	SW_EXPECT_TRUE( sw::HasStaticType_v<sw::TestGrandChildScriptComponent> );

	// 2) castTo & isA (상속 계층 업캐스트, 동일 타입 캐스트, 무관한 타입 실패 검증)
	sw::TestGrandChildScriptComponent grandChild;
	grandChild._scriptSpeed		= 5.0f;
	grandChild._grandChildSpeed = 10.0f;

	// 동일 타입 캐스트
	sw::TestGrandChildScriptComponent* pSelf = sw::castTo<sw::TestGrandChildScriptComponent>( &grandChild );
	SW_ASSERT_NOT_NULL( pSelf );
	SW_EXPECT_EQUAL( 10.0f, pSelf->_grandChildSpeed );

	// 파생 -> 부모 상속 계층 업캐스트
	sw::TestDerivedScriptComponent* pDerived = sw::castTo<sw::TestDerivedScriptComponent>( &grandChild );
	SW_ASSERT_NOT_NULL( pDerived );
	SW_EXPECT_EQUAL( 5.0f, pDerived->_scriptSpeed );

	sw::TestScriptComponent* pBase = sw::castTo<sw::TestScriptComponent>( &grandChild );
	SW_ASSERT_NOT_NULL( pBase );
	SW_EXPECT_EQUAL( 5.0f, pBase->_scriptSpeed );

	// 무관한 타입 간 캐스트 실패 검증
	sw::SampleTestActor* pUnrelated = sw::castTo<sw::SampleTestActor>( &grandChild );
	SW_EXPECT_NULL( pUnrelated );

	// isA 검증
	SW_EXPECT_TRUE( sw::isA<sw::TestScriptComponent>( &grandChild ) );
	SW_EXPECT_TRUE( sw::isA<sw::TestDerivedScriptComponent>( &grandChild ) );
	SW_EXPECT_TRUE( sw::isA<sw::TestGrandChildScriptComponent>( &grandChild ) );
	SW_EXPECT_FALSE( sw::isA<sw::SampleTestActor>( &grandChild ) );

	// nullptr 안전성
	sw::TestGrandChildScriptComponent* pNull = nullptr;
	SW_EXPECT_NULL( sw::castTo<sw::TestDerivedScriptComponent>( pNull ) );
	SW_EXPECT_FALSE( sw::isA<sw::TestDerivedScriptComponent>( pNull ) );
}

/**
 * @brief [Reflection_EnumNames] ContainerKind 및 FunctionNetRole 이름 변환 및 파싱 검증
 */
SW_TEST_CASE( Reflection_EnumNames, ContainerKindAndNetRoleNames )
{
	// 1) ContainerKind 변환 및 파싱
	sw::ContainerKind parsedKind = sw::ContainerKind::None;
	SW_EXPECT_TRUE( sw::tryParseContainerKind( "Sequence", parsedKind ) );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::ContainerKind::Sequence ), static_cast<uint32>( parsedKind ) );

	SW_EXPECT_TRUE( sw::tryParseContainerKind( "Map", parsedKind ) );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::ContainerKind::Map ), static_cast<uint32>( parsedKind ) );

	SW_EXPECT_FALSE( sw::tryParseContainerKind( "InvalidKind", parsedKind ) );

	SW_EXPECT_STREQ( "Sequence", sw::toString( sw::ContainerKind::Sequence ) );
	SW_EXPECT_STREQ( "Map", sw::toString( sw::ContainerKind::Map ) );
	SW_EXPECT_STREQ( "sw::ContainerKind::Sequence", sw::toCppExpr( sw::ContainerKind::Sequence ) );
	SW_EXPECT_STREQ( "Vector", sw::defaultContainerWrapperStem( sw::ContainerKind::Sequence ) );
	SW_EXPECT_STREQ( "Map", sw::defaultContainerWrapperStem( sw::ContainerKind::Map ) );

	// 2) FunctionNetRole 변환 및 파싱
	sw::FunctionNetRole parsedRole = sw::FunctionNetRole::Local;
	SW_EXPECT_TRUE( sw::tryParseFunctionNetRole( "Server", parsedRole ) );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::FunctionNetRole::Server ), static_cast<uint32>( parsedRole ) );

	SW_EXPECT_TRUE( sw::tryParseFunctionNetRole( "Client", parsedRole ) );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::FunctionNetRole::Client ), static_cast<uint32>( parsedRole ) );

	SW_EXPECT_FALSE( sw::tryParseFunctionNetRole( "InvalidRole", parsedRole ) );

	SW_EXPECT_STREQ( "Server", sw::toString( sw::FunctionNetRole::Server ) );
	SW_EXPECT_STREQ( "Client", sw::toString( sw::FunctionNetRole::Client ) );
	SW_EXPECT_STREQ( "sw::FunctionNetRole::Server", sw::toCppExpr( sw::FunctionNetRole::Server ) );
}

/**
 * @brief [Reflection_ReflectAny] ReflectAny 직접 생성, 비어있음 검사, 값 추출 및 타입 불일치 실패 검증
 */
SW_TEST_CASE( Reflection_ReflectAny, ReflectAnyDirectMakeAndExtract )
{
	// 1) 빈 ReflectAny
	sw::ReflectAny emptyAny;
	SW_EXPECT_TRUE( emptyAny.empty() );

	// 2) PolyPayloadA TypeInfo 기반 ReflectAny 생성
	const sw::TypeInfo* pTypeInfo = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::PolyPayloadA" ) );
	SW_ASSERT_NOT_NULL( pTypeInfo );

	sw::PolyPayloadA source;
	source._a = 12345;

	sw::ReflectAny anyValue = sw::ReflectAny::makeFrom( *pTypeInfo, &source );
	SW_EXPECT_FALSE( anyValue.empty() );
	SW_EXPECT_TRUE( anyValue._typeFqn == sw::hashed_string( "sw::PolyPayloadA" ) );

	// 3) 올바른 타입으로 추출
	sw::PolyPayloadA extracted;
	SW_EXPECT_TRUE( anyValue.tryGetFrom( *pTypeInfo, &extracted ) );
	SW_EXPECT_EQUAL( 12345, extracted._a );

	// 4) 다른 타입으로 추출 시 실패 검증
	const sw::TypeInfo* pWrongType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::SampleTestActor" ) );
	SW_ASSERT_NOT_NULL( pWrongType );
	sw::SampleTestActor wrongTarget;
	SW_EXPECT_FALSE( anyValue.tryGetFrom( *pWrongType, &wrongTarget ) );
}
