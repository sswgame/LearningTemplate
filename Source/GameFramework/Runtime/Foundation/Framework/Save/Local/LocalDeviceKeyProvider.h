/**
 * @file LocalDeviceKeyProvider.h
 * @brief 장치 키입니다. 처음 쓸 때 난수 32 B 를 사용자 데이터 폴더의 파일에 두고 그 뒤로는 읽습니다. Windows 는 DPAPI 로 감싸고(이 PC 의 이 사용자만 복호화한다),
 *        리눅스는 소유자만 읽는 파일(0600)입니다.
 * @details 실수와 가벼운 변조를 막는 용도다. 키가 그 PC 에 있으므로 치트는 막지 못한다(경쟁 데이터의 원본은 서버). 키 파일을 잃으면 봉인된 슬롯은 WrongKey 다.
 *          풀리지 않는 키 파일(0 바이트 · 쓰레기 · 다른 사용자)은 `.unreadable` 을 붙여 옆에 남기고 새 키를 만든다 — 그 사용자가 저장을 영영 못 하게 멈추지 않는다.
 *          키 파일은 `FileUtil::writeFileDurable` 로 쓴다(봉투 없는 파일이라 전원이 나가 내용이 비면 위의 복구로만 돌아온다).
 *          계정 단위 키(서버가 준 것)를 쓰려면 다른 `ILocalStoreKeyProvider` 를 준다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalStore.h"

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
        /** @brief 풀리지 않는 키 파일(0 바이트 · 쓰레기 · 다른 사용자)을 옆에 남길 때 붙이는 뒷이름입니다. */
        static constexpr const utf8* kUnreadableKeySuffix = ".unreadable";

        /** @brief @p keyFilePath 에 키를 두고, 새 키의 난수는 @p pSecurityProvider 에서 얻는다(빌려 쓴다). */
        LocalDeviceKeyProvider( string_view keyFilePath, INetSecurityProvider* pSecurityProvider );
        ~LocalDeviceKeyProvider() override;

        [[nodiscard]] bool getSealKey( uint8 ( &outKey )[kKeySize] ) override;

    private:
        /**
         * @brief 키를 읽거나 만들어 둡니다. 실패하면 그 이유입니다 — 로그는 부르는 쪽이 잠금을 놓은 뒤 남긴다.
         * @param outSetAsidePath 풀리지 않는 키 파일을 옆으로 옮기고 새 키를 만들었으면 옮긴 경로(부르는 쪽이 경고한다)
         */
        [[nodiscard]] expected<void> loadOrCreateKeyLocked( string& outSetAsidePath ) SW_REQUIRES_LOCK( _mutex );

    private:
        string                _keyFilePath;
        mutable mutex         _mutex;
        INetSecurityProvider* _pSecurityProvider;
        uint8                 _arrKey[kKeySize] SW_GUARDED_BY( _mutex );
        uint8 _bLoaded        SW_GUARDED_BY( _mutex );
    };
} // namespace sw
