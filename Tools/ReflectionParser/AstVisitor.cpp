#include "pch.h"

#include "ReflectionParser/AstVisitor.h"

#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "ReflectionParser/AnnotationApply.h"
#include "ReflectionParser/ContainerTypeMap.h"
#include "ReflectionParser/ParserContext.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"
#include "ReflectionParser/TypeNameMap.h"

SW_LOG_CALLER( "AstVisitor" );
namespace sw
{
	namespace
	{
		struct AstVisitorInternal
		{
			// ------------------------------------------------------------------------------
			// A) clang CXString / AnnotateAttr 검색 / 소스 폴백
			// ------------------------------------------------------------------------------
			/**
			 * @brief Clang 내부 문자열 객체(CXString)를 C++ 표준 string으로 복사 후 안전하게 해제합니다.
			 * @param cxStr libclang이 반환한 CXString
			 * @return sw::string 변환 결과
			 */
			static string cxStringToStd( CXString cxStr )
			{
				string		result;
				const utf8* cString = clang_getCString( cxStr );
				if ( cString != nullptr )
					result = cString;
				clang_disposeString( cxStr );
				return result;
			}

			/**
			 * @brief CXString을 힙 할당 없이 즉시 string_view로 비교 후 안전하게 해제합니다.
			 */
			static bool cxStringEquals( CXString cxStr, string_view target )
			{
				const utf8* cStr = clang_getCString( cxStr );
				const bool	bEq	 = ( cStr != nullptr && string_view( cStr ) == target );
				clang_disposeString( cxStr );
				return bEq;
			}

			/**
			 * @brief `CXCursor_AnnotateAttr` 노드에서 특정 접두사를 검색하기 위한 컨텍스트 DTO
			 */
			struct AnnotationSearch
			{
				string_view			   _prefix;
				string				   _spelling;
				uint8				   _bFound	 : 1;
				[[maybe_unused]] uint8 _reserved : 7;

				explicit AnnotationSearch( string_view prefix )
					: _prefix{ prefix }
					, _spelling{}
					, _bFound{ SW_FALSE }
					, _reserved{ 0 }
				{
				}
			};

			/**
			 * @brief AST 자식 커서들을 순회하며 `CXCursor_AnnotateAttr` 속성이 지정한 접두사를 포함하는지 검사하는 콜백
			 */
			static CXChildVisitResult annotationSearchVisitor( CXCursor cursor, CXCursor, CXClientData data )
			{
				AnnotationSearch*  search = static_cast<AnnotationSearch*>( data );
				const CXCursorKind kind	  = clang_getCursorKind( cursor );
				if ( kind == CXCursor_AnnotateAttr || kind == CXCursor_UnexposedAttr )
				{
					const CXString cxSpelling = clang_getCursorSpelling( cursor );
					const utf8*	   cStr		  = clang_getCString( cxSpelling );
					if ( cStr != nullptr && string_view( cStr ).find( search->_prefix ) != string_view::npos )
					{
						search->_bFound	  = SW_TRUE;
						search->_spelling = cStr;
						clang_disposeString( cxSpelling );
						return CXChildVisit_Break; // 원하는 어노테이션을 찾았으므로 순회 중단
					}
					clang_disposeString( cxSpelling );
				}
				return CXChildVisit_Continue;
			}

			/** @brief 여러 prefix를 한 번의 자식 순회로 동시에 탐색합니다. */
			struct MultiAnnotationSearch
			{
				struct Entry
				{
					const utf8*			   _pPrefix;
					string				   _spelling;
					uint8				   _bFound	 : 1;
					[[maybe_unused]] uint8 _reserved : 7;

					Entry()
						: _pPrefix{ nullptr }
						, _spelling{}
						, _bFound{ SW_FALSE }
						, _reserved{ 0 }
					{
					}
				};

				Entry _arrEntry[4]{}; ///< 충분한 크기로 고정, nullptr 로 빈 슬롯 표시
				int32 _count = 0;

				void add( const utf8* pPrefix )
				{
					if ( _count < 4 )
					{
						_arrEntry[_count]._pPrefix = pPrefix;
						++_count;
					}
				}

				Entry* get( const utf8* pPrefix )
				{
					for ( int32 entryIndex = 0; entryIndex < _count; ++entryIndex )
					{
						if ( _arrEntry[entryIndex]._pPrefix == pPrefix )
							return &_arrEntry[entryIndex];
					}
					return nullptr;
				}
			};

			/**
			 * @brief 여러 어노테이션 접두사를 한 번의 자식 순회로 매칭하는 콜백.
			 */
			static CXChildVisitResult multiAnnotationVisitor( CXCursor cursor, CXCursor, CXClientData data )
			{
				MultiAnnotationSearch* multi = static_cast<MultiAnnotationSearch*>( data );
				const CXCursorKind	   kind	 = clang_getCursorKind( cursor );
				if ( kind != CXCursor_AnnotateAttr && kind != CXCursor_UnexposedAttr )
					return CXChildVisit_Continue;

				const CXString cxSpelling = clang_getCursorSpelling( cursor );
				const utf8*	   cStr		  = clang_getCString( cxSpelling );
				if ( cStr != nullptr )
				{
					const string_view spellingView( cStr );
					for ( int32 entryIndex = 0; entryIndex < multi->_count; ++entryIndex )
					{
						MultiAnnotationSearch::Entry& entry = multi->_arrEntry[entryIndex];
						if ( entry._bFound == SW_FALSE && spellingView.find( entry._pPrefix ) != string_view::npos )
						{
							entry._bFound	= SW_TRUE;
							entry._spelling = cStr;
						}
					}
				}
				clang_disposeString( cxSpelling );
				return CXChildVisit_Continue;
			}

			/**
			 * @brief 스레드별 소스 파일 내용 캐시 (동일 헤더 내 여러 커서의 중복 디스크 I/O 제거)
			 */

			static const string& getCachedFileContent( const string& path )
			{
				thread_local unordered_map<string, string> s_fileContentCache;
				auto									   it = s_fileContentCache.find( path );
				if ( it != s_fileContentCache.end() )
					return it->second;

				string content;
				FileUtil::readTextFile( path, content );
				auto [insertedIt, dummy] = s_fileContentCache.emplace( path, std::move( content ) );
				return insertedIt->second;
			}

			/** @brief 윈도우 내에서 주석 바깥에 있는 문자열을 뒤에서부터 찾습니다. */
			static size_t rfindOutsideComments( const string_view& window, const string_view& searchStr )
			{
				size_t searchEnd = string::npos;
				size_t pos		 = string::npos;
				while ( true )
				{
					pos = window.rfind( searchStr, searchEnd );
					if ( pos == string_view::npos )
						return string::npos;

					bool bInLineComment	 = false;
					bool bInBlockComment = false;
					for ( size_t index = 0; index < pos; ++index )
					{
						if ( bInLineComment == false && bInBlockComment == false )
						{
							if ( window[index] == '/' && index + 1 < pos )
							{
								if ( window[index + 1] == '/' )
								{
									bInLineComment = true;
									++index;
								}
								else if ( window[index + 1] == '*' )
								{
									bInBlockComment = true;
									++index;
								}
							}
						}
						else if ( bInLineComment )
						{
							if ( window[index] == '\n' )
								bInLineComment = false;
						}
						else if ( bInBlockComment )
						{
							if ( window[index] == '*' && index + 1 < pos && window[index + 1] == '/' )
							{
								bInBlockComment = false;
								++index;
							}
						}
					}

					if ( bInLineComment || bInBlockComment )
					{
						if ( pos == 0 )
							return string::npos;
						searchEnd = pos - 1;
						continue;
					}
					break;
				}
				return pos;
			}

			/**
			 * @brief Clang AST의 매크로 확장 버그 등으로 인해 자식 어노테이션 커서가 누락된 경우,
			 *        실제 소스 파일의 커서 위치 주변을 직접 스캔하여 기본 어노테이션이 존재하는지 보정합니다.
			 */
			static bool sourceHasPrimaryAnnotation( CXCursor cursor, string_view prefix )
			{
				// kReflectAnnotations[] 테이블에서 prefix가 일치하는 항목의 macroOpen을 찾습니다.
				// 새 어노테이션이 PredefinedReflectAnnotation.xxx에 추가되면 자동으로 반영됩니다.
				const utf8* pMacroOpen = nullptr;
				for ( const ReflectAnnotationDesc& desc : kReflectAnnotations )
				{
					if ( prefix == desc._prefix )
					{
						pMacroOpen = desc._macroOpen;
						break;
					}
				}

				if ( pMacroOpen == nullptr )
					return false;

				CXFile file	  = nullptr;
				uint32 line	  = 0;
				uint32 column = 0;
				uint32 offset = 0;
				clang_getFileLocation( clang_getCursorLocation( cursor ), &file, &line, &column, &offset );
				if ( file == nullptr )
					return false;

				const string& content = getCachedFileContent( cxStringToStd( clang_getFileName( file ) ) );
				if ( content.empty() || offset > content.size() )
					return false;

				// 커서 위치 이전 1KB 윈도우 스캔
				const size_t	  lookback	  = ParserContext::getSharedConfig()._sourceLookbackBytes;
				const size_t	  windowStart = ( offset > lookback ) ? ( offset - lookback ) : 0;
				const string_view window( content.data() + windowStart, offset - windowStart );

				const size_t macroPos = rfindOutsideComments( window, pMacroOpen );
				if ( macroPos != string_view::npos )
				{
					const string_view afterMacro = window.substr( macroPos );
					if ( afterMacro.find( ';' ) == string_view::npos &&
						 afterMacro.find( '}' ) == string_view::npos &&
						 afterMacro.find( '{' ) == string_view::npos )
						return true;
				}

				StringBuilder<constant::kMaxBuffer128> needle;
				needle.appendFormat( "annotate(\"%#", prefix );
				const size_t annotatePos = rfindOutsideComments( window, needle.view() );
				if ( annotatePos != string_view::npos )
				{
					const string_view afterAnnotate = window.substr( annotatePos );
					if ( afterAnnotate.find( ';' ) == string_view::npos &&
						 afterAnnotate.find( '}' ) == string_view::npos &&
						 afterAnnotate.find( '{' ) == string_view::npos )
						return true;
				}

				return false;
			}

			/**
			 * @brief AST 자식에 어노테이션이 없는 경우, 소스 파일에서 직접 매크로 괄호 `(...)` 내용을 추적하여
			 *        "PREFIX;args" 형태의 표준 어노테이션 텍스트를 재구성합니다.
			 */
			static string sourceExtractMacroAnnotation( CXCursor cursor, string_view macroName, string_view annotatePrefix )
			{
				CXFile file	  = nullptr;
				uint32 line	  = 0;
				uint32 column = 0;
				uint32 offset = 0;
				clang_getFileLocation( clang_getCursorLocation( cursor ), &file, &line, &column, &offset );
				if ( file == nullptr || macroName.empty() || annotatePrefix.empty() )
					return {};

				const string& content = getCachedFileContent( cxStringToStd( clang_getFileName( file ) ) );
				if ( content.empty() || offset > content.size() )
					return {};

				const size_t	  lookback	  = ParserContext::getSharedConfig()._sourceLookbackBytes;
				const size_t	  windowStart = ( offset > lookback ) ? ( offset - lookback ) : 0;
				const string_view window( content.data() + windowStart, offset - windowStart );

				const size_t macroPos = rfindOutsideComments( window, macroName );
				if ( macroPos == string_view::npos )
					return {};

				const string_view afterMacro = window.substr( macroPos );
				const size_t	  closeParen = afterMacro.find( ')' );
				if ( closeParen != string_view::npos )
				{
					const string_view afterCloseParen = afterMacro.substr( closeParen + 1 );
					if ( afterCloseParen.find( ';' ) != string_view::npos ||
						 afterCloseParen.find( '}' ) != string_view::npos ||
						 afterCloseParen.find( '{' ) != string_view::npos )
						return {};
				}

				// 매크로 괄호 깊이 추적 파싱
				size_t charIndex = windowStart + macroPos + macroName.size();
				size_t argsStart = charIndex;

				int32 depth	   = 1;
				bool  bInQuote = false;

				while ( charIndex < content.size() && depth > 0 )
				{
					const utf8 c = content[charIndex++];
					if ( c == '"' )
					{
						bInQuote = ( bInQuote == false );
						continue;
					}
					if ( bInQuote )
						continue;

					if ( c == '(' )
					{
						++depth;
						continue;
					}
					if ( c == ')' )
					{
						--depth;
						if ( depth == 0 )
							break;
						continue;
					}
				}

				string_view								argsView( content.data() + argsStart, charIndex - argsStart - 1 );
				StringBuilder<constant::kMaxBuffer1024> b;
				b.append( annotatePrefix );
				b.append( argsView );
				return string( b.view() );
			}

			// ------------------------------------------------------------------------------
			// B) 컨테이너 타입 트리 (Vector/Map 중첩)
			// ------------------------------------------------------------------------------
			struct FieldCollector
			{
				vector<ParsedPropertyInfo>* _pProperties = nullptr;
				uint8						_bHasError : 1;
				[[maybe_unused]] uint8		_reserved  : 7;

				FieldCollector()
					: _pProperties{ nullptr }
					, _bHasError{ SW_FALSE }
					, _reserved{ 0 }
				{
				}
			};

			struct MethodCollector
			{
				vector<ParsedFunctionInfo>*			_pMethods;
				const MultiAnnotationSearch::Entry* _pFuncEntry;
				uint8								_bSkipConstructors : 1; ///< Abstract / Static 타입
				[[maybe_unused]] uint8				_reserved		   : 7;

				MethodCollector()
					: _pMethods{ nullptr }
					, _pFuncEntry{ nullptr }
					, _bSkipConstructors{ SW_FALSE }
					, _reserved{ 0 }
				{
				}
			};

			/** @brief `T<A,B>` 에서 최외곽 템플릿 인자를 나눕니다. */
			static vector<string> extractTemplateArgs( string_view typeStr )
			{
				const size_t start = typeStr.find( '<' );
				const size_t end   = typeStr.rfind( '>' );
				if ( start == string_view::npos || end == string_view::npos || end <= start )
					return {};
				return ParserUtil::splitCommaRespectingAngles( typeStr.substr( start + 1, end - start - 1 ) );
			}

			/** @brief 컨테이너 어노테이션 조회용 기본 템플릿/레코드 커서를 해석합니다. */
			static CXCursor containerDeclCursor( CXType type )
			{
				const CXType canonical = clang_getCanonicalType( type );
				CXCursor	 result	   = clang_getTypeDeclaration( canonical );
				if ( clang_Cursor_isNull( result ) )
					return result;

				const CXCursor specialized = clang_getSpecializedCursorTemplate( result );
				if ( clang_Cursor_isNull( specialized ) == 0 )
					result = specialized;
				return result;
			}

			/**
			 * @brief 타입 선언의 REFLECT_CONTAINER(Kind [, WrapperStem]) 를 조회합니다.
			 * @return 어노테이션된 컨테이너 메타를 찾으면 true
			 */
			static bool lookupReflectContainer( CXType type, ContainerKind& outKind, string& outWrapperStem )
			{
				const CXCursor decl = containerDeclCursor( type );
				if ( clang_Cursor_isNull( decl ) )
					return false;

				AnnotationSearch search{ annotationConstants::kReflectContainerPrefix };
				clang_visitChildren( decl, annotationSearchVisitor, &search );
				if ( search._bFound == SW_FALSE )
				{
					search._spelling = sourceExtractMacroAnnotation( decl, annotationConstants::kReflectContainerMacroOpen,
																	 annotationConstants::kReflectContainerPrefix );
					search._bFound	 = search._spelling.empty() == false ? SW_TRUE : SW_FALSE;
				}
				if ( search._bFound == SW_FALSE )
					return false;

				const size_t prefixPos = search._spelling.find( annotationConstants::kReflectContainerPrefix );
				if ( prefixPos == string::npos )
					return false;

				const vector<string> tokens =
					sw::AnnotationApply::splitAnnotationArgs( sw::AnnotationApply::annotationArgText( search._spelling, annotationConstants::kReflectContainerPrefix ) );
				if ( tokens.empty() )
					return SW_FALSE;

				if ( tryParseContainerKind( tokens[0], outKind ) == SW_FALSE || outKind == ContainerKind::None )
					return SW_FALSE;

				if ( tokens.size() >= 2 )
					outWrapperStem = tokens[1];
				else
					outWrapperStem = defaultContainerWrapperStem( outKind );
				return SW_TRUE;
			}

			/** @brief 맵/시퀀스 템플릿 인자로 키·원소·중첩 노드를 채웁니다. */
			static sw::shared_ptr<ParsedContainerNode> nestedContainerFromArg( CXType type, int32 numClangArgs, int32 index,
																			   const string& spellingFallback )
			{
				sw::shared_ptr<ParsedContainerNode> nested;
				if ( 0 <= index && index < numClangArgs )
				{
					const CXType argType = clang_Type_getTemplateArgumentAsType( type, static_cast<uint32>( index ) );
					if ( argType.kind != CXType_Invalid )
						nested = parseContainerFromType( argType );
				}
				if ( ( nested == nullptr || nested->_bIsContainer == SW_FALSE ) && spellingFallback.empty() == SW_FALSE )
					nested = parseContainerFromTypeSpelling( spellingFallback );
				if ( nested != nullptr && nested->_bIsContainer == SW_FALSE )
					nested.reset();
				return nested;
			}

			static void fillContainerNodeArgs( ParsedContainerNode& node, const string& typeSpelling, CXType type )
			{
				const vector<string> args = extractTemplateArgs( typeSpelling );
				const int32			 numClangArgs =
					 ( type.kind == CXType_Invalid ) ? 0 : clang_Type_getNumTemplateArguments( type );

				if ( node._containerKind == ContainerKind::Map )
				{
					if ( args.size() >= 2 )
					{
						node._keyTypeName	  = normalizeTypeName( args[0] );
						node._elementTypeName = normalizeTypeName( args[1] );
						node._elementNested	  = nestedContainerFromArg( type, numClangArgs, 1, args[1] );
					}
				}
				else if ( args.empty() == SW_FALSE )
				{
					node._elementTypeName = normalizeTypeName( args[0] );
					node._elementNested	  = nestedContainerFromArg( type, numClangArgs, 0, args[0] );
				}
			}

			/** @brief 표기 문자열만으로 컨테이너 노드를 만듭니다 (clang 타입 없음). */
			static sw::shared_ptr<ParsedContainerNode> parseContainerFromTypeSpelling( const string& typeSpelling )
			{
				sw::shared_ptr<ParsedContainerNode> node = sw::make_shared<ParsedContainerNode>();
				const ContainerTypeRule*			rule = ContainerTypeMap::instance().match( typeSpelling );
				if ( rule == nullptr )
				{
					node->_bIsContainer = SW_FALSE;
					return node;
				}
				node->_bIsContainer	 = SW_TRUE;
				node->_containerKind = rule->_kind;
				node->_containerType = rule->_type;
				node->_typeName		 = rule->_match;
				CXType invalid{};
				invalid.kind = CXType_Invalid;
				fillContainerNodeArgs( *node, typeSpelling, invalid );
				return node;
			}

			/** @brief clang 타입(또는 표기 폴백)으로 컨테이너 트리를 만듭니다. */
			static sw::shared_ptr<ParsedContainerNode> parseContainerFromType( CXType type )
			{
				sw::shared_ptr<ParsedContainerNode> node = sw::make_shared<ParsedContainerNode>();
				if ( type.kind == CXType_Invalid )
				{
					node->_bIsContainer = SW_FALSE;
					return node;
				}

				const string spelling =
					cxStringToStd( clang_getTypeSpelling( type ) );

				ContainerKind kind = ContainerKind::None;
				string		  wrapperStem;
				if ( lookupReflectContainer( type, kind, wrapperStem ) )
				{
					node->_bIsContainer					 = SW_TRUE;
					node->_containerKind				 = kind;
					node->_containerType				 = wrapperStem;
					const ContainerTypeRule* builtinRule = ContainerTypeMap::instance().match( spelling );
					if ( builtinRule != nullptr )
						node->_typeName = builtinRule->_match;
					fillContainerNodeArgs( *node, spelling, type );
					return node;
				}

				const ContainerTypeRule* rule = ContainerTypeMap::instance().match( spelling );
				if ( rule == nullptr )
				{
					node->_bIsContainer = SW_FALSE;
					return node;
				}
				node->_bIsContainer	 = SW_TRUE;
				node->_containerKind = rule->_kind;
				node->_containerType = rule->_type;
				node->_typeName		 = rule->_match;
				fillContainerNodeArgs( *node, spelling, type );
				return node;
			}

			/** @brief 필드 타입의 컨테이너 트리를 프로퍼티에 복사합니다. */
			static void parseContainerDetails( ParsedPropertyInfo& prop, CXType fieldType )
			{
				prop._containerTree = parseContainerFromType( fieldType );
				if ( prop._containerTree != nullptr && prop._containerTree->_bIsContainer != 0 )
				{
					prop._bIsContainer	  = SW_TRUE;
					prop._containerKind	  = prop._containerTree->_containerKind;
					prop._containerType	  = prop._containerTree->_containerType;
					prop._elementTypeName = prop._containerTree->_elementTypeName;
					prop._keyTypeName	  = prop._containerTree->_keyTypeName;
				}
			}

			// ------------------------------------------------------------------------------
			// C) child collectors — PROPERTY / FUNCTION / BODY / FACTORY / enumerator
			// ------------------------------------------------------------------------------
			/** @brief 필드 선언에서 PROPERTY 메타를 수집합니다. */
			static CXChildVisitResult fieldCollectorVisitor( CXCursor cursor, CXCursor, CXClientData data )
			{
				if ( clang_getCursorKind( cursor ) != CXCursor_FieldDecl )
					return CXChildVisit_Continue;

				AnnotationSearch search{ annotationConstants::kPropertyPrefix };
				clang_visitChildren( cursor, annotationSearchVisitor, &search );
				if ( search._bFound == SW_FALSE && sourceHasPrimaryAnnotation( cursor, annotationConstants::kPropertyPrefix ) == false )
					return CXChildVisit_Continue;

				FieldCollector*	   collector = static_cast<FieldCollector*>( data );
				ParsedPropertyInfo prop;
				const CXType	   fieldType = clang_getCursorType( cursor );
				prop._name					 = cxStringToStd( clang_getCursorSpelling( cursor ) );
				prop._typeName =
					normalizeTypeName( cxStringToStd( clang_getTypeSpelling( fieldType ) ) );
				if ( clang_Cursor_isBitField( cursor ) != 0 )
				{
					const int32 bitWidth = clang_getFieldDeclBitWidth( cursor );
					if ( bitWidth != 1 )
					{
						CXCursor parent = clang_getCursorSemanticParent( cursor );
						SW_LOG_ERROR( "ERROR: PROPERTY() bitfield '%#' in '%#' has bit width %#. Only 1-bit bitfield boolean flags (e.g. uint8 _flag : 1;) are supported in reflection!",
									  prop._name.c_str(), AstVisitor::buildFullyQualifiedName( parent ).c_str(), bitWidth );
						collector->_bHasError = SW_TRUE;
						return CXChildVisit_Break;
					}

					prop._bIsBitField	  = SW_TRUE;
					const int64 bitOffset = clang_Cursor_getOffsetOfField( cursor );
					if ( bitOffset >= 0 )
					{
						prop._bitOffset	 = static_cast<uint32>( bitOffset );
						prop._byteOffset = static_cast<uint32>( bitOffset / 8 );
						prop._bitMask	 = static_cast<uint8>( 1u << ( bitOffset % 8 ) );
					}
				}
				sw::AnnotationApply::parsePropertyAnnotation( search._spelling, prop );
				parseContainerDetails( prop, fieldType );
				collector->_pProperties->push_back( std::move( prop ) );
				return CXChildVisit_Continue;
			}

			/** @brief 메서드/생성자에서 FUNCTION 메타를 수집합니다. */
			static CXChildVisitResult methodCollectorVisitor( CXCursor cursor, CXCursor, CXClientData data )
			{
				const CXCursorKind kind		 = clang_getCursorKind( cursor );
				MethodCollector*   collector = static_cast<MethodCollector*>( data );

				if ( kind != CXCursor_CXXMethod && kind != CXCursor_Constructor && kind != CXCursor_FunctionTemplate )
					return CXChildVisit_Continue;

				// REFLECT_BODY / COMPONENT_FACTORY 마커 — 리플렉트 FUNCTION 이 아님.
				if ( cxStringEquals( clang_getCursorSpelling( cursor ), annotationConstants::kReflectBodyMarkerFn ) ||
					 cxStringEquals( clang_getCursorSpelling( cursor ), annotationConstants::kComponentFactoryMarkerFn ) )
					return CXChildVisit_Continue;

				// REFLECT 타입: 사용자/암시 생성자를 자동 등록합니다(FUNCTION 불필요).
				// Abstract / Static 타입은 생성할 수 없습니다(Unreal UCLASS(Abstract) 스타일).
				if ( kind == CXCursor_Constructor )
				{
					if ( collector->_bSkipConstructors == SW_TRUE )
						return CXChildVisit_Continue;
					if ( clang_CXXConstructor_isCopyConstructor( cursor ) || clang_CXXConstructor_isMoveConstructor( cursor ) )
						return CXChildVisit_Continue;
#if defined( CINDEX_VERSION_MINOR ) && ( CINDEX_VERSION_MINOR >= 63 || ( defined( CINDEX_VERSION_MAJOR ) && CINDEX_VERSION_MAJOR > 0 ) )
					if ( clang_CXXMethod_isDeleted( cursor ) != 0 )
						return CXChildVisit_Continue;
#endif

					ParsedFunctionInfo method;
					method._name		   = annotationConstants::kCtorLookupName;
					method._returnTypeName = annotationConstants::kVoidTypeName;
					method._bConstructor   = SW_TRUE;
					method._category	   = annotationConstants::kConstructorCategory;

					const int32 numArgs = clang_Cursor_getNumArguments( cursor );
					for ( int32 argIndex = 0; argIndex < numArgs; ++argIndex )
					{
						const CXCursor argCursor = clang_Cursor_getArgument( cursor, static_cast<uint32>( argIndex ) );
						method._listParameterTypeName.push_back( normalizeTypeName(
							cxStringToStd( clang_getTypeSpelling( clang_getCursorType( argCursor ) ) ) ) );
					}

					if ( collector->_pFuncEntry != nullptr && collector->_pFuncEntry->_bFound == SW_TRUE )
					{
						sw::AnnotationApply::parseFunctionAnnotation( collector->_pFuncEntry->_spelling, method );
					}
					else
					{
						AnnotationSearch search{ annotationConstants::kFunctionPrefix };
						clang_visitChildren( cursor, annotationSearchVisitor, &search );
						if ( search._bFound == SW_TRUE )
							sw::AnnotationApply::parseFunctionAnnotation( search._spelling, method );
					}

					collector->_pMethods->push_back( std::move( method ) );
					return CXChildVisit_Continue;
				}

				if ( kind != CXCursor_CXXMethod && kind != CXCursor_FunctionDecl )
					return CXChildVisit_Continue;

				if ( kind == CXCursor_Destructor )
					return CXChildVisit_Continue;

				bool   bHasFuncAnn = false;
				string funcSpelling;
				if ( collector->_pFuncEntry != nullptr && collector->_pFuncEntry->_bFound == SW_TRUE )
				{
					bHasFuncAnn	 = true;
					funcSpelling = collector->_pFuncEntry->_spelling;
				}
				else
				{
					AnnotationSearch search{ annotationConstants::kFunctionPrefix };
					clang_visitChildren( cursor, annotationSearchVisitor, &search );
					bHasFuncAnn	 = search._bFound == SW_TRUE;
					funcSpelling = search._spelling;
				}

				// 순수 가상은 실제 AnnotateAttr 가 있어야 합니다. 소스 창 휴리스틱이
				// 이전 타입의 FUNCTION(...) 을 집어 `= 0` 메서드를 잘못 등록할 수 있습니다.
				if ( bHasFuncAnn == false )
				{
					if ( clang_CXXMethod_isPureVirtual( cursor ) )
						return CXChildVisit_Continue;
					if ( sourceHasPrimaryAnnotation( cursor, annotationConstants::kFunctionPrefix ) == false )
						return CXChildVisit_Continue;
				}

				ParsedFunctionInfo method;
				method._name		   = cxStringToStd( clang_getCursorSpelling( cursor ) );
				method._returnTypeName = normalizeTypeName(
					cxStringToStd( clang_getTypeSpelling( clang_getCursorResultType( cursor ) ) ) );
				method._bStatic = clang_CXXMethod_isStatic( cursor ) != 0 ? SW_TRUE : SW_FALSE;
				method._bConst	= clang_CXXMethod_isConst( cursor ) != 0 ? SW_TRUE : SW_FALSE;

				const int32 numArgs = clang_Cursor_getNumArguments( cursor );
				for ( int32 argIndex = 0; argIndex < numArgs; ++argIndex )
				{
					const CXCursor argCursor = clang_Cursor_getArgument( cursor, static_cast<uint32>( argIndex ) );
					method._listParameterTypeName.push_back( normalizeTypeName(
						cxStringToStd( clang_getTypeSpelling( clang_getCursorType( argCursor ) ) ) ) );
				}

				if ( bHasFuncAnn )
					sw::AnnotationApply::parseFunctionAnnotation( funcSpelling, method );

				collector->_pMethods->push_back( std::move( method ) );
				return CXChildVisit_Continue;
			}

			struct BaseClassCollector
			{
				string _ownerFQN;
				string _firstBaseFQN;
				int32  _baseCount = 0;
			};

			/** @brief 베이스 클래스 FQN을 부모로 기록합니다. ParsedTypeInfo 는 부모 하나만 담습니다. */
			static CXChildVisitResult baseClassVisitor( CXCursor cursor, CXCursor, CXClientData data )
			{
				if ( clang_getCursorKind( cursor ) != CXCursor_CXXBaseSpecifier )
					return CXChildVisit_Continue;

				BaseClassCollector* collector = static_cast<BaseClassCollector*>( data );

				const CXType   baseType = clang_getCursorType( cursor );
				const CXCursor baseDecl = clang_getTypeDeclaration( baseType );
				const string   baseFQN =
					  ( clang_Cursor_isNull( baseDecl ) == 0 ) ? AstVisitor::buildFullyQualifiedName( baseDecl ) : string{};

				++collector->_baseCount;
				if ( collector->_baseCount > 1 )
				{
					SW_LOG_WARNING( "%# has multiple base classes; only '%#' is reflected, '%#' is ignored.",
									collector->_ownerFQN, collector->_firstBaseFQN, baseFQN );
					return CXChildVisit_Continue;
				}

				collector->_firstBaseFQN = baseFQN;
				return CXChildVisit_Continue;
			}

			struct StructMemberCollectContext
			{
				BaseClassCollector	   _bases;
				FieldCollector		   _fields;
				MethodCollector		   _methods;
				uint8				   _bBodyFound	  : 1;
				uint8				   _bFactoryFound : 1;
				[[maybe_unused]] uint8 _reserved	  : 6;

				StructMemberCollectContext()
					: _bases{}
					, _fields{}
					, _methods{}
					, _bBodyFound{ SW_FALSE }
					, _bFactoryFound{ SW_FALSE }
					, _reserved{ 0 }
				{
				}
			};

			/** @brief REFLECT 타입 멤버를 한 번의 자식 순회로 수집합니다. */
			static CXChildVisitResult structMemberCollectVisitor( CXCursor cursor, CXCursor parent, CXClientData data )
			{
				StructMemberCollectContext* ctx	 = static_cast<StructMemberCollectContext*>( data );
				const CXCursorKind			kind = clang_getCursorKind( cursor );

				if ( kind == CXCursor_CXXBaseSpecifier )
					return baseClassVisitor( cursor, parent, &ctx->_bases );

				if ( kind == CXCursor_FieldDecl )
				{
					const CXChildVisitResult res = fieldCollectorVisitor( cursor, parent, &ctx->_fields );
					if ( ctx->_fields._bHasError == SW_TRUE )
						return CXChildVisit_Break;
					return res;
				}

				if ( kind == CXCursor_CXXMethod || kind == CXCursor_Constructor || kind == CXCursor_FunctionTemplate ||
					 kind == CXCursor_FunctionDecl || kind == CXCursor_Destructor )
				{
					if ( cxStringEquals( clang_getCursorSpelling( cursor ), annotationConstants::kReflectBodyMarkerFn ) )
					{
						ctx->_bBodyFound = SW_TRUE;
						return CXChildVisit_Continue;
					}
					if ( cxStringEquals( clang_getCursorSpelling( cursor ), annotationConstants::kComponentFactoryMarkerFn ) )
					{
						ctx->_bFactoryFound = SW_TRUE;
						return CXChildVisit_Continue;
					}

					// bodyAnn / factoryAnn / functionAnn 을 한 번의 순회로 수집합니다.
					MultiAnnotationSearch multi;
					multi.add( annotationConstants::kReflectBodyPrefix );
					multi.add( annotationConstants::kComponentFactoryPrefix );
					multi.add( annotationConstants::kFunctionPrefix );
					clang_visitChildren( cursor, multiAnnotationVisitor, &multi );

					const MultiAnnotationSearch::Entry* bodyEntry	 = multi.get( annotationConstants::kReflectBodyPrefix );
					const MultiAnnotationSearch::Entry* factoryEntry = multi.get( annotationConstants::kComponentFactoryPrefix );
					const MultiAnnotationSearch::Entry* funcEntry	 = multi.get( annotationConstants::kFunctionPrefix );

					if ( bodyEntry != nullptr && bodyEntry->_bFound == SW_TRUE )
						ctx->_bBodyFound = SW_TRUE;
					if ( factoryEntry != nullptr && factoryEntry->_bFound == SW_TRUE )
						ctx->_bFactoryFound = SW_TRUE;

					ctx->_methods._pFuncEntry = funcEntry;
					return methodCollectorVisitor( cursor, parent, &ctx->_methods );
				}

				return CXChildVisit_Continue;
			}

			/** @brief AnnotateAttr 접두사만 검사합니다 (소스 폴백 없음 — 검증용 경량 경로). */
			static bool hasAnnotateAttrPrefix( CXCursor cursor, string_view prefix )
			{
				AnnotationSearch search{ prefix };
				clang_visitChildren( cursor, annotationSearchVisitor, &search );
				return search._bFound == SW_TRUE;
			}

			/** @brief Component / SceneComponent 파생인지 베이스 체인을 검사합니다. */
			static CXChildVisitResult componentBaseVisitor( CXCursor cursor, CXCursor, CXClientData clientData )
			{
				if ( clang_getCursorKind( cursor ) != CXCursor_CXXBaseSpecifier )
					return CXChildVisit_Continue;

				const CXType   baseType = clang_getCursorType( cursor );
				const CXCursor baseDecl = clang_getTypeDeclaration( baseType );
				if ( clang_Cursor_isNull( baseDecl ) != 0 )
					return CXChildVisit_Continue;

				if ( isDerivedFromComponent( baseDecl ) == false )
					return CXChildVisit_Continue;

				*static_cast<bool*>( clientData ) = true;
				return CXChildVisit_Break;
			}

			static bool isDerivedFromComponent( CXCursor cursor )
			{
				// thread_local 캐시: 동일 중간 베이스 클래스에 대한 반복 재귀 탐색을 방지합니다.
				thread_local unordered_map<string, bool> s_componentCache;

				const string			 fqn = AstVisitor::buildFullyQualifiedName( cursor );
				const ParserClangConfig& cfg = ParserContext::getSharedConfig();
				for ( const string& baseType : cfg._listComponentBaseType )
				{
					if ( fqn == baseType )
						return true;
				}

				const auto cacheIt = s_componentCache.find( fqn );
				if ( cacheIt != s_componentCache.end() )
					return cacheIt->second;

				// 캐시 미스 — 순환 참조 방지를 위해 먼저 false 로 삽입
				s_componentCache[fqn] = false;

				bool bDerives = false;
				clang_visitChildren( cursor, componentBaseVisitor, &bDerives );

				s_componentCache[fqn] = bDerives;
				return bDerives;
			}

			struct EnumeratorCollector
			{
				vector<ParsedEnumeratorInfo>* _listEnumerator = nullptr;
			};

			/** @brief enumerator 이름·값을 수집합니다. */
			static CXChildVisitResult enumeratorCollectorVisitor( CXCursor cursor, CXCursor, CXClientData data )
			{
				if ( clang_getCursorKind( cursor ) != CXCursor_EnumConstantDecl )
					return CXChildVisit_Continue;

				EnumeratorCollector* collector = static_cast<EnumeratorCollector*>( data );
				ParsedEnumeratorInfo enumerator;
				enumerator._name  = cxStringToStd( clang_getCursorSpelling( cursor ) );
				enumerator._value = clang_getEnumConstantDeclValue( cursor );
				collector->_listEnumerator->push_back( std::move( enumerator ) );
				return CXChildVisit_Continue;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	AstVisitor::AstVisitor( CXTranslationUnit translationUnit )
		: _translationUnit{ translationUnit }
		, _listType{}
		, _listEnum{}
		, _bHasError{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	// ------------------------------------------------------------------------------
	// D) AstVisitor — visit / onStructDecl / onEnumDecl
	// ------------------------------------------------------------------------------
	bool AstVisitor::visit()
	{
		CXCursor rootCursor = clang_getTranslationUnitCursor( _translationUnit );
		clang_visitChildren( rootCursor, visitCursor, this );
		return _bHasError == SW_FALSE;
	}

	CXChildVisitResult AstVisitor::visitCursor( CXCursor cursor, CXCursor, CXClientData clientData )
	{
		AstVisitor*		   self = static_cast<AstVisitor*>( clientData );
		const CXCursorKind kind = clang_getCursorKind( cursor );

		if ( kind == CXCursor_Namespace )
			return CXChildVisit_Recurse;

		// 인클루드된 외부/시스템 헤더 선언들은 즉시 스킵하여 AST 순회 비용 대폭 절감
		const CXSourceLocation loc = clang_getCursorLocation( cursor );
		if ( clang_Location_isFromMainFile( loc ) == 0 )
			return CXChildVisit_Continue;

		if ( kind == CXCursor_FieldDecl )
		{
			if ( AstVisitorInternal::hasAnnotateAttrPrefix( cursor, annotationConstants::kPropertyPrefix ) ||
				 AstVisitorInternal::sourceHasPrimaryAnnotation( cursor, annotationConstants::kPropertyPrefix ) )
			{
				CXCursor parent = clang_getCursorSemanticParent( cursor );
				if ( AstVisitorInternal::hasAnnotateAttrPrefix( parent, annotationConstants::kReflectPrefix ) == false &&
					 AstVisitorInternal::sourceHasPrimaryAnnotation( parent, annotationConstants::kReflectPrefix ) == false )
				{
					SW_LOG_ERROR( "ERROR: PROPERTY() is used in class/struct '%#', but it lacks REFLECT()!", buildFullyQualifiedName( parent ).c_str() );
					self->_bHasError = SW_TRUE;
					return CXChildVisit_Break;
				}
			}
			return CXChildVisit_Continue;
		}

		if ( kind == CXCursor_CXXMethod )
		{
			const bool bHasFunction = AstVisitorInternal::hasAnnotateAttrPrefix( cursor, annotationConstants::kFunctionPrefix ) ||
									  AstVisitorInternal::sourceHasPrimaryAnnotation( cursor, annotationConstants::kFunctionPrefix );
			const bool bHasBody = AstVisitorInternal::hasAnnotateAttrPrefix( cursor, "REFLECT_BODY" ) ||
								  ( AstVisitorInternal::cxStringToStd( clang_getCursorSpelling( cursor ) ) == annotationConstants::kReflectBodyMarkerFn );
			if ( bHasFunction || bHasBody )
			{
				CXCursor   parent		  = clang_getCursorSemanticParent( cursor );
				const bool bParentReflect = AstVisitorInternal::hasAnnotateAttrPrefix( parent, annotationConstants::kReflectPrefix ) ||
											AstVisitorInternal::sourceHasPrimaryAnnotation( parent, annotationConstants::kReflectPrefix );
				if ( bParentReflect == false )
				{
					if ( bHasFunction )
					{
						SW_LOG_ERROR( "ERROR: FUNCTION() is used in class/struct '%#', but it lacks REFLECT()!", buildFullyQualifiedName( parent ).c_str() );
						self->_bHasError = SW_TRUE;
						return CXChildVisit_Break;
					}
					SW_LOG_ERROR( "ERROR: REFLECT_BODY() is used in class/struct '%#', but it lacks REFLECT()!", buildFullyQualifiedName( parent ).c_str() );
					self->_bHasError = SW_TRUE;
					return CXChildVisit_Break;
				}
			}
			return CXChildVisit_Continue;
		}

		// 클래스 템플릿은 FQN 에 인자가 없어 offsetof / TypeRegistrar<T> 가 성립하지 않습니다.
		// 조용히 빠지면 원인 파악이 어려우므로 경고만 남기고 건너뜁니다.

		if ( kind == CXCursor_ClassTemplate || kind == CXCursor_ClassTemplatePartialSpecialization )
		{
			if ( hasAnnotation( cursor, annotationConstants::kReflectPrefix ) )
			{
				SW_LOG_WARNING( "REFLECT on class template is not supported, skipping: %#",
								buildFullyQualifiedName( cursor ) );
			}
			return CXChildVisit_Continue;
		}

		if ( kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl )
		{
			if ( hasAnnotation( cursor, annotationConstants::kReflectPrefix ) )
				self->onStructDeclaration( cursor );

			return CXChildVisit_Recurse;
		}

		if ( kind == CXCursor_EnumDecl )
		{
			if ( hasAnnotation( cursor, annotationConstants::kEnumPrefix ) )
				self->onEnumDeclaration( cursor );
			return CXChildVisit_Continue;
		}

		return CXChildVisit_Continue;
	}

	/**
	 * @brief 대상 커서가 메인 소스 파일에 위치하며 지정한 어노테이션 접두사를 가지는지 검사합니다.
	 */
	bool AstVisitor::hasAnnotation( CXCursor cursor, string_view prefix )
	{
		CXSourceLocation loc = clang_getCursorLocation( cursor );
		if ( clang_Location_isFromMainFile( loc ) == false )
			return false;

		AstVisitorInternal::AnnotationSearch search{ prefix };
		clang_visitChildren( cursor, AstVisitorInternal::annotationSearchVisitor, &search );
		if ( search._bFound == SW_TRUE )
			return true;

		// 기본 매크로만 — "ENUM;BitFlag" 같은 세분 태그에는 폴백하지 않습니다.
		return AstVisitorInternal::sourceHasPrimaryAnnotation( cursor, prefix );
	}

	/**
	 * @brief `REFLECT(...)` 매크로가 붙은 struct/class 선언을 파싱합니다.
	 *
	 * [수집 항목 단계]:
	 * 1. 클래스 이름 및 전체 네임스페이스 경로(FQN) 추출
	 * 2. `REFLECT(...)` 매크로 인자 파싱 (Abstract, Static, Category, Alias 등)
	 * 3. 부모 기본 클래스(Base Class) 탐색 및 상속 관계 연결
	 * 4. `REFLECT_BODY()` 및 `COMPONENT_FACTORY()` 매크로 존재 여부 확인
	 * 5. 자식 멤버 변수(`PROPERTY`) 및 멤버 함수(`FUNCTION`) 메타데이터 재귀 수집
	 */
	void AstVisitor::onStructDeclaration( CXCursor cursor )
	{
		ParsedTypeInfo typeInfo;
		typeInfo._name				 = getCursorSpelling( cursor );
		typeInfo._fullyQualifiedName = buildFullyQualifiedName( cursor );

		BLOCK( "Parse REFLECT Annotation" )
		{
			AstVisitorInternal::AnnotationSearch reflectSearch{ annotationConstants::kReflectPrefix };
			clang_visitChildren( cursor, AstVisitorInternal::annotationSearchVisitor, &reflectSearch );
			if ( reflectSearch._bFound == SW_FALSE )
			{
				reflectSearch._spelling = AstVisitorInternal::sourceExtractMacroAnnotation( cursor, annotationConstants::kReflectMacroOpen, annotationConstants::kReflectPrefix );
				reflectSearch._bFound	= reflectSearch._spelling.empty() == false ? SW_TRUE : SW_FALSE;
			}
			if ( reflectSearch._bFound == SW_TRUE )
				sw::AnnotationApply::parseReflectAnnotation( reflectSearch._spelling, typeInfo );
			// C++ 순수 가상 함수가 포함된 추상 클래스이면 UCLASS(Abstract)처럼 플래그 설정
			if ( clang_CXXRecord_isAbstract( cursor ) != 0 )
				typeInfo._bAbstract = SW_TRUE;
		}

		BLOCK( "Collect Bases / Markers / Fields / Methods" )
		{
			AstVisitorInternal::StructMemberCollectContext collect{};
			collect._bases._ownerFQN			= typeInfo._fullyQualifiedName;
			collect._methods._bSkipConstructors = ( typeInfo._bAbstract == SW_TRUE || typeInfo._bStatic == SW_TRUE ) ? SW_TRUE : SW_FALSE;
			collect._fields._pProperties		= &typeInfo._listProperty;
			collect._methods._pMethods			= &typeInfo._listMethod;
			clang_visitChildren( cursor, AstVisitorInternal::structMemberCollectVisitor, &collect );
			if ( collect._fields._bHasError == SW_TRUE )
			{
				_bHasError = SW_TRUE;
				return;
			}
			typeInfo._parentFQN			= collect._bases._firstBaseFQN;
			typeInfo._bReflectBody		= collect._bBodyFound == SW_TRUE ? SW_TRUE : SW_FALSE;
			typeInfo._bComponentFactory = ( collect._bFactoryFound == SW_TRUE || AstVisitorInternal::isDerivedFromComponent( cursor ) ) ? SW_TRUE : SW_FALSE;
		}

		SW_LOG_TRACE( "REFLECT class : %#  (props=%# methods=%# abstract=%# static=%# body=%# factory=%#)",
					  typeInfo._fullyQualifiedName, typeInfo._listProperty.size(), typeInfo._listMethod.size(),
					  typeInfo._bAbstract ? 1 : 0, typeInfo._bStatic ? 1 : 0, typeInfo._bReflectBody ? 1 : 0,
					  typeInfo._bComponentFactory ? 1 : 0 );
		_listType.push_back( std::move( typeInfo ) );
	}

	/**
	 * @brief `ENUM(...)` 매크로가 붙은 열거형(Enum / Enum Class) 선언을 파싱합니다.
	 *
	 * [수집 항목 단계]:
	 * 1. 열거형의 모든 원소(Enumerator) 이름 및 정수 값 추출
	 * 2. `ENUM(...)` 어노테이션 속성(Alias, Count, Invalid 등) 파싱
	 * 3. 모든 값이 2의 거듭제곱 형태인지 자동 분석하여 비트플래그(BitFlag) 여부 판정
	 */
	void AstVisitor::onEnumDeclaration( CXCursor cursor )
	{
		ParsedEnumInfo enumInfo;
		enumInfo._name				 = getCursorSpelling( cursor );
		enumInfo._fullyQualifiedName = buildFullyQualifiedName( cursor );

		BLOCK( "Collect Enumerators" )
		{
			// 모든 열거자 항목(이름, 정수값) 수집
			AstVisitorInternal::EnumeratorCollector enumeratorCollector{ &enumInfo._listEnumerator };
			clang_visitChildren( cursor, AstVisitorInternal::enumeratorCollectorVisitor, &enumeratorCollector );
		}

		BLOCK( "Parse ENUM annotation (Alias / …)" )
		{
			// 소스 ENUM(...) 을 우선해 Alias= 가 BitFlag annotate 에 가려지지 않게 합니다.
			string enumSpelling = AstVisitorInternal::sourceExtractMacroAnnotation( cursor, annotationConstants::kEnumMacroOpen, annotationConstants::kEnumPrefix );
			if ( enumSpelling.empty() )
			{
				AstVisitorInternal::AnnotationSearch enumSearch{ annotationConstants::kEnumPrefix };
				clang_visitChildren( cursor, AstVisitorInternal::annotationSearchVisitor, &enumSearch );
				enumSpelling = enumSearch._spelling;
			}
			if ( enumSpelling.empty() == false )
				sw::AnnotationApply::parseEnumAnnotation( enumSpelling, enumInfo );
			if ( enumInfo._countEnumerator.empty() == false && enumInfo._invalidEnumerator.empty() )
				enumInfo._invalidEnumerator = enumInfo._countEnumerator;
		}

		BLOCK( "Check BitFlag" )
		{
			// 명시적 BitFlag 어노테이션이 없더라도, 값이 모두 1, 2, 4, 8... 비트 패턴이면 BitFlag로 자동 감지
			if ( enumInfo._bIsBitFlag == 0 )
			{
				bool  allPowerOf2  = enumInfo._listEnumerator.empty() == false;
				int32 nonZeroCount = 0;
				for ( const ParsedEnumeratorInfo& e : enumInfo._listEnumerator )
				{
					if ( e._value != 0 )
					{
						++nonZeroCount;
						if ( ( e._value & ( e._value - 1 ) ) != 0 )
						{
							allPowerOf2 = false;
							break;
						}
					}
				}
				if ( allPowerOf2 && nonZeroCount > 1 )
					enumInfo._bIsBitFlag = 1;
			}
		}

		SW_LOG_INFO( "ENUM          : %#  (%# values, _bIsBitFlag=%# aliases=%#)",
					 enumInfo._fullyQualifiedName, enumInfo._listEnumerator.size(), enumInfo._bIsBitFlag ? "true" : "false",
					 enumInfo._listAlias.size() );
		_listEnum.push_back( std::move( enumInfo ) );
	}

	/**
	 * @brief 커서의 순수 식별자 명칭을 반환합니다.
	 */
	string AstVisitor::getCursorSpelling( CXCursor cursor )
	{
		return AstVisitorInternal::cxStringToStd( clang_getCursorSpelling( cursor ) );
	}

	string AstVisitor::buildFullyQualifiedName( CXCursor cursor )
	{
		vector<string> listPart;
		CXCursor	   current = cursor;
		while ( true )
		{
			const CXCursorKind kind = clang_getCursorKind( current );
			if ( kind == CXCursor_TranslationUnit )
				break;

			const string spelling = AstVisitorInternal::cxStringToStd( clang_getCursorSpelling( current ) );
			if ( spelling.empty() == false )
				listPart.push_back( spelling );

			current = clang_getCursorSemanticParent( current );
		}

		StringBuilder<constant::kMaxBuffer1024> fqn;
		for ( auto it = listPart.rbegin(); it != listPart.rend(); ++it )
		{
			if ( fqn.size() > 0 )
				fqn.append( "::" );
			fqn.append( *it );
		}
		return string( fqn.view() );
	}
} // namespace sw
