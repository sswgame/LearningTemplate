#include "pch.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"

namespace sw
{
    ServiceTransaction::ServiceTransaction()
        : _listWrite{}
    {
    }

    void ServiceTransaction::put( const hashed_string& table, string_view key, vector<uint8> bytes, uint64 expectedVersion )
    {
        ServiceWrite& write    = _listWrite.emplace_back();
        write._bytes           = std::move( bytes );
        write._key             = string{ key };
        write._table           = table;
        write._expectedVersion = expectedVersion;
        write._kind            = ServiceWrite::Kind::Put;
    }

    void ServiceTransaction::erase( const hashed_string& table, string_view key, uint64 expectedVersion )
    {
        ServiceWrite& write    = _listWrite.emplace_back();
        write._key             = string{ key };
        write._table           = table;
        write._expectedVersion = expectedVersion;
        write._kind            = ServiceWrite::Kind::Erase;
    }

    void ServiceTransaction::requireVersion( const hashed_string& table, string_view key, uint64 expectedVersion )
    {
        ServiceWrite& write    = _listWrite.emplace_back();
        write._key             = string{ key };
        write._table           = table;
        write._expectedVersion = expectedVersion;
        write._kind            = ServiceWrite::Kind::Require;
    }

    bool ServiceTransaction::isWellFormed() const
    {
        if ( _listWrite.empty() || static_cast<int32>( _listWrite.size() ) > kMaxWriteCount )
            return false;
        for ( size_t writeIndex = 0; writeIndex < _listWrite.size(); ++writeIndex )
        {
            const ServiceWrite& write = _listWrite[writeIndex];
            if ( write._table.empty() || isValidKey( write._key ) == false )
                return false;
            if ( write._bytes.size() > static_cast<size_t>( IServiceStoreConnection::kMaxRecordSize ) )
                return false;
            for ( size_t otherIndex = 0; otherIndex < writeIndex; ++otherIndex )
            {
                const bool bSameKey = _listWrite[otherIndex]._table == write._table && _listWrite[otherIndex]._key == write._key;
                if ( bSameKey )
                    return false;
            }
        }
        return true;
    }

    bool ServiceTransaction::isValidKey( string_view key )
    {
        if ( key.empty() || key.size() > static_cast<size_t>( IServiceStoreConnection::kMaxKeySize ) )
            return false;
        for ( const utf8 ch : key )
        {
            const bool bLower = 'a' <= ch && ch <= 'z';
            const bool bDigit = '0' <= ch && ch <= '9';
            const bool bMark  = ch == '_' || ch == '.' || ch == '/' || ch == '-';
            if ( bLower == false && bDigit == false && bMark == false )
                return false;
        }
        return true;
    }
} // namespace sw
