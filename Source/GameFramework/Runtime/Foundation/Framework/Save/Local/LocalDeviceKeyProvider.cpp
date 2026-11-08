#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalDeviceKeyProvider.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
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
        std::memset( _arrKey, 0, sizeof( _arrKey ) );
    }

    bool LocalDeviceKeyProvider::getSealKey( uint8 ( &outKey )[kKeySize] )
    {
        optional<Error> failure;
        {
            ScopedLock<mutex> lock{ _mutex };
            expected<void>    loaded = _bLoaded == SW_TRUE ? expected<void>{} : loadOrCreateKeyLocked();
            if ( loaded.has_value() )
            {
                std::memcpy( outKey, _arrKey, sizeof( _arrKey ) );
                return true;
            }
            failure = std::move( loaded ).error();
        }
        // 로그는 잠금을 놓은 뒤 남긴다(잠금은 잎 잠금, 결정 5-3).
        SW_LOG_ERROR_ID( "LocalDeviceKeyProvider.KeyUnavailable", "%# (%#)", failure->message(), failure->detail() );
        return false;
    }

    expected<void> LocalDeviceKeyProvider::loadOrCreateKeyLocked()
    {
        using Internal = LocalDeviceKeyProviderInternal;
        if ( FileUtil::isRegularFile( _keyFilePath ) )
        {
            vector<uint8> bytes;
            if ( FileUtil::readFile( _keyFilePath, bytes ) == false || Internal::unprotectKey( bytes, _arrKey, kKeySize ) == false )
            {
                // 다른 PC · 다른 사용자에서 온 키 파일이다 — 새로 만들지 않는다(그러면 이 키로 봉인한 슬롯을 영영 못 푼다). 슬롯은 WrongKey 가 아니라 IOError 다.
                return unexpected( Error{ ErrorKind::ExternalFailure, "Local store key file cannot be opened on this device/user", _keyFilePath } );
            }
            _bLoaded = SW_TRUE;
            return {};
        }
        if ( _pSecurityProvider == nullptr || _pSecurityProvider->fillRandomBytes( _arrKey, kKeySize ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "Could not generate a local store key (no security provider or no random bytes)" } );
        vector<uint8> bytes;
        const bool    bWritten = Internal::protectKey( _arrKey, kKeySize, bytes ) && FileUtil::ensureParentDirectoryExists( _keyFilePath ) &&
                              FileUtil::writeFile( _keyFilePath, bytes.data(), bytes.size() );
        if ( bWritten == false )
        {
            return unexpected( Error{ ErrorKind::ExternalFailure, "Could not write the local store key file", _keyFilePath } );
        }
        Internal::restrictToOwner( _keyFilePath );
        _bLoaded = SW_TRUE;
        return {};
    }
} // namespace sw
