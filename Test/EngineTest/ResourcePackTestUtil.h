/**
 * @file ResourcePackTestUtil.h
 * @brief 시험 · 벤치가 함께 쓰는 팩(SWPK) 파일 만들기입니다.
 */
#pragma once
#include "Core/Container/pair.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Resource/ResourcePackTypes.h"

namespace sw::test
{
    /** @brief 시험용 팩 파일을 쓰는 도우미입니다(쿠커와 같은 배치 — 헤더 · 4096 정렬 페이로드 · FAT · 스트링 풀). */
    struct ResourcePackTestUtil
    {
        /**
         * @brief @p listFileContent 의 (가상 경로, 본문) 쌍을 @p compression 으로 담은 팩을 @p packPath 에 씁니다.
         * @return 파일을 다 썼으면 true. 코덱이 등록돼 있지 않으면 false 입니다.
         */
        static bool createPackFile( const string& packPath, uint32 dlcAppId, PackCompressionType compression, const vector<pair<string, string>>& listFileContent,
                                    bool bIncludeDebugStringPool = false );
    };
} // namespace sw::test
