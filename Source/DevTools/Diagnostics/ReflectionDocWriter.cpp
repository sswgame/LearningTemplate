#include "pch.h"

#include "DevTools/Diagnostics/ReflectionDocWriter.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionEnumNames.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionUnits.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        struct ReflectionDocWriterInternal
        {
            using MarkdownBuilder = StringBuilder<constant::buffer::kMax4096>;

            /** @brief 표 칸에 넣을 글 — `|` 와 줄바꿈이 표를 깨지 않게 바꿉니다. */
            static string escapeCell( const string_view text )
            {
                string escaped;
                escaped.reserve( text.size() );
                for ( const utf8 character : text )
                {
                    if ( character == '|' )
                        escaped += "\\|";
                    else if ( character == '\n' || character == '\r' )
                        escaped += ' ';
                    else
                        escaped += character;
                }
                return escaped;
            }

            /** @brief 문서 안 이동 고리 이름입니다(`sw::Foo` → `sw-foo`). */
            static string makeAnchor( const string_view fqn )
            {
                string anchor;
                for ( const utf8 character : fqn )
                {
                    const bool bAnchorCharacter = ( 'a' <= character && character <= 'z' ) || ( '0' <= character && character <= '9' ) || character == '_';
                    if ( bAnchorCharacter )
                        anchor += character;
                    else if ( 'A' <= character && character <= 'Z' )
                        anchor += static_cast<utf8>( character - 'A' + 'a' );
                    else if ( character == ':' && ( anchor.empty() || anchor.back() != '-' ) )
                        anchor += '-';
                }
                return anchor;
            }

            static void appendParameterList( MarkdownBuilder& out, const vector<FunctionParameterInfo>& listParameter )
            {
                for ( size_t paramIndex = 0; paramIndex < listParameter.size(); ++paramIndex )
                {
                    const FunctionParameterInfo& parameter = listParameter[paramIndex];
                    if ( paramIndex > 0 )
                        out.append( ", " );
                    out.append( parameter._typeName );
                    if ( parameter._name.empty() == false )
                        out.appendFormat( " %#", parameter._name );
                    if ( parameter.hasDefaultValue() )
                        out.appendFormat( " = %#", parameter._defaultValue );
                }
            }

            /** @brief 쉼표로 이은 목록에 항목 하나를 붙입니다. */
            static void appendFlag( string& text, const string_view item )
            {
                if ( text.empty() == false )
                    text += ", ";
                text += item;
            }

            /** @brief 쉼표로 이은 목록에 `이름=값` 항목 하나를 붙입니다(문서 글이라 잘리지 않게 StringBuilder). */
            static void appendFlag( string& text, const string_view format, const string_view value )
            {
                StringBuilder<constant::buffer::kMax256> flag;
                flag.appendFormat( format, value );
                appendFlag( text, flag.view() );
            }

            /** @brief 프로퍼티의 플래그 · 역할 · 표시 메타를 한 칸에 씁니다. */
            static string makePropertyFlags( const PropertyInfo& prop )
            {
                const PropertyMetadata& meta = *prop._metadata;
                string                  text;
                if ( meta._bReadOnly == SW_TRUE )
                    appendFlag( text, "ReadOnly" );
                if ( meta._bTransient == SW_TRUE )
                    appendFlag( text, "Transient" );
                if ( meta._bReplicated == SW_TRUE )
                {
                    if ( meta._repNotify.empty() )
                        appendFlag( text, "Replicated" );
                    else
                        appendFlag( text, "RepNotify=%#", meta._repNotify.c_str() );
                }
                if ( meta._bSaveGame == SW_TRUE )
                    appendFlag( text, "SaveGame" );
                if ( meta._bRuntimeState == SW_TRUE )
                    appendFlag( text, "RuntimeState" );
                if ( meta._bInterp == SW_TRUE )
                    appendFlag( text, "Interp" );
                if ( meta._validate.empty() == false )
                    appendFlag( text, "Validate=%#", meta._validate.c_str() );
                if ( meta._bAssetPath == SW_TRUE )
                {
                    if ( meta._assetType.empty() )
                        appendFlag( text, "Asset" );
                    else
                        appendFlag( text, "Asset=%#", meta._assetType );
                }
                if ( prop._bIsContainer == SW_TRUE )
                    appendFlag( text, toString( prop._containerKind ) );
                const PropertyEditorMetadata& editor = meta.getEditor(); // 에디터 메타데이터가 붙지 않은 구성(배포)은 기본값이라 빈 칸이다
                if ( editor._editCondition.empty() == false )
                    appendFlag( text, "EditCondition=`%#`", editor._editCondition );
                if ( const ReflectUnit* pUnit = ReflectUnit::findUnit( meta._unit ) )
                    appendFlag( text, "Units=%#", pUnit->_pName );
                else if ( const string* pUnits = editor.findCustomMeta( hashed_string( "Units" ) ) )
                    appendFlag( text, "Units=%#", *pUnits );
                return text;
            }

            static string makeRange( const PropertyMetadata& meta )
            {
                if ( meta._bHasMinRange == SW_FALSE && meta._bHasMaxRange == SW_FALSE )
                    return string();
                StringBuilder<constant::buffer::kMax64> text;
                if ( meta._bHasMinRange == SW_TRUE )
                    text.appendFormat( "%#", meta._minRange );
                else
                    text.append( '-' );
                text.append( " ~ " );
                if ( meta._bHasMaxRange == SW_TRUE )
                    text.appendFormat( "%#", meta._maxRange );
                else
                    text.append( '-' );
                return string( text.view() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string ReflectionDocWriter::makeModuleFileName( const hashed_string& moduleName )
    {
        StringBuilder<constant::buffer::kMax128> fileName;
        fileName.appendFormat( "%#.md", moduleName.empty() ? "Unknown" : moduleName.c_str() );
        return string( fileName.view() );
    }

    string ReflectionDocWriter::makeTypeMarkdown( const TypeInfo& type )
    {
        ReflectionDocWriterInternal::MarkdownBuilder out;
        out.appendFormat( "## %#\n\n", type._fullyQualifiedName.c_str() );
        if ( type._parentFQN.empty() == false )
            out.appendFormat( "- 부모: `%#`\n", type._parentFQN.c_str() );
        if ( type._bAbstract == SW_TRUE )
            out.append( "- Abstract\n" );
        if ( type._addComponent != nullptr )
            out.append( "- 컴포넌트(이름으로 만들 수 있다)\n" );
        const TypeEditorMetadata& typeEditor = type._metadata->getEditor();
        if ( typeEditor._category.empty() == false )
            out.appendFormat( "- 분류: %#\n", typeEditor._category );
        if ( typeEditor._tooltip.empty() == false )
            out.appendFormat( "- 설명: %#\n", ReflectionDocWriterInternal::escapeCell( typeEditor._tooltip ) );
        if ( type._pValidate != nullptr )
            out.append( "- 타입 검증 함수가 있다\n" );

        if ( type._listProperty.empty() == false )
        {
            out.append( "\n| 프로퍼티 | 타입 | 기본값 | 범위 | 플래그 | 설명 |\n|---|---|---|---|---|---|\n" );
            // 타입마다 절이 따로라 선언한 칸만 적는다(기반 칸은 기반 타입의 절) — 걷기 한 곳(`ReflectPropertyVisitor`)의 선언 칸 걷기다.
            ReflectVisitOptions options;
            options._bWithBase = SW_FALSE;
            const ReflectPropertyVisitor propertyVisitor{ options };
            (void)propertyVisitor.visitProperties( type, static_cast<const void*>( nullptr ), [&out]( const ReflectVisitConstField& field )
            {
                const PropertyInfo& prop    = field._property;
                const string&       tooltip = prop._metadata->getEditor()._tooltip;
                out.appendFormat( "| `%#` | `%#` | %# | %# | %# | %# |\n", prop._name.c_str(), ReflectionDocWriterInternal::escapeCell( prop._typeName.view() ),
                                  ReflectionDocWriterInternal::escapeCell( prop._metadata->_defaultValue ), ReflectionDocWriterInternal::makeRange( *prop._metadata ),
                                  ReflectionDocWriterInternal::makePropertyFlags( prop ), ReflectionDocWriterInternal::escapeCell( tooltip ) );
                return ReflectVisitFlow::Continue;
            } );
        }

        bool bFunctionHeader = false;
        for ( const FunctionInfo& function : type._listMethod )
        {
            if ( function._metadata->_bConstructor == SW_TRUE )
                continue;
            if ( bFunctionHeader == false )
            {
                out.append( "\n**함수**\n\n" );
                bFunctionHeader = true;
            }
            out.appendFormat( "- `%# %#(", function._returnTypeName.empty() ? string_view( "void" ) : string_view( function._returnTypeName ), function._name );
            ReflectionDocWriterInternal::appendParameterList( out, function._listParameter );
            out.append( ")`" );
            if ( function._metadata->_netRole != FunctionNetRole::Local )
                out.appendFormat( " — %#", toString( function._metadata->_netRole ) );
            out.append( '\n' );
        }

        if ( type._listEvent.empty() == false )
        {
            out.append( "\n**이벤트**\n\n" );
            for ( const EventInfo& event : type._listEvent )
            {
                out.appendFormat( "- `%#(", event._name.c_str() );
                ReflectionDocWriterInternal::appendParameterList( out, event._listParameter );
                out.append( ")`\n" );
            }
        }
        out.append( '\n' );
        return string( out.view() );
    }

    string ReflectionDocWriter::makeEnumMarkdown( const EnumInfo& info )
    {
        ReflectionDocWriterInternal::MarkdownBuilder out;
        out.appendFormat( "## %#%#\n\n| 이름 | 값 |\n|---|---|\n", info._fullyQualifiedName.c_str(), info._bIsBitFlag == SW_TRUE ? " (Flags)" : "" );
        vector<pair<int64, hashed_string>> listEntry;
        for ( const auto& [value, name] : info._mapValueToName )
        {
            listEntry.emplace_back( value, name );
        }
        VectorUtil::sort( listEntry, []( const pair<int64, hashed_string>& lhs, const pair<int64, hashed_string>& rhs )
        { return lhs.first < rhs.first || ( lhs.first == rhs.first && lhs.second.lexicalLess( rhs.second ) ); } );
        for ( const auto& [value, name] : listEntry )
        {
            out.appendFormat( "| `%#` | %# |\n", name, value );
        }
        out.append( '\n' );
        return string( out.view() );
    }

    uint32 ReflectionDocWriter::writeMarkdown( const TypeRegistry& registry, const string_view outputDir )
    {
        if ( FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( outputDir ) ) == false )
        {
            SW_LOG_ERROR_ID( "ReflectionDocWriter.CannotCreateReflectionDocs", "Cannot create reflection docs folder '%#'", outputDir );
            return 0;
        }

        // 모듈마다 타입 · 열거형을 이름 순으로 모은다 — 같은 등록이면 같은 문서다.
        vector<const TypeInfo*> listType;
        registry.forEachType( [&listType]( const TypeInfo& type )
        {
            if ( type.isPrimitive() == false )
                listType.push_back( &type );
        } );
        vector<const EnumInfo*> listEnum;
        registry.forEachEnum( [&listEnum]( const EnumInfo& info )
        { listEnum.push_back( &info ); } );
        VectorUtil::sort( listType, []( const TypeInfo* pLhs, const TypeInfo* pRhs )
        { return pLhs->_fullyQualifiedName.lexicalLess( pRhs->_fullyQualifiedName ); } );
        VectorUtil::sort( listEnum, []( const EnumInfo* pLhs, const EnumInfo* pRhs )
        { return pLhs->_fullyQualifiedName.lexicalLess( pRhs->_fullyQualifiedName ); } );

        vector<hashed_string> listModule;
        for ( const TypeInfo* pType : listType )
        {
            VectorUtil::addUnique( listModule, pType->_moduleName );
        }
        for ( const EnumInfo* pEnum : listEnum )
        {
            VectorUtil::addUnique( listModule, pEnum->_moduleName );
        }
        VectorUtil::sort( listModule, HashedStringLexicalLess{} );

        ReflectionDocWriterInternal::MarkdownBuilder index;
        index.append( "# 리플렉션 API\n\n`App --write-reflection-docs` 가 등록된 타입에서 만든 문서입니다(손으로 고치지 않습니다).\n\n| 모듈 | 타입 | 열거형 |\n|---|---|---|\n" );
        uint32 fileCount = 0;
        for ( const hashed_string& moduleName : listModule )
        {
            const string                                 fileName   = makeModuleFileName( moduleName );
            const utf8*                                  pShownName = moduleName.empty() ? "Unknown" : moduleName.c_str();
            ReflectionDocWriterInternal::MarkdownBuilder page;
            page.appendFormat( "# %#\n\n[목록으로](index.md)\n\n", pShownName );
            uint32 typeCount = 0;
            uint32 enumCount = 0;
            for ( const TypeInfo* pType : listType )
            {
                if ( pType->_moduleName != moduleName )
                    continue;
                page.append( makeTypeMarkdown( *pType ) );
                ++typeCount;
            }
            for ( const EnumInfo* pEnum : listEnum )
            {
                if ( pEnum->_moduleName != moduleName )
                    continue;
                page.append( makeEnumMarkdown( *pEnum ) );
                ++enumCount;
            }
            if ( FileUtil::writeTextFile( FileUtil::toDiskPath( FileUtil::joinPath( outputDir, fileName ) ), page.view() ) == false )
                continue;
            ++fileCount;
            index.appendFormat( "| [%#](%#) | %# | %# |\n", pShownName, fileName, typeCount, enumCount );
        }
        index.append( "\n## 타입\n\n" );
        for ( const TypeInfo* pType : listType )
        {
            index.appendFormat( "- [`%#`](%#", pType->_fullyQualifiedName.c_str(), makeModuleFileName( pType->_moduleName ) );
            index.appendFormat( "#%#)\n", ReflectionDocWriterInternal::makeAnchor( pType->_fullyQualifiedName.view() ) );
        }
        if ( FileUtil::writeTextFile( FileUtil::toDiskPath( FileUtil::joinPath( outputDir, "index.md" ) ), index.view() ) )
            ++fileCount;
        SW_LOG_INFO_ID( "ReflectionDocWriter.WroteReflectionDocsFiles", "Wrote reflection docs: %# files, %# types, %# enums -> %#", fileCount, static_cast<uint32>( listType.size() ),
                        static_cast<uint32>( listEnum.size() ), outputDir );
        return fileCount;
    }
} // namespace sw
