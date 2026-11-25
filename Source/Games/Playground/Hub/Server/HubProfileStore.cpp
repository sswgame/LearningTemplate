#include "pch.h"

#include "Games/Playground/Hub/Server/HubProfileStore.h"

#include "Core/Log/Logger.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceReadSetConnection.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct HubProfileStoreInternal
        {
            static const hashed_string& getTable()
            {
                static const hashed_string table{ HubProfileStore::kTableName };
                return table;
            }

            /** @brief 진행 하나를 읽는 일입니다. */
            class ReadJob final : public ServiceStoreJob
            {
            public:
                ReadJob( shared_ptr<HubProfileStore::Inbox> pInbox, uint64 accountID )
                    : _pInbox{ std::move( pInbox ) }
                    , _completion{}
                {
                    _completion._accountID = accountID;
                }

                void complete() override { _pInbox->push( std::move( _completion ) ); }

            private:
                void run( ServiceReadSetConnection& connection ) override
                {
                    ServiceRecord record;
                    _completion._result  = connection.readRecord( getTable(), HubProfileStore::makeRecordKey( _completion._accountID ), record );
                    _completion._bytes   = std::move( record._bytes );
                    _completion._version = record._version;
                }

            private:
                shared_ptr<HubProfileStore::Inbox> _pInbox;
                HubProfileStore::Completion        _completion;
            };

            /** @brief 진행 하나를 판 조건으로 쓰는 일입니다. 결과는 여기서 로그로 남긴다 — 링크가 먼저 내려가도 저장 여부가 남는다. */
            class WriteJob final : public ServiceStoreJob
            {
            public:
                WriteJob( shared_ptr<HubProfileStore::Inbox> pInbox, uint64 accountID, vector<uint8> bytes, uint64 expectedVersion )
                    : _pInbox{ std::move( pInbox ) }
                    , _completion{}
                    , _expectedVersion{ expectedVersion }
                {
                    _completion._accountID = accountID;
                    _completion._bytes     = std::move( bytes );
                    _completion._bWrite    = SW_TRUE;
                }

                void complete() override
                {
                    if ( _completion._result == ServiceStoreResult::Ok )
                        SW_LOG_INFO_ID( "Hub.ProfileSaved", "[Hub] profile of account %# saved", _completion._accountID );
                    else if ( isCancelled() == false ) // 종료 중 취소는 저장소가 내려가는 길이다 — 고장과 가른다
                        SW_LOG_ERROR_ID( "Hub.ProfileSaveFailed", "[Hub] profile of account %# was not saved (store result %#)", _completion._accountID,
                                         static_cast<int32>( _completion._result ) );
                    _pInbox->push( std::move( _completion ) );
                }

            private:
                void run( ServiceReadSetConnection& connection ) override
                {
                    ServiceTransaction transaction;
                    transaction.put( getTable(), HubProfileStore::makeRecordKey( _completion._accountID ), _completion._bytes, _expectedVersion );
                    ServiceCommitInfo info;
                    _completion._result  = connection.commit( transaction, &info );
                    _completion._version = info._commitVersion;
                }

            private:
                shared_ptr<HubProfileStore::Inbox> _pInbox;
                HubProfileStore::Completion        _completion;
                uint64                             _expectedVersion;
            };
        };
    } // namespace

    HubProfileStore::HubProfileStore()
        : _source{}
        , _pInbox{ make_shared<Inbox>() }
        , _mapAccount{}
        , _listCompletionScratch{}
        , _savedCount{ 0 }
        , _skippedSaveCount{ 0 }
        , _pendingJobCount{ 0 }
    {
    }

    string HubProfileStore::makeRecordKey( uint64 accountID )
    {
        string key = "a";
        ServiceKeyUtil::appendHex64( key, accountID );
        return key;
    }

    bool HubProfileStore::encodeRecord( const HubProfile& profile, vector<uint8>& outBytes )
    {
        BitWriter  writer( std::move( outBytes ) );
        const bool bWritten = profile.write( writer );
        outBytes            = writer.releaseBytes();
        if ( bWritten == false )
            outBytes.clear();
        return bWritten;
    }

    bool HubProfileStore::decodeRecord( const vector<uint8>& bytes, HubProfile& outProfile )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        return outProfile.read( reader );
    }

    ServiceStore* HubProfileStore::findStore( const utf8* pPurpose, uint64 accountID ) const
    {
        ServiceStore* const pStore = _source.isBound() ? _source() : nullptr;
        if ( pStore == nullptr )
            SW_LOG_ERROR_ID( "Hub.ProfileStoreMissing", "[Hub] no profile store for the %# of account %# - the dedicated server's online services (store) are not running", pPurpose,
                             accountID );
        return pStore;
    }

    bool HubProfileStore::waitCompletions( const Deadline& deadline ) const
    {
        ServiceStore* const pStore = _source.isBound() ? _source() : nullptr;
        if ( pStore == nullptr )
            return false;
        (void)pStore->waitCompletions( deadline );
        return true;
    }

    void HubProfileStore::requestLoad( uint64 accountID )
    {
        AccountState& state = _mapAccount[accountID];
        state._bReleased    = SW_FALSE; // 다시 들어왔다 — 읽기가 기억을 다시 채운다
        if ( state._bWriting )
        {
            state._bLoadWaiting = SW_TRUE; // 남은 쓰기 뒤에 읽는다 — 끊긴 접속의 저장을 재접속이 읽는다
            return;
        }
        submitRead( accountID );
    }

    void HubProfileStore::submitRead( uint64 accountID )
    {
        ++_pendingJobCount;
        ServiceStore* const pStore = findStore( "read", accountID );
        if ( pStore == nullptr )
        {
            Completion failed;
            failed._accountID = accountID;
            _pInbox->push( std::move( failed ) ); // 다음 `poll` 이 받지 않는 읽기로 낸다
            return;
        }
        pStore->submit( make_unique<HubProfileStoreInternal::ReadJob>( _pInbox, accountID ) );
    }

    bool HubProfileStore::requestSave( uint64 accountID, const HubProfile& profile )
    {
        const auto iter = _mapAccount.find( accountID );
        if ( iter == _mapAccount.end() || iter->second._bKnown == SW_FALSE )
        {
            SW_LOG_ERROR_ID( "Hub.ProfileSaveUnread", "[Hub] profile of account %# was not saved - it was never read, so its stored version is unknown", accountID );
            return false;
        }
        vector<uint8> bytes;
        if ( encodeRecord( profile, bytes ) == false )
        {
            SW_LOG_ERROR_ID( "Hub.ProfileOutOfRange", "[Hub] profile of account %# was not saved - a value is outside its declared range (level %#, coins %#)", accountID,
                             profile._level, profile._coins );
            return false;
        }
        AccountState&        state  = iter->second;
        const vector<uint8>& latest = state._bQueued ? state._queuedBytes : state._bWriting ? state._writingBytes
                                                                                            : state._storedBytes;
        // 견주기는 직렬화한 바이트로 한다 — 진행에 칸이 늘어도 `write` 가 싣는 한 놓치지 않는다.
        if ( latest == bytes )
        {
            ++_skippedSaveCount;
            return false;
        }
        if ( state._bWriting )
        {
            state._queuedBytes = std::move( bytes ); // 앞 쓰기가 끝나면 그 판으로 맡긴다(가장 새 것만)
            state._bQueued     = SW_TRUE;
            return true;
        }
        submitWrite( accountID, state, std::move( bytes ) );
        return true;
    }

    void HubProfileStore::submitWrite( uint64 accountID, AccountState& state, vector<uint8> bytes )
    {
        state._bWriting     = SW_TRUE;
        state._writingBytes = bytes;
        ++_pendingJobCount;
        ServiceStore* const pStore = findStore( "save", accountID );
        if ( pStore == nullptr )
        {
            Completion failed;
            failed._accountID = accountID;
            failed._bWrite    = SW_TRUE;
            _pInbox->push( std::move( failed ) );
            return;
        }
        pStore->submit( make_unique<HubProfileStoreInternal::WriteJob>( _pInbox, accountID, std::move( bytes ), state._version ) );
    }

    void HubProfileStore::releaseAccount( uint64 accountID )
    {
        const auto iter = _mapAccount.find( accountID );
        if ( iter == _mapAccount.end() )
            return;
        iter->second._bReleased = SW_TRUE;
        if ( settleAccount( accountID, iter->second ) )
            _mapAccount.erase( iter );
    }

    bool HubProfileStore::settleAccount( uint64 accountID, AccountState& state )
    {
        if ( state._bWriting )
            return false;
        if ( state._bLoadWaiting )
        {
            state._bLoadWaiting = SW_FALSE;
            submitRead( accountID );
            return false;
        }
        return state._bReleased == SW_TRUE;
    }

    void HubProfileStore::poll( vector<HubProfileLoad>& outListLoad )
    {
        _listCompletionScratch.clear();
        {
            ScopedLock<mutex> lock{ _pInbox->_mutex };
            _listCompletionScratch.swap( _pInbox->_listCompletion );
        }
        for ( Completion& completion : _listCompletionScratch )
        {
            --_pendingJobCount;
            const auto iter = _mapAccount.find( completion._accountID );
            if ( completion._bWrite == SW_TRUE )
            {
                if ( iter == _mapAccount.end() )
                    continue; // 링크가 다시 서며 기억을 비웠다
                AccountState& state = iter->second;
                state._bWriting     = SW_FALSE;
                if ( completion._result == ServiceStoreResult::Ok )
                {
                    ++_savedCount;
                    state._storedBytes = std::move( completion._bytes );
                    state._version     = completion._version;
                }
                else
                {
                    // 적용됐는지 모르거나(Unavailable) 다른 서버가 그새 썼다(Conflict) — 판을 모르므로 이 세션의 쓰기를 멈춘다(덮지 않는다). 다음 접속이 다시 읽는다.
                    state._bKnown  = SW_FALSE;
                    state._bQueued = SW_FALSE;
                }
                if ( state._bQueued )
                {
                    state._bQueued = SW_FALSE;
                    submitWrite( completion._accountID, state, std::move( state._queuedBytes ) );
                    continue;
                }
                if ( settleAccount( completion._accountID, state ) )
                    _mapAccount.erase( iter );
                continue;
            }
            HubProfileLoad load;
            load._accountID = completion._accountID;
            if ( completion._result == ServiceStoreResult::NotFound )
                load._bOk = SW_TRUE; // 새 계정 — 기본 진행
            else if ( completion._result == ServiceStoreResult::Ok )
            {
                load._bOk    = decodeRecord( completion._bytes, load._profile ) ? SW_TRUE : SW_FALSE;
                load._bFound = load._bOk;
            }
            if ( load._bOk == SW_TRUE && iter != _mapAccount.end() )
            {
                AccountState& state = iter->second;
                state._bKnown       = SW_TRUE;
                state._version      = completion._version; // 없음이면 `kAbsentVersion` — 첫 쓰기는 "없어야 한다" 조건이다
                state._storedBytes  = std::move( completion._bytes );
            }
            if ( load._bOk == SW_FALSE )
                SW_LOG_ERROR_ID( "Hub.ProfileLoadFailed", "[Hub] profile of account %# could not be read (store result %#) - the login is refused so the stored progress is not overwritten",
                                 load._accountID, static_cast<int32>( completion._result ) );
            outListLoad.push_back( load );
        }
    }
} // namespace sw
