/**
 * @file ModuleImagePatch.h
 * @brief 섀도 복사본의 ELF 동적 문자열(SONAME · NEEDED)을 제자리에서 바꿉니다(리눅스 핫 리로드).
 * @details 리눅스 동적 링커는 `DT_NEEDED` 를 풀 때 이미 올라온 라이브러리 중 SONAME 이 같은 **먼저 올라온 것**을 씁니다. 섀도 복사본은
 *          파일만 복사하므로 SONAME 이 원본과 같고, 연쇄 리로드는 "전부 prepare(새 이미지 로드) → commit(옛 이미지 내림)" 순서라서
 *          prepare 중인 새 킷이 아직 올라와 있는 **옛** GameFramework 에 묶입니다. 그래서 복사본을 올리기 전에 SONAME 을 세대마다 고유한
 *          이름으로 바꾸고, 의존 모듈 복사본의 NEEDED 를 그 이름으로 바꿉니다 — UE 가 빌드마다 번호 붙은 이름으로 다시 링크하는 것을
 *          복사 시점에 하는 셈입니다. Windows 에서 지연 로드 훅(`DelayLoadNotifyHook.cpp`)이 하는 일의 리눅스 짝입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct ModuleImagePatch
     * @brief ELF64(리틀 엔디언) 공유 라이브러리의 동적 섹션 문자열을 **같은 길이로** 바꿉니다.
     * @details 파일 바이트만 다루므로 어느 플랫폼에서나 돕니다(테스트는 Windows 에서도 합성 ELF 로 돈다). 쓰는 곳은 리눅스의
     *          `LiveReloadManager` 뿐입니다. 문자열 표를 늘리지 않으므로 섹션 · 세그먼트 배치가 그대로입니다.
     */
    struct ModuleImagePatch
    {
        static constexpr int64 kTagNeeded = 1;  ///< DT_NEEDED
        static constexpr int64 kTagSoname = 14; ///< DT_SONAME

        /** @brief 동적 섹션의 DT_SONAME 을 읽습니다. ELF64 LE 가 아니거나 SONAME 이 없으면 false 입니다. */
        static bool readSoname( const vector<uint8>& bytes, string& outSoname );

        /**
         * @brief 동적 섹션에서 태그가 @p tag 인 항목 중 문자열이 @p from 인 것을 @p to 로 바꾸고, 바꾼 항목 수를 반환합니다.
         * @details @p to 는 @p from 과 길이가 같아야 합니다(문자열 표 안에서 제자리로 덮습니다). 다르거나 ELF64 LE 가 아니면 0 입니다.
         */
        static uint32 replaceDynamicString( vector<uint8>& inoutBytes, int64 tag, string_view from, string_view to );

        /**
         * @brief 세대 @p generation 을 담은, @p soname 과 **길이가 같은** 이름을 만듭니다(예: `libGameFramework.so` → `libGameFrame0003.so`).
         * @details 확장자(`.so` 부터) 앞의 끝 네 글자를 36진 세대로 바꿉니다. 세대는 프로세스 안에서 모듈과 무관하게 하나씩 오르므로
         *          앞부분이 같은 두 모듈도 같은 이름을 받지 않습니다.
         * @return 만들 수 없으면(`.so` 가 없거나 그 앞이 `lib` + 네 글자보다 짧다) 빈 문자열입니다.
         */
        static string makeGenerationName( string_view soname, uint32 generation );
    };
} // namespace sw
