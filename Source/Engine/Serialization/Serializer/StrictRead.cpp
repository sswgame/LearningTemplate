#include "pch.h"

#include "Engine/Serialization/Serializer/StrictRead.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    StrictRead::StrictRead( const TypeInfo& typeInfo, const SoftReadResult result, vector<SchemaOrphanValue> listOrphan, vector<string> listMissing )
        : _pTypeInfo{ &typeInfo }
        , _listOrphan{ std::move( listOrphan ) }
        , _listMissing{ std::move( listMissing ) }
        , _result{ result }
    {
    }

    bool StrictRead::isRead() const noexcept
    {
        return _result == SoftReadResult::Read && _listOrphan.empty() && _listMissing.empty();
    }

    bool StrictRead::report( const string_view sourceName ) const
    {
        const utf8* const pTypeName = _pTypeInfo->_name.c_str();
        if ( _result == SoftReadResult::NotParsed )
        {
            // 왜 못 읽었는지(`Xml.ParseError` · `Xml.WrongRoot`)는 안쪽이 이미 알렸다 — 어느 파일인지를 더한다.
            SW_LOG_ERROR_ID( "Serialization.NotRead", "%#: not a readable <%#> document", sourceName, pTypeName );
            return false;
        }
        for ( const SchemaOrphanValue& orphan : _listOrphan )
        {
            SW_LOG_ERROR_ID( "Serialization.OrphanValue", "%#: '%#' is unknown to %# or its value '%#' cannot be read", sourceName, orphan.getDisplayName(), pTypeName, orphan._text.c_str() );
        }
        for ( const string& missingPath : _listMissing )
        {
            SW_LOG_ERROR_ID( "Serialization.RequiredMissing", "%#: <%#> has no required '%#' or it is empty (PROPERTY( Required ) - the initial value is not a default)", sourceName, pTypeName,
                             missingPath.c_str() );
        }
        return isRead();
    }

    string StrictRead::describe() const
    {
        if ( _result == SoftReadResult::NotParsed )
            return string( "not a readable document" );
        StringBuilder<constant::buffer::kMax512> problem;
        for ( const SchemaOrphanValue& orphan : _listOrphan )
        {
            if ( problem.view().empty() == false )
                problem.append( "; " );
            problem.appendFormat( "unknown key '%#' or a value that cannot be read (%#)", orphan.getDisplayName(), orphan._text );
        }
        for ( const string& missingPath : _listMissing )
        {
            if ( problem.view().empty() == false )
                problem.append( "; " );
            problem.appendFormat( "required '%#' is missing or empty", missingPath );
        }
        if ( _result == SoftReadResult::Rejected )
            problem.append( problem.view().empty() ? "a value outside its PROPERTY( Min, Max )" : "; a value outside its PROPERTY( Min, Max )" );
        return string( problem.view() );
    }
} // namespace sw
