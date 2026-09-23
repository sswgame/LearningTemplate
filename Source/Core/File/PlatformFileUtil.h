/**
 * @file PlatformFileUtil.h
 * @brief 플랫폼마다 이름이 다른 stdio 원시 연산입니다.
 *
 * @details 안전한 파일 열기와 64비트 오프셋 탐색은 Windows 와 POSIX 에서 함수 이름이 다릅니다. 쓰는 곳마다 그 `#if` 를
 *          적다 보니 `FileUtil` 에 5벌, `Logger` 에 1벌, Engine 의 `ResourcePackReader` 에 5벌이 쌓였습니다. 그러면
 *          플랫폼을 하나 더 지원할 때 세 파일을 모두 찾아내 빠짐없이 고쳐야 합니다. 그래서 원시 연산은 여기 한 곳에만 둡니다.
 *
 * @note 지금은 분기가 한 줄짜리라 이 `.cpp` 하나가 세 플랫폼을 모두 담습니다. 비동기 IO 나 메모리 매핑처럼 플랫폼별 코드가
 *       커지면 `File/Windows` · `File/Linux` · `File/Mac` 으로 옮기면 됩니다(FileDialog · FileWatcher 가 이미 그 형태입니다).
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct PlatformFileUtil
     * @brief stdio 기반 플랫폼 파일 원시 연산 모음입니다.
     */
    struct PlatformFileUtil
    {
        /** @brief 오프셋 · 크기 질의가 실패했음을 나타내는 값입니다. */
        static constexpr int64 kInvalidOffset = -1;

        /**
         * @brief 파일을 엽니다. 실패하면 nullptr 입니다.
         * @param pFilePath 널 종료 경로. 구분자 정규화는 호출하는 쪽이 먼저 합니다.
         * @param pMode fopen 모드 문자열("rb", "wb", "a" 등)
         */
        static FILE* openFile( const utf8* pFilePath, const utf8* pMode );

        /**
         * @brief 64비트 오프셋으로 이동합니다.
         * @param origin SEEK_SET / SEEK_CUR / SEEK_END
         */
        static bool seekTo( FILE* pFile, int64 offset, int32 origin );

        /** @brief 현재 오프셋을 반환합니다. 실패하면 kInvalidOffset 입니다. */
        static int64 tellPosition( FILE* pFile );

        /**
         * @brief 열린 파일의 전체 크기를 반환하고 오프셋을 처음으로 되돌립니다.
         * @details 호출 전의 오프셋은 보존하지 않습니다. 파일을 열 필요 없이 크기만 알고 싶으면 `FileUtil::getFileSize` 를 쓰십시오.
         * @return 바이트 수. 실패하면 kInvalidOffset 입니다.
         */
        static int64 getOpenFileSizeAndRewind( FILE* pFile );
    };
} // namespace sw
