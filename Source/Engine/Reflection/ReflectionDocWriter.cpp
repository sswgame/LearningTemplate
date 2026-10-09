#include "pch.h"

#include "Engine/Reflection/ReflectionDocWriter.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionEnumNames.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

SW_LOG_CALLER( "ReflectionDocWriter" );
namespace sw
{
    namespace
    {
        struct ReflectionDocWriterInternal
        {
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
                    if ( ( 'a' <= character && character <= 'z' ) || ( '0' <= character && character <= '9' ) || character == '_' )
                        anchor += character;
                    else if ( 'A' <= character && character <= 'Z' )
                        anchor += static_cast<utf8>( character - 'A' + 'a' );
                    else if ( character == ':' && ( anchor.empty() || anchor.back() != '-' ) )
                        anchor += '-';
                }
                return anchor;
            }

            static void appendParameterList( string& out, const vector<FunctionParameterInfo>& listParameter )
            {
                for ( size_t paramIndex = 0; paramIndex < listParameter.size(); ++paramIndex )
                {
                    const FunctionParameterInfo& parameter = listParameter[paramIndex];
                    if ( paramIndex > 0 )
                        out += ", ";
                    out += parameter._typeName;
                    if ( parameter._name.empty() == false )
                        out += " " + parameter._name;
                    if ( parameter.hasDefaultValue() )
                        out += " = " + parameter._defaultValue;
                }
            }

            /** @brief 프로퍼티의 플래그 · 역할 · 표시 메타를 한 칸에 씁니다. */
            static string makePropertyFlags( const PropertyInfo& prop )
            {
                const PropertyMetadata& meta = prop._metadata;
                string                  text;
                const auto              add = [&text]( const string_view item )
                {
                    if ( text.empty() == false )
                        text += ", ";
                    text += item;
                };
                if ( meta._bReadOnly == SW_TRUE )
                    add( "ReadOnly" );
                if ( meta._bTransient == SW_TRUE )
                    add( "Transient" );
                if ( meta._bReplicated == SW_TRUE )
                    add( meta._repNotify.empty() ? string( "Replicated" ) : string( "RepNotify=" ) + meta._repNotify.c_str() );
                if ( meta._bSaveGame == SW_TRUE )
                    add( "SaveGame" );
                if ( meta._bInterp == SW_TRUE )
                    add( "Interp" );
                if ( meta._validate.empty() == false )
                    add( string( "Validate=" ) + meta._validate.c_str() );
                if ( meta._bAssetPath == SW_TRUE )
                    add( meta._assetType.empty() ? string( "Asset" ) : "Asset=" + meta._assetType );
                if ( prop._bIsContainer == SW_TRUE )
                    add( toString( prop._containerKind ) );
#if !defined( SW_SHIPPING )
                if ( meta._editCondition.empty() == false )
                    add( "EditCondition=`" + meta._editCondition + "`" );
                const string* pUnits = meta.findCustomMeta( hashed_string( "Units" ) );
                if ( pUnits != nullptr )
                    add( "Units=" + *pUnits );
#endif
                return text;
            }

            static string makeRange( const PropertyMetadata& meta )
            {
                if ( meta._bHasMinRange == SW_FALSE && meta._bHasMaxRange == SW_FALSE )
                    return string();
                string text = meta._bHasMinRange == SW_TRUE ? to_string( meta._minRange ) : string( "-" );
                text += " ~ ";
                text += meta._bHasMaxRange == SW_TRUE ? to_string( meta._maxRange ) : string( "-" );
                return text;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string ReflectionDocWriter::makeModuleFileName( const hashed_string& moduleName )
    {
        return ( moduleName.empty() ? string( "Unknown" ) : string( moduleName.c_str() ) ) + ".md";
    }

    string ReflectionDocWriter::makeTypeMarkdown( const TypeInfo& type )
    {
        string out = "## " + string( type._fullyQualifiedName.c_str() ) + "\n\n";
        if ( type._parentFQN.empty() == false )
            out += "- 부모: `" + string( type._parentFQN.c_str() ) + "`\n";
        if ( type._bAbstract == SW_TRUE )
            out += "- Abstract\n";
        if ( type._addComponent != nullptr )
            out += "- 컴포넌트(이름으로 만들 수 있다)\n";
#if !defined( SW_SHIPPING )
        if ( type._metadata._category.empty() == false )
            out += "- 분류: " + type._metadata._category + "\n";
        if ( type._metadata._tooltip.empty() == false )
            out += "- 설명: " + ReflectionDocWriterInternal::escapeCell( type._metadata._tooltip ) + "\n";
#endif
        if ( type._pValidate != nullptr )
            out += "- 타입 검증 함수가 있다\n";

        if ( type._listProperty.empty() == false )
        {
            out += "\n| 프로퍼티 | 타입 | 기본값 | 범위 | 플래그 | 설명 |\n|---|---|---|---|---|---|\n";
            for ( const PropertyInfo& prop : type._listProperty )
            {
                string tooltip;
#if !defined( SW_SHIPPING )
                tooltip = prop._metadata._tooltip;
#endif
                out += "| `" + string( prop._name.c_str() ) + "` | `" + ReflectionDocWriterInternal::escapeCell( prop._typeName.view() ) + "` | " +
                       ReflectionDocWriterInternal::escapeCell( prop._metadata._defaultValue ) + " | " + ReflectionDocWriterInternal::makeRange( prop._metadata ) +
                       " | " + ReflectionDocWriterInternal::makePropertyFlags( prop ) + " | " + ReflectionDocWriterInternal::escapeCell( tooltip ) + " |\n";
            }
        }

        bool bFunctionHeader = false;
        for ( const FunctionInfo& function : type._listMethod )
        {
            if ( function._metadata._bConstructor == SW_TRUE )
                continue;
            if ( bFunctionHeader == false )
            {
                out += "\n**함수**\n\n";
                bFunctionHeader = true;
            }
            out += "- `" + ( function._returnTypeName.empty() ? string( "void" ) : function._returnTypeName ) + " " + function._name + "(";
            ReflectionDocWriterInternal::appendParameterList( out, function._listParameter );
            out += ")`";
            if ( function._metadata._netRole != FunctionNetRole::Local )
                out += string( " — " ) + toString( function._metadata._netRole );
            out += "\n";
        }

        if ( type._listEvent.empty() == false )
        {
            out += "\n**이벤트**\n\n";
            for ( const EventInfo& event : type._listEvent )
            {
                out += "- `" + string( event._name.c_str() ) + "(";
                ReflectionDocWriterInternal::appendParameterList( out, event._listParameter );
                out += ")`\n";
            }
        }
        out += "\n";
        return out;
    }

    string ReflectionDocWriter::makeEnumMarkdown( const EnumInfo& info )
    {
        string                             out = "## " + string( info._fullyQualifiedName.c_str() ) + ( info._bIsBitFlag == SW_TRUE ? " (Flags)" : "" ) + "\n\n| 이름 | 값 |\n|---|---|\n";
        vector<pair<int64, hashed_string>> listEntry;
        for ( const auto& [value, name] : info._mapValueToName )
        {
            listEntry.emplace_back( value, name );
        }
        std::sort( listEntry.begin(), listEntry.end(), []( const pair<int64, hashed_string>& lhs, const pair<int64, hashed_string>& rhs )
        { return lhs.first < rhs.first || ( lhs.first == rhs.first && lhs.second.lexicalLess( rhs.second ) ); } );
        for ( const auto& [value, name] : listEntry )
        {
            out += "| `" + string( name.c_str() ) + "` | " + to_string( value ) + " |\n";
        }
        out += "\n";
        return out;
    }

    uint32 ReflectionDocWriter::writeMarkdown( const TypeRegistry& registry, const string_view outputDir )
    {
        if ( FileUtil::ensureDirectoryExists( outputDir ) == false )
        {
            SW_LOG_ERROR( "Cannot create reflection docs folder '%#'", outputDir );
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
        std::sort( listType.begin(), listType.end(), []( const TypeInfo* pLhs, const TypeInfo* pRhs )
        { return pLhs->_fullyQualifiedName.lexicalLess( pRhs->_fullyQualifiedName ); } );
        std::sort( listEnum.begin(), listEnum.end(), []( const EnumInfo* pLhs, const EnumInfo* pRhs )
        { return pLhs->_fullyQualifiedName.lexicalLess( pRhs->_fullyQualifiedName ); } );

        vector<hashed_string> listModule;
        const auto            addModule = [&listModule]( const hashed_string& moduleName )
        {
            for ( const hashed_string& known : listModule )
            {
                if ( known == moduleName )
                    return;
            }
            listModule.push_back( moduleName );
        };
        for ( const TypeInfo* pType : listType )
        {
            addModule( pType->_moduleName );
        }
        for ( const EnumInfo* pEnum : listEnum )
        {
            addModule( pEnum->_moduleName );
        }
        std::sort( listModule.begin(), listModule.end(), HashedStringLexicalLess{} );

        string index     = "# 리플렉션 API\n\n`App --write-reflection-docs` 가 등록된 타입에서 만든 문서입니다(손으로 고치지 않습니다).\n\n| 모듈 | 타입 | 열거형 |\n|---|---|---|\n";
        uint32 fileCount = 0;
        for ( const hashed_string& moduleName : listModule )
        {
            const string fileName  = makeModuleFileName( moduleName );
            string       page      = "# " + string( moduleName.empty() ? "Unknown" : moduleName.c_str() ) + "\n\n[목록으로](index.md)\n\n";
            uint32       typeCount = 0;
            uint32       enumCount = 0;
            for ( const TypeInfo* pType : listType )
            {
                if ( pType->_moduleName != moduleName )
                    continue;
                page += makeTypeMarkdown( *pType );
                ++typeCount;
            }
            for ( const EnumInfo* pEnum : listEnum )
            {
                if ( pEnum->_moduleName != moduleName )
                    continue;
                page += makeEnumMarkdown( *pEnum );
                ++enumCount;
            }
            if ( FileUtil::writeTextFile( FileUtil::joinPath( outputDir, fileName ), page ) == false )
                continue;
            ++fileCount;
            index += "| [" + string( moduleName.empty() ? "Unknown" : moduleName.c_str() ) + "](" + fileName + ") | " + to_string( typeCount ) + " | " +
                     to_string( enumCount ) + " |\n";
        }
        index += "\n## 타입\n\n";
        for ( const TypeInfo* pType : listType )
        {
            index += "- [`" + string( pType->_fullyQualifiedName.c_str() ) + "`](" + makeModuleFileName( pType->_moduleName ) + "#" +
                     ReflectionDocWriterInternal::makeAnchor( pType->_fullyQualifiedName.view() ) + ")\n";
        }
        if ( FileUtil::writeTextFile( FileUtil::joinPath( outputDir, "index.md" ), index ) )
            ++fileCount;
        SW_LOG_INFO( "Wrote reflection docs: %# files, %# types, %# enums -> %#", fileCount, static_cast<uint32>( listType.size() ),
                     static_cast<uint32>( listEnum.size() ), outputDir );
        return fileCount;
    }
} // namespace sw
