#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalDeviceKeyProvider.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include <cstring>

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"

    #include <dpapi.h>
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
    #include <sys/stat.h>
#else
    #error "Unsupported platform"
#endif

namespace sw
{
    namespace
    {
        struct LocalDeviceKeyProviderInternal
        {
            /** @brief 키를 파일에 둘 바이트로 감쌉니다(Windows DPAPI — 이 사용자만 푼다, 그 밖은 그대로). */
            [[nodiscard]] static bool protectKey( const uint8* pKey, int32 keySize, vector<uint8>& outBytes )
            {
#if defined( SW_PLATFORM_WINDOWS )
                DATA_BLOB input{ static_cast<DWORD>( keySize ), const_cast<BYTE*>( pKey ) };
                DATA_BLOB output{};
                if ( CryptProtectData( &input, L"SWEngine local store key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output ) == FALSE )
                    return false;
                outBytes.assign( output.pbData, output.pbData + output.cbData );
                LocalFree( output.pbData );
                return true;
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
                // OS 키 보관소 없이 소유자 전용 파일로 둔다 — Android · 웹은 앱 샌드박스가 다른 앱에서 가린다(유니티 PlayerPrefs 와 같은 수준).
                outBytes.assign( pKey, pKey + keySize );
                return true;
#else
    #error "Unsupported platform"
#endif
            }

            [[nodiscard]] static bool unprotectKey( const vector<uint8>& bytes, uint8* pOutKey, int32 keySize )
            {
#if defined( SW_PLATFORM_WINDOWS )
                DATA_BLOB input{ static_cast<DWORD>( bytes.size() ), const_cast<BYTE*>( bytes.data() ) };
                DATA_BLOB output{};
                if ( CryptUnprotectData( &input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output ) == FALSE )
                    return false;
                const bool bSized = output.cbData == static_cast<DWORD>( keySize );
                if ( bSized )
                    std::memcpy( pOutKey, output.pbData, static_cast<size_t>( keySize ) );
                SecureZeroMemory( output.pbData, output.cbData );
                LocalFree( output.pbData );
                return bSized;
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
                if ( bytes.size() != static_cast<size_t>( keySize ) )
                    return false;
                std::memcpy( pOutKey, bytes.data(), static_cast<size_t>( keySize ) );
                return true;
#else
    #error "Unsupported platform"
#endif
            }

            static void restrictToOwner( const string& path )
            {
#if defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
                if ( ::chmod( path.c_str(), S_IRUSR | S_IWUSR ) != 0 )
                    SW_LOG_WARNING_ID( "LocalDeviceKeyProvider.CouldNotRestrictLocal", "Could not restrict the local store key file to its owner: %#", path.c_str() );
#elif defined( SW_PLATFORM_WINDOWS )
                (void)path; // DPAPI 가 이미 이 사용자에게만 풀린다
#else
    #error "Unsupported platform"
#endif
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LocalDeviceKeyProvider::LocalDeviceKeyProvider( string_view keyFilePath, INetSecurityProvider* pSecurityProvider )
        : _keyFilePath{ keyFilePath }
        , _mutex{}
        , _pSecurityProvider{ pSecurityProvider }
        , _arrKey{}
        , _bLoaded{ SW_FALSE }
    {
    }

    LocalDeviceKeyProvider::~LocalDeviceKeyProvider()
    {
        Memory::wipe( _arrKey, sizeof( _arrKey ) );
    }

    bool LocalDeviceKeyProvider::getSealKey( uint8 ( &outKey )[kKeySize] )
    {
        optional<Error> failure;
        string          setAsidePath;
        {
            ScopedLock<mutex> lock{ _mutex };
            expected<void>    loaded = _bLoaded == SW_TRUE ? expected<void>{} : loadOrCreateKeyLocked( setAsidePath );
            if ( loaded.has_value() )
                std::memcpy( outKey, _arrKey, sizeof( _arrKey ) );
            else
                failure = std::move( loaded ).error();
        }
        // 로그는 잠금을 놓은 뒤 남긴다(잠금은 잎 잠금, 결정 5-3).
        if ( setAsidePath.empty() == false )
            SW_LOG_WARNING_ID( "LocalDeviceKeyProvider.UnreadableKeyReplaced", "The local store key file could not be opened on this device/user - kept it as '%#' and made a new key (slots sealed with the old key read as WrongKey)",
                               setAsidePath.c_str() );
        if ( failure.has_value() == false )
            return true;
        SW_LOG_ERROR_ID( "LocalDeviceKeyProvider.KeyUnavailable", "%# (%#)", failure->message(), failure->detail() );
        return false;
    }

    expected<void> LocalDeviceKeyProvider::loadOrCreateKeyLocked( string& outSetAsidePath )
    {
        using Internal = LocalDeviceKeyProviderInternal;
        if ( FileUtil::isRegularFile( _keyFilePath ) )
        {
            vector<uint8> bytes;
            if ( FileUtil::readFile( _keyFilePath, bytes ) == false )
                return unexpected( Error{ ErrorKind::ExternalFailure, "Local store key file could not be read", _keyFilePath } ); // 잠김 · 권한 — 다음 요청이 다시 읽는다
            if ( Internal::unprotectKey( bytes, _arrKey, kKeySize ) )
            {
                _bLoaded = SW_TRUE;
                return {};
            }
            // 읽었는데 풀리지 않는다 — 0 바이트 · 쓰레기(전원이 나간 쓰기) · 다른 PC · 다른 사용자의 파일이다. 오류로 멈추면 그 사용자는 파일을 지울 때까지
            // 저장도 읽기도 못 한다. 지우지 않고 옆에 남긴 뒤 새 키를 만든다 — 다른 사용자의 파일이었으면 되돌려 놓아 옛 슬롯을 다시 푼다(유니티 PlayerPrefs 가 깨진 파일을 비우는 자리보다 보수적).
            // 앞서 남긴 것도 지우지 않는다(번호를 붙인 새 이름). 새 키는 아래 쓰기가 원래 이름에 바꿔 끼운다.
            outSetAsidePath       = FileUtil::reserveUniquePath( _keyFilePath + kUnreadableKeySuffix, "", false );
            const bool bKeptAside = outSetAsidePath.empty() == false && ( bytes.empty() || FileUtil::writeFileDurable( outSetAsidePath, bytes.data(), bytes.size() ) );
            if ( bKeptAside == false )
            {
                outSetAsidePath.clear();
                return unexpected( Error{ ErrorKind::ExternalFailure, "Local store key file is unreadable and could not be set aside", _keyFilePath } );
            }
        }
        if ( _pSecurityProvider == nullptr || _pSecurityProvider->fillRandomBytes( _arrKey, kKeySize ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "Could not generate a local store key (no security provider or no random bytes)" } );
        vector<uint8> bytes;
        const bool    bWritten = Internal::protectKey( _arrKey, kKeySize, bytes ) && FileUtil::ensureParentDirectoryExists( _keyFilePath ) &&
                              FileUtil::writeFileDurable( _keyFilePath, bytes.data(), bytes.size() );
        if ( bWritten == false )
        {
            return unexpected( Error{ ErrorKind::ExternalFailure, "Could not write the local store key file", _keyFilePath } );
        }
        Internal::restrictToOwner( _keyFilePath );
        _bLoaded = SW_TRUE;
        return {};
    }
} // namespace sw
