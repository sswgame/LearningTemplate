/**
 * @file PlatformFileUtil.h
 * @brief 플랫폼마다 이름이 다른 stdio 원시 연산입니다.
 *
 * @details 안전한 파일 열기와 64비트 오프셋 탐색은 Windows 와 POSIX 에서 함수 이름이 다릅니다. 쓰는 곳마다 그 `#if` 를
 *          적으면(`FileUtil` · `Logger` · Engine 의 `ResourcePackReader`) 플랫폼을 하나 더 지원할 때 모두 찾아내 빠짐없이
 *          고쳐야 합니다. 그래서 원시 연산은 여기 한 곳에만 둡니다.
 *
 * @note 분기가 짧아 이 `.cpp` 하나가 두 플랫폼을 모두 담습니다. 플랫폼 코드가 큰 것(비동기 IO 백엔드 · FileDialog · FileWatcher)은
 *       `File/Windows` · `File/Linux` 에 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief OS 파일 핸들(Windows `HANDLE` · POSIX 파일 서술자)을 담는 값입니다. 위치 지정 읽기(`readNativeFileAt`)와 비동기 IO(`AsyncFileIO`)가 씁니다.
     * @details `FILE*` 와 달리 공유 파일 위치가 없어 여러 스레드가 같은 핸들을 잠금 없이 동시에 읽습니다.
     */
    using NativeFileHandle = int64;

    /** @brief 열지 못한 핸들입니다(Windows `INVALID_HANDLE_VALUE` · POSIX -1 과 같은 값). */
    inline constexpr NativeFileHandle kInvalidNativeFileHandle = -1;

    /**
     * @struct PlatformFileUtil
     * @brief stdio 기반 플랫폼 파일 원시 연산 모음입니다.
     */
    struct SW_API PlatformFileUtil
    {
        /** @brief 오프셋 · 크기 질의가 실패했음을 나타내는 값입니다. */
        static constexpr int64 kInvalidOffset = -1;

        /**
         * @brief 파일을 엽니다. 실패하면 nullptr 입니다.
         * @param pFilePath 널 종료 **UTF-8** 경로. 구분자 정규화는 호출하는 쪽이 먼저 합니다.
         * @param pMode fopen 모드 문자열("rb", "wb", "a" 등)
         * @details 다른 프로세스의 읽기 · 쓰기를 막지 않습니다(POSIX `fopen` 과 같다) — 실행 중인 로그 파일을 다른 프로그램이 열 수 있다.
         *          Windows 에서는 UTF-16 으로 바꿔 엽니다(`_wfsopen`, `_SH_DENYNO`). 좁은 문자 `fopen_s` 는 경로를 **ANSI 코드 페이지**(한국어
         *          Windows 는 CP949)로 해석해, 한글이 들어간 UTF-8 경로를 다른 이름으로 읽습니다. 읽기(`readRange`)도 UTF-16 으로
         *          열므로 쓰기와 읽기가 같은 파일을 봅니다.
         */
        static FILE* openFile( const utf8* pFilePath, const utf8* pMode );

        /**
         * @brief @p pSourcePath 를 @p pTargetPath 자리로 옮기며, 이미 있으면 **한 번에** 바꿔치기합니다(같은 볼륨 안에서 원자적).
         * @details 저장은 임시 파일에 다 쓴 뒤 이것으로 바꿔 끼웁니다. 도중에 죽어도 원본은 온전하거나 새 파일이 온전하며, 반쯤 쓴 파일이
         *          남지 않습니다. Windows 는 `MoveFileExW( REPLACE_EXISTING | WRITE_THROUGH )`, POSIX 는 `rename` 입니다.
         * @return 성공하면 true 입니다. 다른 프로세스가 대상 파일을 삭제 공유 없이 열고 있으면 Windows 에서 실패할 수 있습니다.
         */
        static bool replaceFile( const utf8* pSourcePath, const utf8* pTargetPath );

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

        /**
         * @brief 읽기 전용으로 OS 핸들을 엽니다. 다른 프로세스의 읽기 · 쓰기 · 삭제를 막지 않습니다(`openFile` 과 같은 공유).
         * @details Windows 는 `FILE_FLAG_OVERLAPPED` 로 엽니다 — 같은 핸들로 IOCP 비동기 읽기와 `readNativeFileAt` 동기 읽기를 함께 합니다.
         * @return 열지 못했으면(없음 · 권한 · 디렉터리) `kInvalidNativeFileHandle` 입니다.
         */
        static NativeFileHandle openNativeFileForRead( const utf8* pFilePath );
        /** @brief `openNativeFileForRead` 로 연 핸들을 닫습니다. 잘못된 핸들이면 아무 일도 하지 않습니다. */
        static void closeNativeFile( NativeFileHandle handle );
        /** @brief 열린 핸들의 파일 크기입니다. 실패하면 `kInvalidOffset` 입니다. */
        static int64 getNativeFileSize( NativeFileHandle handle );
        /**
         * @brief @p offset 에서 최대 @p size 바이트를 @p pDst 로 읽습니다(동기, 위치 지정 — 공유 파일 위치를 쓰지 않는다).
         * @details 여러 스레드가 같은 핸들로 동시에 불러도 됩니다. Windows 는 스레드마다 이벤트 하나를 두고 그 낮은 비트를 세워, 핸들이 완료 포트에
         *          묶여 있어도 이 읽기의 완료가 포트로 가지 않게 합니다.
         * @param outReadBytes 실제로 읽은 바이트. 파일 끝에서 멈추면 @p size 보다 작습니다.
         * @return OS 가 실패하면 false 입니다(파일 끝은 실패가 아니다).
         */
        [[nodiscard]] static bool readNativeFileAt( NativeFileHandle handle, uint64 offset, void* pDst, size_t size, size_t& outReadBytes );
    };
} // namespace sw
