#include "pch.h"

#include "GameFramework/Base/Online/Config/RemoteConfig.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

#include <cstring>

namespace sw
{
    SW_LOG_CALLER( "RemoteConfig" );

    namespace
    {
        struct RemoteConfigInternal
        {
            static constexpr uint64 kRecordVersion   = 1;
            static constexpr uint64 kVersion         = 1;
            static constexpr int32  kTypeBitCount    = 2;
            static constexpr int32  kListPageSize    = 256;
            static constexpr int32  kMaxKeySize      = 128;
            static constexpr int32  kMaxSnapshotKeys = 4096;
            static constexpr utf8   kChangedTopic[]  = "config.changed";

            static void writeValue( BitWriter& outWriter, const RemoteConfigValue& value )
            {
                outWriter.writeBits( static_cast<uint32>( value._type ), kTypeBitCount );
                outWriter.writeVarInt( value._integer );
                uint64 realBits = 0;
                std::memcpy( &realBits, &value._real, sizeof( realBits ) );
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
                std::memcpy( &outValue._real, &realBits, sizeof( realBits ) );
                if ( ServiceKeyUtil::readString( reader, RemoteConfigValue::kMaxTextSize, outValue._text ) == false )
                    return false;
                const int64 rollout          = reader.readVarInt();
                outValue._bClientVisible     = reader.readBool() ? SW_TRUE : SW_FALSE;
                outValue._rolloutBasisPoints = static_cast<int32>( rollout );
                return reader.hasOverflowed() == false && isValidValue( outValue );
            }

            static bool isValidValue( const RemoteConfigValue& value )
            {
                return value._text.size() <= static_cast<size_t>( RemoteConfigValue::kMaxTextSize ) && 0 <= value._rolloutBasisPoints &&
                       value._rolloutBasisPoints <= RemoteConfigValue::kFullRolloutPoints;
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

            static uint64 mixBits( uint64 value )
            {
                value ^= value >> 30;
                value *= HashUtil::kSplitMixMultiplier0;
                value ^= value >> 27;
                value *= HashUtil::kSplitMixMultiplier1;
                value ^= value >> 31;
                return value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 표 전체를 읽는다(저장소 스레드), 결과를 바꿔 넣는다(서비스 스레드). */
    class RemoteConfig::ReloadWork final : public IServiceStoreWork
    {
    public:
        explicit ReloadWork( RemoteConfig* pConfig )
            : _mapEntry{}
            , _pConfig{ pConfig }
            , _result{ ServiceStoreResult::Ok }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            string cursor;
            while ( true )
            {
                vector<ServiceRecord> listRecord;
                _result = connection.listRecords( getTable(), string_view{}, cursor, RemoteConfigInternal::kListPageSize, false, listRecord );
                if ( _result != ServiceStoreResult::Ok )
                    return;
                for ( const ServiceRecord& record : listRecord )
                {
                    Entry entry;
                    if ( RemoteConfigInternal::decodeRecord( record._bytes, entry._value ) == false )
                        continue; // 깨진 줄은 없는 것으로 — complete 가 경고하지 않는다(저장소 스레드), 운영 도구가 본다
                    entry._version         = record._version;
                    _mapEntry[record._key] = std::move( entry );
                }
                if ( static_cast<int32>( listRecord.size() ) < RemoteConfigInternal::kListPageSize )
                    return;
                cursor = listRecord.back()._key;
            }
        }

        void complete() override { _pConfig->onReloadCompleted( _result, _mapEntry ); }

    private:
        map<string, Entry> _mapEntry;
        RemoteConfig*      _pConfig;
        ServiceStoreResult _result;
    };
} // namespace sw

namespace sw
{
    /** @brief 값 하나를 판 조건 + 감사 줄로 쓴다. */
    class RemoteConfig::SetWork final : public IServiceStoreWork
    {
    public:
        SetWork( RemoteConfig* pConfig, IServerBus* pBus, string key, const RemoteConfigValue& value, const ServiceAuditEntry& audit, uint64 expectedVersion )
            : _key{ std::move( key ) }
            , _value{ value }
            , _audit{ audit }
            , _pConfig{ pConfig }
            , _pBus{ pBus }
            , _expectedVersion{ expectedVersion }
            , _commitVersion{ 0 }
            , _result{ ServiceStoreResult::Ok }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            ServiceTransaction transaction;
            transaction.put( getTable(), _key, RemoteConfigInternal::encodeRecord( _value ), _expectedVersion );
            // 같은 판 위의 같은 키 바꾸기는 같은 감사 키 — 재시도가 줄을 둘 만들지 않는다
            ServiceAuditLog::stageEntry( transaction, _audit, _expectedVersion, StringUtil::computeHash64( _key.data(), _key.size(), false ) );
            ServiceCommitInfo info;
            _result        = connection.commit( transaction, &info );
            _commitVersion = info._commitVersion;
        }

        void complete() override { _pConfig->onSetCompleted( _result, _pBus, _key, _value, _commitVersion ); }

    private:
        string             _key;
        RemoteConfigValue  _value;
        ServiceAuditEntry  _audit;
        RemoteConfig*      _pConfig;
        IServerBus*        _pBus;
        uint64             _expectedVersion;
        uint64             _commitVersion;
        ServiceStoreResult _result;
    };
} // namespace sw

namespace sw
{
    RemoteConfig::RemoteConfig()
        : _mapEntry{}
        , _snapshotHash{ 0 }
        , _pendingWorkCount{ 0 }
        , _lastReloadResult{ ServiceStoreResult::Ok }
        , _lastSetResult{ ServiceStoreResult::Ok }
    {
        recomputeSnapshotHash();
    }

    RemoteConfig::~RemoteConfig() = default;

    void RemoteConfig::requestReload( IServiceStore& store )
    {
        ++_pendingWorkCount;
        store.submit( sw::make_unique<ReloadWork>( this ) );
    }

    void RemoteConfig::submitSet( IServiceStore& store, IServerBus* pBus, string_view key, const RemoteConfigValue& value, const ServiceAuditEntry& audit )
    {
        if ( isValidKey( key ) == false || RemoteConfigInternal::isValidValue( value ) == false || ServiceAuditLog::isValidEntry( audit ) == false )
        {
            SW_LOG_WARNING( "Rejected remote config change '%#' — invalid key, value or audit entry", key );
            _lastSetResult = ServiceStoreResult::Invalid;
            return;
        }
        const auto   entryIt         = _mapEntry.find( key );
        const uint64 expectedVersion = entryIt == _mapEntry.end() ? ServiceRecord::kAbsentVersion : entryIt->second._version;
        ++_pendingWorkCount;
        store.submit( sw::make_unique<SetWork>( this, pBus, string{ key }, value, audit, expectedVersion ) );
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

    int32 RemoteConfig::computeRolloutBucket( string_view flag, uint64 accountID )
    {
        uint8 arrAccountByte[8];
        for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
        {
            arrAccountByte[byteIndex] = static_cast<uint8>( accountID >> ( byteIndex * 8 ) );
        }
        uint64 hash = StringUtil::computeHash64( flag.data(), flag.size(), false );
        hash        = StringUtil::computeHash64( reinterpret_cast<const utf8*>( arrAccountByte ), sizeof( arrAccountByte ), false, hash );
        return static_cast<int32>( RemoteConfigInternal::mixBits( hash ) % static_cast<uint64>( RemoteConfigValue::kFullRolloutPoints ) );
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
        _snapshotHash              = StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ), static_cast<size_t>( writer.getByteCount() ), false );
    }

    void RemoteConfig::onReloadCompleted( ServiceStoreResult result, map<string, Entry>& inoutMapEntry )
    {
        --_pendingWorkCount;
        _lastReloadResult = result;
        if ( result != ServiceStoreResult::Ok )
        {
            SW_LOG_WARNING( "Remote config reload failed — keeping the previous values" );
            return;
        }
        _mapEntry.swap( inoutMapEntry );
        recomputeSnapshotHash();
    }

    void RemoteConfig::onSetCompleted( ServiceStoreResult result, IServerBus* pBus, const string& key, const RemoteConfigValue& value, uint64 version )
    {
        --_pendingWorkCount;
        _lastSetResult = result;
        if ( result != ServiceStoreResult::Ok )
            return;
        Entry& entry   = _mapEntry[key];
        entry._value   = value;
        entry._version = version;
        recomputeSnapshotHash();
        if ( pBus != nullptr )
            pBus->publish( RemoteConfigInternal::kChangedTopic, reinterpret_cast<const uint8*>( key.data() ), static_cast<int32>( key.size() ) );
    }
} // namespace sw
