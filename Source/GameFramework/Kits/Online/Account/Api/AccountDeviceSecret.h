/**
 * @file AccountDeviceSecret.h
 * @brief 게스트 로그인의 장치 비밀(난수 32 B)을 로컬 저장(`ILocalStore` 슬롯 `account/device`, 봉인 Encrypted — 장치 키)에서 읽고, 없으면 만들어 씁니다.
 * @details 로컬 저장의 완료는 주인(게임)이 거둬 이 객체에 넘긴다(`handleCompletion`) — 저장소 하나의 완료 소비자는 하나다. 다른 기계에서 옮겨 온 슬롯(장치 키가
 *          달라 풀리지 않음)은 새로 만들지 않고 실패로 둔다(새로 만들면 그 게스트 계정을 영영 잃는다 — 사용자에게 연동을 권한다).
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Account/Protocol/AccountTypes.h"

namespace sw
{
    struct LocalStoreCompletion;

    class ILocalStore;
    class INetSecurityProvider;

    /** @brief 장치 비밀을 마련하는 상태입니다. */
    enum class AccountDeviceSecretState : uint8
    {
        Idle = 0,
        Reading,
        Writing,
        Ready, ///< `getSecret` 을 쓸 수 있다
        Failed
    };

    /**
     * @class AccountDeviceSecret
     * @brief 장치 비밀 하나입니다.
     */
    class SW_GF_API AccountDeviceSecret
    {
    public:
        static constexpr const utf8* kSlot = "account/device";

        AccountDeviceSecret();

        /** @brief 읽기를 맡깁니다. @p pStore · @p pProvider 는 빌려 쓴다(완료까지 산다). */
        void begin( ILocalStore* pStore, INetSecurityProvider* pProvider );
        /** @brief 로컬 저장의 완료 하나를 넘깁니다. 이 객체의 요청이면 true(처리했다). */
        bool handleCompletion( const LocalStoreCompletion& completion );

        AccountDeviceSecretState getState() const { return _state; }
        const uint8 ( &getSecret() const )[LoginConstant::kDeviceSecretSize] { return _arrSecret; }

    private:
        ILocalStore*             _pStore;
        INetSecurityProvider*    _pProvider;
        uint64                   _requestId;
        uint8                    _arrSecret[LoginConstant::kDeviceSecretSize];
        AccountDeviceSecretState _state;
    };
} // namespace sw
