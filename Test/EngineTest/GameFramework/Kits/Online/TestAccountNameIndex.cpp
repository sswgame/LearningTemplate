// 계정 이름 색인 — 다른 키트의 저장소 일 안에서 오프라인 계정을 이름(대소문자 무시) · id 로 찾는다. 게스트의 만든 이름은 색인에 없다(id 로는 찾는다).
#include "pch.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/AccountNameIndex.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 다른 키트(친구 · 우편)의 저장소 일 모양 — run 은 저장소 스레드, 결과는 complete 뒤 시험 스레드가 본다. */
    class NameLookupWork final : public IServiceStoreWork
    {
    public:
        NameLookupWork( const IAccountNameIndex* pIndex, string displayName, AccountID accountID, AccountIdentity* pOutByName, AccountIdentity* pOutByID,
                        ServiceStoreResult* pOutNameResult, ServiceStoreResult* pOutIDResult )
            : _displayName{ std::move( displayName ) }
            , _pIndex{ pIndex }
            , _pOutByName{ pOutByName }
            , _pOutByID{ pOutByID }
            , _pOutNameResult{ pOutNameResult }
            , _pOutIDResult{ pOutIDResult }
            , _accountID{ accountID }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            *_pOutNameResult = _pIndex->readIdentityByDisplayName( connection, _displayName, *_pOutByName );
            *_pOutIDResult   = _pIndex->readIdentity( connection, _accountID, *_pOutByID );
        }

        void complete() override {}

    private:
        string                   _displayName;
        const IAccountNameIndex* _pIndex;
        AccountIdentity*         _pOutByName;
        AccountIdentity*         _pOutByID;
        ServiceStoreResult*      _pOutNameResult;
        ServiceStoreResult*      _pOutIDResult;
        AccountID                _accountID;
    };

    struct NameIndexRig
    {
        MemoryServiceDatabase  _database;
        MemoryServiceStore     _store;
        NetSecurityLoginCrypto _crypto;
        LoginService           _loginService;
        AccountNameIndex       _index;
        uint64                 _nextTag;

        NameIndexRig()
            : _database{}
            , _store{ &_database }
            , _crypto{ &NetSecurity::getProvider() }
            , _loginService{}
            , _index{}
            , _nextTag{ 1 }
        {
            LoginSettings settings;
            settings._passwordHashParams._memoryKiB                        = 256;
            settings._passwordHashParams._iterationCount                   = 1;
            const uint8 arrMasterKey[LoginTicketAuthority::kMasterKeySize] = { 7 };
            _loginService.initialize( &_store, &_crypto, settings, arrMasterKey );
        }

        ~NameIndexRig()
        {
            _store.shutdown();
            (void)_store.pollCompletions();
            _loginService.shutdown();
        }

        LoginCompletion settle()
        {
            (void)_store.pollCompletions();
            vector<LoginCompletion> listCompletion;
            _loginService.drainCompletions( listCompletion );
            return listCompletion.empty() ? LoginCompletion{} : listCompletion.back();
        }

        void lookUp( string_view displayName, AccountID accountID, AccountIdentity& outByName, AccountIdentity& outByID, ServiceStoreResult& outNameResult,
                     ServiceStoreResult& outIDResult )
        {
            _store.submit( sw::make_unique<NameLookupWork>( &_index, string( displayName ), accountID, &outByName, &outByID, &outNameResult, &outIDResult ) );
            (void)_store.pollCompletions();
        }
    };
} // namespace

SW_TEST_CASE( AccountNameIndexTest, FindsOfflineAccountsByNameAndID )
{
    NameIndexRig    rig;
    LoginCredential credential;
    credential._loginName = "Hero_01";
    credential._password  = "password123";
    rig._loginService.registerAccount( credential, 1000, rig._nextTag++ );
    const LoginCompletion registered = rig.settle();
    SW_ASSERT_TRUE( registered._result == LoginResult::Ok );
    const AccountID heroID = registered._identity._accountID;
    SW_ASSERT_TRUE( heroID != kInvalidAccountID );

    AccountIdentity    byName;
    AccountIdentity    byID;
    ServiceStoreResult nameResult = ServiceStoreResult::Invalid;
    ServiceStoreResult idResult   = ServiceStoreResult::Invalid;
    rig.lookUp( "HERO_01", heroID, byName, byID, nameResult, idResult ); // 로그인하지 않았다 — 접속 상태에는 없다
    SW_ASSERT_TRUE( nameResult == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( heroID, byName._accountID );
    SW_EXPECT_TRUE( byName._displayName == "Hero_01" );
    SW_ASSERT_TRUE( idResult == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( byID._displayName == "Hero_01" );

    rig.lookUp( "nobody", 0xBEEF, byName, byID, nameResult, idResult );
    SW_EXPECT_TRUE( nameResult == ServiceStoreResult::NotFound );
    SW_EXPECT_TRUE( idResult == ServiceStoreResult::NotFound );
    rig.lookUp( "bad name!", heroID, byName, byID, nameResult, idResult ); // 이름 규칙 밖 — 저장소를 읽지 않고 없음
    SW_EXPECT_TRUE( nameResult == ServiceStoreResult::NotFound );

    rig._database.armFault( ServiceStoreFault::RejectRead );
    rig.lookUp( "hero_01", heroID, byName, byID, nameResult, idResult );
    SW_EXPECT_TRUE( nameResult == ServiceStoreResult::Unavailable ); // 없음과 아픔을 가른다
}
