#include "pch.h"

#include "Engine/Telemetry/TelemetrySchema.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    SW_LOG_CALLER( "TelemetrySchema" );

    namespace
    {
        struct TelemetrySchemaInternal
        {
            static constexpr const utf8* kRootName           = "TelemetrySchema";
            static constexpr int32       kUnversionedSchema  = 1;    ///< `version` 을 적지 않은 스키마의 판
            static constexpr uint32      kMinFileBytes       = 1024; ///< 스풀 파일 하나의 하한 — 사건 한 줄보다 작으면 회전만 한다
            static constexpr uint32      kMaxBreadcrumbCount = 256;  ///< 크래시 보고에 붙이는 최근 사건 수의 상한

            /** @brief `maxTotalBytes`(64 비트)를 읽습니다. 없으면 그대로, 깨졌거나 `kMinFileBytes` 아래면 오류와 false 입니다. */
            [[nodiscard]] static bool readTotalBytes( const XMLNode& node, string_view sourceName, uint64& inoutBytes )
            {
                std::optional<string> text;
                if ( node.readOptionalAttribute( "maxTotalBytes", text ) == false )
                    return false;
                if ( text.has_value() == false )
                    return true;
                uint64     bytes  = 0;
                const bool bValid = StringUtil::parseUint64( *text, bytes ) && bytes >= kMinFileBytes;
                if ( bValid == false )
                {
                    SW_LOG_ERROR( "%#: <Pipeline maxTotalBytes=\"%#\"> must be a byte count of at least %#", sourceName, *text, kMinFileBytes );
                    return false;
                }
                inoutBytes = bytes;
                return true;
            }
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
            static bool reportUnknownNames( const XMLNode& node, const utf8* const ( &arrAttribute )[Count], string_view sourceName )
            {
                return XMLNameCheck::reportUnknownAttributes( node, arrAttribute, sourceName, LogLevel::Warning );
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
        XMLDocument doc;
        string      sourceName( path );
        if ( doc.loadResource( path, &sourceName ) == false )
        {
            SW_LOG_WARNING( "Failed to read telemetry schema %#", path );
            return false;
        }
        return loadRoot( doc.getRoot( TelemetrySchemaInternal::kRootName ), sourceName );
    }

    bool TelemetrySchema::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
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
        _settings = TelemetryPipelineConfig{};
        _version  = 0;
    }

    const TelemetryEventDef* TelemetrySchema::findEvent( const hashed_string& eventID ) const
    {
        for ( const TelemetryEventDef& event : _listEvent )
        {
            if ( event._id == eventID )
                return &event;
        }
        return nullptr;
    }

    bool TelemetrySchema::loadRoot( const XMLNode& root, string_view sourceName )
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
        TelemetryPipelineConfig   settings  = _settings;
        bValid                              = XMLNameCheck::reportUnknownChildren( root, Internal::kArrRootChild, sourceName, LogLevel::Warning ) && bValid;
        for ( XMLNode node = root.findChild( "Pipeline" ); node; node = node.findNextSibling( "Pipeline" ) )
        {
            bValid = Internal::reportUnknownNames( node, Internal::kArrPipelineAttribute, sourceName ) && bValid;
            // 적지 않은 칸은 TelemetryPipelineConfig 의 초기값이다. 깨진 값 · 범위 밖 값은 자르지 않고 오류이며, 파일 전체를 받지 않는다.
            constexpr uint32 kAnyCount = invalid_index::kUint32;
            bValid                     = node.readOptionalAttributeInRange( "batchEvents", 1u, kAnyCount, settings._batchEvents ) && bValid;
            bValid                     = node.readOptionalAttributeInRange( "flushSeconds", 0.0f, MathUtil::kMaxFloat, settings._flushSeconds ) && bValid;
            bValid                     = node.readOptionalAttributeInRange( "maxFileBytes", Internal::kMinFileBytes, kAnyCount, settings._maxFileBytes ) && bValid;
            bValid                     = node.readOptionalAttributeInRange( "maxFiles", 1u, kAnyCount, settings._maxFiles ) && bValid;
            bValid                     = node.readOptionalAttributeInRange( "sessionSample", 0.0f, 1.0f, settings._sessionSampleRate ) && bValid;
            bValid                     = node.readOptionalAttributeInRange( "breadcrumbs", 0u, Internal::kMaxBreadcrumbCount, settings._breadcrumbCount ) && bValid;
            bValid                     = Internal::readTotalBytes( node, sourceName, settings._maxTotalBytes ) && bValid;
        }
        for ( XMLNode node = root.findChild( "Event" ); node; node = node.findNextSibling( "Event" ) )
        {
            bValid = Internal::reportUnknownNames( node, Internal::kArrEventAttribute, sourceName ) && bValid;
            bValid = XMLNameCheck::reportUnknownChildren( node, Internal::kArrEventChild, sourceName, LogLevel::Warning ) && bValid;
            TelemetryEventDef event;
            event._id       = hashed_string( node.getAttributeText( "id" ) );
            event._category = hashed_string( node.getAttributeText( "category" ) );
            bValid          = node.readOptionalAttributeInRange( "sample", 0.0f, 1.0f, event._sampleRate ) && bValid;
            if ( event._id.empty() )
            {
                SW_LOG_WARNING( "%#: <Event> without an id", sourceName );
                bValid = false;
                continue;
            }
            for ( XMLNode fieldNode = node.findChild( "Field" ); fieldNode; fieldNode = fieldNode.findNextSibling( "Field" ) )
            {
                bValid = Internal::reportUnknownNames( fieldNode, Internal::kArrFieldAttribute, sourceName ) && bValid;
                TelemetryFieldDef field;
                const string_view name = fieldNode.getAttributeText( "name" );
                field._name            = hashed_string( name );
                std::optional<bool> bRequired;
                bValid           = fieldNode.readOptionalAttribute( "required", bRequired ) && bValid;
                field._bRequired = bRequired.has_value() && *bRequired ? SW_TRUE : SW_FALSE; // 적지 않으면 선택 칸이다
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
        int32 schemaVersion = Internal::kUnversionedSchema;
        bValid              = root.readOptionalAttributeInRange( "version", Internal::kUnversionedSchema, MathUtil::kMaxInt32, schemaVersion ) && bValid;
        if ( bValid == false )
            return false;
        _listEvent = std::move( listEvent );
        _settings  = settings;
        _version   = MathUtil::max( _version, schemaVersion );
        return true;
    }
} // namespace sw
