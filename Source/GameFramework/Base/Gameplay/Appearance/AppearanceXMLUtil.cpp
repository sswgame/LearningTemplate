#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"

namespace sw
{
    SW_LOG_CALLER( "Appearance" );

    namespace
    {
        struct AppearanceXMLUtilInternal
        {
            /**
             * @brief 벡터 속성 하나를 @p pParse 로 읽어 @p inoutValue 에 담습니다. 선택 속성(@p bRequired = false)은 없으면 값을 그대로 둡니다.
             * @details 있는데 읽히지 않거나 필수인데 없으면 @p report 에 오류를 더하고 값을 그대로 둡니다.
             */
            template <typename TVector>
            static void readVector( const XMLNode& node, const utf8* pAttribute, bool bRequired, const utf8* pShape, TVector& inoutValue, bool ( *pParse )( string_view, TVector& ),
                                    AppearanceLoadReport& report, string_view sourceName )
            {
                const utf8* pText = node.findAttribute( pAttribute );
                if ( pText == nullptr )
                {
                    if ( bRequired )
                        report.addError( "%#: <%#> needs %#=\"%#\"", sourceName, node.getName(), pAttribute, pShape );
                    return;
                }
                TVector parsed{};
                if ( pParse( string_view( pText ), parsed ) == false )
                {
                    report.addError( "%#: <%#> %#=\"%#\" must be numbers shaped \"%#\"", sourceName, node.getName(), pAttribute, pText, pShape );
                    return;
                }
                inoutValue = parsed;
            }

            /** @brief 셰이더 파라미터 값 "x" · "x y" · "x y z" · "x y z w" 를 읽습니다. 파라미터가 스칼라인지 색인지는 읽는 때 알 수 없어 1~4 개를 받고, 모자란 성분은 0 입니다. */
            [[nodiscard]] static bool parseShaderValue( string_view text, float4& outValue )
            {
                float4  vec4{};
                float3  vec3{};
                float2  vec2{};
                float32 scalar = 0.0f;
                if ( GameDataXML::parseFloat4( text, vec4 ) )
                    outValue = vec4;
                else if ( GameDataXML::parseFloat3( text, vec3 ) )
                    outValue = float4( vec3._x, vec3._y, vec3._z, 0.0f );
                else if ( GameDataXML::parseFloat2( text, vec2 ) )
                    outValue = float4( vec2._x, vec2._y, 0.0f, 0.0f );
                else if ( StringUtil::parseFloat( text, scalar ) )
                    outValue = float4( scalar, 0.0f, 0.0f, 0.0f );
                else
                    return false;
                return true;
            }
        };
    } // namespace

    void AppearanceLoadReport::addMessage( string_view message )
    {
        _listError.push_back( string( message ) );
        SW_LOG_WARNING( "%#", message );
    }

    uint32 AppearanceLoadReport::countContaining( string_view text ) const
    {
        uint32 count = 0;
        for ( const string& error : _listError )
        {
            if ( string_view( error.c_str(), error.size() ).find( text ) != string_view::npos )
                ++count;
        }
        return count;
    }

    string AppearanceLoadReport::joined() const
    {
        string result;
        for ( const string& error : _listError )
        {
            result += "\n  ";
            result += error;
        }
        return result;
    }

    bool AppearanceXMLUtil::reportUnknownAttributes( const XMLNode& node, const utf8* const* ppKnownName, uint32 knownCount, AppearanceLoadReport& report, string_view sourceName )
    {
        vector<const utf8*> listUnknown;
        if ( XMLNameCheck::collectUnknownAttributes( node, ppKnownName, knownCount, listUnknown ) == 0 )
            return true;
        for ( const utf8* pName : listUnknown )
        {
            report.addError( "%#: <%#> has unknown attribute '%#'", sourceName, node.getName(), pName );
        }
        return false;
    }

    void AppearanceXMLUtil::reportUnknownChild( const XMLNode& parent, const XMLNode& child, AppearanceLoadReport& report, string_view sourceName )
    {
        report.addError( "%#: <%#> has unknown element <%#>", sourceName, parent.getName(), child.getName() );
    }

    hashed_string AppearanceXMLUtil::readOptionalName( const XMLNode& node, const utf8* pAttribute )
    {
        return XMLReadUtil::readOptionalName( node, pAttribute );
    }

    hashed_string AppearanceXMLUtil::readRequiredName( const XMLNode& node, const utf8* pAttribute, AppearanceLoadReport& report, string_view sourceName )
    {
        const hashed_string name = XMLReadUtil::readOptionalName( node, pAttribute );
        if ( name.empty() )
            report.addError( "%#: <%#> needs a non-empty name attribute '%#'", sourceName, node.getName() != nullptr ? node.getName() : "?", pAttribute );
        return name;
    }

    void AppearanceXMLUtil::readNameList( const XMLNode& node, const utf8* pAttribute, vector<hashed_string>& outListName )
    {
        GameDataXML::appendNameList( node.getAttributeText( pAttribute ), ",; ", outListName );
    }

    void AppearanceXMLUtil::readOptionalFloat3( const XMLNode& node, const utf8* pAttribute, float3& inoutValue, AppearanceLoadReport& report, string_view sourceName )
    {
        AppearanceXMLUtilInternal::readVector( node, pAttribute, false, "x y z", inoutValue, &GameDataXML::parseFloat3, report, sourceName );
    }

    void AppearanceXMLUtil::readOptionalFloat4( const XMLNode& node, const utf8* pAttribute, float4& inoutValue, AppearanceLoadReport& report, string_view sourceName )
    {
        AppearanceXMLUtilInternal::readVector( node, pAttribute, false, "r g b a", inoutValue, &GameDataXML::parseFloat4, report, sourceName );
    }

    void AppearanceXMLUtil::readOptionalFloat( const XMLNode& node, const utf8* pAttribute, float32 minValue, float32 maxValue, float32& inoutValue, AppearanceLoadReport& report,
                                               string_view sourceName )
    {
        if ( GameDataXML::readOptionalNumber( node, pAttribute, sourceName, minValue, maxValue, inoutValue ) == false )
            report.addError( "%#: <%#> has an invalid %#", sourceName, node.getName(), pAttribute );
    }

    void AppearanceXMLUtil::readOptionalInt( const XMLNode& node, const utf8* pAttribute, int32 minValue, int32 maxValue, int32& inoutValue, AppearanceLoadReport& report,
                                             string_view sourceName )
    {
        if ( GameDataXML::readOptionalNumber( node, pAttribute, sourceName, minValue, maxValue, inoutValue ) == false )
            report.addError( "%#: <%#> has an invalid %#", sourceName, node.getName(), pAttribute );
    }

    void AppearanceXMLUtil::readRequiredFloat4( const XMLNode& node, const utf8* pAttribute, float4& outValue, AppearanceLoadReport& report, string_view sourceName )
    {
        AppearanceXMLUtilInternal::readVector( node, pAttribute, true, "r g b a", outValue, &GameDataXML::parseFloat4, report, sourceName );
    }

    void AppearanceXMLUtil::readRequiredShaderValue( const XMLNode& node, const utf8* pAttribute, float4& outValue, AppearanceLoadReport& report, string_view sourceName )
    {
        AppearanceXMLUtilInternal::readVector( node, pAttribute, true, "x [y [z [w]]]", outValue, &AppearanceXMLUtilInternal::parseShaderValue, report, sourceName );
    }

    void AppearanceXMLUtil::readPlacement( const XMLNode& node, const utf8* pSocketAttribute, AppearancePlacement& outPlacement, AppearanceLoadReport& report, string_view sourceName )
    {
        outPlacement._listSocket.clear();
        readNameList( node, pSocketAttribute, outPlacement._listSocket );
        outPlacement._offset   = float3::Zero;
        outPlacement._rotation = float3::Zero;
        readOptionalFloat3( node, "offset", outPlacement._offset, report, sourceName );
        readOptionalFloat3( node, "rotation", outPlacement._rotation, report, sourceName );
    }

    void AppearanceXMLUtil::readTags( const XMLNode& node, const utf8* pAttribute, TagContainer& outTags )
    {
        StringUtil::forEachToken( node.getAttributeText( pAttribute ), ",; ", [&]( string_view token )
        {
            outTags.addTag( TagID::request( token ) );
        } );
    }

    bool AppearanceXMLUtil::containsName( const vector<hashed_string>& listName, const hashed_string& name )
    {
        return VectorUtil::contains( listName, name );
    }
} // namespace sw
