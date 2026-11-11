#include "pch.h"

#include "Engine/Telemetry/TelemetrySchema.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    namespace
    {
        struct TelemetrySchemaInternal
        {
            static constexpr const utf8* kRootName = "TelemetrySchema";

            static constexpr const utf8* kArrRootChild[]  = { "Pipeline", "Event" };
            static constexpr const utf8* kArrEventChild[] = { "Field" };
            /** @brief 필드 이름으로 쓸 수 없는 이름 — 사건 줄의 고정 키와 겹친다. */
            static constexpr const utf8* kArrReservedFieldName[] = { "type", "event", "seq", "t", "sample", "session" };

            static bool isReservedFieldName( string_view name )
            {
                for ( const utf8* pReserved : kArrReservedFieldName )
                {
                    if ( StringUtil::equals( name, pReserved, true ) )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TelemetryFieldDef::TelemetryFieldDef()
        : _name{}
        , _type{ TelemetryFieldType::String }
        , _bRequired{ SW_FALSE }
    {
    }

    const utf8* toString( TelemetryFieldType type )
    {
        return getEnumName( type );
    }

    const TelemetryFieldDef* TelemetryEventDef::findField( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&TelemetryFieldDef::_name>( _listField, name );
    }

    TelemetrySchema::TelemetrySchema()
        : _listEvent{}
        , _settings{}
        , _version{ 0 }
    {
    }

    bool TelemetrySchema::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        XMLDocument       doc;
        string            sourceName( path );
        if ( doc.loadResource( assetID, &sourceName ) == false )
        {
            SW_LOG_WARNING_ID( "TelemetrySchema.FailedReadTelemetrySchema", "Failed to read telemetry schema %#", path );
            return false;
        }
        return loadRoot( doc.getRoot( TelemetrySchemaInternal::kRootName ), sourceName );
    }

    bool TelemetrySchema::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_WARNING_ID( "TelemetrySchema.FailedParseTelemetrySchema", "Failed to parse telemetry schema %#", sourceName );
            return false;
        }
        return loadRoot( doc.getRoot( TelemetrySchemaInternal::kRootName ), sourceName );
    }

    void TelemetrySchema::clear()
    {
        _listEvent.clear();
        _settings = TelemetryPipelineConfig{};
        _version  = 0;
    }

    const TelemetryEventDef* TelemetrySchema::findEvent( const hashed_string& eventID ) const
    {
        return VectorUtil::findBy<&TelemetryEventDef::_id>( _listEvent, eventID );
    }

    bool TelemetrySchema::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Internal = TelemetrySchemaInternal;
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING_ID( "TelemetrySchema.MissingRoot", "%#: missing <%#> root", sourceName, Internal::kRootName );
            return false;
        }
        // 파일 하나는 통째로 받거나 통째로 버린다 — 반쯤 읽힌 스키마는 어느 사건이 빠졌는지 보이지 않는다.
        TelemetrySchemaRootParams rootParams;
        bool                      bValid    = XMLFields::readProperties( root, rootParams, sourceName );
        vector<TelemetryEventDef> listEvent = _listEvent;
        TelemetryPipelineConfig   settings  = _settings;
        bValid                              = XMLNameCheck::reportUnknownChildren( root, Internal::kArrRootChild, sourceName ) && bValid;
        // 적지 않은 칸은 정의 구조체의 초기값이다. 깨진 값 · 범위 밖 값은 자르지 않고 오류이며, 파일 전체를 받지 않는다(칸 · 범위는 PROPERTY 가 정본이다).
        for ( XMLNode node = root.findChild( "Pipeline" ); node; node = node.findNextSibling( "Pipeline" ) )
        {
            bValid = XMLFields::readProperties( node, settings, sourceName ) && XMLNameCheck::reportUnknownChildren( node, nullptr, 0, sourceName ) && bValid;
        }
        for ( XMLNode node = root.findChild( "Event" ); node; node = node.findNextSibling( "Event" ) )
        {
            TelemetryEventDef event;
            bValid = XMLFields::readProperties( node, event, sourceName ) && bValid;
            bValid = XMLNameCheck::reportUnknownChildren( node, Internal::kArrEventChild, sourceName ) && bValid;
            if ( event._id.empty() )
            {
                bValid = false;
                continue;
            }
            for ( XMLNode fieldNode = node.findChild( "Field" ); fieldNode; fieldNode = fieldNode.findNextSibling( "Field" ) )
            {
                TelemetryFieldDef field;
                bValid              = XMLFields::readProperties( fieldNode, field, sourceName ) && bValid;
                const bool bBadName = field._name.empty() || Internal::isReservedFieldName( field._name.view() ) || event.findField( field._name ) != nullptr;
                if ( bBadName )
                {
                    SW_LOG_WARNING_ID( "Telemetry.FieldNameInvalid", "%#: event '%#' has an empty, reserved or repeated field name '%#'", sourceName, event._id.c_str(), field._name.c_str() );
                    bValid = false;
                    continue;
                }
                event._listField.push_back( field );
            }
            bool bDuplicate = false;
            for ( const TelemetryEventDef& other : listEvent )
            {
                bDuplicate = bDuplicate || other._id == event._id;
            }
            if ( bDuplicate )
            {
                SW_LOG_WARNING_ID( "Telemetry.EventDeclaredTwice", "%#: event '%#' is declared twice", sourceName, event._id.c_str() );
                bValid = false;
                continue;
            }
            listEvent.push_back( event );
        }
        const int32 schemaVersion = rootParams._version;
        if ( bValid == false )
            return false;
        _listEvent = std::move( listEvent );
        _settings  = settings;
        _version   = MathUtil::max( _version, schemaVersion );
        return true;
    }

    /** @brief 텔레메트리 스키마 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( TelemetrySchema, AssetTypeDef::makeDataFile( "telemetryschema", AssetPathRule{ {}, {}, ".telemetry.xml" }, AssetTypeLoader::of<TelemetrySchema>() ) );
} // namespace sw
