/**
 * @file CodeGenerator.cpp
 * @brief Reflection .gen.cpp 코드 생성기 구현
 */
#include "CodeGenerator.h"
#include "ParserUtil.h"
#include "Core/Common/Common.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/String/formatString.h"

namespace sw::tool
{
	CodeGenerator::CodeGenerator(
		const std::vector<ParsedTypeInfo>& types,
		const std::vector<ParsedEnumInfo>& enums,
		const std::string&				   sourceFilePath,
		const std::string&				   outputDir )
		: _types( types )
		, _enums( enums )
		, _sourceFilePath( sourceFilePath )
		, _outputDir( outputDir )
	{
	}

	bool CodeGenerator::isComponentDerived( const ParsedTypeInfo& typeInfo )
	{
		if ( typeInfo.name == "Component" || typeInfo.name == "SceneComponent" )
			return true;
		if ( typeInfo.fullyQualifiedName == "sw::Component" || typeInfo.fullyQualifiedName == "sw::SceneComponent" )
			return true;
		if ( typeInfo.parentFQN == "sw::Component" || typeInfo.parentFQN == "sw::SceneComponent" )
			return true;
		// Heuristic for deeper Component derivatives when only immediate parent is recorded.
		if ( typeInfo.parentFQN.find( "Component" ) != std::string::npos )
			return true;
		return false;
	}

	bool CodeGenerator::generate()
	{
		FileUtil::createDirectory( _outputDir );
		_outputFilePath = makeGeneratedCppPath( _outputDir, _sourceFilePath );

		CodeEmitBuffer buffer;

		if ( _types.empty() && _enums.empty() )
		{
			buffer.appendFormat( "// No reflected types found in %#\n", _sourceFilePath );
		}
		else
		{
			bool bNeedsComponentFactory = false;
			for ( const ParsedTypeInfo& typeInfo : _types )
			{
				if ( isComponentDerived( typeInfo ) )
				{
					bNeedsComponentFactory = true;
					break;
				}
			}

			emitFileHeader( buffer, bNeedsComponentFactory );
			buffer.append( "namespace sw::generated\n{\n" );

			for ( const ParsedTypeInfo& typeInfo : _types )
			{
				emitTypeRegistrar( buffer, typeInfo );
				if ( isComponentDerived( typeInfo ) )
					emitComponentFactoryRegistrar( buffer, typeInfo );
			}

			for ( const ParsedEnumInfo& enumInfo : _enums )
				emitEnumRegistrar( buffer, enumInfo );

			buffer.append( "} // namespace sw::generated\n" );
		}

		const std::string newContent( buffer.view() );

		if ( FileUtil::isFileExist( _outputFilePath ) )
		{
			const std::string existingContent = readTextFile( _outputFilePath );
			if ( existingContent.empty() == false && existingContent == newContent )
			{
				SW_LOG_INFO( "[CodeGenerator] Incremental check: %# is up-to-date, skipping write.", _outputFilePath );
				return true;
			}
		}

		if ( writeTextFile( _outputFilePath, newContent ) == false )
		{
			SW_LOG_ERROR( "[CodeGenerator] Failed to open output: %#", _outputFilePath );
			return false;
		}

		SW_LOG_INFO( "[CodeGenerator] Generated: %#", _outputFilePath );
		return true;
	}

	void CodeGenerator::emitFileHeader( CodeEmitBuffer& out, bool bNeedsComponentFactory ) const
	{
		out.append( "// AUTO-GENERATED -- DO NOT EDIT\n" );
		out.appendFormat( "// Source: %#\n\n", _sourceFilePath );
		out.append( "#include \"Core/Common/Common.h\"\n" );
		out.append( "#include \"Core/Reflection/ReflectionCore.h\"\n" );
		if ( bNeedsComponentFactory )
		{
			out.append( "#include \"Core/Object/ComponentManager.h\"\n" );
			out.append( "#include \"Core/Common/CoreServices.h\"\n" );
		}
		out.appendFormat( "#include \"%#\"\n\n", _sourceFilePath );
	}

	void CodeGenerator::emitComponentFactoryRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
	{
		const std::string id = sanitizeIdentifier( typeInfo.fullyQualifiedName );

		out.appendFormat( "\t// ── %# factory ──────────────────────\n", typeInfo.fullyQualifiedName );
		out.appendFormat( "\tstruct %#_FactoryRegistrar\n\t{\n", id );
		out.append( "\t\tstatic void RegisterFactory(sw::ComponentManager& manager)\n\t\t{\n" );
		out.appendFormat( "\t\t\tmanager.registerComponentType<%#>( sw::hashed_string( \"%#\" ) );\n",
						  typeInfo.fullyQualifiedName, typeInfo.name );
		out.append( "\t\t}\n\n" );
		out.appendFormat( "\t\t%#_FactoryRegistrar()\n\t\t{\n", id );
		out.append( "\t\t\tstatic sw::ComponentFactoryRegistrar reg( &RegisterFactory, SW_COMPONENT_FACTORY_MODULE_HEAD() );\n" );
		out.append( "\t\t}\n\t};\n" );
		out.appendFormat( "\tstatic %#_FactoryRegistrar g_%#_factory_registrar;\n\n", id, id );
	}

	void CodeGenerator::emitTypeRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
	{
		const std::string id = sanitizeIdentifier( typeInfo.fullyQualifiedName );

		out.appendFormat( "\t// ── %# ──────────────────────────────\n", typeInfo.fullyQualifiedName );
		out.appendFormat( "\tstruct %#_Registrar\n\t{\n", id );
		out.append( "\t\tstatic void RegisterType(sw::TypeRegistry& registry)\n\t\t{\n" );
		out.append( "\t\t\tsw::TypeInfo info;\n" );
		out.appendFormat( "\t\t\tinfo._name               = sw::hashed_string( \"%#\" );\n", typeInfo.name );
		out.appendFormat( "\t\t\tinfo._fullyQualifiedName = sw::hashed_string( \"%#\" );\n", typeInfo.fullyQualifiedName );
		out.appendFormat( "\t\t\tinfo._parentFQN          = sw::hashed_string( \"%#\" );\n", typeInfo.parentFQN );
		out.appendFormat( "\t\t\tinfo._size               = sizeof( %# );\n", typeInfo.fullyQualifiedName );

		if ( typeInfo.properties.empty() == false )
		{
			out.append( "\t\t\tinfo._propertyList =\n\t\t\t{\n" );
			for ( const ParsedPropertyInfo& prop : typeInfo.properties )
			{
				if ( prop.isContainer )
				{
					const utf8* kindStr = ( prop.containerKind == "Map" ) ? "sw::ContainerKind::Map" : "sw::ContainerKind::Sequence";

					utf8 wrapperType[constant::kMaxBuffer1024];
					formatstring( wrapperType, constant::kMaxBuffer1024,
								  "sw::%#Wrapper<decltype(std::declval<%#>().%#)>",
								  prop.containerType,
								  typeInfo.fullyQualifiedName,
								  prop.name );

					out.appendFormat(
						"\t\t\t\t{ sw::hashed_string( \"%#\" ),\n"
						"\t\t\t\t  sw::hashed_string( \"%#\" ),\n"
						"\t\t\t\t  offsetof( %#, %# ),\n"
						"\t\t\t\t  true,\n"
						"\t\t\t\t  %#,\n"
						"\t\t\t\t  sw::hashed_string( \"%#\" ),\n"
						"\t\t\t\t  sw::hashed_string( \"%#\" ),\n"
						"\t\t\t\t  std::make_shared<%#>() },\n",
						prop.name,
						prop.typeName,
						typeInfo.fullyQualifiedName,
						prop.name,
						kindStr,
						prop.elementTypeName,
						prop.keyTypeName,
						wrapperType );
				}
				else
				{
					out.appendFormat(
						"\t\t\t\t{ sw::hashed_string( \"%#\" ),\n"
						"\t\t\t\t  sw::hashed_string( \"%#\" ),\n"
						"\t\t\t\t  offsetof( %#, %# ),\n"
						"\t\t\t\t  false, sw::ContainerKind::None,\n"
						"\t\t\t\t  sw::hashed_string(), sw::hashed_string(), nullptr },\n",
						prop.name,
						prop.typeName,
						typeInfo.fullyQualifiedName,
						prop.name );
				}
			}
			out.append( "\t\t\t};\n" );
		}

		out.append( "\t\t\tregistry.registerClass( info );\n" );
		out.append( "\t\t}\n\n" );
		out.appendFormat( "\t\t%#_Registrar()\n\t\t{\n", id );
		out.append( "\t\t\tstatic sw::TypeRegistrar reg( &RegisterType, SW_TYPE_MODULE_HEAD() );\n" );
		out.append( "\t\t}\n\t};\n" );
		out.appendFormat( "\tstatic %#_Registrar g_%#_registrar;\n\n", id, id );
	}

	void CodeGenerator::emitEnumRegistrar( CodeEmitBuffer& out, const ParsedEnumInfo& enumInfo ) const
	{
		const std::string id = sanitizeIdentifier( enumInfo.fullyQualifiedName );

		out.appendFormat( "\t// ── %# ──────────────────────────────\n", enumInfo.fullyQualifiedName );
		out.appendFormat( "\tstruct %#_Registrar\n\t{\n", id );
		out.append( "\t\tstatic void RegisterEnum(sw::TypeRegistry& registry)\n\t\t{\n" );
		out.append( "\t\t\tsw::EnumInfo info;\n" );
		out.appendFormat( "\t\t\tinfo._name               = sw::hashed_string( \"%#\" );\n", enumInfo.name );
		out.appendFormat( "\t\t\tinfo._fullyQualifiedName = sw::hashed_string( \"%#\" );\n", enumInfo.fullyQualifiedName );
		out.appendFormat( "\t\t\tinfo._bIsBitFlag         = %#;\n", enumInfo.isBitFlag ? "true" : "false" );

		if ( enumInfo.enumerators.empty() == false )
		{
			out.append( "\t\t\tinfo._mapNameToValue =\n\t\t\t{\n" );
			for ( const ParsedEnumeratorInfo& e : enumInfo.enumerators )
			{
				out.appendFormat( "\t\t\t\t{ sw::hashed_string( \"%#\" ), %# },\n",
								  e.name, e.value );
			}
			out.append( "\t\t\t};\n" );

			out.append( "\t\t\tinfo._mapValueToName =\n\t\t\t{\n" );
			for ( const ParsedEnumeratorInfo& e : enumInfo.enumerators )
			{
				out.appendFormat( "\t\t\t\t{ %#, sw::hashed_string( \"%#\" ) },\n",
								  e.value, e.name );
			}
			out.append( "\t\t\t};\n" );
		}

		out.append( "\t\t\tregistry.registerEnum( info );\n" );
		out.append( "\t\t}\n\n" );
		out.appendFormat( "\t\t%#_Registrar()\n\t\t{\n", id );
		out.append( "\t\t\tstatic sw::EnumRegistrar reg( &RegisterEnum, SW_ENUM_MODULE_HEAD() );\n" );
		out.append( "\t\t}\n\t};\n" );
		out.appendFormat( "\tstatic %#_Registrar g_%#_registrar;\n\n", id, id );
	}

	std::string CodeGenerator::sanitizeIdentifier( const std::string& fqn )
	{
		std::string result = fqn;
		size_t		pos	   = 0;
		while ( ( pos = result.find( "::", pos ) ) != std::string::npos )
		{
			result.replace( pos, 2, "_" );
			pos += 1;
		}
		for ( utf8& c : result )
		{
			if ( !std::isalnum( static_cast<unsigned char>( c ) ) && c != '_' )
				c = '_';
		}
		return result;
	}
} // namespace sw::tool
