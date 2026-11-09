#include "pch.h"

#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/BitUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct RemoteConfigInternal
        {
            static constexpr uint64 kRecordVersion   = 2; ///< 2 에서 값 종류가 3 비트가 됐다(실험). 옛 버전 레코드는 다시 쓴다
            static constexpr uint64 kVersion         = 2;
            static constexpr int32  kTypeBitCount    = 3;
            static constexpr int32  kMaxVariantCount = 16;
            static constexpr int32  kListPageSize    = 256;
            static constexpr int32  kMaxKeySize      = 128;
            static constexpr int32  kMaxSnapshotKeys = 4096;

            static void writeValue( BitWriter& outWriter, const RemoteConfigValue& value )
            {
                outWriter.writeBits( static_cast<uint32>( value._type ), kTypeBitCount );
                outWriter.writeVarInt( value._integer );
                const uint64 realBits = BitUtil::doubleToBits( value._real );
                outWriter.writeUint32( static_cast<uint32>( realBits ) );
                outWriter.writeUint32( static_cast<uint32>( realBits >> 32 ) );
                ServiceKeyUtil::writeString( outWriter, value._text );
                outWriter.writeVarInt( value._rolloutBasisPoints );
                outWriter.writeBool( value._bClientVisible == SW_TRUE );
            }

            [[nodiscard]] static bool readValue( BitReader& reader, RemoteConfigValue& outValue )
            {
                outValue._type        = static_cast<RemoteConfigValueType>( reader.readBits( kTypeBitCount ) );
                outValue._integer     = reader.readVarInt();
                const uint64 lowBits  = reader.readUint32();
                const uint64 highBits = reader.readUint32();
                const uint64 realBits = lowBits | ( highBits << 32 );
                outValue._real        = BitUtil::bitsToDouble( realBits );
                if ( ServiceKeyUtil::readString( reader, RemoteConfigValue::kMaxTextSize, outValue._text ) == false )
                    return false;
                const int64 rollout          = reader.readVarInt();
                outValue._bClientVisible     = reader.readBool() ? SW_TRUE : SW_FALSE;
                outValue._rolloutBasisPoints = static_cast<int32>( rollout );
                return reader.hasOverflowed() == false && isValidValue( outValue );
            }

            static bool isValidValue( const RemoteConfigValue& value )
            {
                const bool bBasicOk = value._type <= RemoteConfigValueType::Experiment && value._text.size() <= static_cast<size_t>( RemoteConfigValue::kMaxTextSize ) &&
                                      0 <= value._rolloutBasisPoints && value._rolloutBasisPoints <= RemoteConfigValue::kFullRolloutPoints;
                if ( bBasicOk == false )
                    return false;
                vector<RemoteConfigVariant> listVariant;
                return value._type != RemoteConfigValueType::Experiment || RemoteConfig::parseVariants( value._text, listVariant );
            }

            static bool isCodeText( string_view text )
            {
                if ( text.empty() || text.size() > static_cast<size_t>( kMaxKeySize ) )
                    return false;
                for ( const utf8 ch : text )
                {
                    const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_';
                    if ( bAllowed == false )
                        return false;
                }
                return true;
            }

            static vector<uint8> encodeRecord( const RemoteConfigValue& value )
            {
                BitWriter writer;
                writer.writeVarUint( kRecordVersion );
                writeValue( writer, value );
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decodeRecord( const vector<uint8>& bytes, RemoteConfigValue& outValue )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readVarUint() != kRecordVersion )
                    return false;
                return readValue( reader, outValue );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RemoteConfigExperimentMetrics::RemoteConfigExperimentMetrics( MetricRegistry* pRegistry )
        : _pRegistry{ pRegistry }
    {
    }

    bool RemoteConfigExperimentMetrics::recordExposure( const RemoteConfig& config, string_view experiment, uint64 accountID )
    {
        return add( config, experiment, accountID, "exposure" );
    }

    bool RemoteConfigExperimentMetrics::recordEvent( const RemoteConfig& config, string_view experiment, uint64 accountID, string_view eventName )
    {
        if ( RemoteConfigInternal::isCodeText( eventName ) == false )
            return false;
        return add( config, experiment, accountID, eventName );
    }

    bool RemoteConfigExperimentMetrics::add( const RemoteConfig& config, string_view experiment, uint64 accountID, string_view eventName )
    {
        string variant;
        if ( config.findVariant( experiment, accountID, variant ) == false )
            return false;
        if ( _pRegistry == nullptr )
            return true;
        const vector<MetricLabel> listLabel = {
            {"experiment", string( experiment )},
            {   "variant",              variant},
            {     "event",  string( eventName )}
        };
        MetricCounter* pCounter = _pRegistry->registerCounter( "remote_config_experiment_total", "A/B experiment exposures and events per variant", listLabel );
        if ( pCounter != nullptr )
            pCounter->add();
        return true;
    }
} // namespace sw

namespace sw
{
    ServiceStoreRead<map<string, RemoteConfig::Entry>> RemoteConfig::readTable( IServiceStoreConnection& connection )
    {
        ServiceStoreRead<map<string, Entry>> read;
        string                               cursor;
        while ( true )
        {
            vector<ServiceRecord> listRecord;
            read._result = connection.listRecords( getTable(), string_view{}, cursor, RemoteConfigInternal::kListPageSize, false, listRecord );
            if ( read._result != ServiceStoreResult::Ok )
                return read;
            for ( const ServiceRecord& record : listRecord )
            {
                Entry entry;
                if ( RemoteConfigInternal::decodeRecord( record._bytes, entry._value ) == false )
                    continue; // 깨진 줄은 없는 것으로 — 완료가 경고하지 않는다(저장소 스레드), 운영 도구가 본다
                entry._version           = record._version;
                read._value[record._key] = std::move( entry );
            }
            if ( static_cast<int32>( listRecord.size() ) < RemoteConfigInternal::kListPageSize )
                return read;
            cursor = listRecord.back()._key;
        }
    }

    ServiceStoreRead<uint64> RemoteConfig::writeValue( IServiceStoreConnection& connection, const string& key, const RemoteConfigValue& value, const ServiceAuditEntry& audit,
                                                       uint64 expectedVersion )
    {
        ServiceTransaction transaction;
        transaction.put( getTable(), key, RemoteConfigInternal::encodeRecord( value ), expectedVersion );
        // 같은 버전 위의 같은 키 바꾸기는 같은 감사 키 — 재시도가 줄을 둘 만들지 않는다
        ServiceAuditLog::stageEntry( transaction, audit, expectedVersion, HashUtil::computeStringHash64( key.data(), key.size(), false ) );
        ServiceCommitInfo        info;
        ServiceStoreRead<uint64> write;
        write._result = connection.commit( transaction, &info );
        write._value  = info._commitVersion;
        return write;
    }
} // namespace sw

namespace sw
{
    RemoteConfig::RemoteConfig()
        : _mapEntry{}
        , _snapshotHash{ 0 }
        , _store{}
        , _lastReloadResult{ ServiceStoreResult::Ok }
        , _lastSetResult{ ServiceStoreResult::Ok }
    {
        recomputeSnapshotHash();
    }

    RemoteConfig::~RemoteConfig() = default;

    void RemoteConfig::requestReload( IServiceStore& store )
    {
        _store.bindStore( &store ); // 부르는 쪽이 저장소를 넘긴다 — 같은 저장소다
        _store.submit( []( IServiceStoreConnection& connection )
        { return readTable( connection ); },
                       [this]( ServiceStoreRead<map<string, Entry>>& read, ServiceStoreCompletion completion )
        { onReloadCompleted( read._result, completion.isCancelled(), read._value ); } );
    }

    void RemoteConfig::submitSet( IServiceStore& store, string_view key, const RemoteConfigValue& value, const ServiceAuditEntry& audit )
    {
        const bool bRejectedChange = isValidKey( key ) == false || RemoteConfigInternal::isValidValue( value ) == false || ServiceAuditLog::isValidEntry( audit ) == false;
        if ( bRejectedChange )
        {
            SW_LOG_WARNING_ID( "RemoteConfig.ChangeRejected", "Rejected remote config change '%#' — invalid key, value or audit entry", key );
            _lastSetResult = ServiceStoreResult::Invalid;
            return;
        }
        const auto   entryIt         = _mapEntry.find( key );
        const uint64 expectedVersion = entryIt == _mapEntry.end() ? ServiceRecord::kAbsentVersion : entryIt->second._version;
        _store.bindStore( &store );
        _store.submit( [name = string{ key }, value, audit, expectedVersion]( IServiceStoreConnection& connection )
        { return writeValue( connection, name, value, audit, expectedVersion ); },
                       [this, name = string{ key }, value]( const ServiceStoreRead<uint64>& write )
        { onSetCompleted( write._result, name, value, write._value ); } );
    }

    bool RemoteConfig::findInteger( string_view key, int64& outValue ) const
    {
        const Entry* pEntry = findEntry( key, RemoteConfigValueType::Integer );
        if ( pEntry == nullptr )
            return false;
        outValue = pEntry->_value._integer;
        return true;
    }

    bool RemoteConfig::findReal( string_view key, float64& outValue ) const
    {
        const Entry* pEntry = findEntry( key, RemoteConfigValueType::Real );
        if ( pEntry == nullptr )
            return false;
        outValue = pEntry->_value._real;
        return true;
    }

    bool RemoteConfig::findText( string_view key, string& outValue ) const
    {
        const Entry* pEntry = findEntry( key, RemoteConfigValueType::Text );
        if ( pEntry == nullptr )
            return false;
        outValue = pEntry->_value._text;
        return true;
    }

    bool RemoteConfig::isFeatureEnabled( string_view flag, uint64 accountID, bool bDefault ) const
    {
        const Entry* pEntry = findEntry( flag, RemoteConfigValueType::Flag );
        if ( pEntry == nullptr )
            return bDefault;
        if ( pEntry->_value._integer == 0 )
            return false;
        return computeRolloutBucket( flag, accountID ) < pEntry->_value._rolloutBasisPoints;
    }

    void RemoteConfig::writeClientSnapshot( BitWriter& outWriter ) const
    {
        uint64 visibleCount = 0;
        for ( const auto& [key, entry] : _mapEntry )
        {
            if ( entry._value._bClientVisible == SW_TRUE )
                ++visibleCount;
            (void)key;
        }
        outWriter.writeVarUint( RemoteConfigInternal::kVersion );
        outWriter.writeVarUint( visibleCount );
        for ( const auto& [key, entry] : _mapEntry )
        {
            if ( entry._value._bClientVisible == SW_FALSE )
                continue;
            ServiceKeyUtil::writeString( outWriter, key );
            RemoteConfigInternal::writeValue( outWriter, entry._value );
        }
    }

    bool RemoteConfig::readClientSnapshot( BitReader& reader )
    {
        if ( reader.readVarUint() != RemoteConfigInternal::kVersion )
            return false;
        const uint64 count = reader.readVarUint();
        if ( count > static_cast<uint64>( RemoteConfigInternal::kMaxSnapshotKeys ) )
            return false;
        map<string, Entry> mapEntry;
        for ( uint64 index = 0; index < count; ++index )
        {
            string key;
            Entry  entry;
            if ( ServiceKeyUtil::readString( reader, RemoteConfigInternal::kMaxKeySize, key ) == false || isValidKey( key ) == false )
                return false;
            if ( RemoteConfigInternal::readValue( reader, entry._value ) == false )
                return false;
            mapEntry[key] = std::move( entry );
        }
        if ( reader.hasOverflowed() )
            return false;
        _mapEntry.swap( mapEntry );
        recomputeSnapshotHash();
        return true;
    }

    const hashed_string& RemoteConfig::getTable()
    {
        static const hashed_string s_table{ "remote_config" };
        return s_table;
    }

    bool RemoteConfig::isValidKey( string_view key )
    {
        if ( key.empty() || key.size() > static_cast<size_t>( RemoteConfigInternal::kMaxKeySize ) )
            return false;
        for ( const utf8 ch : key )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    bool RemoteConfig::parseVariants( string_view text, vector<RemoteConfigVariant>& outListVariant )
    {
        outListVariant.clear();
        int32 totalWeight = 0;
        while ( text.empty() == false )
        {
            const size_t      separator = text.find( ';' );
            const string_view part      = text.substr( 0, separator );
            text                        = separator == string_view::npos ? string_view{} : text.substr( separator + 1 );
            const size_t colon          = part.find( ':' );
            if ( colon == string_view::npos )
                return false;
            RemoteConfigVariant variant;
            variant._name        = string( part.substr( 0, colon ) );
            const bool bParsed   = RemoteConfigInternal::isCodeText( variant._name ) && StringUtil::parseInt( part.substr( colon + 1 ), variant._weightBasisPoints );
            const bool bWeightOk = bParsed && 0 < variant._weightBasisPoints && variant._weightBasisPoints <= RemoteConfigValue::kFullRolloutPoints - totalWeight;
            if ( bWeightOk == false )
                return false;
            for ( const RemoteConfigVariant& existing : outListVariant )
            {
                if ( existing._name == variant._name )
                    return false;
            }
            totalWeight += variant._weightBasisPoints;
            outListVariant.push_back( std::move( variant ) );
            if ( static_cast<int32>( outListVariant.size() ) > RemoteConfigInternal::kMaxVariantCount )
                return false;
        }
        return totalWeight == RemoteConfigValue::kFullRolloutPoints;
    }

    bool RemoteConfig::findVariant( string_view experiment, uint64 accountID, string& outVariant ) const
    {
        const Entry* pEntry = findEntry( experiment, RemoteConfigValueType::Experiment );
        if ( pEntry == nullptr )
            return false;
        vector<RemoteConfigVariant> listVariant;
        if ( parseVariants( pEntry->_value._text, listVariant ) == false )
            return false; // 읽기와 쓰기가 검사했으므로 생기지 않는다
        const int32 bucket     = computeRolloutBucket( experiment, accountID );
        int32       cumulative = 0;
        for ( const RemoteConfigVariant& variant : listVariant )
        {
            cumulative += variant._weightBasisPoints;
            if ( bucket < cumulative )
            {
                outVariant = variant._name;
                return true;
            }
        }
        return false;
    }

    int32 RemoteConfig::computeRolloutBucket( string_view flag, uint64 accountID )
    {
        uint8 arrAccountByte[8];
        for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
        {
            arrAccountByte[byteIndex] = static_cast<uint8>( accountID >> ( byteIndex * 8 ) );
        }
        uint64 hash = HashUtil::computeStringHash64( flag.data(), flag.size(), false );
        hash        = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( arrAccountByte ), sizeof( arrAccountByte ), false, hash );
        return static_cast<int32>( HashUtil::mix64( hash ) % static_cast<uint64>( RemoteConfigValue::kFullRolloutPoints ) );
    }

    const RemoteConfig::Entry* RemoteConfig::findEntry( string_view key, RemoteConfigValueType type ) const
    {
        const auto entryIt = _mapEntry.find( key );
        if ( entryIt == _mapEntry.end() || entryIt->second._value._type != type )
            return nullptr;
        return &entryIt->second;
    }

    void RemoteConfig::recomputeSnapshotHash()
    {
        BitWriter writer;
        writeClientSnapshot( writer );
        const vector<uint8>& bytes = writer.getBytes();
        _snapshotHash              = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bytes.data() ), static_cast<size_t>( writer.getByteCount() ), false );
    }

    void RemoteConfig::onReloadCompleted( ServiceStoreResult result, bool bCancelled, map<string, Entry>& inoutMapEntry )
    {
        _lastReloadResult = result;
        if ( result != ServiceStoreResult::Ok )
        {
            if ( bCancelled )
                return; // 저장소가 종료 중이라 취소된 첫 읽기다 — 고장이 아니므로 경고하지 않는다. 이전 값을 그대로 둔다
            SW_LOG_WARNING_ID( "RemoteConfig.ReloadFailed", "Remote config reload failed — keeping the previous values" );
            return;
        }
        _mapEntry.swap( inoutMapEntry );
        recomputeSnapshotHash();
    }

    void RemoteConfig::onSetCompleted( ServiceStoreResult result, const string& key, const RemoteConfigValue& value, uint64 version )
    {
        _lastSetResult = result;
        if ( result != ServiceStoreResult::Ok )
            return;
        Entry& entry   = _mapEntry[key];
        entry._value   = value;
        entry._version = version;
        recomputeSnapshotHash();
    }
} // namespace sw
