/**
 * @file ResourcePackTestUtil.h
 * @brief 시험용 `.pack` 파일을 만드는 도우미입니다(`ResourcePackTest` · 로더 퍼징의 씨앗).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/pair.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    enum class PackCompressionType : uint8;

    /**
     * @brief 파일 목록(경로 → 내용)으로 팩 파일을 씁니다(헤더 · 4096 정렬 데이터 · FAT · 선택적 디버그 문자열 풀). 쓰지 못하면 false 입니다.
     * @details 리더와 같은 길로 코덱을 찾는다(팩 종류 → 코덱 종류 표 + 등록부). None 은 그대로 싣는다.
     */
    bool createTestPackFile( const string& packPath, uint32 dlcAppId, PackCompressionType compression, const vector<sw::pair<string, string>>& listFileContent,
                             bool bIncludeDebugStringPool = false );
} // namespace sw
