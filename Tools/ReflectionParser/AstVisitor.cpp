/**
 * @file AstVisitor.cpp
 * @brief libclang AST 순회 및 REFLECT/ENUM 메타데이터 수집
 */
#include "AstVisitor.h"
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
			bool		found = false;
		};

		CXChildVisitResult annotationSearchVisitor( CXCursor cursor, CXCursor, CXClientData data )
		{
			auto* search = static_cast<AnnotationSearch*>( data );
			if ( clang_getCursorKind( cursor ) == CXCursor_AnnotateAttr )
			{
				const std::string spelling = cxStringToStd( clang_getCursorSpelling( cursor ) );
				if ( spelling.find( search->prefix ) != std::string::npos )
				{
					search->found = true;
					return CXChildVisit_Break;
				}
			}
			return CXChildVisit_Continue;
		}

		struct FieldCollector
		{
			std::vector<ParsedPropertyInfo>* properties = nullptr;
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

			AnnotationSearch search{ "PROPERTY;", false };
			clang_visitChildren( cursor, annotationSearchVisitor, &search );
			if ( search.found == false )
				return CXChildVisit_Continue;

			auto*			   collector = static_cast<FieldCollector*>( data );
			ParsedPropertyInfo prop;
			prop.name	  = cxStringToStd( clang_getCursorSpelling( cursor ) );
			prop.typeName = cxStringToStd( clang_getTypeSpelling( clang_getCursorType( cursor ) ) );
			parseContainerDetails( prop );
			collector->properties->push_back( std::move( prop ) );
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
	}

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

		AnnotationSearch search{ prefix, false };
		clang_visitChildren( cursor, annotationSearchVisitor, &search );
		if ( search.found )
			return true;

		// AnnotateAttr가 자식에 없더라도 메인 파일 선언은 통과 (매크로 확장 위치 차이 보정)
		return true;
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

		SW_LOG_INFO( "[AstVisitor] REFLECT class : %#  (%# properties)",
					 typeInfo.fullyQualifiedName, typeInfo.properties.size() );
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
}
