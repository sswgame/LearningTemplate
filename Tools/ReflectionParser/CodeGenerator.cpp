/**
 * @file CodeGenerator.cpp
 * @brief Reflection .gen.cpp 코드 생성기 구현
 */
#include "CodeGenerator.h"
#include "ParserUtil.h"
#include "Core/Common/Common.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/String/formatString.h"
#include <cstring>

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
		out.append( "#include \"Core/Utility/Task/TaskTypes.h\"\n" );
		if ( bNeedsComponentFactory )
		{
			out.append( "#include \"Core/Object/ComponentManager.h\"\n" );
			out.append( "#include \"Core/Common/CoreServices.h\"\n" );
		}
		out.appendFormat( "#include \"%#\"\n\n", _sourceFilePath );
	}

	std::string CodeGenerator::normalizeTypeName( const std::string& clangSpelling )
	{
		std::string t = clangSpelling;
		// Strip common qualifiers / references for TaskArgs get<T>
		auto stripPrefix = [&]( const char* prefix )
		{
			const size_t n = std::strlen( prefix );
			while ( t.rfind( prefix, 0 ) == 0 )
				t.erase( 0, n );
		};
		stripPrefix( "const " );
		stripPrefix( "volatile " );
		while ( t.empty() == false && ( t.back() == '&' || t.back() == ' ' || t.back() == '*' ) )
		{
			if ( t.back() == '*' )
				break; // keep pointers as-is (unsupported for get by value)
			t.pop_back();
		}
		while ( t.empty() == false && t.back() == ' ' )
			t.pop_back();

		// Types.h aliases live in the global namespace (not sw::).
		if ( t == "int" || t == "signed int" )
			return "int32";
		if ( t == "unsigned int" || t == "unsigned" )
			return "uint32";
		if ( t == "long long" || t == "signed long long" )
			return "int64";
		if ( t == "unsigned long long" )
			return "uint64";
		if ( t == "float" )
			return "float32";
		if ( t == "double" )
			return "float64";
		if ( t == "int32_t" )
			return "int32";
		if ( t == "uint32_t" )
			return "uint32";
		if ( t == "int64_t" )
			return "int64";
		if ( t == "uint64_t" )
			return "uint64";
		if ( t == "std::basic_string<char>" || t == "basic_string<char>" )
			return "std::string";
		if ( t == "string" )
			return "std::string";
		return t;
	}

	void CodeGenerator::emitMethodList( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
	{
		for ( const ParsedFunctionInfo& method : typeInfo.methods )
		{
			const std::string retType = normalizeTypeName( method.returnTypeName );

			out.append( "\t\t\t{\n" );
			out.append( "\t\t\t\tsw::FunctionInfo funcInfo;\n" );
			out.appendFormat( "\t\t\t\tfuncInfo._name           = \"%#\";\n", method.name );
			out.appendFormat( "\t\t\t\tfuncInfo._hashName       = sw::hashed_string( \"%#\" );\n", method.name );
			out.appendFormat( "\t\t\t\tfuncInfo._returnTypeName = \"%#\";\n", retType );
			out.append( "\t\t\t\tfuncInfo._paramTypeNames = { " );
			for ( size_t i = 0; i < method.paramTypeNames.size(); ++i )
			{
				const std::string p = normalizeTypeName( method.paramTypeNames[i] );
				out.appendFormat( "\"%#\"", p );
				if ( i + 1 < method.paramTypeNames.size() )
					out.append( ", " );
			}
			out.append( " };\n" );

			std::string callArgs;
			for ( size_t i = 0; i < method.paramTypeNames.size(); ++i )
			{
				const std::string p = normalizeTypeName( method.paramTypeNames[i] );
				if ( i > 0 )
					callArgs += ", ";
				callArgs += "args.get<" + p + ">( " + std::to_string( i ) + " )";
			}

			out.append( "\t\t\t\tauto invokerCb = []( void* objPtr, const sw::TaskArgs& args ) -> sw::TaskValue\n" );
			out.append( "\t\t\t\t{\n" );
			if ( method.paramTypeNames.empty() )
				out.append( "\t\t\t\t\t(void)args;\n" );
			out.appendFormat( "\t\t\t\t\tauto* self = static_cast<%#*>( objPtr );\n", typeInfo.fullyQualifiedName );
			if ( retType == "void" )
			{
				out.appendFormat( "\t\t\t\t\tself->%#(%#);\n", method.name, callArgs.c_str() );
				out.append( "\t\t\t\t\treturn sw::TaskValue{};\n" );
			}
			else
			{
				out.appendFormat( "\t\t\t\t\treturn sw::TaskValue{ self->%#(%#) };\n", method.name, callArgs.c_str() );
			}
			out.append( "\t\t\t\t};\n" );
			out.append( "\t\t\t\tfuncInfo._invoker = SW_DELEGATE_LAMBDA( sw::Delegate<sw::TaskValue( void*, const sw::TaskArgs& )>, invokerCb );\n" );
			out.append( "\t\t\t\tinfo._methods.push_back( funcInfo );\n" );
			out.append( "\t\t\t}\n" );
		}
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
				const std::string aliasExpr = prop.alias.empty()
												  ? "sw::hashed_string()"
												  : ( "sw::hashed_string( \"" + prop.alias + "\" )" );

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
						"\t\t\t\t  std::make_shared<%#>(),\n"
						"\t\t\t\t  %# },\n",
						prop.name,
						prop.typeName,
						typeInfo.fullyQualifiedName,
						prop.name,
						kindStr,
						prop.elementTypeName,
						prop.keyTypeName,
						wrapperType,
						aliasExpr );
				}
				else
				{
					out.appendFormat(
						"\t\t\t\t{ sw::hashed_string( \"%#\" ),\n"
						"\t\t\t\t  sw::hashed_string( \"%#\" ),\n"
						"\t\t\t\t  offsetof( %#, %# ),\n"
						"\t\t\t\t  false, sw::ContainerKind::None,\n"
						"\t\t\t\t  sw::hashed_string(), sw::hashed_string(), nullptr,\n"
						"\t\t\t\t  %# },\n",
						prop.name,
						prop.typeName,
						typeInfo.fullyQualifiedName,
						prop.name,
						aliasExpr );
				}
			}
			out.append( "\t\t\t};\n" );
		}

		if ( typeInfo.methods.empty() == false )
			emitMethodList( out, typeInfo );

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
