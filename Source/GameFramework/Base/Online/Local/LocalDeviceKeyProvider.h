/**
 * @file LocalDeviceKeyProvider.h
 * @brief 장치 키 — 처음 쓸 때 난수 32 B 를 사용자 데이터 폴더의 파일에 두고 그 뒤로는 읽습니다. Windows 는 DPAPI 로 감싸(이 PC 의 이 사용자만 푼다),
 *        리눅스는 소유자만 읽는 파일(0600)입니다.
 * @details 실수 · 가벼운 변조 막기다 — 키가 그 PC 에 있으므로 치트는 막지 못한다(경쟁 데이터의 정본은 서버). 키 파일을 잃으면 봉인된 슬롯은 WrongKey 다.
 *          계정 단위 키(서버가 준 것)를 쓰려면 다른 `ILocalStoreKeyProvider` 를 준다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class INetSecurityProvider;

    /**
     * @class LocalDeviceKeyProvider
     * @brief 키 파일 하나의 장치 키입니다. 한 번 읽으면 메모리에 둔다(아무 스레드에서나).
     */
    class SW_GF_API LocalDeviceKeyProvider final : public ILocalStoreKeyProvider
    {
    public:
        static constexpr const utf8* kKeyFileName = "device.key";

        /** @brief @p keyFilePath 에 키를 두고, 새 키의 난수는 @p pSecurityProvider 에서 얻는다(빌려 쓴다). */
        LocalDeviceKeyProvider( string_view keyFilePath, INetSecurityProvider* pSecurityProvider );
        ~LocalDeviceKeyProvider() override;

        [[nodiscard]] bool getSealKey( uint8 ( &outKey )[kKeySize] ) override;

    private:
        [[nodiscard]] bool loadOrCreateKeyLocked();

        string                _keyFilePath;
        mutable mutex         _mutex;
        INetSecurityProvider* _pSecurityProvider;
        uint8                 _arrKey[kKeySize];
        uint8                 _bLoaded;
    };
} // namespace sw
