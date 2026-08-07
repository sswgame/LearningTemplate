/**
 * @file AstVisitor.cpp
 * @brief libclang AST 순회 및 REFLECT/ENUM 메타데이터 수집
 */
#include "AstVisitor.h"
#include "ParserUtil.h"
#include "Core/Common/Common.h"
#include "Core/Utility/String/StringBuilder.h"
#include "Core/Utility/String/StringUtil.h"

namespace sw::tool
{
	namespace
	{
		std::string cxStringToStd( CXString cxStr )
		{
			std::string result;
			if ( const utf8* c = clang_getCString( cxStr ) )
				result = c;
			clang_disposeString( cxStr );
			return result;
		}

		struct AnnotationSearch
		{
			std::string prefix;
			std::string spelling;
			bool		found = false;
		};

		CXChildVisitResult annotationSearchVisitor( CXCursor cursor, CXCursor, CXClientData data )
		{
			auto*			   search = static_cast<AnnotationSearch*>( data );
			const CXCursorKind kind	  = clang_getCursorKind( cursor );
			if ( kind == CXCursor_AnnotateAttr || kind == CXCursor_UnexposedAttr )
			{
				const std::string spelling = cxStringToStd( clang_getCursorSpelling( cursor ) );
				if ( spelling.find( search->prefix ) != std::string::npos )
				{
					search->found	 = true;
					search->spelling = spelling;
					return CXChildVisit_Break;
				}
			}
			return CXChildVisit_Continue;
		}

		/** @brief AnnotateAttr가 AST 자식으로 안 붙는 경우(매크로 위치) 소스 텍스트로 보정 */
		bool sourceHasPrimaryAnnotation( CXCursor cursor, const std::string& prefix )
		{
			const char* macroName = nullptr;
			if ( prefix == "REFLECT;" )
				macroName = "REFLECT(";
			else if ( prefix == "ENUM;" )
				macroName = "ENUM(";
			else if ( prefix == "PROPERTY;" )
				macroName = "PROPERTY(";
			else if ( prefix == "FUNCTION;" )
				macroName = "FUNCTION(";
			else
				return false;

			CXFile	 file	= nullptr;
			unsigned line	= 0;
			unsigned column = 0;
			unsigned offset = 0;
			clang_getFileLocation( clang_getCursorLocation( cursor ), &file, &line, &column, &offset );
			if ( file == nullptr )
				return false;

			const std::string path	  = cxStringToStd( clang_getFileName( file ) );
			const std::string content = readTextFile( path );
			if ( content.empty() || offset > content.size() )
				return false;

			const size_t windowStart = ( offset > 512 ) ? ( offset - 512 ) : 0;
			const std::string_view window( content.data() + windowStart, offset - windowStart );
			if ( window.find( macroName ) != std::string_view::npos )
				return true;

			const std::string annotateNeedle = std::string( "annotate(\"" ) + prefix;
			return window.find( annotateNeedle ) != std::string_view::npos;
		}

		std::string parseAnnotationAlias( const std::string& annotationSpelling )
		{
			const size_t aliasPos = annotationSpelling.find( "Alias" );
			if ( aliasPos == std::string::npos )
				return {};

			const size_t eqPos = annotationSpelling.find( '=', aliasPos );
			if ( eqPos == std::string::npos )
				return {};

			size_t valueStart = eqPos + 1;
			while ( valueStart < annotationSpelling.size() &&
					( annotationSpelling[valueStart] == ' ' || annotationSpelling[valueStart] == '\t' ) )
				++valueStart;

			if ( valueStart >= annotationSpelling.size() )
				return {};

			if ( annotationSpelling[valueStart] == '"' )
			{
				const size_t endQuote = annotationSpelling.find( '"', valueStart + 1 );
				if ( endQuote == std::string::npos )
					return {};
				return annotationSpelling.substr( valueStart + 1, endQuote - valueStart - 1 );
			}

			size_t valueEnd = valueStart;
			while ( valueEnd < annotationSpelling.size() )
			{
				const char c = annotationSpelling[valueEnd];
				if ( c == ',' || c == ';' || c == ' ' || c == '\t' || c == ')' )
					break;
				++valueEnd;
			}
			return annotationSpelling.substr( valueStart, valueEnd - valueStart );
		}

		struct FieldCollector
		{
			std::vector<ParsedPropertyInfo>* properties = nullptr;
		};

		struct MethodCollector
		{
			std::vector<ParsedFunctionInfo>* methods = nullptr;
		};

		std::vector<std::string> extractTemplateArgs( const std::string& typeStr )
		{
			std::vector<std::string> args;
			const size_t			 start = typeStr.find( '<' );
			const size_t			 end   = typeStr.rfind( '>' );
			if ( start == std::string::npos || end == std::string::npos || end <= start )
				return args;

			const std::string inner = typeStr.substr( start + 1, end - start - 1 );
			int32			  depth = 0;
			size_t			  last	= 0;
			for ( size_t i = 0; i < inner.size(); ++i )
			{
				if ( inner[i] == '<' )
					++depth;
				else if ( inner[i] == '>' )
					--depth;
				else if ( inner[i] == ',' && depth == 0 )
				{
					args.push_back( StringUtil::trim( inner.substr( last, i - last ) ) );
					last = i + 1;
				}
			}
			if ( last < inner.size() )
				args.push_back( StringUtil::trim( inner.substr( last ) ) );
			return args;
		}

		void parseContainerDetails( ParsedPropertyInfo& prop )
		{
			const std::string& t = prop.typeName;

			auto setSequence = [&]( const utf8* containerType )
			{
				prop.isContainer   = true;
				prop.containerKind = "Sequence";
				prop.containerType = containerType;
				const auto args	   = extractTemplateArgs( t );
				if ( args.empty() == false )
					prop.elementTypeName = args[0];
			};

			auto setMap = [&]( const utf8* containerType )
			{
				prop.isContainer   = true;
				prop.containerKind = "Map";
				prop.containerType = containerType;
				const auto args	   = extractTemplateArgs( t );
				if ( args.size() >= 2 )
				{
					prop.keyTypeName	 = args[0];
					prop.elementTypeName = args[1];
				}
			};

			if ( t.find( "unordered_set" ) != std::string::npos )
				setSequence( "UnorderedSet" );
			else if ( t.find( "set" ) != std::string::npos )
				setSequence( "Set" );
			else if ( t.find( "vector" ) != std::string::npos || t.find( "TArray" ) != std::string::npos )
				setSequence( "Vector" );
			else if ( t.find( "list" ) != std::string::npos )
				setSequence( "List" );
			else if ( t.find( "deque" ) != std::string::npos )
				setSequence( "Deque" );
			else if ( t.find( "array" ) != std::string::npos )
				setSequence( "Array" );
			else if ( t.find( "unordered_map" ) != std::string::npos )
				setMap( "UnorderedMap" );
			else if ( t.find( "map" ) != std::string::npos )
				setMap( "Map" );
		}

		CXChildVisitResult fieldCollectorVisitor( CXCursor cursor, CXCursor, CXClientData data )
		{
			if ( clang_getCursorKind( cursor ) != CXCursor_FieldDecl )
				return CXChildVisit_Continue;

			AnnotationSearch search{ "PROPERTY;", {}, false };
			clang_visitChildren( cursor, annotationSearchVisitor, &search );
			if ( search.found == false )
				return CXChildVisit_Continue;

			auto*			   collector = static_cast<FieldCollector*>( data );
			ParsedPropertyInfo prop;
			prop.name	  = cxStringToStd( clang_getCursorSpelling( cursor ) );
			prop.typeName = cxStringToStd( clang_getTypeSpelling( clang_getCursorType( cursor ) ) );
			prop.alias	  = parseAnnotationAlias( search.spelling );
			parseContainerDetails( prop );
			collector->properties->push_back( std::move( prop ) );
			return CXChildVisit_Continue;
		}

		CXChildVisitResult methodCollectorVisitor( CXCursor cursor, CXCursor, CXClientData data )
		{
			const CXCursorKind kind = clang_getCursorKind( cursor );
			if ( kind != CXCursor_CXXMethod && kind != CXCursor_FunctionDecl )
				return CXChildVisit_Continue;

			if ( clang_CXXMethod_isStatic( cursor ) || kind == CXCursor_Constructor || kind == CXCursor_Destructor ||
				 clang_CXXConstructor_isCopyConstructor( cursor ) || clang_CXXConstructor_isMoveConstructor( cursor ) )
				return CXChildVisit_Continue;

			AnnotationSearch search{ "FUNCTION;", {}, false };
			clang_visitChildren( cursor, annotationSearchVisitor, &search );
			if ( search.found == false )
				return CXChildVisit_Continue;

			auto*			   collector = static_cast<MethodCollector*>( data );
			ParsedFunctionInfo method;
			method.name			  = cxStringToStd( clang_getCursorSpelling( cursor ) );
			method.returnTypeName = cxStringToStd( clang_getTypeSpelling( clang_getCursorResultType( cursor ) ) );

			const int32 numArgs = clang_Cursor_getNumArguments( cursor );
			for ( int32 i = 0; i < numArgs; ++i )
			{
				const CXCursor argCursor = clang_Cursor_getArgument( cursor, i );
				method.paramTypeNames.push_back(
					cxStringToStd( clang_getTypeSpelling( clang_getCursorType( argCursor ) ) ) );
			}

			collector->methods->push_back( std::move( method ) );
			return CXChildVisit_Continue;
		}

		struct BaseClassCollector
		{
			std::string firstBaseFQN;
		};

		CXChildVisitResult baseClassVisitor( CXCursor cursor, CXCursor, CXClientData data )
		{
			if ( clang_getCursorKind( cursor ) != CXCursor_CXXBaseSpecifier )
				return CXChildVisit_Continue;

			auto* collector = static_cast<BaseClassCollector*>( data );
			if ( collector->firstBaseFQN.empty() == false )
				return CXChildVisit_Break;

			const CXType   baseType = clang_getCursorType( cursor );
			const CXCursor baseDecl = clang_getTypeDeclaration( baseType );
			collector->firstBaseFQN = AstVisitor::buildFullyQualifiedName( baseDecl );
			return CXChildVisit_Break;
		}

		struct EnumeratorCollector
		{
			std::vector<ParsedEnumeratorInfo>* enumerators = nullptr;
		};

		CXChildVisitResult enumeratorCollectorVisitor( CXCursor cursor, CXCursor, CXClientData data )
		{
			if ( clang_getCursorKind( cursor ) != CXCursor_EnumConstantDecl )
				return CXChildVisit_Continue;

			auto*				 collector = static_cast<EnumeratorCollector*>( data );
			ParsedEnumeratorInfo enumerator;
			enumerator.name	 = cxStringToStd( clang_getCursorSpelling( cursor ) );
			enumerator.value = clang_getEnumConstantDeclValue( cursor );
			collector->enumerators->push_back( std::move( enumerator ) );
			return CXChildVisit_Continue;
		}
	} // namespace

	AstVisitor::AstVisitor( CXTranslationUnit tu )
		: _translationUnit( tu )
	{
	}

	void AstVisitor::visit()
	{
		CXCursor rootCursor = clang_getTranslationUnitCursor( _translationUnit );
		clang_visitChildren( rootCursor, visitCursor, this );
	}

	CXChildVisitResult AstVisitor::visitCursor( CXCursor cursor, CXCursor, CXClientData clientData )
	{
		auto*			   self = static_cast<AstVisitor*>( clientData );
		const CXCursorKind kind = clang_getCursorKind( cursor );

		if ( kind == CXCursor_Namespace )
			return CXChildVisit_Recurse;

		if ( kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl )
		{
			if ( kind == CXCursor_ClassTemplatePartialSpecialization )
				return CXChildVisit_Continue;

			if ( hasAnnotation( cursor, "REFLECT;" ) )
				self->onStructDecl( cursor );

			return CXChildVisit_Recurse;
		}

		if ( kind == CXCursor_EnumDecl )
		{
			if ( hasAnnotation( cursor, "ENUM;" ) )
				self->onEnumDecl( cursor );
			return CXChildVisit_Continue;
		}

		return CXChildVisit_Continue;
	}

	bool AstVisitor::hasAnnotation( CXCursor cursor, const std::string& prefix )
	{
		CXSourceLocation loc = clang_getCursorLocation( cursor );
		if ( clang_Location_isFromMainFile( loc ) == false )
			return false;

		AnnotationSearch search{ prefix, {}, false };
		clang_visitChildren( cursor, annotationSearchVisitor, &search );
		if ( search.found )
			return true;

		// Primary macros only — never fallback for refined tags like "ENUM;BitFlag".
		return sourceHasPrimaryAnnotation( cursor, prefix );
	}

	void AstVisitor::onStructDecl( CXCursor cursor )
	{
		ParsedTypeInfo typeInfo;
		typeInfo.name				= getCursorSpelling( cursor );
		typeInfo.fullyQualifiedName = buildFullyQualifiedName( cursor );

		BaseClassCollector baseCollector;
		clang_visitChildren( cursor, baseClassVisitor, &baseCollector );
		typeInfo.parentFQN = baseCollector.firstBaseFQN;

		FieldCollector fieldCollector{ &typeInfo.properties };
		clang_visitChildren( cursor, fieldCollectorVisitor, &fieldCollector );

		MethodCollector methodCollector{ &typeInfo.methods };
		clang_visitChildren( cursor, methodCollectorVisitor, &methodCollector );

		SW_LOG_INFO( "[AstVisitor] REFLECT class : %#  (%# properties, %# methods)",
					 typeInfo.fullyQualifiedName, typeInfo.properties.size(), typeInfo.methods.size() );
		_types.push_back( std::move( typeInfo ) );
	}

	void AstVisitor::onEnumDecl( CXCursor cursor )
	{
		ParsedEnumInfo enumInfo;
		enumInfo.name				= getCursorSpelling( cursor );
		enumInfo.fullyQualifiedName = buildFullyQualifiedName( cursor );

		EnumeratorCollector enumeratorCollector{ &enumInfo.enumerators };
		clang_visitChildren( cursor, enumeratorCollectorVisitor, &enumeratorCollector );

		if ( hasAnnotation( cursor, "ENUM;BitFlag" ) || hasAnnotation( cursor, "ENUM;FLAG" ) || hasAnnotation( cursor, "ENUM;Bitwise" ) )
		{
			enumInfo.isBitFlag = true;
		}
		else
		{
			bool  allPowerOf2  = enumInfo.enumerators.empty() == false;
			int32 nonZeroCount = 0;
			for ( const ParsedEnumeratorInfo& e : enumInfo.enumerators )
			{
				if ( e.value != 0 )
				{
					++nonZeroCount;
					if ( ( e.value & ( e.value - 1 ) ) != 0 )
					{
						allPowerOf2 = false;
						break;
					}
				}
			}
			if ( allPowerOf2 && nonZeroCount > 1 )
				enumInfo.isBitFlag = true;
		}

		SW_LOG_INFO( "[AstVisitor] ENUM          : %#  (%# values, isBitFlag=%#)",
					 enumInfo.fullyQualifiedName, enumInfo.enumerators.size(), enumInfo.isBitFlag ? "true" : "false" );
		_enums.push_back( std::move( enumInfo ) );
	}

	std::string AstVisitor::getCursorSpelling( CXCursor cursor )
	{
		return cxStringToStd( clang_getCursorSpelling( cursor ) );
	}

	std::string AstVisitor::buildFullyQualifiedName( CXCursor cursor )
	{
		std::vector<std::string> parts;
		CXCursor				 current = cursor;
		while ( true )
		{
			const CXCursorKind kind = clang_getCursorKind( current );
			if ( kind == CXCursor_TranslationUnit )
				break;

			const std::string spelling = cxStringToStd( clang_getCursorSpelling( current ) );
			if ( spelling.empty() == false )
				parts.push_back( spelling );

			current = clang_getCursorSemanticParent( current );
		}

		StringBuilder<constant::kMaxBuffer1024> fqn;
		for ( auto it = parts.rbegin(); it != parts.rend(); ++it )
		{
			if ( fqn.size() > 0 )
				fqn.append( "::" );
			fqn.append( *it );
		}
		return std::string( fqn.view() );
	}
} // namespace sw::tool
