#include "pch.h"

#include "ModuleManager/ModuleImagePatch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct ModuleImagePatchInternal
        {
            static constexpr uint64 kElfHeaderSize     = 64;
            static constexpr uint64 kProgramHeaderSize = 56;
            static constexpr uint64 kDynamicEntrySize  = 16;
            static constexpr uint32 kSegmentLoad       = 1;  ///< PT_LOAD
            static constexpr uint32 kSegmentDynamic    = 2;  ///< PT_DYNAMIC
            static constexpr int64  kTagNull           = 0;  ///< DT_NULL
            static constexpr int64  kTagStringTable    = 5;  ///< DT_STRTAB
            static constexpr int64  kTagStringSize     = 10; ///< DT_STRSZ
            static constexpr uint32 kGenerationDigits  = 4;
            static constexpr uint32 kLibPrefixLength   = 3; ///< "lib"
            // 헤더 안 필드 위치(ELF64). 이름을 붙여 두면 "왜 0x38 인가" 를 표준 문서와 대조할 수 있다.
            static constexpr uint64 kHeaderProgramOffset = 0x20; ///< e_phoff
            static constexpr uint64 kHeaderProgramSize   = 0x36; ///< e_phentsize
            static constexpr uint64 kHeaderProgramCount  = 0x38; ///< e_phnum
            static constexpr uint64 kProgramFileOffset   = 8;    ///< p_offset
            static constexpr uint64 kProgramAddress      = 16;   ///< p_vaddr
            static constexpr uint64 kProgramFileSize     = 32;   ///< p_filesz
            static constexpr uint64 kDynamicValue        = 8;    ///< d_val

            /** @brief 동적 섹션과 문자열 표의 **파일 안** 위치입니다. */
            struct DynamicView
            {
                uint64 _dynamicOffset{ 0 };
                uint64 _dynamicSize{ 0 };
                uint64 _stringOffset{ 0 };
                uint64 _stringSize{ 0 };
            };

            template <typename T>
            [[nodiscard]] static bool readAt( const vector<uint8>& bytes, uint64 offset, T& outValue )
            {
                if ( offset > bytes.size() || bytes.size() - offset < sizeof( T ) )
                    return false;
                Memory::copy( &outValue, bytes.data() + offset, sizeof( T ) );
                return true;
            }

            /** @brief 가상 주소를 PT_LOAD 세그먼트로 파일 위치로 바꿉니다. 어느 세그먼트에도 없으면 false 입니다. */
            static bool mapAddressToOffset( const vector<uint8>& bytes, uint64 programHeaderOffset, uint16 programHeaderCount,
                                            uint16 programHeaderSize, uint64 address, uint64& outOffset )
            {
                for ( uint16 headerIndex = 0; headerIndex < programHeaderCount; ++headerIndex )
                {
                    const uint64 base = programHeaderOffset + static_cast<uint64>( headerIndex ) * programHeaderSize;
                    uint32       type{ 0 };
                    uint64       fileOffset{ 0 };
                    uint64       virtualAddress{ 0 };
                    uint64       fileSize{ 0 };
                    const bool   bRead = readAt( bytes, base, type ) && readAt( bytes, base + kProgramFileOffset, fileOffset ) &&
                                       readAt( bytes, base + kProgramAddress, virtualAddress ) && readAt( bytes, base + kProgramFileSize, fileSize );
                    if ( bRead == false || type != kSegmentLoad )
                        continue;
                    if ( virtualAddress <= address && address < virtualAddress + fileSize )
                    {
                        outOffset = address - virtualAddress + fileOffset;
                        return true;
                    }
                }
                return false;
            }

            /** @brief ELF64 LE 인지 보고, 동적 섹션과 문자열 표의 파일 위치를 찾습니다. */
            [[nodiscard]] static bool findDynamic( const vector<uint8>& bytes, DynamicView& outView )
            {
                if ( bytes.size() < kElfHeaderSize )
                    return false;
                const bool bElfMagic = bytes[0] == 0x7F && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F';
                const bool b64Little = bytes[4] == 2 && bytes[5] == 1; // ELFCLASS64 · ELFDATA2LSB
                if ( bElfMagic == false || b64Little == false )
                    return false;

                uint64     programHeaderOffset{ 0 };
                uint16     programHeaderSize{ 0 };
                uint16     programHeaderCount{ 0 };
                const bool bHeaderRead = readAt( bytes, kHeaderProgramOffset, programHeaderOffset ) &&
                                         readAt( bytes, kHeaderProgramSize, programHeaderSize ) &&
                                         readAt( bytes, kHeaderProgramCount, programHeaderCount );
                if ( bHeaderRead == false || programHeaderSize < kProgramHeaderSize )
                    return false;

                bool bFoundDynamic = false;
                for ( uint16 headerIndex = 0; headerIndex < programHeaderCount; ++headerIndex )
                {
                    const uint64 base = programHeaderOffset + static_cast<uint64>( headerIndex ) * programHeaderSize;
                    uint32       type{ 0 };
                    if ( readAt( bytes, base, type ) == false || type != kSegmentDynamic )
                        continue;
                    bFoundDynamic = readAt( bytes, base + kProgramFileOffset, outView._dynamicOffset ) && readAt( bytes, base + kProgramFileSize, outView._dynamicSize );
                    break;
                }
                if ( bFoundDynamic == false )
                    return false;

                uint64 stringAddress{ 0 };
                bool   bHasStringTable = false;
                for ( uint64 entryOffset = 0; entryOffset + kDynamicEntrySize <= outView._dynamicSize; entryOffset += kDynamicEntrySize )
                {
                    int64  tag{ 0 };
                    uint64 value{ 0 };
                    if ( readAt( bytes, outView._dynamicOffset + entryOffset, tag ) == false || readAt( bytes, outView._dynamicOffset + entryOffset + kDynamicValue, value ) == false )
                        return false;
                    if ( tag == kTagNull )
                        break;
                    if ( tag == kTagStringTable )
                    {
                        stringAddress   = value;
                        bHasStringTable = true;
                    }
                    else if ( tag == kTagStringSize )
                    {
                        outView._stringSize = value;
                    }
                }
                if ( bHasStringTable == false || outView._stringSize == 0 )
                    return false;
                if ( mapAddressToOffset( bytes, programHeaderOffset, programHeaderCount, programHeaderSize, stringAddress, outView._stringOffset ) == false )
                    return false;
                return outView._stringOffset <= bytes.size() && bytes.size() - outView._stringOffset >= outView._stringSize;
            }

            // PE32+ 필드 위치(Microsoft PE/COFF 사양). 이름을 붙여 두면 사양 표와 대조할 수 있다.
            static constexpr uint64 kDosNewHeaderOffset       = 0x3C;   ///< e_lfanew
            static constexpr uint32 kPeSignature              = 0x4550; ///< "PE\0\0"
            static constexpr uint64 kCoffSectionCount         = 4 + 2;  ///< 서명 뒤 NumberOfSections
            static constexpr uint64 kCoffOptionalHeaderSize   = 4 + 16; ///< 서명 뒤 SizeOfOptionalHeader
            static constexpr uint64 kCoffHeaderEnd            = 4 + 20; ///< 서명 + COFF 헤더 = 선택 헤더의 시작
            static constexpr uint16 kOptionalMagicPe32Plus    = 0x20B;
            static constexpr uint64 kOptionalDirectoryCount   = 108; ///< NumberOfRvaAndSizes(PE32+)
            static constexpr uint64 kOptionalDirectoryBegin   = 112; ///< DataDirectory[0](PE32+)
            static constexpr uint64 kDirectoryEntrySize       = 8;
            static constexpr uint32 kDirectoryImport          = 1;  ///< IMAGE_DIRECTORY_ENTRY_IMPORT
            static constexpr uint32 kDirectoryDelayImport     = 13; ///< IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT
            static constexpr uint64 kSectionHeaderSize        = 40;
            static constexpr uint64 kSectionVirtualSize       = 8;
            static constexpr uint64 kSectionVirtualAddress    = 12;
            static constexpr uint64 kSectionRawSize           = 16;
            static constexpr uint64 kSectionRawOffset         = 20;
            static constexpr uint64 kImportDescriptorSize     = 20; ///< IMAGE_IMPORT_DESCRIPTOR
            static constexpr uint64 kImportDescriptorName     = 12;
            static constexpr uint64 kImportDescriptorThunk    = 16;   ///< FirstThunk
            static constexpr uint64 kDelayDescriptorSize      = 32;   ///< IMAGE_DELAYLOAD_DESCRIPTOR
            static constexpr uint64 kDelayDescriptorName      = 4;    ///< DllNameRVA
            static constexpr uint32 kMaxImportDescriptorCount = 4096; ///< 깨진 표가 끝없이 돌지 않게

            /** @brief PE32+ 의 섹션 표 위치와 import 디렉터리 둘입니다. */
            struct PeView
            {
                uint64 _sectionOffset{ 0 };
                uint32 _importRva{ 0 };
                uint32 _delayImportRva{ 0 };
                uint16 _sectionCount{ 0 };
            };

            /** @brief PE32+ 인지 보고, 섹션 표와 두 import 디렉터리의 RVA 를 찾습니다. */
            [[nodiscard]] static bool findPe( const vector<uint8>& bytes, PeView& outView )
            {
                const bool bNotPeImage = bytes.size() < kDosNewHeaderOffset + 4 || bytes[0] != 'M' || bytes[1] != 'Z';
                if ( bNotPeImage )
                    return false;
                uint32     peOffset{ 0 };
                uint32     signature{ 0 };
                const bool bInvalidPeSignature = readAt( bytes, kDosNewHeaderOffset, peOffset ) == false || readAt( bytes, peOffset, signature ) == false || signature != kPeSignature;
                if ( bInvalidPeSignature )
                    return false;
                uint16     optionalHeaderSize{ 0 };
                uint16     magic{ 0 };
                uint32     directoryCount{ 0 };
                const bool bHeaderRead = readAt( bytes, peOffset + kCoffSectionCount, outView._sectionCount ) &&
                                         readAt( bytes, peOffset + kCoffOptionalHeaderSize, optionalHeaderSize ) &&
                                         readAt( bytes, peOffset + kCoffHeaderEnd, magic ) &&
                                         readAt( bytes, peOffset + kCoffHeaderEnd + kOptionalDirectoryCount, directoryCount );
                if ( bHeaderRead == false || magic != kOptionalMagicPe32Plus )
                    return false;
                const uint64 directoryBase = peOffset + kCoffHeaderEnd + kOptionalDirectoryBegin;
                if ( directoryCount > kDirectoryImport && readAt( bytes, directoryBase + kDirectoryImport * kDirectoryEntrySize, outView._importRva ) == false )
                    return false;
                if ( directoryCount > kDirectoryDelayImport &&
                     readAt( bytes, directoryBase + kDirectoryDelayImport * kDirectoryEntrySize, outView._delayImportRva ) == false )
                    return false;
                outView._sectionOffset = peOffset + kCoffHeaderEnd + optionalHeaderSize;
                return outView._sectionOffset + static_cast<uint64>( outView._sectionCount ) * kSectionHeaderSize <= bytes.size();
            }

            /** @brief RVA 를 섹션 표로 파일 위치로 바꿉니다. 파일에 실린 바이트가 아니면 false 입니다. */
            static bool mapRvaToOffset( const vector<uint8>& bytes, const PeView& view, uint64 rva, uint64& outOffset )
            {
                for ( uint16 sectionIndex = 0; sectionIndex < view._sectionCount; ++sectionIndex )
                {
                    const uint64 base = view._sectionOffset + static_cast<uint64>( sectionIndex ) * kSectionHeaderSize;
                    uint32       virtualSize{ 0 };
                    uint32       virtualAddress{ 0 };
                    uint32       rawSize{ 0 };
                    uint32       rawOffset{ 0 };
                    const bool   bRead = readAt( bytes, base + kSectionVirtualSize, virtualSize ) && readAt( bytes, base + kSectionVirtualAddress, virtualAddress ) &&
                                       readAt( bytes, base + kSectionRawSize, rawSize ) && readAt( bytes, base + kSectionRawOffset, rawOffset );
                    if ( bRead == false )
                        return false;
                    const uint64 extent = ( virtualSize > rawSize ) ? virtualSize : rawSize;
                    if ( virtualAddress <= rva && rva < virtualAddress + extent )
                    {
                        if ( rva - virtualAddress >= rawSize )
                            return false;
                        outOffset = rva - virtualAddress + rawOffset;
                        return outOffset < bytes.size();
                    }
                }
                return false;
            }

            /** @brief 파일 위치 @p offset 에서 시작하는 NUL 로 끝나는 문자열입니다. 파일 끝까지 NUL 이 없으면 빈 뷰입니다. */
            static string_view cStringAt( const vector<uint8>& bytes, uint64 offset )
            {
                if ( offset >= bytes.size() )
                    return {};
                const utf8*  pBegin = reinterpret_cast<const utf8*>( bytes.data() + offset );
                const uint64 limit  = bytes.size() - offset;
                uint64       length{ 0 };
                while ( length < limit && pBegin[length] != '\0' )
                {
                    ++length;
                }
                if ( length == limit )
                    return {};
                return string_view{ pBegin, static_cast<size_t>( length ) };
            }

            /**
             * @brief @p table 의 서술자마다 DLL 이름 문자열의 파일 위치를 모읍니다.
             * @return 표를 끝까지 읽었으면 true(표가 없으면 빈 목록). 이름이 파일 밖을 가리키면 false 입니다.
             */
            [[nodiscard]] static bool collectImportNameOffsets( const vector<uint8>& bytes, const PeView& view, ModuleImagePatch::ImportTable table, vector<uint64>& outListOffset )
            {
                outListOffset.clear();
                const bool   bDelay         = table == ModuleImagePatch::ImportTable::Delay;
                const uint32 directoryRva   = bDelay ? view._delayImportRva : view._importRva;
                const uint64 descriptorSize = bDelay ? kDelayDescriptorSize : kImportDescriptorSize;
                const uint64 nameField      = bDelay ? kDelayDescriptorName : kImportDescriptorName;
                if ( directoryRva == 0 )
                    return true;
                uint64 directoryOffset{ 0 };
                if ( mapRvaToOffset( bytes, view, directoryRva, directoryOffset ) == false )
                    return false;
                for ( uint32 descriptorIndex = 0; descriptorIndex < kMaxImportDescriptorCount; ++descriptorIndex )
                {
                    const uint64 descriptor = directoryOffset + static_cast<uint64>( descriptorIndex ) * descriptorSize;
                    uint32       nameRva{ 0 };
                    uint32       firstThunkRva{ 0 };
                    if ( readAt( bytes, descriptor + nameField, nameRva ) == false )
                        return false;
                    // 일반 import 표는 모두 0 인 서술자로, 지연 import 표는 이름 칸이 0 인 서술자로 끝난다.
                    if ( bDelay == false && readAt( bytes, descriptor + kImportDescriptorThunk, firstThunkRva ) == false )
                        return false;
                    if ( nameRva == 0 && firstThunkRva == 0 )
                        return true;
                    uint64 nameOffset{ 0 };
                    if ( mapRvaToOffset( bytes, view, nameRva, nameOffset ) == false || cStringAt( bytes, nameOffset ).empty() )
                        return false;
                    outListOffset.push_back( nameOffset );
                }
                return false;
            }

            /** @brief 문자열 표 안의 @p index 에서 시작하는 NUL 로 끝나는 문자열입니다. 표를 벗어나면 빈 뷰입니다. */
            static string_view stringAt( const vector<uint8>& bytes, const DynamicView& view, uint64 index )
            {
                if ( index >= view._stringSize )
                    return {};
                const utf8*  pBegin = reinterpret_cast<const utf8*>( bytes.data() + view._stringOffset + index );
                const uint64 limit  = view._stringSize - index;
                uint64       length{ 0 };
                while ( length < limit && pBegin[length] != '\0' )
                {
                    ++length;
                }
                if ( length == limit )
                    return {};
                return string_view{ pBegin, static_cast<size_t>( length ) };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ModuleImagePatch::readSoname( const vector<uint8>& bytes, string& outSoname )
    {
        ModuleImagePatchInternal::DynamicView view{};
        if ( ModuleImagePatchInternal::findDynamic( bytes, view ) == false )
            return false;

        for ( uint64 entryOffset = 0; entryOffset + ModuleImagePatchInternal::kDynamicEntrySize <= view._dynamicSize;
              entryOffset += ModuleImagePatchInternal::kDynamicEntrySize )
        {
            int64  tag{ 0 };
            uint64 value{ 0 };
            // 잘린 이미지는 거기서 끝난 것으로 본다(DT_NULL 과 같다).
            const bool bRead = ModuleImagePatchInternal::readAt( bytes, view._dynamicOffset + entryOffset, tag ) &&
                               ModuleImagePatchInternal::readAt( bytes, view._dynamicOffset + entryOffset + ModuleImagePatchInternal::kDynamicValue, value );
            if ( bRead == false || tag == ModuleImagePatchInternal::kTagNull )
                break;
            if ( tag != kTagSoname )
                continue;
            const string_view soname = ModuleImagePatchInternal::stringAt( bytes, view, value );
            if ( soname.empty() )
                return false;
            outSoname = string{ soname };
            return true;
        }
        return false;
    }

    uint32 ModuleImagePatch::replaceDynamicString( vector<uint8>& inoutBytes, int64 tag, string_view from, string_view to )
    {
        if ( from.empty() || from.size() != to.size() )
            return 0;

        ModuleImagePatchInternal::DynamicView view{};
        if ( ModuleImagePatchInternal::findDynamic( inoutBytes, view ) == false )
            return 0;

        uint32 replacedCount{ 0 };
        for ( uint64 entryOffset = 0; entryOffset + ModuleImagePatchInternal::kDynamicEntrySize <= view._dynamicSize;
              entryOffset += ModuleImagePatchInternal::kDynamicEntrySize )
        {
            int64  entryTag{ 0 };
            uint64 value{ 0 };
            // 잘린 이미지는 거기서 끝난 것으로 본다(DT_NULL 과 같다).
            const bool bRead = ModuleImagePatchInternal::readAt( inoutBytes, view._dynamicOffset + entryOffset, entryTag ) &&
                               ModuleImagePatchInternal::readAt( inoutBytes, view._dynamicOffset + entryOffset + ModuleImagePatchInternal::kDynamicValue, value );
            if ( bRead == false || entryTag == ModuleImagePatchInternal::kTagNull )
                break;
            if ( entryTag != tag || ModuleImagePatchInternal::stringAt( inoutBytes, view, value ) != from )
                continue;
            Memory::copy( inoutBytes.data() + view._stringOffset + value, to.data(), to.size() );
            ++replacedCount;
        }
        return replacedCount;
    }

    bool ModuleImagePatch::findEngineABIStamp( const vector<uint8>& bytes, string& outStamp )
    {
        const string_view view{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() };
        const string_view marker{ kEngineABIStampMarker };
        // 표식 문자열 자체가 다른 자리(예: 이 함수가 든 모듈의 상수)에도 있을 수 있다. 뒤에 16진 40 글자가 온전히 붙은 것을 찾을 때까지 넘긴다.
        for ( size_t markerPos = view.find( marker ); markerPos != string_view::npos; markerPos = view.find( marker, markerPos + 1 ) )
        {
            if ( view.size() - markerPos < marker.size() + kEngineABIStampDigits )
                return false;
            const string_view digits = view.substr( markerPos + marker.size(), kEngineABIStampDigits );
            bool              bAllHex{ true };
            for ( const utf8 digit : digits )
            {
                const bool bHexDigit = ( '0' <= digit && digit <= '9' ) || ( 'a' <= digit && digit <= 'f' );
                if ( bHexDigit == false )
                {
                    bAllHex = false;
                    break;
                }
            }
            if ( bAllHex == false )
                continue;
            outStamp = string{ view.substr( markerPos, marker.size() + kEngineABIStampDigits ) };
            return true;
        }
        return false;
    }

    string ModuleImagePatch::makeGenerationName( string_view libraryName, uint32 generation )
    {
        size_t     extensionPos           = libraryName.find( ".so" );
        const bool bShouldTryDLLExtension = extensionPos == string_view::npos && libraryName.size() > 4 && StringUtil::endsWith( libraryName, ".dll", true );
        if ( bShouldTryDLLExtension )
            extensionPos = libraryName.size() - 4;
        if ( extensionPos == string_view::npos )
            return {};
        // 접두어(`lib`) 뒤에 원래 이름이 한 글자는 남아야 세대 이름에서 모듈을 알아본다.
        const size_t prefixLength = StringUtil::startsWith( libraryName, "lib" ) ? ModuleImagePatchInternal::kLibPrefixLength : 0;
        const size_t minimumStem  = prefixLength + 1 + ModuleImagePatchInternal::kGenerationDigits;
        if ( extensionPos < minimumStem )
            return {};

        constexpr const utf8* kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
        string                name{ libraryName };
        uint32                remaining = generation;
        for ( uint32 digitIndex = 0; digitIndex < ModuleImagePatchInternal::kGenerationDigits; ++digitIndex )
        {
            name[extensionPos - 1 - digitIndex] = kDigits[remaining % 36];
            remaining /= 36;
        }
        return name;
    }

    bool ModuleImagePatch::readImportDLLNames( const vector<uint8>& bytes, ImportTable table, vector<string>& outListDLLName )
    {
        outListDLLName.clear();
        ModuleImagePatchInternal::PeView view{};
        vector<uint64>                   listOffset;
        if ( ModuleImagePatchInternal::findPe( bytes, view ) == false || ModuleImagePatchInternal::collectImportNameOffsets( bytes, view, table, listOffset ) == false )
            return false;
        for ( const uint64 offset : listOffset )
        {
            outListDLLName.push_back( string{ ModuleImagePatchInternal::cStringAt( bytes, offset ) } );
        }
        return true;
    }

    bool ModuleImagePatch::hasImportDirectory( const vector<uint8>& bytes, ImportTable table )
    {
        ModuleImagePatchInternal::PeView view{};
        if ( ModuleImagePatchInternal::findPe( bytes, view ) == false )
            return false;
        return ( table == ImportTable::Delay ? view._delayImportRva : view._importRva ) != 0;
    }

    uint32 ModuleImagePatch::replaceImportDLLName( vector<uint8>& inoutBytes, string_view from, string_view to )
    {
        if ( from.empty() || from.size() != to.size() )
            return 0;
        ModuleImagePatchInternal::PeView view{};
        vector<uint64>                   listOffset;
        if ( ModuleImagePatchInternal::findPe( inoutBytes, view ) == false ||
             ModuleImagePatchInternal::collectImportNameOffsets( inoutBytes, view, ImportTable::Load, listOffset ) == false )
            return 0;

        uint32 replacedCount{ 0 };
        for ( const uint64 offset : listOffset )
        {
            // Windows 로더는 DLL 이름을 대소문자 없이 맞춘다 — 링커가 적은 철자(`gameframework.dll` 일 수도 있다)와 무관하게 바꾼다.
            if ( StringUtil::equals( ModuleImagePatchInternal::cStringAt( inoutBytes, offset ), from, true ) == false )
                continue;
            Memory::copy( inoutBytes.data() + offset, to.data(), to.size() );
            ++replacedCount;
        }
        return replacedCount;
    }
} // namespace sw
