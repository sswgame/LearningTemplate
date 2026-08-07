/**
 * @file TestReflection.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "TestFramework/TestModuleHeads.h"
#include "Core/Common/Common.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Reflection/Serializer.h"
#include "TestSampleActor.h"
#include <deque>

namespace sw
{
	struct DummyBase
	{
	};

	struct DummyActor
	{
		int32		_hp	   = 0;
		std::string _name  = "";
		float32		_speed = 0.0f;
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
		int32						 _id	= 101;
		std::string					 _title = "HeroData";
		int64						 _flags = static_cast<int64>( DummyBitFlag::OptionA ) | static_cast<int64>( DummyBitFlag::OptionC );
		std::vector<int32>			 _scores;
		std::map<std::string, int32> _stats;
	};
} // namespace sw

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
		info._propertyList =
			{
				{	  sw::hashed_string( "_hp" ),		  sw::hashed_string( "int32" ),
				  offsetof( sw::DummyActor,	_hp ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
				{ sw::hashed_string( "_name" ), sw::hashed_string( "std::string" ),
				  offsetof( sw::DummyActor,	_name ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
				{sw::hashed_string( "_speed" ),	 sw::hashed_string( "float32" ),
				  offsetof( sw::DummyActor, _speed ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		};
		registry.registerClass( info );
	}

	{
		sw::TypeInfo info;
		info._name				 = sw::hashed_string( "ComplexData" );
		info._fullyQualifiedName = sw::hashed_string( "sw::ComplexData" );
		info._parentFQN			 = sw::hashed_string( "" );
		info._size				 = sizeof( sw::ComplexData );
		info._propertyList =
			{
				{ sw::hashed_string( "_id" ), sw::hashed_string( "int32" ),
				  offsetof( sw::ComplexData, _id ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
				{ sw::hashed_string( "_title" ), sw::hashed_string( "std::string" ),
				  offsetof( sw::ComplexData, _title ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
				{ sw::hashed_string( "_flags" ), sw::hashed_string( "sw::DummyBitFlag" ),
				  offsetof( sw::ComplexData, _flags ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
				{ sw::hashed_string( "_scores" ), sw::hashed_string( "std::vector<int32>" ),
				  offsetof( sw::ComplexData, _scores ), true, sw::ContainerKind::Sequence, sw::hashed_string( "int32" ), sw::hashed_string(),
				  std::make_shared<sw::VectorWrapper<std::vector<int32>>>() },
				{ sw::hashed_string( "_stats" ), sw::hashed_string( "std::map<std::string, int32>" ),
				  offsetof( sw::ComplexData, _stats ), true, sw::ContainerKind::Map, sw::hashed_string( "int32" ), sw::hashed_string( "std::string" ),
				  std::make_shared<sw::MapWrapper<std::map<std::string, int32>>>() },
		};
		registry.registerClass( info );
	}
}

static void RegisterEnums( sw::TypeRegistry& registry )
{

	{
		sw::EnumInfo info;
		info._name				 = sw::hashed_string( "DummyType" );
		info._fullyQualifiedName = sw::hashed_string( "sw::DummyType" );
		info._bIsBitFlag		 = false;
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
		info._bIsBitFlag		 = true;
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
	RegistrarInit()
	{
		static sw::TypeRegistrar s_regType( &RegisterTypes, swTestTypeHead() );
		static sw::EnumRegistrar s_regEnum( &RegisterEnums, swTestEnumHead() );
	}
};

static RegistrarInit s_RegistrarInit;

SW_TEST_CASE( Reflection_TypeRegistry, FindRegisteredClass )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );

	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_EQUAL( std::string( "DummyActor" ), std::string( info->_name.c_str() ) );
	SW_EXPECT_EQUAL( sizeof( sw::DummyActor ), info->_size );
}

SW_TEST_CASE( Reflection_TypeRegistry, FindNonExistentClass )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::NotExist" ) );

	SW_EXPECT_TRUE( info == nullptr );
}

SW_TEST_CASE( Reflection_TypeInfo, PropertyCount )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( info->_propertyList.size() ) );
}

SW_TEST_CASE( Reflection_TypeInfo, FindExistingProperty )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	const sw::PropertyInfo* prop = info->findProperty( sw::hashed_string( "_hp" ) );
	SW_EXPECT_TRUE( prop != nullptr );
	if ( prop == nullptr )
		return;

	SW_EXPECT_EQUAL( std::string( "_hp" ), std::string( prop->_name.c_str() ) );
	SW_EXPECT_EQUAL( std::string( "int32" ), std::string( prop->_typeName.c_str() ) );
	SW_EXPECT_FALSE( prop->_bIsContainer );
}

SW_TEST_CASE( Reflection_TypeInfo, PropertyMetadataSupport )
{
	sw::PropertyInfo prop;
	prop._metadata._category  = "Rendering";
	prop._metadata._tooltip	  = "Controls light intensity";
	prop._metadata._minRange  = 0.0f;
	prop._metadata._maxRange  = 100.0f;
	prop._metadata._bHasRange = true;

	SW_EXPECT_EQUAL( std::string( "Rendering" ), prop._metadata._category );
	SW_EXPECT_EQUAL( std::string( "Controls light intensity" ), prop._metadata._tooltip );
	SW_EXPECT_NEAR_EQUAL( 0.0f, prop._metadata._minRange, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 100.0f, prop._metadata._maxRange, 1e-4f );
	SW_EXPECT_TRUE( prop._metadata._bHasRange );
}

SW_TEST_CASE( Reflection_Serialization, ObjectDiffSerializationDelta )
{
	const sw::TypeInfo* info = sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info != nullptr )
	{
		sw::DummyActor cdoActor;
		sw::DummyActor modActor;
		modActor._hp = 999;

		std::vector<uint8> diffBuf;
		bool			   diffOk = sw::ObjectDiffSerializer::serializeDiff( diffBuf, &cdoActor, &modActor, *info );
		SW_EXPECT_TRUE( diffOk );
		SW_EXPECT_FALSE( diffBuf.empty() );
	}
}

SW_TEST_CASE( Reflection_TypeInfo, FindNonExistentProperty )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	const sw::PropertyInfo* prop = info->findProperty( sw::hashed_string( "_notExist" ) );
	SW_EXPECT_TRUE( prop == nullptr );
}

SW_TEST_CASE( Reflection_TypeInfo, IsA_SameType )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_TRUE( info->isA( sw::hashed_string( "sw::DummyActor" ) ) );
}

SW_TEST_CASE( Reflection_TypeInfo, IsA_ParentType )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_TRUE( info->isA( sw::hashed_string( "sw::DummyBase" ) ) );
}

SW_TEST_CASE( Reflection_TypeInfo, IsA_UnrelatedType )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_FALSE( info->isA( sw::hashed_string( "sw::UnrelatedType" ) ) );
}

SW_TEST_CASE( Reflection_PropertyInfo, OffsetCorrectness )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
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
		SW_EXPECT_EQUAL( offsetof( sw::DummyActor, _hp ), hpProp->_offset );

	if ( nameProp != nullptr )
		SW_EXPECT_EQUAL( offsetof( sw::DummyActor, _name ), nameProp->_offset );

	if ( speedProp != nullptr )
		SW_EXPECT_EQUAL( offsetof( sw::DummyActor, _speed ), speedProp->_offset );
}

SW_TEST_CASE( Reflection_PropertyInfo, GetValuePtr )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
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

SW_TEST_CASE( Reflection_PropertyInfo, SetValue )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
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

SW_TEST_CASE( Reflection_PropertyInfo, SetValue_NoDuplicateWrite )
{
	const sw::TypeInfo* info =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
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

SW_TEST_CASE( Reflection_Containers, VectorWrapper )
{
	std::vector<int32>					  vec = { 10, 20, 30 };
	sw::VectorWrapper<std::vector<int32>> wrapper;

	SW_EXPECT_EQUAL( 3u, wrapper.getSize( &vec ) );
	SW_EXPECT_EQUAL( 10, *static_cast<int32*>( wrapper.getElement( &vec, 0 ) ) );
	SW_EXPECT_EQUAL( 30, *static_cast<int32*>( wrapper.getElement( &vec, 2 ) ) );

	wrapper.addElementDefault( &vec );
	SW_EXPECT_EQUAL( 4u, wrapper.getSize( &vec ) );
	SW_EXPECT_EQUAL( 0, *static_cast<int32*>( wrapper.getElement( &vec, 3 ) ) );

	wrapper.clear( &vec );
	SW_EXPECT_EQUAL( 0u, wrapper.getSize( &vec ) );
}

SW_TEST_CASE( Reflection_Containers, ListWrapper )
{
	std::list<std::string>					lst = { "alpha", "beta" };
	sw::ListWrapper<std::list<std::string>> wrapper;

	SW_EXPECT_EQUAL( 2u, wrapper.getSize( &lst ) );
	SW_EXPECT_EQUAL( std::string( "alpha" ), *static_cast<std::string*>( wrapper.getElement( &lst, 0 ) ) );

	wrapper.clear( &lst );
	SW_EXPECT_EQUAL( 0u, wrapper.getSize( &lst ) );
}

SW_TEST_CASE( Reflection_Containers, DequeWrapper )
{
	std::deque<float32>					  dq = { 1.5f, 2.5f, 3.5f };
	sw::DequeWrapper<std::deque<float32>> wrapper;

	SW_EXPECT_EQUAL( 3u, wrapper.getSize( &dq ) );
	SW_EXPECT_NEAR_EQUAL( 2.5f, *static_cast<float32*>( wrapper.getElement( &dq, 1 ) ), 0.001f );
}

SW_TEST_CASE( Reflection_Containers, SetWrapper )
{
	std::set<int32>					st = { 100, 200, 300 };
	sw::SetWrapper<std::set<int32>> wrapper;

	SW_EXPECT_EQUAL( 3u, wrapper.getSize( &st ) );
	SW_EXPECT_EQUAL( 100, *static_cast<const int32*>( wrapper.getElementConst( &st, 0 ) ) );
}

SW_TEST_CASE( Reflection_Containers, MapWrapper )
{
	std::map<std::string, int32> mp = {
		{"Atk", 50},
		{"Def", 20}
	  };
	sw::MapWrapper<std::map<std::string, int32>> wrapper;

	SW_EXPECT_EQUAL( 2u, wrapper.getSize( &mp ) );

	int elementCount = 0;
	wrapper.forEach( &mp, [&]( const void* kPtr, const void* vPtr )
	{
		elementCount++;
		const auto* key = static_cast<const std::string*>( kPtr );
		const auto* val = static_cast<const int32*>( vPtr );
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

	std::string newKey = "Speed";
	int32		newVal = 10;
	wrapper.insertKeyValue( &mp, &newKey, &newVal );
	SW_EXPECT_EQUAL( 3u, wrapper.getSize( &mp ) );
	SW_EXPECT_EQUAL( 10, mp["Speed"] );
}

SW_TEST_CASE( Reflection_Containers, AdditionalContainerWrappers )
{

	std::array<int32, 4>				   arr = { 1, 2, 3, 4 };
	sw::ArrayWrapper<std::array<int32, 4>> arrWrapper;
	SW_EXPECT_EQUAL( 4u, arrWrapper.getSize( &arr ) );
	SW_EXPECT_EQUAL( 3, *static_cast<int32*>( arrWrapper.getElement( &arr, 2 ) ) );

	std::unordered_set<std::string>							 uSet = { "alpha", "beta" };
	sw::UnorderedSetWrapper<std::unordered_set<std::string>> uSetWrapper;
	SW_EXPECT_EQUAL( 2u, uSetWrapper.getSize( &uSet ) );

	std::unordered_map<std::string, int32> uMap = {
		{ "Hp", 100 }
	  };
	sw::UnorderedMapWrapper<std::unordered_map<std::string, int32>> uMapWrapper;
	SW_EXPECT_EQUAL( 1u, uMapWrapper.getSize( &uMap ) );
	SW_EXPECT_EQUAL( sizeof( std::string ), uMapWrapper.getKeySize() );
	SW_EXPECT_EQUAL( sizeof( int32 ), uMapWrapper.getValueSize() );
}

SW_TEST_CASE( Reflection_EnumBitFlag, BitFlagDetectionAndToString )
{
	const sw::EnumInfo* info =
		sw::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyBitFlag" ) );

	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_TRUE( info->_bIsBitFlag );

	int64			  flagVal = static_cast<int64>( sw::DummyBitFlag::OptionA ) | static_cast<int64>( sw::DummyBitFlag::OptionC );
	sw::hashed_string flagStr = info->toStringFlags( flagVal );

	SW_EXPECT_EQUAL( std::string( "OptionA | OptionC" ), std::string( flagStr.c_str() ) );
}

SW_TEST_CASE( Reflection_EnumBitFlag, StringFlagsToValue )
{
	const sw::EnumInfo* info =
		sw::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyBitFlag" ) );

	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	int64 val	   = info->stringFlagsToValue( "OptionB | OptionC" );
	int64 expected = static_cast<int64>( sw::DummyBitFlag::OptionB ) | static_cast<int64>( sw::DummyBitFlag::OptionC );

	SW_EXPECT_EQUAL( expected, val );
}

SW_TEST_CASE( Reflection_Serialization, BinaryRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id		= 999;
	src._title	= "BinaryTest";
	src._flags	= static_cast<int64>( sw::DummyBitFlag::OptionB );
	src._scores = { 100, 200, 300 };

	std::vector<uint8> buffer;
	sw::BinarySerializer::serialize( &src, *typeInfo, buffer );
	SW_EXPECT_TRUE( buffer.empty() == false );

	sw::ComplexData dst;
	bool			success = sw::BinarySerializer::deserialize( &dst, *typeInfo, buffer.data(), buffer.size() );

	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 999, dst._id );
	SW_EXPECT_EQUAL( std::string( "BinaryTest" ), dst._title );
	SW_EXPECT_EQUAL( static_cast<int64>( sw::DummyBitFlag::OptionB ), dst._flags );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( dst._scores.size() ) );
	if ( dst._scores.size() == 3 )
	{
		SW_EXPECT_EQUAL( 100, dst._scores[0] );
		SW_EXPECT_EQUAL( 300, dst._scores[2] );
	}
}

SW_TEST_CASE( Reflection_Serialization, JsonRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id		= 777;
	src._title	= "JsonTest";
	src._scores = { 5, 10, 15 };

	std::string json = sw::JsonSerializer::serialize( &src, *typeInfo );
	SW_EXPECT_TRUE( json.empty() == false );

	sw::ComplexData dst;
	bool			success = sw::JsonSerializer::deserialize( &dst, *typeInfo, json );

	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 777, dst._id );
	SW_EXPECT_EQUAL( std::string( "JsonTest" ), dst._title );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( dst._scores.size() ) );
}

SW_TEST_CASE( Reflection_Serialization, XmlRapidXmlRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( !typeInfo )
		return;

	sw::ComplexData src;
	src._id		= 888;
	src._title	= "XmlTest";
	src._scores = { 1, 2, 3, 4 };

	std::string xml = sw::XmlSerializer::serialize( &src, *typeInfo );
	SW_EXPECT_TRUE( !xml.empty() );

	sw::ComplexData dst;
	bool			success = sw::XmlSerializer::deserialize( &dst, *typeInfo, xml );

	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 888, dst._id );
	SW_EXPECT_EQUAL( std::string( "XmlTest" ), dst._title );
	SW_EXPECT_EQUAL( 4u, static_cast<uint32>( dst._scores.size() ) );
}

struct SimpleXmlBackend : public sw::IXmlBackend
{
	std::string									 result;
	std::unordered_map<std::string, std::string> kvMap;

	void initXmlSerialization( const utf8* rootTag ) override { result += "<" + std::string( rootTag ) + ">"; }
	void writeValue( const utf8* tag, const utf8* value ) override
	{
		result += "<" + std::string( tag ) + ">" + std::string( value ) + "</" + std::string( tag ) + ">";
	}
	void		beginArray( const utf8* tag ) override { result += "<" + std::string( tag ) + ">"; }
	void		writeArrayItem( const utf8* value ) override { result += "<item>" + std::string( value ) + "</item>"; }
	void		endArray() override { result += "</array>"; }
	void		beginMap( const utf8* tag ) override { result += "<" + std::string( tag ) + ">"; }
	void		beginMapEntry() override {}
	void		writeMapKey( const utf8* key ) override { result += "<key>" + std::string( key ) + "</key>"; }
	void		writeMapValue( const utf8* value ) override { result += "<value>" + std::string( value ) + "</value>"; }
	void		endMapEntry() override {}
	void		endMap() override { result += "</map>"; }
	std::string endSerialize() override { return result; }

	bool initXmlDeserialization( const utf8* xmlStr, const utf8* rootTag ) override
	{
		(void)rootTag;
		std::string str( xmlStr );
		size_t		pos = 0;
		while ( ( pos = str.find( '<', pos ) ) != std::string::npos )
		{
			size_t closeTag = str.find( '>', pos );
			if ( closeTag == std::string::npos )
				break;
			std::string tag = str.substr( pos + 1, closeTag - pos - 1 );
			if ( !tag.empty() && tag[0] != '/' )
			{
				size_t endTagPos = str.find( "</" + tag + ">", closeTag );
				if ( endTagPos != std::string::npos )
				{
					std::string val = str.substr( closeTag + 1, endTagPos - closeTag - 1 );
					kvMap[tag]		= val;
				}
			}
			pos = closeTag + 1;
		}
		return true;
	}
	bool readValue( const utf8* tag, std::string& outValue ) override
	{
		auto it = kvMap.find( std::string( tag ) );
		if ( it != kvMap.end() )
		{
			outValue = it->second;
			return true;
		}
		return false;
	}
	bool iterateArray( const utf8*, const sw::XmlArrayItemDelegate& ) override { return false; }
	bool iterateMap( const utf8*, const sw::XmlMapItemDelegate& ) override { return false; }
};

SW_TEST_CASE( Reflection_Serialization, CustomXmlBackend )
{
	const sw::TypeInfo* typeInfo =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id	   = 999;
	src._title = "CustomBackend";

	SimpleXmlBackend backend;
	std::string		 xml = sw::XmlSerializer::serialize( &src, *typeInfo, backend );
	SW_EXPECT_TRUE( xml.empty() == false );

	sw::ComplexData	 dst;
	SimpleXmlBackend readBackend;
	bool			 success = sw::XmlSerializer::deserialize( &dst, *typeInfo, readBackend, xml );
	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 999, dst._id );
	SW_EXPECT_EQUAL( std::string( "CustomBackend" ), dst._title );
}

SW_TEST_CASE( Reflection_Serialization, CustomSerializeContext )
{
	sw::SerializeContext customCtx = sw::SerializeContext::getDefault();

	customCtx.registerTextHandler(
		sw::hashed_string( "int32" ),
		[]( const void* ptr )
	{ return std::to_string( ( *static_cast<const int32*>( ptr ) ) * 10 ); },
		[]( void* ptr, std::string_view s )
	{ *static_cast<int32*>( ptr ) = std::stoi( std::string( s ) ) / 10; return true; } );

	const sw::TypeInfo* typeInfo =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( typeInfo == nullptr )
		return;

	sw::ComplexData src;
	src._id = 50;

	std::string json = sw::JsonSerializer::serialize( &src, *typeInfo, customCtx );
	SW_EXPECT_TRUE( json.find( "\"_id\":500" ) != std::string::npos );

	sw::ComplexData dst;
	bool			success = sw::JsonSerializer::deserialize( &dst, *typeInfo, json, customCtx );
	SW_EXPECT_TRUE( success );
	SW_EXPECT_EQUAL( 50, dst._id );
}

SW_TEST_CASE( Reflection_EnumInfo, FindRegisteredEnum )
{
	const sw::EnumInfo* info =
		sw::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyType" ) );

	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
		return;

	SW_EXPECT_EQUAL( std::string( "DummyType" ), std::string( info->_name.c_str() ) );
}

SW_TEST_CASE( Reflection_EnumInfo, ValueToString )
{
	const sw::EnumInfo* info =
		sw::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyType" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
	{
		return;
	}

	SW_EXPECT_EQUAL( std::string( "None" ), std::string( info->toString( 0 ).c_str() ) );
	SW_EXPECT_EQUAL( std::string( "TypeA" ), std::string( info->toString( 1 ).c_str() ) );
	SW_EXPECT_EQUAL( std::string( "TypeB" ), std::string( info->toString( 2 ).c_str() ) );
}

SW_TEST_CASE( Reflection_EnumInfo, InvalidValueReturnsDefault )
{
	const sw::EnumInfo* info =
		sw::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyType" ) );
	SW_EXPECT_TRUE( info != nullptr );
	if ( info == nullptr )
	{
		return;
	}

	sw::hashed_string name = info->toString( 999 );
	SW_EXPECT_TRUE( name == sw::hashed_string() );
}

SW_TEST_CASE( Reflection_InnerTypes, FindOuterStruct )
{
	const sw::TypeInfo* typeInfoFqn =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct" ) );
	SW_EXPECT_TRUE( typeInfoFqn != nullptr );

	const sw::TypeInfo* typeInfoShort =
		sw::getTypeRegistry().findType( sw::hashed_string( "OuterStruct" ) );
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

SW_TEST_CASE( Reflection_InnerTypes, FindInnerStruct )
{
	const sw::TypeInfo* typeInfoFqn =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerStruct" ) );
	SW_EXPECT_TRUE( typeInfoFqn != nullptr );

	const sw::TypeInfo* typeInfoShort =
		sw::getTypeRegistry().findType( sw::hashed_string( "InnerStruct" ) );
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
		SW_EXPECT_EQUAL( std::string( "TestNested" ), *propData->getValuePtr<std::string>( &instance ) );
		SW_EXPECT_NEAR_EQUAL( 9.5f, *propScore->getValuePtr<float32>( &instance ), 0.001f );
	}
}

SW_TEST_CASE( Reflection_InnerTypes, FindInnerClass )
{
	const sw::TypeInfo* typeInfoFqn =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerClass" ) );
	SW_EXPECT_TRUE( typeInfoFqn != nullptr );

	const sw::TypeInfo* typeInfoShort =
		sw::getTypeRegistry().findType( sw::hashed_string( "InnerClass" ) );
	SW_EXPECT_TRUE( typeInfoShort != nullptr );

	if ( !typeInfoFqn )
		return;

	sw::InnerNamespaceForTest::OuterStruct::InnerClass instance;
	const sw::PropertyInfo*							   propId = typeInfoFqn->findProperty( sw::hashed_string( "_id" ) );
	SW_EXPECT_TRUE( propId != nullptr );
	if ( propId )
	{
		int64 newId = 8888;
		propId->setValue( &instance, newId );
		SW_EXPECT_EQUAL( 8888, *propId->getValuePtr<int64>( &instance ) );
	}
}

SW_TEST_CASE( Reflection_InnerTypes, FindInnerEnum )
{
	const sw::EnumInfo* enumInfoFqn =
		sw::getTypeRegistry().findEnum( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerEnum" ) );
	SW_EXPECT_TRUE( enumInfoFqn != nullptr );

	const sw::EnumInfo* enumInfoShort =
		sw::getTypeRegistry().findEnum( sw::hashed_string( "InnerEnum" ) );
	SW_EXPECT_TRUE( enumInfoShort != nullptr );

	if ( !enumInfoFqn )
		return;

	SW_EXPECT_TRUE( enumInfoFqn->_bIsBitFlag );
	uint32		combined = 1 | 4;
	std::string flagStr	 = enumInfoFqn->toStringFlags( combined ).c_str();
	SW_EXPECT_TRUE( flagStr.find( "OptionA" ) != std::string::npos );
	SW_EXPECT_TRUE( flagStr.find( "OptionC" ) != std::string::npos );
}

SW_TEST_CASE( Reflection_InnerTypes, InnerStructSerializationRoundtrip )
{
	const sw::TypeInfo* typeInfo =
		sw::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerStruct" ) );
	SW_EXPECT_TRUE( typeInfo != nullptr );
	if ( !typeInfo )
		return;

	sw::InnerNamespaceForTest::OuterStruct::InnerStruct src;
	src._innerData = "SerializationTest";
	src._score	   = 12.34f;

	std::string json = sw::JsonSerializer::serialize( &src, *typeInfo );
	SW_EXPECT_TRUE( !json.empty() );

	sw::InnerNamespaceForTest::OuterStruct::InnerStruct dstJson;
	bool												jsonOk = sw::JsonSerializer::deserialize( &dstJson, *typeInfo, json );
	SW_EXPECT_TRUE( jsonOk );
	SW_EXPECT_EQUAL( std::string( "SerializationTest" ), dstJson._innerData );
	SW_EXPECT_NEAR_EQUAL( 12.34f, dstJson._score, 0.01f );

	std::string xml = sw::XmlSerializer::serialize( &src, *typeInfo );
	SW_EXPECT_TRUE( !xml.empty() );

	sw::InnerNamespaceForTest::OuterStruct::InnerStruct dstXml;
	bool												xmlOk = sw::XmlSerializer::deserialize( &dstXml, *typeInfo, xml );
	SW_EXPECT_TRUE( xmlOk );
	SW_EXPECT_EQUAL( std::string( "SerializationTest" ), dstXml._innerData );
	SW_EXPECT_NEAR_EQUAL( 12.34f, dstXml._score, 0.01f );
}

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
	info._propertyList		 = {
		  { sw::hashed_string( "_currentHp" ), sw::hashed_string( "int32" ),
			offsetof( AliasTestActor, _currentHp ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr, sw::hashed_string( "hp" ) }
	   };

	std::string	   oldJson = "{\"hp\": 250}";
	AliasTestActor actor;
	bool		   jsonOk = sw::JsonSerializer::deserialize( &actor, info, oldJson );
	SW_EXPECT_TRUE( jsonOk );
	SW_EXPECT_EQUAL( 250, actor._currentHp );

	struct ReorderActor1
	{
		int32 _fieldA = 10;
		int32 _fieldB = 20;
	};

	sw::TypeInfo info1;
	info1._name				  = sw::hashed_string( "ReorderActor" );
	info1._fullyQualifiedName = sw::hashed_string( "sw::ReorderActor" );
	info1._size				  = sizeof( ReorderActor1 );
	info1._propertyList		  = {
		  {sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ),
			offsetof( ReorderActor1, _fieldA ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		  {sw::hashed_string( "_fieldB" ), sw::hashed_string( "int32" ),
			offsetof( ReorderActor1, _fieldB ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
	};

	ReorderActor1	   src;
	std::vector<uint8> binBuf;
	sw::BinarySerializer::serialize( &src, info1, binBuf );

	sw::TypeInfo infoReordered = info1;
	std::swap( infoReordered._propertyList[0], infoReordered._propertyList[1] );

	ReorderActor1 dst;
	dst._fieldA = 0;
	dst._fieldB = 0;
	bool binOk	= sw::BinarySerializer::deserialize( &dst, infoReordered, binBuf.data(), binBuf.size() );
	SW_EXPECT_TRUE( binOk );
	SW_EXPECT_EQUAL( 10, dst._fieldA );
	SW_EXPECT_EQUAL( 20, dst._fieldB );
}

SW_TEST_CASE( Reflection_TypeInfo, PropertyInfoMatchesName )
{
	sw::PropertyInfo prop;
	prop._name	= sw::hashed_string( "_currentHp" );
	prop._alias = sw::hashed_string( "hp" );

	SW_EXPECT_TRUE( prop.matchesName( sw::hashed_string( "_currentHp" ) ) );
	SW_EXPECT_TRUE( prop.matchesName( sw::hashed_string( "hp" ) ) );
	SW_EXPECT_TRUE( prop.matchesName( sw::hashed_string( "HP" ) ) );
	SW_EXPECT_FALSE( prop.matchesName( sw::hashed_string( "mana" ) ) );
}

SW_TEST_CASE( Reflection_EnumInfo, EnumInfoFlagsStringConversion )
{
	sw::EnumInfo info;
	info._name			 = sw::hashed_string( "ESampleFlags" );
	info._bIsBitFlag	 = true;
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
	uint32 _score = 0;
};

SW_TEST_CASE( Reflection_Binding, BiDirectionalPropertyBinding )
{
	sw::PropertyInfo prop( sw::hashed_string( "score" ), sw::hashed_string( "uint32" ), offsetof( TestBindingActor, _score ) );

	bool bCalled = false;
	auto cb		 = [&bCalled]( const sw::PropertyInfo& p, const void* inst )
	{
		(void)p;
		(void)inst;
		bCalled = true;
	};
	prop.bindOnChanged( SW_DELEGATE_LAMBDA( sw::PropertyInfo::PropertyBindingDelegate, cb ) );

	TestBindingActor actor{};
	prop.setValue( &actor, 500u );
	SW_EXPECT_EQUAL( 500u, actor._score );
	SW_EXPECT_TRUE( bCalled );
}

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
	info._propertyList		 = {
		  {sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ),
			offsetof( VersionedActor, _fieldA ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		  {sw::hashed_string( "_fieldB" ), sw::hashed_string( "int32" ),
			offsetof( VersionedActor, _fieldB ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
	 };

	std::vector<uint8> buffer;
	sw::BinarySerializer::serializeVersioned( 102, &actor, info, buffer );
	SW_EXPECT_TRUE( buffer.size() > sizeof( uint32 ) );

	VersionedActor restored;
	restored._fieldA   = 0;
	restored._fieldB   = 0;
	uint32 readVersion = 0;
	bool   ok		   = sw::BinarySerializer::deserializeVersioned( readVersion, &restored, info, buffer.data(), buffer.size() );
	SW_EXPECT_TRUE( ok );
	SW_EXPECT_EQUAL( 102u, readVersion );
	SW_EXPECT_EQUAL( 777, restored._fieldA );
	SW_EXPECT_EQUAL( 888, restored._fieldB );
}

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
	info._propertyList		 = {
		  { sw::hashed_string( "_val" ), sw::hashed_string( "int32" ),
			offsetof( SimpleJsonActor, _val ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr }
	   };

	std::string prettyStr = sw::JsonSerializer::serializePretty( &actor, info, 4 );
	SW_EXPECT_TRUE( prettyStr.find( '\n' ) != std::string::npos );
	SW_EXPECT_TRUE( prettyStr.find( "    \"_val\": 42" ) != std::string::npos );
}

SW_TEST_CASE( Reflection_TypeInfo, DynamicMethodInvoke )
{
	struct InvokableTestActor
	{
		int32 _score = 0;
		void  addScore( int32 delta ) { _score += delta; }
	} actor;

	sw::FunctionInfo funcInfo;
	funcInfo._name	   = "addScore";
	funcInfo._hashName = sw::hashed_string( "addScore" );
	auto invokerCb	   = []( void* objPtr, const sw::TaskArgs& args ) -> sw::TaskValue
	{
		static_cast<InvokableTestActor*>( objPtr )->addScore( args.get<int32>( 0 ) );
		return sw::TaskValue{};
	};
	funcInfo._invoker = SW_DELEGATE_LAMBDA( sw::Delegate<sw::TaskValue( void*, const sw::TaskArgs& )>, invokerCb );

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "InvokableTestActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::InvokableTestActor" );
	info._size				 = sizeof( InvokableTestActor );
	info._methods.push_back( funcInfo );

	sw::getTypeRegistry().registerClass( info );

	sw::TaskArgs args{ 50 };
	sw::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::InvokableTestActor" ), sw::hashed_string( "addScore" ), args );

	SW_EXPECT_EQUAL( 50, actor._score );
}

#include "Core/Reflection/EnumFlagUtils.h"
#include "Core/Reflection/Serializer.h"

enum class TestFlag : uint32
{
	None	= 0,
	Read	= 1 << 0,
	Write	= 1 << 1,
	Execute = 1 << 2
};
SW_ENUM_FLAGS( TestFlag );

SW_TEST_CASE( Reflection_EnumFlag, EnumFlagOperators )
{
	TestFlag flag = TestFlag::Read | TestFlag::Write;
	SW_EXPECT_TRUE( sw::hasFlag( flag, TestFlag::Read ) );
	SW_EXPECT_TRUE( sw::hasFlag( flag, TestFlag::Write ) );
	SW_EXPECT_FALSE( sw::hasFlag( flag, TestFlag::Execute ) );

	flag |= TestFlag::Execute;
	SW_EXPECT_TRUE( sw::hasFlag( flag, TestFlag::Execute ) );
}

SW_TEST_CASE( Reflection_Cloning, ObjectDeepCopyClone )
{
	struct CloneableActor
	{
		int32 _health = 100;
		float _speed  = 5.5f;
	} srcActor, dstActor;

	srcActor._health = 250;
	srcActor._speed	 = 12.0f;

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "CloneableActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::CloneableActor" );
	info._size				 = sizeof( CloneableActor );
	info._propertyList		 = {
		  {sw::hashed_string( "_health" ), sw::hashed_string( "int32" ), offsetof( CloneableActor, _health ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		  { sw::hashed_string( "_speed" ), sw::hashed_string( "float" ), offsetof( CloneableActor,  _speed ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
	 };

	bool cloneOk = sw::BinarySerializer::cloneObject( &dstActor, &srcActor, info );
	SW_EXPECT_TRUE( cloneOk );
	SW_EXPECT_EQUAL( 250, dstActor._health );
	SW_EXPECT_NEAR_EQUAL( 12.0f, dstActor._speed, 1e-4f );
}

SW_TEST_CASE( Reflection_FunctionMacro, AnnotatedMethodInvoke )
{
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
	funcInfo._name	   = "takeDamage";
	funcInfo._hashName = sw::hashed_string( "takeDamage" );
	auto takeDamageCb  = []( void* objPtr, const sw::TaskArgs& args ) -> sw::TaskValue
	{
		static_cast<FunctionAnnotatedActor*>( objPtr )->takeDamage( args.get<int32>( 0 ) );
		return sw::TaskValue{};
	};
	funcInfo._invoker = SW_DELEGATE_LAMBDA( sw::Delegate<sw::TaskValue( void*, const sw::TaskArgs& )>, takeDamageCb );

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "FunctionAnnotatedActor" );
	info._fullyQualifiedName = sw::hashed_string( "sw::FunctionAnnotatedActor" );
	info._size				 = sizeof( FunctionAnnotatedActor );
	info._methods.push_back( funcInfo );

	sw::getTypeRegistry().registerClass( info );

	sw::TaskArgs  args{ 35 };
	sw::TaskValue invokeResult = sw::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::FunctionAnnotatedActor" ), sw::hashed_string( "takeDamage" ), args );

	SW_EXPECT_EQUAL( 65, actor._health );
}
