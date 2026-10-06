/**
 * @file LedgerTransferWork.h
 * @brief 이동 하나를 저장소 스레드에서 커밋하고(`Ledger::executeTransfer`) 맡긴 스레드에서 결과를 넘기는 일입니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class LedgerTransferWork
     * @brief `store.submit( make_unique<LedgerTransferWork>( request, Delegate<void( const LedgerTransferOutcome& )>::create<&X::onTransferred>( this ) ) )`.
     * @details 요청은 복사해 든다. `_pPolicy` 는 빌려 쓰므로 일이 끝날 때까지 살아야 한다(카탈로그는 서비스와 같이 산다).
     */
    class SW_GF_API LedgerTransferWork final : public IServiceStoreWork
    {
    public:
        using CompleteDelegate = Delegate<void( const LedgerTransferOutcome& )>;

        LedgerTransferWork( const LedgerTransferRequest& request, CompleteDelegate onComplete );

        void run( IServiceStoreConnection& connection ) override;
        void complete() override;

    private:
        LedgerTransferRequest _request;
        LedgerTransferOutcome _outcome;
        CompleteDelegate      _onComplete;
    };
} // namespace sw
