/**
 * @file AccountDeviceSecret.h
 * @brief 게스트 로그인의 장치 비밀(난수 32 B)을 로컬 저장소(`ILocalStore` 슬롯 `account/device`, 장치 키로 봉인한 Encrypted)에서 읽고, 없으면 만들어 저장합니다.
 * @details 로컬 저장소의 완료는 소유자(게임)가 회수해서 이 객체에 넘긴다(`handleCompletion`). 저장소 하나의 완료 소비자는 하나이기 때문이다.
 *          다른 기계에서 옮겨 온 슬롯은 장치 키가 달라 풀리지 않는다. 이 경우 새로 만들지 않고 실패로 둔다. 새로 만들면 그 게스트 계정을 영영 잃으므로 사용자에게 연동을 권한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountTypes.h"

namespace sw
{
    struct LocalStoreCompletion;

    class ILocalStore;
    class INetSecurityProvider;

    /** @brief 장치 비밀을 준비하는 상태입니다. */
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
     * @brief 장치 비밀 하나를 관리합니다.
     */
    class SW_KIT_API AccountDeviceSecret
    {
    public:
        static constexpr const utf8* kSlot = "account/device";

        AccountDeviceSecret();

        /** @brief 읽기를 맡깁니다. @p pStore 와 @p pProvider 는 빌려 쓰며 완료까지 살아 있어야 합니다. */
        void begin( ILocalStore* pStore, INetSecurityProvider* pProvider );
        /** @brief 로컬 저장소의 완료 하나를 넘깁니다. 이 객체의 요청이면 처리하고 true 를 반환합니다. */
        bool handleCompletion( const LocalStoreCompletion& completion );

        AccountDeviceSecretState getState() const { return _state; }
        const uint8 ( &getSecret() const )[LoginConstant::kDeviceSecretSize] { return _arrSecret; }

    private:
        ILocalStore*             _pStore;
        INetSecurityProvider*    _pProvider;
        uint64                   _requestID;
        uint8                    _arrSecret[LoginConstant::kDeviceSecretSize];
        AccountDeviceSecretState _state;
    };
} // namespace sw
