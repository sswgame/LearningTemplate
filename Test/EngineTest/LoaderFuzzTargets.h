/**
 * @file LoaderFuzzTargets.h
 * @brief 로더 퍼징 대상 표 — 바이트 하나를 받아 엔진 로더 하나에 먹이는 함수와, 그 로더의 씨앗(저장소의 실제 파일 · 만든 바이트)입니다.
 * @details 시드 고정 변이 퍼저(`LoaderFuzzTest`, CI 의 nogpu)가 이 표를 돈다. 대상을 하나 더하는 것은 이 표에 한 줄을 더하는 것이다.
 *          함수 모양이 libFuzzer 의 `LLVMFuzzerTestOneInput` 과 같아 그대로 붙일 수 있다 — 다만 Windows 의 `clang_rt.fuzzer` 는 정적 CRT(/MT)
 *          빌드뿐이라 동적 CRT(/MD) 인 엔진과 링크되지 않는다(LNK2038). 커버리지 안내 퍼징은 리눅스 clang 에서 붙인다(백로그 1-9).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace test
{
    /** @brief 바이트 하나를 로더에 먹입니다. 결과(성공 · 실패)는 보지 않는다 — 죽지 않고 · 단언하지 않고 · 끝나는지만 본다. */
    using LoaderFuzzFunc = void ( * )( const uint8* pData, size_t size );

    /** @brief 씨앗을 모읍니다(저장소 파일 · 만든 바이트). 리소스 루트를 못 찾으면 만든 씨앗만 냅니다. */
    using LoaderSeedFunc = void ( * )( sw::vector<sw::vector<uint8>>& outListSeed );

    /** @brief 표의 한 줄입니다. */
    struct LoaderFuzzTarget
    {
        const utf8*    _pName;
        LoaderFuzzFunc _pfnRun;
        LoaderSeedFunc _pfnCollectSeed;
        bool           _bText; ///< 텍스트 형식이면 true — 변이기가 숫자 · 구두점을 노리는 변이를 섞는다
    };

    /** @brief 대상 표 전체입니다. */
    sw::vector_reference<const LoaderFuzzTarget> getLoaderFuzzTargets();

    /** @brief 추적 모드가 마지막 입력을 남기는 자리(임시 폴더의 `sw_fuzz_last_<대상>.bin`)입니다. */
    sw::string makeFuzzTracePath( sw::string_view targetName );

    /** @brief 이름으로 대상을 찾습니다. 없으면 nullptr 입니다. */
    const LoaderFuzzTarget* findLoaderFuzzTarget( sw::string_view name );
} // namespace test
