#include "pch.h"

#include "Engine/Telemetry/TelemetrySchema.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"
#include "Engine/Utility/Xml/XmlNameCheck.h"

namespace sw
{
    SW_LOG_CALLER( "TelemetrySchema" );

    namespace
    {
        struct TelemetrySchemaInternal
        {
            static constexpr const utf8* kRootName               = "TelemetrySchema";
            static constexpr const utf8* kArrRootAttribute[]     = { "version" };
            static constexpr const utf8* kArrRootChild[]         = { "Pipeline", "Event" };
            static constexpr const utf8* kArrPipelineAttribute[] = { "batchEvents", "flushSeconds", "maxFileBytes", "maxFiles", "maxTotalBytes", "sessionSample", "breadcrumbs" };
            static constexpr const utf8* kArrEventAttribute[]    = { "id", "category", "sample" };
            static constexpr const utf8* kArrEventChild[]        = { "Field" };
            static constexpr const utf8* kArrFieldAttribute[]    = { "name", "type", "required" };
            static constexpr const utf8* kArrFieldTypeName[]     = { "bool", "int", "float", "string" };
            /** @brief 필드 이름으로 쓸 수 없는 이름 — 사건 줄의 고정 키와 겹친다. */
            static constexpr const utf8* kArrReservedFieldName[] = { "type", "event", "seq", "t", "sample", "session" };

            template <size_t Count>
            static bool reportUnknownNames( const XmlNode& node, const utf8* const ( &arrAttribute )[Count], string_view sourceName )
            {
                return XmlNameCheck::reportUnknownAttributes( node, arrAttribute, sourceName, LogLevel::Warning );
            }

            [[nodiscard]] static bool parseFieldType( string_view text, TelemetryFieldType& outType )
            {
                for ( uint32 index = 0; index < 4; ++index )
                {
                    if ( StringUtil::equals( text, kArrFieldTypeName[index], true ) )
                    {
                        outType = static_cast<TelemetryFieldType>( index );
                        return true;
                    }
                }
                return false;
            }

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
    const utf8* toString( TelemetryFieldType type )
    {
        return TelemetrySchemaInternal::kArrFieldTypeName[static_cast<uint32>( type )];
    }

    const TelemetryFieldDef* TelemetryEventDef::findField( const hashed_string& name ) const
    {
        for ( const TelemetryFieldDef& field : _listField )
        {
            if ( field._name == name )
                return &field;
        }
        return nullptr;
    }

    TelemetrySchema::TelemetrySchema()
        : _listEvent{}
        , _settings{}
        , _version{ 0 }
    {
    }

    bool TelemetrySchema::loadFromResource( string_view path )
    {
        XmlDocument doc;
        string      sourceName( path );
        if ( doc.loadPath( path, &sourceName ) == false )
        {
            SW_LOG_WARNING( "Failed to read telemetry schema %#", path );
            return false;
        }
        return loadRoot( doc.getRoot( TelemetrySchemaInternal::kRootName ), sourceName );
    }

    bool TelemetrySchema::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_WARNING( "Failed to parse telemetry schema %#", sourceName );
            return false;
        }
        return loadRoot( doc.getRoot( TelemetrySchemaInternal::kRootName ), sourceName );
    }

    void TelemetrySchema::clear()
    {
        _listEvent.clear();
        _settings = TelemetryPipelineSettings{};
        _version  = 0;
    }

    const TelemetryEventDef* TelemetrySchema::findEvent( const hashed_string& eventId ) const
    {
        for ( const TelemetryEventDef& event : _listEvent )
        {
            if ( event._id == eventId )
                return &event;
        }
        return nullptr;
    }

    bool TelemetrySchema::loadRoot( const XmlNode& root, string_view sourceName )
    {
        using Internal = TelemetrySchemaInternal;
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "%#: missing <%#> root", sourceName, Internal::kRootName );
            return false;
        }
        // 파일 하나는 통째로 받거나 통째로 버린다 — 반쯤 읽힌 스키마는 어느 사건이 빠졌는지 보이지 않는다.
        bool                      bValid    = Internal::reportUnknownNames( root, Internal::kArrRootAttribute, sourceName );
        vector<TelemetryEventDef> listEvent = _listEvent;
        TelemetryPipelineSettings settings  = _settings;
        bValid                              = XmlNameCheck::reportUnknownChildren( root, Internal::kArrRootChild, sourceName, LogLevel::Warning ) && bValid;
        for ( XmlNode node = root.findChild( "Pipeline" ); node; node = node.findNextSibling( "Pipeline" ) )
        {
            bValid                      = Internal::reportUnknownNames( node, Internal::kArrPipelineAttribute, sourceName ) && bValid;
            settings._batchEvents       = static_cast<uint32>( MathUtil::max( 1, node.getAttributeInt( "batchEvents", static_cast<int32>( settings._batchEvents ) ) ) );
            settings._flushSeconds      = MathUtil::max( 0.0f, node.getAttributeFloat( "flushSeconds", settings._flushSeconds ) );
            settings._maxFileBytes      = static_cast<uint32>( MathUtil::max( 1024, node.getAttributeInt( "maxFileBytes", static_cast<int32>( settings._maxFileBytes ) ) ) );
            settings._maxFiles          = static_cast<uint32>( MathUtil::max( 1, node.getAttributeInt( "maxFiles", static_cast<int32>( settings._maxFiles ) ) ) );
            settings._maxTotalBytes     = static_cast<uint64>( MathUtil::max( 1024.0f, node.getAttributeFloat( "maxTotalBytes", static_cast<float32>( settings._maxTotalBytes ) ) ) );
            settings._sessionSampleRate = MathUtil::clamp( node.getAttributeFloat( "sessionSample", settings._sessionSampleRate ), 0.0f, 1.0f );
            settings._breadcrumbCount   = static_cast<uint32>( MathUtil::clamp( node.getAttributeInt( "breadcrumbs", static_cast<int32>( settings._breadcrumbCount ) ), 0, 256 ) );
        }
        for ( XmlNode node = root.findChild( "Event" ); node; node = node.findNextSibling( "Event" ) )
        {
            bValid = Internal::reportUnknownNames( node, Internal::kArrEventAttribute, sourceName ) && bValid;
            bValid = XmlNameCheck::reportUnknownChildren( node, Internal::kArrEventChild, sourceName, LogLevel::Warning ) && bValid;
            TelemetryEventDef event;
            event._id                = hashed_string( node.getAttributeText( "id" ) );
            event._category          = hashed_string( node.getAttributeText( "category" ) );
            const float32 sampleRate = node.getAttributeFloat( "sample", event._sampleRate );
            if ( sampleRate < 0.0f || sampleRate > 1.0f )
            {
                SW_LOG_WARNING( "%#: event '%#' has a sample rate %# outside 0..1", sourceName, event._id.c_str(), sampleRate );
                bValid = false;
            }
            event._sampleRate = MathUtil::clamp( sampleRate, 0.0f, 1.0f );
            if ( event._id.empty() )
            {
                SW_LOG_WARNING( "%#: <Event> without an id", sourceName );
                bValid = false;
                continue;
            }
            for ( XmlNode fieldNode = node.findChild( "Field" ); fieldNode; fieldNode = fieldNode.findNextSibling( "Field" ) )
            {
                bValid = Internal::reportUnknownNames( fieldNode, Internal::kArrFieldAttribute, sourceName ) && bValid;
                TelemetryFieldDef field;
                const string_view name = fieldNode.getAttributeText( "name" );
                field._name            = hashed_string( name );
                field._bRequired       = fieldNode.getAttributeBool( "required", false ) ? SW_TRUE : SW_FALSE;
                if ( Internal::parseFieldType( fieldNode.getAttributeText( "type" ), field._type ) == false )
                {
                    SW_LOG_WARNING( "%#: field '%#' of '%#' has an unknown type '%#' (bool, int, float, string)", sourceName, name, event._id.c_str(),
                                    fieldNode.getAttributeText( "type" ) );
                    bValid = false;
                }
                const bool bBadName = name.empty() || Internal::isReservedFieldName( name ) || event.findField( field._name ) != nullptr;
                if ( bBadName )
                {
                    SW_LOG_WARNING( "%#: event '%#' has an empty, reserved or repeated field name '%#'", sourceName, event._id.c_str(), name );
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
                SW_LOG_WARNING( "%#: event '%#' is declared twice", sourceName, event._id.c_str() );
                bValid = false;
                continue;
            }
            listEvent.push_back( event );
        }
        if ( bValid == false )
            return false;
        _listEvent = std::move( listEvent );
        _settings  = settings;
        _version   = MathUtil::max( _version, root.getAttributeInt( "version", 1 ) );
        return true;
    }
} // namespace sw
