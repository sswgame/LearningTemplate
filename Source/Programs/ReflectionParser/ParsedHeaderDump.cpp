#include "pch.h"

#include "Programs/ReflectionParser/ParsedHeaderDump.h"

#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/FixedCodecGenerator.h"
#include "Programs/ReflectionParser/ParsedReflection.h"

#include <cstdio>

namespace sw
{
    namespace
    {
        struct ParsedHeaderDumpInternal
        {
            /** @brief `타입 이름 = 기본값` 꼴로 인자 목록을 씁니다. */
            static void appendParameterList( StringBuilder<constant::buffer::kMax8192>& out, const vector<ParsedParameterInfo>& listParameter )
            {
                for ( size_t paramIndex = 0; paramIndex < listParameter.size(); ++paramIndex )
                {
                    const ParsedParameterInfo& parameter = listParameter[paramIndex];
                    out.appendFormat( "%#%#", paramIndex == 0 ? "" : ", ", parameter._typeName );
                    if ( parameter._name.empty() == false )
                        out.appendFormat( " %#", parameter._name );
                    if ( parameter._defaultValue.empty() == false )
                        out.appendFormat( " = %#", parameter._defaultValue );
                }
            }
        };
    } // namespace

    void printParsedHeaderDump( const string& inputFile, const ParsedHeader& parsed, const FixedCodecGenerator& fixedCodec )
    {
        StringBuilder<constant::buffer::kMax8192> out;
        out.appendFormat( "== %#\n", inputFile );
        for ( size_t typeIndex = 0; typeIndex < parsed._listType.size(); ++typeIndex )
        {
            const ParsedTypeInfo& type = parsed._listType[typeIndex];
            out.appendFormat( "REFLECT %#", type._fullyQualifiedName );
            if ( type._parentFQN.empty() == false )
                out.appendFormat( " : %#", type._parentFQN );
            if ( type._bComponentFactory == SW_TRUE )
                out.append( "  [component factory]" );
            if ( type._bAbstract == SW_TRUE )
                out.append( "  [abstract]" );
            if ( type._bStatic == SW_TRUE )
                out.append( "  [static]" );
            if ( type._validate.empty() == false )
                out.appendFormat( "  Validate=%#", type._validate );
            out.append( "\n" );
            for ( const ParsedPropertyInfo& prop : type._listProperty )
            {
                out.appendFormat( "  PROPERTY %# : %#", prop._name, prop._typeName );
                if ( prop._bIsBitField == SW_TRUE )
                    out.append( "  [bit field - located at runtime]" );
                else if ( prop._bIsAccessor == SW_TRUE )
                    out.appendFormat( "  [accessor %#()]", prop._memberName );
                if ( prop._bIsContainer == SW_TRUE )
                {
                    out.appendFormat( "  [container %#", prop._containerType );
                    if ( prop._keyTypeName.empty() == false )
                        out.appendFormat( " key=%#", prop._keyTypeName );
                    if ( prop._elementTypeName.empty() == false )
                        out.appendFormat( " element=%#", prop._elementTypeName );
                    out.append( "]" );
                }
                if ( prop._bHasMinRange == SW_TRUE )
                    out.appendFormat( "  Min=%#", prop._minRange );
                if ( prop._bHasMaxRange == SW_TRUE )
                    out.appendFormat( "  Max=%#", prop._maxRange );
                if ( prop._bHasMaxCount == SW_TRUE )
                    out.appendFormat( "  MaxCount=%#", prop._maxCount );
                if ( prop._defaultValue.empty() == false )
                    out.appendFormat( "  Default=\"%#\"", prop._defaultValue );
                if ( prop._bAssetPath == SW_TRUE )
                    out.appendFormat( "  [asset %#]", prop._assetType.empty() ? "any" : prop._assetType.c_str() );
                const pair<uint8, const utf8*> arrFlag[] = {
                    {       prop._bReadOnly,        "ReadOnly"},
                    {      prop._bTransient,       "Transient"},
                    {   prop._bXMLAttribute,    "XMLAttribute"},
                    {    prop._bSkipIfEmpty,     "SkipIfEmpty"},
                    {       prop._bRequired,        "Required"},
                    {     prop._bInheritKey,      "InheritKey"},
                    {prop._bHideInInspector, "HideInInspector"},
                    {     prop._bReplicated,      "Replicated"},
                    {       prop._bSaveGame,        "SaveGame"},
                    {   prop._bRuntimeState,    "RuntimeState"},
                    {         prop._bInterp,          "Interp"},
                    {       prop._bColorHDR,        "ColorHDR"},
                    {      prop._bMultiline,       "Multiline"},
                };
                for ( const auto& [bSet, pFlagName] : arrFlag )
                {
                    if ( bSet == SW_TRUE )
                        out.appendFormat( "  [%#]", pFlagName );
                }
                if ( prop._bHasUIMinRange == SW_TRUE )
                    out.appendFormat( "  UIMin=%#", prop._uiMinRange );
                if ( prop._bHasUIMaxRange == SW_TRUE )
                    out.appendFormat( "  UIMax=%#", prop._uiMaxRange );
                if ( prop._units.empty() == false )
                    out.appendFormat( "  Units=%#", prop._units );
                if ( prop._editCondition.empty() == false )
                    out.appendFormat( "  EditCondition=\"%#\"%#", prop._editCondition, prop._bEditConditionHides == SW_TRUE ? "(hides)" : "" );
                if ( prop._fileFilter.empty() == false )
                    out.appendFormat( "  FileFilter=\"%#\"", prop._fileFilter );
                if ( prop._validate.empty() == false )
                    out.appendFormat( "  Validate=%#", prop._validate );
                if ( prop._repNotify.empty() == false )
                    out.appendFormat( "  RepNotify=%#%#", prop._repNotify, prop._bRepNotifyTakesOldValue == SW_TRUE ? "(old)" : "()" );
                for ( const string& alias : prop._listAlias )
                {
                    out.appendFormat( "  alias=%#", alias );
                }
                out.append( "\n" );
            }
            for ( const ParsedFunctionInfo& method : type._listMethod )
            {
                out.appendFormat( "  FUNCTION %#(", method._name );
                ParsedHeaderDumpInternal::appendParameterList( out, method._listParameter );
                out.appendFormat( ") -> %#", method._returnTypeName.empty() ? "void" : method._returnTypeName.c_str() );
                if ( method._bStatic == SW_TRUE )
                    out.append( "  [static]" );
                if ( method._bCallInEditor == SW_TRUE )
                    out.append( "  [CallInEditor]" );
                if ( method._bExec == SW_TRUE )
                    out.append( "  [Exec]" );
                if ( method._editorPreview.empty() == false )
                    out.appendFormat( "  [EditorPreview=%#]", method._editorPreview );
                if ( method._bConstructor == SW_TRUE )
                    out.append( "  [constructor]" );
                out.append( "\n" );
            }
            for ( const ParsedEventInfo& event : type._listEvent )
            {
                out.appendFormat( "  EVENT %#(", event._annotation._name );
                ParsedHeaderDumpInternal::appendParameterList( out, event._listParameter );
                out.append( ")\n" );
            }
            out.appendFormat( "%#\n", fixedCodec.makeDumpLine( typeIndex ) );
        }
        for ( const ParsedEnumInfo& enumInfo : parsed._listEnum )
        {
            out.appendFormat( "ENUM %# : %#%#\n", enumInfo._fullyQualifiedName, enumInfo._underlyingType,
                              enumInfo._bIsBitFlag == SW_TRUE ? "  [Flags]" : "" );
            for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
            {
                out.appendFormat( "  %# = %#\n", enumerator._name, enumerator._value );
            }
        }
        std::fwrite( out.c_str(), 1, out.size(), stdout );
        std::fflush( stdout );
    }
} // namespace sw
