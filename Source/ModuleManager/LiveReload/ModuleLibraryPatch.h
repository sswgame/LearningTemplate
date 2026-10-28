/**
 * @file ModuleManager/LiveReload/ModuleLibraryPatch.h
 * @brief 섀도 복사본을 올리기 전에 파일 바이트를 읽고 고칩니다(엔진 ABI 스탬프 · 리눅스 SONAME · Windows import DLL 이름).
 *
 * @note 핫 리로드(`LiveReloadManager`)와 그 테스트만 쓰고, Shipping 빌드에서 함께 빠집니다(`Source/ModuleManager/CMakeLists.txt` 의 제외 목록).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Module/EngineABIStampMarker.h"

namespace sw
{
    /**
     * @struct ModuleLibraryPatch
     * @brief 공유 라이브러리가 의존을 부르는 이름을 **같은 길이로** 바꿉니다 — ELF64(리틀 엔디언)의 동적 섹션 문자열과 PE32+ 의 import DLL 이름.
     * @details 파일 바이트만 다루므로 어느 플랫폼에서나 돕니다(테스트는 합성 ELF · 합성 PE 로 돈다). 쓰는 곳은 `LiveReloadManager` 뿐입니다.
     *          문자열을 늘리지 않으므로 섹션 · 세그먼트 배치가 그대로입니다.
     * @note 왜 고치는가: 로더는 의존을 이름으로 풀고, 같은 이름의 라이브러리가 이미 올라와 있으면 그것을 씁니다(리눅스는 SONAME, Windows 는 파일
     *       이름). 섀도 복사본을 원본 이름 그대로 두면 연쇄 리로드("전부 prepare(새 라이브러리 로드) → commit(옛 라이브러리 내림)")의 prepare 중인 새 킷이
     *       아직 올라와 있는 **옛** 의존에 묶이고(리눅스), 원본 이름의 import 는 원본 DLL 을 한 벌 더 올립니다(Windows). 그래서 복사본에 세대마다
     *       고유한 이름을 주고(리눅스 SONAME · Windows 파일 이름), 의존 모듈 복사본이 그 이름을 부르게 바꿉니다 — 언리얼이 빌드마다 번호 붙은
     *       이름으로 다시 링크하는 것을 복사 시점에 하는 셈입니다. 그래서 모듈끼리 지연 로드(`/DELAYLOAD`)를 쓰지 않고 일반 import 로 묶습니다
     *       (lld 20 의 지연 로드 썽크는 첫 호출의 float 인자를 망가뜨린다).
     */
    struct ModuleLibraryPatch
    {
        static constexpr int64 kTagNeeded = 1;  ///< DT_NEEDED
        static constexpr int64 kTagSoname = 14; ///< DT_SONAME

        /** @brief PE import 테이블 종류입니다. */
        enum class ImportTable : uint8
        {
            Load,  ///< 일반 import(IMAGE_DIRECTORY_ENTRY_IMPORT). 로더가 로드할 때 바인딩한다
            Delay, ///< 지연 import(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT). 첫 호출이 썽크를 지나 바인딩한다
        };
        /** @brief 엔진 ABI 스탬프 문자열의 머리입니다. 뒤에 SHA-1 16진 40 글자가 옵니다(정본 `EngineABIStampMarker.h`). */
        static constexpr const utf8* kEngineABIStampMarker = SW_ENGINE_ABI_STAMP_MARKER;
        static constexpr uint32      kEngineABIStampDigits = 40;

        /** @brief 동적 섹션의 DT_SONAME 을 읽습니다. ELF64 LE 가 아니거나 SONAME 이 없으면 false 입니다. */
        [[nodiscard]] static bool readSoname( const vector<uint8>& bytes, string& outSoname );

        /**
         * @brief 동적 섹션에서 태그가 @p tag 인 항목 중 문자열이 @p from 인 것을 @p to 로 바꾸고, 바꾼 항목 수를 반환합니다.
         * @details @p to 는 @p from 과 길이가 같아야 합니다(문자열 표 안에서 제자리로 덮습니다). 다르거나 ELF64 LE 가 아니면 0 입니다.
         */
        static uint32 replaceDynamicString( vector<uint8>& inoutBytes, int64 tag, string_view from, string_view to );

        /**
         * @brief 세대 @p generation 을 담은, @p libraryName 과 **길이가 같은** 이름을 만듭니다
         *        (예: `libGameFramework.so` → `libGameFrame0003.so`, `GF_Mailbox.dll` → `GF_Mai0003.dll`).
         * @details 확장자(`.so` · `.dll` 부터) 앞의 끝 네 글자를 36진 세대로 바꿉니다. 세대는 프로세스 안에서 모듈과 무관하게 하나씩 오르므로
         *          앞부분이 같은 두 모듈도 같은 이름을 받지 않습니다. `lib` 접두어는 남깁니다(도구가 이름을 알아보는 자리).
         * @return 만들 수 없으면(확장자가 없거나 그 앞이 접두어 + 한 글자 + 네 글자보다 짧다) 빈 문자열입니다.
         */
        static string makeGenerationName( string_view libraryName, uint32 generation );

        /**
         * @brief PE32+ 라이브러리 @p bytes 의 @p table 에 적힌 DLL 이름을 순서대로 모읍니다.
         * @return PE32+ 로 읽었으면 true(표가 없으면 빈 목록). PE32+ 가 아니거나 잘렸으면 false 입니다.
         */
        [[nodiscard]] static bool readImportDLLNames( const vector<uint8>& bytes, ImportTable table, vector<string>& outListDLLName );

        /** @brief PE32+ 라이브러리 @p bytes 의 데이터 디렉터리에 @p table 이 있으면(RVA 가 0 이 아니면) true 입니다. 담긴 서술자 수와 무관합니다. */
        [[nodiscard]] static bool hasImportDirectory( const vector<uint8>& bytes, ImportTable table );

        /**
         * @brief PE32+ 라이브러리의 일반 import 표에서 DLL 이름이 @p from 인 항목(대소문자 무시)을 @p to 로 바꾸고, 바꾼 항목 수를 반환합니다.
         * @details @p to 는 @p from 과 길이가 같아야 합니다(이름 문자열을 제자리로 덮습니다). 다르거나 PE32+ 가 아니면 0 입니다.
         */
        static uint32 replaceImportDLLName( vector<uint8>& inoutBytes, string_view from, string_view to );

        /**
         * @brief 모듈 파일 바이트에서 엔진 ABI 스탬프(`swEngineABIStamp:<sha1>`)을 찾습니다. 이 함수만은 ELF 에 한정되지 않습니다(DLL · SO 모두).
         * @details 핫 리로드는 **모듈 코드가 한 줄도 돌기 전에**(정적 초기화 전) 돌고 있는 엔진과 같은 헤더로 빌드됐는지 봐야 하므로, 심볼을
         *          찾지 않고 파일에서 표식 문자열을 찾습니다.
         * @return 표식과 그 뒤 16진 40 글자가 온전히 있으면 true 입니다(@p outStamp 는 표식을 포함한 전체).
         */
        [[nodiscard]] static bool findEngineABIStamp( const vector<uint8>& bytes, string& outStamp );
    };
} // namespace sw
