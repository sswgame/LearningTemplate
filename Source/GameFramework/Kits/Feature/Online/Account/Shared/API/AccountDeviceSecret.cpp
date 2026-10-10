#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Shared/API/AccountDeviceSecret.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "GameFramework/Base/Online/Local/LocalStore.h"

namespace sw
{
    SW_LOG_CALLER( "AccountDeviceSecret" );
} // namespace sw

namespace sw
{
    AccountDeviceSecret::AccountDeviceSecret()
        : _pStore{ nullptr }
        , _pProvider{ nullptr }
        , _requestId{ 0 }
        , _arrSecret{}
        , _state{ AccountDeviceSecretState::Idle }
    {
    }

    void AccountDeviceSecret::begin( ILocalStore* pStore, INetSecurityProvider* pProvider )
    {
        _pStore    = pStore;
        _pProvider = pProvider;
        _state     = AccountDeviceSecretState::Reading;
        _requestId = _pStore->submitRead( kSlot );
    }

    bool AccountDeviceSecret::handleCompletion( const LocalStoreCompletion& completion )
    {
        if ( completion._requestId != _requestId || _requestId == 0 )
            return false;
        _requestId = 0;
        if ( _state == AccountDeviceSecretState::Reading )
        {
            const bool bRead = completion._result == LocalStoreResult::Ok && static_cast<int32>( completion._bytes.size() ) == LoginConstant::kDeviceSecretSize;
            if ( bRead )
            {
                Memory::copy( _arrSecret, completion._bytes.data(), LoginConstant::kDeviceSecretSize );
                _state = AccountDeviceSecretState::Ready;
                return true;
            }
            if ( completion._result != LocalStoreResult::NotFound )
            {
                // 다른 장치 · 깨진 슬롯 — 새로 만들면 그 게스트 계정을 잃는다.
                SW_LOG_WARNING( "guest device secret could not be read (%#) — not replacing it", static_cast<int32>( completion._result ) );
                _state = AccountDeviceSecretState::Failed;
                return true;
            }
            if ( _pProvider->fillRandomBytes( _arrSecret, LoginConstant::kDeviceSecretSize ) == false )
            {
                _state = AccountDeviceSecretState::Failed;
                return true;
            }
            LocalStoreWriteOptions options;
            options._seal = LocalStoreSeal::Encrypted;
            _state        = AccountDeviceSecretState::Writing;
            _requestId    = _pStore->submitWrite( kSlot, vector<uint8>( _arrSecret, _arrSecret + LoginConstant::kDeviceSecretSize ), options );
            return true;
        }
        _state = completion._result == LocalStoreResult::Ok ? AccountDeviceSecretState::Ready : AccountDeviceSecretState::Failed;
        return true;
    }
} // namespace sw
