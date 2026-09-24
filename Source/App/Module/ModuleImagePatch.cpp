#include "pch.h"

#include "App/Module/ModuleImagePatch.h"

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
            static bool readAt( const vector<uint8>& bytes, uint64 offset, T& outValue )
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
            static bool findDynamic( const vector<uint8>& bytes, DynamicView& outView )
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
            ModuleImagePatchInternal::readAt( bytes, view._dynamicOffset + entryOffset, tag );
            ModuleImagePatchInternal::readAt( bytes, view._dynamicOffset + entryOffset + ModuleImagePatchInternal::kDynamicValue, value );
            if ( tag == ModuleImagePatchInternal::kTagNull )
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
            ModuleImagePatchInternal::readAt( inoutBytes, view._dynamicOffset + entryOffset, entryTag );
            ModuleImagePatchInternal::readAt( inoutBytes, view._dynamicOffset + entryOffset + ModuleImagePatchInternal::kDynamicValue, value );
            if ( entryTag == ModuleImagePatchInternal::kTagNull )
                break;
            if ( entryTag != tag || ModuleImagePatchInternal::stringAt( inoutBytes, view, value ) != from )
                continue;
            Memory::copy( inoutBytes.data() + view._stringOffset + value, to.data(), to.size() );
            ++replacedCount;
        }
        return replacedCount;
    }

    string ModuleImagePatch::makeGenerationName( string_view soname, uint32 generation )
    {
        const size_t extensionPos = soname.find( ".so" );
        if ( extensionPos == string_view::npos )
            return {};
        const size_t minimumStem = ModuleImagePatchInternal::kLibPrefixLength + ModuleImagePatchInternal::kGenerationDigits;
        if ( extensionPos < minimumStem )
            return {};

        constexpr const utf8* kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
        string                name{ soname };
        uint32                remaining = generation;
        for ( uint32 digitIndex = 0; digitIndex < ModuleImagePatchInternal::kGenerationDigits; ++digitIndex )
        {
            name[extensionPos - 1 - digitIndex] = kDigits[remaining % 36];
            remaining /= 36;
        }
        return name;
    }
} // namespace sw
