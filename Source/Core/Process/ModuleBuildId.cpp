#include "pch.h"

#include "Core/Process/ModuleBuildId.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_LINUX )
    #include <link.h>
#endif

namespace sw
{
    namespace
    {
        struct ModuleBuildIdInternal
        {
            static void appendHex( StringBuilder<constant::kMaxBuffer256>& text, const uint8* pBytes, size_t count, bool bUpper )
            {
                static constexpr utf8 kLowerDigit[] = "0123456789abcdef";
                static constexpr utf8 kUpperDigit[] = "0123456789ABCDEF";
                const utf8*           pDigit        = bUpper ? kUpperDigit : kLowerDigit;
                for ( size_t index = 0; index < count; ++index )
                {
                    const utf8 arrPair[3] = { pDigit[pBytes[index] >> 4], pDigit[pBytes[index] & 0xF], '\0' };
                    text.append( arrPair );
                }
            }

#if defined( SW_PLATFORM_WINDOWS )
            /** @brief CodeView 7(RSDS) 레코드 — 링커가 PE 디버그 디렉터리에 적는다(`/DEBUG`). */
            struct CodeViewRecord
            {
                DWORD _signature; ///< 'RSDS'
                GUID  _guid;
                DWORD _age;
                utf8  _arrPdbPath[1];
            };

            static constexpr DWORD kRsdsSignature = FourCcUtil::make( "RSDS" );
#elif defined( SW_PLATFORM_LINUX )
            struct BuildIdQuery
            {
                uintptr_t     _address{ 0 };
                ModuleBuildId _result{};
                bool          _bMain{ false };
                bool          _bFound{ false };
            };

            /** @brief 이미지가 주소를 담는가(첫 이미지 — 실행 파일 — 를 찾을 때는 늘 참). */
            static bool containsAddress( const dl_phdr_info* pInfo, uintptr_t address )
            {
                for ( uint16 headerIndex = 0; headerIndex < pInfo->dlpi_phnum; ++headerIndex )
                {
                    const ElfW( Phdr ) & header = pInfo->dlpi_phdr[headerIndex];
                    if ( header.p_type != PT_LOAD )
                        continue;
                    const uintptr_t begin = pInfo->dlpi_addr + header.p_vaddr;
                    if ( begin <= address && address < begin + header.p_memsz )
                        return true;
                }
                return false;
            }

            static int32 visitImage( dl_phdr_info* pInfo, size_t, void* pContext )
            {
                BuildIdQuery* pQuery = static_cast<BuildIdQuery*>( pContext );
                if ( pQuery->_bMain == false && containsAddress( pInfo, pQuery->_address ) == false )
                    return 0;
                pQuery->_bFound             = true;
                pQuery->_result._modulePath = ( pInfo->dlpi_name != nullptr && pInfo->dlpi_name[0] != '\0' ) ? pInfo->dlpi_name : FileUtil::getExecutablePath();
                for ( uint16 headerIndex = 0; headerIndex < pInfo->dlpi_phnum; ++headerIndex )
                {
                    const ElfW( Phdr ) & header = pInfo->dlpi_phdr[headerIndex];
                    if ( header.p_type != PT_NOTE )
                        continue;
                    const uint8* pCursor = reinterpret_cast<const uint8*>( pInfo->dlpi_addr + header.p_vaddr );
                    const uint8* pEnd    = pCursor + header.p_memsz;
                    while ( pCursor + sizeof( ElfW( Nhdr ) ) <= pEnd )
                    {
                        const ElfW( Nhdr )* pNote = reinterpret_cast<const ElfW( Nhdr )*>( pCursor );
                        const size_t nameSize     = ( pNote->n_namesz + 3u ) & ~size_t( 3 );
                        const size_t descSize     = ( pNote->n_descsz + 3u ) & ~size_t( 3 );
                        const uint8* pName        = pCursor + sizeof( ElfW( Nhdr ) );
                        const uint8* pDesc        = pName + nameSize;
                        const bool   bGnu         = pNote->n_namesz == 4 && StringUtil::equals( string_view( reinterpret_cast<const utf8*>( pName ), 3 ), "GNU" );
                        if ( pNote->n_type == NT_GNU_BUILD_ID && bGnu && pDesc + pNote->n_descsz <= pEnd )
                        {
                            StringBuilder<constant::kMaxBuffer256> text;
                            appendHex( text, pDesc, pNote->n_descsz, false );
                            pQuery->_result._id = text.c_str();
                            return 1;
                        }
                        pCursor = pDesc + descSize;
                    }
                }
                return 1;
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    ModuleBuildId ModuleBuildId::find( const void* pAddressInside )
    {
        using Internal = ModuleBuildIdInternal;
        ModuleBuildId result;
#if defined( SW_PLATFORM_WINDOWS )
        HMODULE hModule = nullptr;
        if ( pAddressInside == nullptr )
            hModule = GetModuleHandleW( nullptr );
        else if ( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>( pAddressInside ),
                                      &hModule ) == FALSE )
            return result;
        if ( hModule == nullptr )
            return result;
        utf16 arrPath[constant::kMaxPathSize]{};
        if ( GetModuleFileNameW( hModule, arrPath, constant::kMaxPathSize ) > 0 )
            result._modulePath = StringUtil::utf16ToUtf8( arrPath );
        const uint8*            pBase = reinterpret_cast<const uint8*>( hModule );
        const IMAGE_DOS_HEADER* pDos  = reinterpret_cast<const IMAGE_DOS_HEADER*>( pBase );
        if ( pDos->e_magic != IMAGE_DOS_SIGNATURE )
            return result;
        const IMAGE_NT_HEADERS* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>( pBase + pDos->e_lfanew );
        if ( pNt->Signature != IMAGE_NT_SIGNATURE )
            return result;
        const IMAGE_DATA_DIRECTORY&  directory  = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        const uint32                 entryCount = directory.Size / static_cast<uint32>( sizeof( IMAGE_DEBUG_DIRECTORY ) );
        const IMAGE_DEBUG_DIRECTORY* pEntry     = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>( pBase + directory.VirtualAddress );
        for ( uint32 entryIndex = 0; directory.VirtualAddress != 0 && entryIndex < entryCount; ++entryIndex )
        {
            if ( pEntry[entryIndex].Type != IMAGE_DEBUG_TYPE_CODEVIEW || pEntry[entryIndex].AddressOfRawData == 0 )
                continue;
            const Internal::CodeViewRecord* pRecord = reinterpret_cast<const Internal::CodeViewRecord*>( pBase + pEntry[entryIndex].AddressOfRawData );
            if ( pRecord->_signature != Internal::kRsdsSignature )
                continue;
            // symstore 의 열쇠: GUID 를 Data1 · Data2 · Data3 은 수로, Data4 는 바이트로 대문자 16진 32 자리 + age 16진.
            StringBuilder<constant::kMaxBuffer256> text;
            text.appendFormat( "%#%#%#", Fmt( static_cast<uint32>( pRecord->_guid.Data1 ), Format().hexUpper().width( 8 ).zeroPad() ),
                               Fmt( static_cast<uint32>( pRecord->_guid.Data2 ), Format().hexUpper().width( 4 ).zeroPad() ),
                               Fmt( static_cast<uint32>( pRecord->_guid.Data3 ), Format().hexUpper().width( 4 ).zeroPad() ) );
            Internal::appendHex( text, pRecord->_guid.Data4, sizeof( pRecord->_guid.Data4 ), true );
            text.appendFormat( "%#", Fmt( static_cast<uint32>( pRecord->_age ), Format().hexUpper() ) );
            result._id        = text.c_str();
            result._debugFile = pRecord->_arrPdbPath;
            break;
        }
#elif defined( SW_PLATFORM_LINUX )
        Internal::BuildIdQuery query{};
        query._address = reinterpret_cast<uintptr_t>( pAddressInside );
        query._bMain   = pAddressInside == nullptr;
        dl_iterate_phdr( &Internal::visitImage, &query );
        if ( query._bFound )
            result = query._result;
#endif
        return result;
    }
} // namespace sw
