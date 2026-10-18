/**
 * @file ContentHash.h
 * @brief 쿠킹 판정이 쓰는 파일 내용 해시(CR 을 뺀 바이트의 FNV-1a 64)와 그 캐시입니다. 셰이더 쿠킹 스탬프와 쿠킹 기록(`CookRecord`)이 같은 값을 씁니다.
 * @details 판정은 파일 시각이 아니라 내용이다 — 이 저장소는 `git pull` 이 소스와 산출물의 시각을 임의의 순서로 바꾼다.
 *          캐시는 파일 크기와 쓰기 시각이 그대로일 때만 다시 읽지 않는다(셰이더 요청 하나가 같은 소스를 여러 번 본다 — 한 번 여는 데 약 200µs).
 *          줄 끝 정규화(CR 제거)는 `Scripts/generate/CookAssets.py` 의 스탬프 해시와 같아야 한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @class ContentHash
     * @brief 파일 · 바이트의 내용 해시입니다. 0 은 "읽지 못함" 입니다(내용 해시가 0 인 파일은 사실상 없다).
     */
    class SW_API ContentHash
    {
    public:
        /** @brief 바이트의 내용 해시입니다(CR 을 빼고 FNV-1a 64). */
        static uint64 computeBytes( const uint8* pData, size_t size );
        /** @brief 바이트의 내용 해시입니다. */
        static uint64 computeBytes( const vector<uint8>& bytes ) { return computeBytes( bytes.data(), bytes.size() ); }
        /** @brief 파일의 내용 해시입니다. 크기와 쓰기 시각이 지난번과 같으면 파일을 열지 않습니다. 읽지 못하면 0 입니다. */
        static uint64 computeFile( string_view absPath );
        /** @brief 캐시를 비웁니다(테스트가 같은 경로의 파일을 같은 시각에 다시 쓸 때). */
        static void clearCache();
    };
} // namespace sw
