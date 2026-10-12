/**
 * @file TestModuleImagePatch.cpp
 * @brief ModuleImagePatch — 리눅스 핫 리로드가 섀도 복사본의 SONAME · NEEDED 를 같은 길이로 바꾸는 패처.
 * @details 패처는 파일 바이트만 다루므로 Windows 에서도 돈다. 여기서는 동적 섹션만 가진 **최소 ELF64 이미지를 직접 만들어**
 *          바꿔야 할 것만 바뀌고(SONAME · 맞는 NEEDED) 나머지는 그대로인지(다른 NEEDED · 길이가 다른 요청 · ELF 가 아닌 바이트) 본다.
 *          실제 `.so` 에 대한 확인은 리눅스 CI 의 `ArchitectureTest.ReloadedDependentsBindToTheCurrentImages` 가 한다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"

#include "ModuleHost/ModuleImagePatch.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 합성 이미지가 올라간다고 치는 가상 주소입니다. 파일 위치와 달라야 주소 → 위치 변환을 제대로 본다. */
    constexpr uint64 kLoadAddress = 0x10000;

    /** @brief 바이트 버퍼의 @p offset 에 값을 적습니다. */
    template <typename T>
    void writeAt( sw::vector<uint8>& bytes, uint64 offset, T value )
    {
        sw::Memory::copy( bytes.data() + offset, &value, sizeof( T ) );
    }

    /** @brief 버퍼 안에 @p text 가 그대로 들어 있는지 봅니다. */
    bool containsText( const sw::vector<uint8>& bytes, sw::string_view text )
    {
        const sw::string_view view{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() };
        return view.find( text ) != sw::string_view::npos;
    }

    /**
     * @brief NEEDED 둘(libGameFramework.so · libEngine.so)과 SONAME(libGF_Overworld.so)을 가진 최소 ELF64 LE 이미지를 만듭니다.
     * @details 배치: ELF 헤더(64) · 프로그램 헤더 둘(PT_LOAD 는 파일 전체, PT_DYNAMIC) · 동적 항목 여섯 · 문자열 표.
     */
    sw::vector<uint8> makeSyntheticSharedObject()
    {
        constexpr uint64 kHeaderSize        = 64;
        constexpr uint64 kProgramHeaderSize = 56;
        constexpr uint64 kDynamicOffset     = kHeaderSize + 2 * kProgramHeaderSize;
        constexpr uint64 kDynamicCount      = 6;
        constexpr uint64 kDynamicSize       = kDynamicCount * 16;
        constexpr uint64 kStringOffset      = kDynamicOffset + kDynamicSize;

        const sw::string stringTable         = sw::string{ "\0libGameFramework.so\0libEngine.so\0libGF_Overworld.so\0", 54 };
        const uint64     neededGameFramework = 1;
        const uint64     neededEngine        = neededGameFramework + 20;
        const uint64     soname              = neededEngine + 13;
        const uint64     fileSize            = kStringOffset + stringTable.size();

        sw::vector<uint8> bytes( static_cast<size_t>( fileSize ), 0 );
        bytes[0] = 0x7F;
        bytes[1] = 'E';
        bytes[2] = 'L';
        bytes[3] = 'F';
        bytes[4] = 2;                     // ELFCLASS64
        bytes[5] = 1;                     // ELFDATA2LSB
        bytes[6] = 1;                     // EV_CURRENT
        writeAt<uint16>( bytes, 16, 3 );  // ET_DYN
        writeAt<uint16>( bytes, 18, 62 ); // EM_X86_64
        writeAt<uint32>( bytes, 20, 1 );
        writeAt<uint64>( bytes, 0x20, kHeaderSize );
        writeAt<uint16>( bytes, 0x36, static_cast<uint16>( kProgramHeaderSize ) );
        writeAt<uint16>( bytes, 0x38, 2 );

        const uint64 loadHeader = kHeaderSize;
        writeAt<uint32>( bytes, loadHeader, 1 ); // PT_LOAD
        writeAt<uint64>( bytes, loadHeader + 8, 0 );
        writeAt<uint64>( bytes, loadHeader + 16, kLoadAddress );
        writeAt<uint64>( bytes, loadHeader + 32, fileSize );
        writeAt<uint64>( bytes, loadHeader + 40, fileSize );

        const uint64 dynamicHeader = kHeaderSize + kProgramHeaderSize;
        writeAt<uint32>( bytes, dynamicHeader, 2 ); // PT_DYNAMIC
        writeAt<uint64>( bytes, dynamicHeader + 8, kDynamicOffset );
        writeAt<uint64>( bytes, dynamicHeader + 16, kLoadAddress + kDynamicOffset );
        writeAt<uint64>( bytes, dynamicHeader + 32, kDynamicSize );
        writeAt<uint64>( bytes, dynamicHeader + 40, kDynamicSize );

        const int64  arrTag[kDynamicCount]   = { 1, 1, 14, 5, 10, 0 }; // NEEDED · NEEDED · SONAME · STRTAB · STRSZ · NULL
        const uint64 arrValue[kDynamicCount] = { neededGameFramework, neededEngine, soname, kLoadAddress + kStringOffset, stringTable.size(), 0 };
        for ( uint64 entryIndex = 0; entryIndex < kDynamicCount; ++entryIndex )
        {
            writeAt<int64>( bytes, kDynamicOffset + entryIndex * 16, arrTag[entryIndex] );
            writeAt<uint64>( bytes, kDynamicOffset + entryIndex * 16 + 8, arrValue[entryIndex] );
        }
        sw::Memory::copy( bytes.data() + kStringOffset, stringTable.data(), stringTable.size() );
        return bytes;
    }

    /** @brief 버퍼의 @p offset 에 NUL 로 끝나는 문자열을 적습니다. */
    void writeTextAt( sw::vector<uint8>& bytes, uint64 offset, sw::string_view text )
    {
        sw::Memory::copy( bytes.data() + offset, text.data(), text.size() );
        bytes[static_cast<size_t>( offset + text.size() )] = 0;
    }

    /** @brief 합성 PE 의 섹션(RVA 0x1000 ↔ 파일 0x200)에서 RVA 를 파일 위치로 옮깁니다. */
    uint64 syntheticFileOffset( uint32 rva )
    {
        return static_cast<uint64>( rva - 0x1000u ) + 0x200u;
    }

    /**
     * @brief 일반 import 둘(GameFramework.dll · Engine.dll)과 지연 import 하나(TracyClient.dll)를 가진 최소 PE32+ 이미지를 만듭니다.
     * @details 배치: DOS 헤더(e_lfanew = 0x40) · PE 서명 · COFF 헤더(섹션 하나) · PE32+ 선택 헤더(디렉터리 16) · 섹션 표 · 섹션 하나.
     *          섹션은 RVA 0x1000 ↔ 파일 0x200 이라 RVA 를 파일 위치로 옮기는 길을 지난다.
     */
    sw::vector<uint8> makeSyntheticPortableExecutable()
    {
        constexpr uint64 kPeOffset       = 0x40;
        constexpr uint64 kOptionalOffset = kPeOffset + 4 + 20;
        constexpr uint16 kOptionalSize   = 240;
        constexpr uint64 kSectionTable   = kOptionalOffset + kOptionalSize;
        constexpr uint64 kRawOffset      = 0x200;
        constexpr uint32 kSectionRva     = 0x1000;
        constexpr uint32 kImportRva      = kSectionRva;        // 서술자 셋(둘 + 끝)
        constexpr uint32 kDelayRva       = kSectionRva + 0x80; // 서술자 둘(하나 + 끝)
        constexpr uint32 kStringRva      = kSectionRva + 0x100;
        constexpr uint64 kFileSize       = kRawOffset + 0x200;

        sw::vector<uint8> bytes( static_cast<size_t>( kFileSize ), 0 );
        bytes[0] = 'M';
        bytes[1] = 'Z';
        writeAt<uint32>( bytes, 0x3C, static_cast<uint32>( kPeOffset ) );
        writeAt<uint32>( bytes, kPeOffset, 0x4550 );     // "PE\0\0"
        writeAt<uint16>( bytes, kPeOffset + 4, 0x8664 ); // x64
        writeAt<uint16>( bytes, kPeOffset + 4 + 2, 1 );  // NumberOfSections
        writeAt<uint16>( bytes, kPeOffset + 4 + 16, kOptionalSize );
        writeAt<uint16>( bytes, kOptionalOffset, 0x20B );    // PE32+
        writeAt<uint32>( bytes, kOptionalOffset + 108, 16 ); // NumberOfRvaAndSizes
        writeAt<uint32>( bytes, kOptionalOffset + 112 + 1 * 8, kImportRva );
        writeAt<uint32>( bytes, kOptionalOffset + 112 + 13 * 8, kDelayRva );

        writeAt<uint32>( bytes, kSectionTable + 8, 0x200 ); // VirtualSize
        writeAt<uint32>( bytes, kSectionTable + 12, kSectionRva );
        writeAt<uint32>( bytes, kSectionTable + 16, 0x200 ); // SizeOfRawData
        writeAt<uint32>( bytes, kSectionTable + 20, static_cast<uint32>( kRawOffset ) );

        const uint32 gameFrameworkName = kStringRva;
        const uint32 engineName        = kStringRva + 0x20;
        const uint32 tracyName         = kStringRva + 0x40;
        writeTextAt( bytes, syntheticFileOffset( gameFrameworkName ), "GameFramework.dll" );
        writeTextAt( bytes, syntheticFileOffset( engineName ), "Engine.dll" );
        writeTextAt( bytes, syntheticFileOffset( tracyName ), "TracyClient.dll" );

        // IMAGE_IMPORT_DESCRIPTOR(20 바이트): Name +12 · FirstThunk +16. 모두 0 인 서술자가 끝이다.
        writeAt<uint32>( bytes, syntheticFileOffset( kImportRva ) + 12, gameFrameworkName );
        writeAt<uint32>( bytes, syntheticFileOffset( kImportRva ) + 16, kSectionRva + 0x180 );
        writeAt<uint32>( bytes, syntheticFileOffset( kImportRva ) + 20 + 12, engineName );
        writeAt<uint32>( bytes, syntheticFileOffset( kImportRva ) + 20 + 16, kSectionRva + 0x190 );
        // IMAGE_DELAYLOAD_DESCRIPTOR(32 바이트): Attributes +0 · DllNameRVA +4. 이름 칸이 0 인 서술자가 끝이다.
        writeAt<uint32>( bytes, syntheticFileOffset( kDelayRva ), 1 );
        writeAt<uint32>( bytes, syntheticFileOffset( kDelayRva ) + 4, tracyName );
        return bytes;
    }
} // namespace

/**
 * @brief [ModuleImagePatchTest] 세대 이름은 길이와 확장자를 지키고, 세대마다 다르며, 너무 짧은 이름은 만들지 않는다
 * @details 문자열 표를 늘리지 않고 제자리에서 덮으므로 길이가 같아야 한다. `lib` 와 확장자는 동적 링커 · 도구가 이름을 알아보는
 *          자리라 남긴다.
 */
SW_TEST_CASE( ModuleImagePatchTest, GenerationNameKeepsTheLengthAndTheExtension )
{
    const sw::string third = sw::ModuleImagePatch::makeGenerationName( "libGameFramework.so", 3 );
    SW_EXPECT_STREQ( "libGameFrame0003.so", third.c_str() );
    SW_EXPECT_EQUAL( sw::string_view{ "libGameFramework.so" }.size(), third.size() );
    SW_EXPECT_STREQ( "libGameFrame0010.so", sw::ModuleImagePatch::makeGenerationName( "libGameFramework.so", 36 ).c_str() );
    SW_EXPECT_TRUE( sw::ModuleImagePatch::makeGenerationName( "libGameFramework.so", 4 ) != third );

    SW_EXPECT_TRUE( sw::ModuleImagePatch::makeGenerationName( "libA.so", 1 ).empty() );
    SW_EXPECT_TRUE( sw::ModuleImagePatch::makeGenerationName( "libGameFramework", 1 ).empty() );
}

/**
 * @brief [ModuleImagePatchTest] SONAME 과 맞는 NEEDED 만 제자리에서 바뀌고, 다른 NEEDED 와 파일 크기는 그대로다
 */
SW_TEST_CASE( ModuleImagePatchTest, SonameAndMatchingNeededAreRewrittenInPlace )
{
    sw::vector<uint8> bytes    = makeSyntheticSharedObject();
    const size_t      sizeBase = bytes.size();

    sw::string soname;
    SW_ASSERT_TRUE( sw::ModuleImagePatch::readSoname( bytes, soname ) );
    SW_EXPECT_STREQ( "libGF_Overworld.so", soname.c_str() );

    SW_EXPECT_EQUAL( 1u, sw::ModuleImagePatch::replaceDynamicString( bytes, sw::ModuleImagePatch::kTagNeeded, "libGameFramework.so", "libGameFrame0007.so" ) );
    SW_EXPECT_TRUE( containsText( bytes, "libGameFrame0007.so" ) );
    SW_EXPECT_FALSE( containsText( bytes, "libGameFramework.so" ) );
    SW_EXPECT_TRUE( containsText( bytes, "libEngine.so" ) );

    // SONAME 은 NEEDED 요청으로는 바뀌지 않는다 — 태그가 다르다.
    SW_EXPECT_EQUAL( 0u, sw::ModuleImagePatch::replaceDynamicString( bytes, sw::ModuleImagePatch::kTagNeeded, "libGF_Overworld.so", "libGF_Overw0008.so" ) );
    SW_EXPECT_EQUAL( 1u, sw::ModuleImagePatch::replaceDynamicString( bytes, sw::ModuleImagePatch::kTagSoname, "libGF_Overworld.so", "libGF_Overw0008.so" ) );
    SW_ASSERT_TRUE( sw::ModuleImagePatch::readSoname( bytes, soname ) );
    SW_EXPECT_STREQ( "libGF_Overw0008.so", soname.c_str() );

    SW_EXPECT_EQUAL( sizeBase, bytes.size() );
}

/**
 * @brief [ModuleImagePatchTest] 길이가 다른 요청 · ELF 가 아닌 바이트 · 잘린 파일은 건드리지 않는다
 */
SW_TEST_CASE( ModuleImagePatchTest, MismatchedLengthOrForeignBytesAreLeftAlone )
{
    sw::vector<uint8>       bytes    = makeSyntheticSharedObject();
    const sw::vector<uint8> original = bytes;
    SW_EXPECT_EQUAL( 0u, sw::ModuleImagePatch::replaceDynamicString( bytes, sw::ModuleImagePatch::kTagNeeded, "libGameFramework.so", "libGF.so" ) );
    SW_EXPECT_TRUE( bytes == original );

    sw::vector<uint8> notElf( 128, 0x41 );
    sw::string        soname;
    SW_EXPECT_FALSE( sw::ModuleImagePatch::readSoname( notElf, soname ) );
    SW_EXPECT_EQUAL( 0u, sw::ModuleImagePatch::replaceDynamicString( notElf, sw::ModuleImagePatch::kTagSoname, "AAAA", "BBBB" ) );

    sw::vector<uint8> truncated( original.begin(), original.begin() + 100 );
    SW_EXPECT_FALSE( sw::ModuleImagePatch::readSoname( truncated, soname ) );
}

/**
 * @brief [ModuleImagePatchTest] 엔진 ABI 스탬프는 표식 뒤에 16진 40 글자가 온전할 때만 찾는다
 * @details 핫 리로드는 모듈 코드가 돌기 전에 파일 바이트에서 이 스탬프를 찾는다. 앞쪽의 불완전한 표식(표식 문자열 자체만 든 상수)은
 *          건너뛰고, 모자란 글자 · 16진이 아닌 글자는 스탬프로 치지 않는다.
 */
SW_TEST_CASE( ModuleImagePatchTest, EngineAbiStampIsFoundOnlyWithAFullDigest )
{
    const sw::string digest = "0123456789abcdef0123456789abcdef01234567";
    const sw::string stamp  = sw::string{ sw::ModuleImagePatch::kEngineAbiStampMarker } + digest;
    const sw::string text   = sw::string{ "junk" } + sw::ModuleImagePatch::kEngineAbiStampMarker + " more junk " + stamp + " tail";

    sw::vector<uint8> bytes( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
    sw::string        found;
    SW_ASSERT_TRUE( sw::ModuleImagePatch::findEngineAbiStamp( bytes, found ) );
    SW_EXPECT_STREQ( stamp.c_str(), found.c_str() );

    const sw::string  shortText = sw::string{ sw::ModuleImagePatch::kEngineAbiStampMarker } + "0123";
    sw::vector<uint8> shortBytes( reinterpret_cast<const uint8*>( shortText.data() ), reinterpret_cast<const uint8*>( shortText.data() ) + shortText.size() );
    SW_EXPECT_FALSE( sw::ModuleImagePatch::findEngineAbiStamp( shortBytes, found ) );

    const sw::string  upperText = sw::string{ sw::ModuleImagePatch::kEngineAbiStampMarker } + "0123456789ABCDEF0123456789ABCDEF01234567";
    sw::vector<uint8> upperBytes( reinterpret_cast<const uint8*>( upperText.data() ), reinterpret_cast<const uint8*>( upperText.data() ) + upperText.size() );
    SW_EXPECT_FALSE( sw::ModuleImagePatch::findEngineAbiStamp( upperBytes, found ) );
}

/**
 * @brief [ModuleImagePatchTest] Windows 세대 이름은 `.dll` 앞 끝 네 글자를 바꾸고 길이를 지킨다 — 다섯 글자보다 짧은 이름은 만들지 않는다
 * @details 로더가 import 를 파일 이름으로 맞추므로 복사본 파일 이름이 곧 이 이름이다(`ModuleShadowCopy::writeShadowCopy`).
 */
SW_TEST_CASE( ModuleImagePatchTest, DllGenerationNameKeepsTheLength )
{
    const sw::string third = sw::ModuleImagePatch::makeGenerationName( "GF_Mailbox.dll", 3 );
    SW_EXPECT_STREQ( "GF_Mai0003.dll", third.c_str() );
    SW_EXPECT_EQUAL( sw::string_view{ "GF_Mailbox.dll" }.size(), third.size() );
    SW_EXPECT_STREQ( "GameFrame0010.dll", sw::ModuleImagePatch::makeGenerationName( "GameFramework.dll", 36 ).c_str() );
    SW_EXPECT_STREQ( "S0001.dll", sw::ModuleImagePatch::makeGenerationName( "SWGam.dll", 1 ).c_str() );
    SW_EXPECT_TRUE( sw::ModuleImagePatch::makeGenerationName( "GF_A.dll", 1 ).empty() );
}

/**
 * @brief [ModuleImagePatchTest] PE32+ 의 일반 import · 지연 import 이름을 표마다 읽는다
 * @details 지연 import 표는 `LiveReloadManager` 가 모듈 이미지를 올리기 전에 보는 그물이고(있으면 거절), 일반 import 표는 세대 이름으로 바꿀 자리다.
 */
SW_TEST_CASE( ModuleImagePatchTest, PortableExecutableImportTablesAreRead )
{
    const sw::vector<uint8> bytes = makeSyntheticPortableExecutable();
    sw::vector<sw::string>  listName;
    SW_ASSERT_TRUE( sw::ModuleImagePatch::readImportDllNames( bytes, sw::ModuleImagePatch::ImportTable::Load, listName ) );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listName.size() ) );
    SW_EXPECT_STREQ( "GameFramework.dll", listName[0].c_str() );
    SW_EXPECT_STREQ( "Engine.dll", listName[1].c_str() );

    SW_ASSERT_TRUE( sw::ModuleImagePatch::readImportDllNames( bytes, sw::ModuleImagePatch::ImportTable::Delay, listName ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listName.size() ) );
    SW_EXPECT_STREQ( "TracyClient.dll", listName[0].c_str() );
    SW_EXPECT_TRUE( sw::ModuleImagePatch::hasImportDirectory( bytes, sw::ModuleImagePatch::ImportTable::Delay ) );
    SW_EXPECT_TRUE( sw::ModuleImagePatch::hasImportDirectory( bytes, sw::ModuleImagePatch::ImportTable::Load ) );
    SW_EXPECT_FALSE( sw::ModuleImagePatch::hasImportDirectory( makeSyntheticSharedObject(), sw::ModuleImagePatch::ImportTable::Delay ) );
}

/**
 * @brief [ModuleImagePatchTest] 일반 import 의 DLL 이름은 대소문자 없이 맞춰 제자리에서 바뀌고, 다른 import · 지연 import · 파일 크기는 그대로다
 */
SW_TEST_CASE( ModuleImagePatchTest, PortableExecutableImportNameIsRewrittenInPlace )
{
    sw::vector<uint8> bytes    = makeSyntheticPortableExecutable();
    const size_t      sizeBase = bytes.size();
    SW_EXPECT_EQUAL( 1u, sw::ModuleImagePatch::replaceImportDllName( bytes, "gameframework.dll", "GameFrame0007.dll" ) );

    sw::vector<sw::string> listName;
    SW_ASSERT_TRUE( sw::ModuleImagePatch::readImportDllNames( bytes, sw::ModuleImagePatch::ImportTable::Load, listName ) );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listName.size() ) );
    SW_EXPECT_STREQ( "GameFrame0007.dll", listName[0].c_str() );
    SW_EXPECT_STREQ( "Engine.dll", listName[1].c_str() );
    // 지연 import 는 바꾸는 자리가 아니다(모듈 이미지에는 없어야 한다).
    SW_EXPECT_EQUAL( 0u, sw::ModuleImagePatch::replaceImportDllName( bytes, "TracyClient.dll", "TracyCl0001.dll" ) );
    SW_EXPECT_TRUE( containsText( bytes, "TracyClient.dll" ) );
    SW_EXPECT_EQUAL( sizeBase, bytes.size() );
}

/**
 * @brief [ModuleImagePatchTest] 길이가 다른 요청 · PE 가 아닌 바이트 · 잘린 이미지는 건드리지 않고 읽기도 거절한다
 */
SW_TEST_CASE( ModuleImagePatchTest, PortableExecutableMismatchOrForeignBytesAreLeftAlone )
{
    sw::vector<uint8>       bytes    = makeSyntheticPortableExecutable();
    const sw::vector<uint8> original = bytes;
    SW_EXPECT_EQUAL( 0u, sw::ModuleImagePatch::replaceImportDllName( bytes, "GameFramework.dll", "GF.dll" ) );
    SW_EXPECT_TRUE( bytes == original );

    sw::vector<sw::string>  listName;
    const sw::vector<uint8> elf = makeSyntheticSharedObject();
    SW_EXPECT_FALSE( sw::ModuleImagePatch::readImportDllNames( elf, sw::ModuleImagePatch::ImportTable::Load, listName ) );
    sw::vector<uint8> notPe( 128, 0x41 );
    SW_EXPECT_EQUAL( 0u, sw::ModuleImagePatch::replaceImportDllName( notPe, "AAAA", "BBBB" ) );

    const sw::vector<uint8> truncated( original.begin(), original.begin() + 0x220 );
    SW_EXPECT_FALSE( sw::ModuleImagePatch::readImportDllNames( truncated, sw::ModuleImagePatch::ImportTable::Load, listName ) );
}
