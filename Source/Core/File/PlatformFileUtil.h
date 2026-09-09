/**
 * @file PlatformFileUtil.h
 * @brief 플랫폼마다 이름이 다른 stdio 원시 연산.
 *
 * @details 안전한 파일 열기와 64비트 오프셋 탐색은 Windows 와 POSIX 에서 함수 이름이 다르다.
 *          그 `#if` 를 쓰는 곳마다 적는 동안 `FileUtil` 에 5벌, `Logger` 에 1벌, Engine 의
 *          `ResourcePackReader` 에 5벌이 쌓였다 — 플랫폼을 하나 더 지원하려면 세 파일을 찾아내
 *          빠뜨리지 않아야 한다. 원시 연산은 여기 한 곳에만 둔다.
 *
 * @note 지금은 분기가 한 줄짜리라 이 `.cpp` 하나가 세 플랫폼을 다 담는다. 비동기 IO·메모리
 *       매핑처럼 플랫폼 표면이 커지면 `File/Windows` · `File/Linux` · `File/Mac` 으로 옮길
 *       자리다(FileDialog·FileWatcher 가 이미 그 형태다).
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct PlatformFileUtil
     * @brief stdio 기반 플랫폼 파일 원시 연산 모음.
     */
    struct PlatformFileUtil
    {
        /** @brief 오프셋·크기 질의가 실패했음을 나타냅니다. */
        static constexpr int64 kInvalidOffset = -1;

        /**
         * @brief 파일을 엽니다. 실패하면 nullptr.
         * @param pFilePath 널 종료 경로. 구분자 정규화는 호출부가 먼저 합니다.
         * @param pMode fopen 모드 문자열("rb", "wb", "a" 등).
         */
        static FILE* openFile( const utf8* pFilePath, const utf8* pMode );

        /**
         * @brief 64비트 오프셋으로 탐색합니다.
         * @param origin SEEK_SET / SEEK_CUR / SEEK_END.
         */
        static bool seekTo( FILE* pFile, int64 offset, int32 origin );

        /** @brief 현재 오프셋을 돌려줍니다. 실패하면 kInvalidOffset. */
        static int64 tellPosition( FILE* pFile );

        /**
         * @brief 열린 파일의 전체 크기를 돌려주고 오프셋을 처음으로 되돌립니다.
         * @details 호출 전 오프셋은 보존하지 않습니다. 파일을 열 이유 없이 크기만 알고 싶으면
         *          `FileUtil::getFileSize` 를 씁니다.
         * @return 바이트 수. 실패하면 kInvalidOffset.
         */
        static int64 getOpenFileSizeAndRewind( FILE* pFile );
    };
} // namespace sw
