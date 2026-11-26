#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/Foundation.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "Programs/ReflectionParser/Config/ParserConfig.h"
#include "Programs/ReflectionParser/Emit/CodeGenerator.h"
#include "Programs/ReflectionParser/Emit/EmitTemplateStore.h"
#include "Programs/ReflectionParser/Parse/AnnotationFields.h"
#include "Programs/ReflectionParser/TypeMap/ContainerTypeMap.h"
#include "Programs/ReflectionParser/TypeMap/TypeNameMap.h"
#include "Programs/ReflectionParser/TypeMap/TypeSpelling.h"

namespace sw
{
    namespace
    {
        struct CodeGeneratorEditorMetadataInternal
        {
            /** @brief 에디터 메타데이터 한 묶음을 개발 전용 단위의 줄(`EditorMetadataRow`)들로 더합니다. @p nameExpr 은 대상의 이름 글(타입 · 열거형 자신이면 `nullptr`)입니다. */
            static void appendEditorRows( vector<string>& outListRow, const utf8* pTarget, const string& nameExpr, const AnnotationOutput& output )
            {
                const auto append = [&outListRow]( const string_view row )
                { outListRow.push_back( string( row ) ); };
                for ( const auto& [field, text] : output._listText )
                {
                    StringBuilder<constant::buffer::kMax512> row;
                    row.appendFormat( "{ Target::%#, Field::%#, %#, %# },", pTarget, field, nameExpr, CodeEmit::quoted( text ) );
                    append( row.view() );
                }
                for ( const string& flag : output._listFlag )
                {
                    StringBuilder<constant::buffer::kMax256> row;
                    row.appendFormat( "{ Target::%#, Field::%#, %# },", pTarget, flag, nameExpr );
                    append( row.view() );
                }
                for ( const auto& [key, value] : output._listCustomMeta )
                {
                    StringBuilder<constant::buffer::kMax512> row;
                    row.appendFormat( "{ Target::%#, Field::CustomMeta, %#, %#, %# },", pTarget, nameExpr, CodeEmit::quoted( value ), CodeEmit::quoted( key ) );
                    append( row.view() );
                }
            }

            /** @brief 에디터 메타데이터 줄 표와 정적 등록(@p pKind 는 `Type` · `Enum`)을 출력합니다. 줄이 없으면 아무것도 쓰지 않고 false 입니다. */
            static bool emitEditorRowTable( CodeEmit& emit, const string& fqn, const utf8* pKind, const vector<string>& listRow )
            {
                if ( listRow.empty() )
                    return false;
                const string id = CodeGenerator::sanitizeIdentifier( fqn );
                emit.linef( "// ── %#", fqn );
                emit.linef( "static constexpr ::sw::EditorMetadataRow kRows_%#[] =", id );
                emit.line( "{" );
                emit.push();
                for ( const string& row : listRow )
                {
                    emit.line( row );
                }
                emit.pop();
                emit.line( "};" );
                emit.linef( "static const ::sw::%#EditorMetadataSource    s_%#_editorMetadata{ %#, kRows_%#, %#u };", pKind, id, CodeEmit::quoted( fqn ), id,
                            static_cast<uint32>( listRow.size() ) );
                emit.linef( "static const ::sw::%#EditorMetadataRegistrar s_%#_editorMetadataRegistrar{ &s_%#_editorMetadata };", pKind, id, id );
                emit.blank();
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string CodeGenerator::makeEditorMetadataText() const
    {
        CodeEmitBuffer buffer;
        emitEditorMetadata( buffer );
        return string( buffer.view() );
    }

    void CodeGenerator::emitEditorMetadata( CodeEmitBuffer& buffer ) const
    {
        appendTemplate( buffer, EmitTemplate::EditorMetadataFileHeader, {
                                                                            {templatekey::kSourcePath,                   _sourceFilePath},
                                                                            {    templatekey::kBanner, sw::config::kTextReflectionBanner}
        } );
        CodeEmit emit( buffer );
        bool     bAny = false;
        for ( const ParsedTypeInfo& typeInfo : _header._listType )
        {
            vector<string> listRow;
            collectTypeEditorRows( typeInfo, listRow );
            bAny = CodeGeneratorEditorMetadataInternal::emitEditorRowTable( emit, typeInfo._fullyQualifiedName, "Type", listRow ) || bAny;
        }
        for ( const ParsedEnumInfo& enumInfo : _header._listEnum )
        {
            vector<string>   listRow;
            AnnotationOutput enumOutput = AnnotationOutput::collectEditor( enumInfo );
            CodeGeneratorEditorMetadataInternal::appendEditorRows( listRow, "Enum", "nullptr", enumOutput );
            // 열거자마다의 표시 메타(`ENUMERATOR( … )`)는 열거자 이름으로 단다 — 적은 열거자만.
            for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
            {
                AnnotationOutput output = AnnotationOutput::collectEditor( enumerator );
                CodeGeneratorEditorMetadataInternal::appendEditorRows( listRow, "Enumerator", CodeEmit::quoted( enumerator._name ), output );
            }
            bAny = CodeGeneratorEditorMetadataInternal::emitEditorRowTable( emit, enumInfo._fullyQualifiedName, "Enum", listRow ) || bAny;
        }
        if ( bAny == false )
            emit.linef( "// No editor metadata in %#", _sourceFilePath );
        buffer.append( "} // namespace sw::generated::editormetadata\n" );
    }

    void CodeGenerator::collectTypeEditorRows( const ParsedTypeInfo& typeInfo, vector<string>& outListRow ) const
    {
        AnnotationOutput typeOutput = AnnotationOutput::collectEditor( typeInfo );
        CodeGeneratorEditorMetadataInternal::appendEditorRows( outListRow, "Type", "nullptr", typeOutput );

        // 이름은 등록 단위와 같다 — 프로퍼티 · 이벤트는 이름, 메서드는 찾는 이름(`FunctionInfo::_hashName`, 생성자는 `makeCtorLookupName`).
        const auto appendProperty = [&outListRow]( const ParsedPropertyInfo& prop, const utf8* pTarget )
        {
            AnnotationOutput output = AnnotationOutput::collectEditor( prop );
            const string     name   = CodeEmit::quoted( prop._name );
            CodeGeneratorEditorMetadataInternal::appendEditorRows( outListRow, pTarget, name, output );
            const auto appendNumber = [&outListRow, &name, pTarget]( const utf8* pField, const float32 value )
            {
                StringBuilder<constant::buffer::kMax256> row;
                row.appendFormat( "{ Target::%#, Field::%#, %#, nullptr, nullptr, %# },", pTarget, pField, name, CodeEmit::floatLiteral( value ) );
                outListRow.push_back( string( row.view() ) );
            };
            if ( prop._bHasUIMinRange != SW_FALSE )
                appendNumber( "UIMinRange", prop._uiMinRange );
            if ( prop._bHasUIMaxRange != SW_FALSE )
                appendNumber( "UIMaxRange", prop._uiMaxRange );
        };
        for ( const ParsedPropertyInfo& prop : typeInfo._listProperty )
        {
            appendProperty( prop, "Property" );
        }
        for ( const ParsedEventInfo& event : typeInfo._listEvent )
        {
            appendProperty( event._annotation, "Event" );
        }
        for ( const ParsedFunctionInfo& method : typeInfo._listMethod )
        {
            AnnotationOutput output     = AnnotationOutput::collectEditor( method );
            const string     lookupName = ( method._bConstructor != SW_FALSE ) ? makeCtorLookupName( method, _session ) : method._name;
            CodeGeneratorEditorMetadataInternal::appendEditorRows( outListRow, "Function", CodeEmit::quoted( lookupName ), output );
        }
    }
} // namespace sw
