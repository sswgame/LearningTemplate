#include "pch.h"

#include "GameFramework/Base/Online/Local/LocalDeviceKeyProvider.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include <cstring>

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <dpapi.h>
#elif defined( SW_PLATFORM_LINUX )
    #include <sys/stat.h>
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
#else
                outBytes.assign( pKey, pKey + keySize );
                return true;
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
#else
                if ( bytes.size() != static_cast<size_t>( keySize ) )
                    return false;
                std::memcpy( pOutKey, bytes.data(), static_cast<size_t>( keySize ) );
                return true;
#endif
            }

            static void restrictToOwner( const string& path )
            {
#if defined( SW_PLATFORM_LINUX )
                if ( ::chmod( path.c_str(), S_IRUSR | S_IWUSR ) != 0 )
                    SW_LOG_WARNING( "Could not restrict the local store key file to its owner: %#", path.c_str() );
#else
                (void)path; // DPAPI 가 이미 이 사용자에게만 풀린다
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

    LocalDeviceKeyProvider::~LocalDeviceKeyProvider() { std::memset( _arrKey, 0, sizeof( _arrKey ) ); }

    bool LocalDeviceKeyProvider::getSealKey( uint8 ( &outKey )[kKeySize] )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bLoaded == SW_FALSE && loadOrCreateKeyLocked() == false )
            return false;
        std::memcpy( outKey, _arrKey, sizeof( _arrKey ) );
        return true;
    }

    bool LocalDeviceKeyProvider::loadOrCreateKeyLocked()
    {
        using Internal = LocalDeviceKeyProviderInternal;
        if ( FileUtil::isRegularFile( _keyFilePath ) )
        {
            vector<uint8> bytes;
            if ( FileUtil::readFile( _keyFilePath, bytes ) == false || Internal::unprotectKey( bytes, _arrKey, kKeySize ) == false )
            {
                // 다른 PC · 다른 사용자에서 온 키 파일이다 — 새로 만들지 않는다(그러면 이 키로 봉인한 슬롯을 영영 못 푼다). 슬롯은 WrongKey 가 아니라 IOError 다.
                SW_LOG_ERROR( "Local store key file cannot be opened on this device/user: %#", _keyFilePath.c_str() );
                return false;
            }
            _bLoaded = SW_TRUE;
            return true;
        }
        if ( _pSecurityProvider == nullptr || _pSecurityProvider->fillRandomBytes( _arrKey, kKeySize ) == false )
            return false;
        vector<uint8> bytes;
        const bool    bWritten = Internal::protectKey( _arrKey, kKeySize, bytes ) && FileUtil::ensureParentDirectoryExists( _keyFilePath ) &&
                              FileUtil::writeFile( _keyFilePath, bytes.data(), bytes.size() );
        if ( bWritten == false )
        {
            SW_LOG_ERROR( "Could not write the local store key file: %#", _keyFilePath.c_str() );
            return false;
        }
        Internal::restrictToOwner( _keyFilePath );
        _bLoaded = SW_TRUE;
        return true;
    }
} // namespace sw
