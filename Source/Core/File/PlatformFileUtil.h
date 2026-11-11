/**
 * @file PlatformFileUtil.h
 * @brief 플랫폼마다 이름이 다른 stdio 원시 연산입니다.
 *
 * @details 안전한 파일 열기와 64비트 오프셋 탐색은 Windows 와 POSIX 에서 함수 이름이 다릅니다. 쓰는 곳마다 그 `#if` 를
 *          적으면(`FileUtil` · `Logger` · Engine 의 `PackReader`) 플랫폼을 하나 더 지원할 때 모두 찾아내 빠짐없이
 *          고쳐야 합니다. 그래서 원시 연산은 여기 한 곳에만 둡니다.
 *
 * @note 분기가 짧아 이 `.cpp` 하나가 두 플랫폼을 모두 담습니다. 플랫폼 코드가 큰 것(비동기 IO 백엔드 · FileDialog · FileWatcher)은
 *       `File/Windows` · `File/Linux` 에 있습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

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
     * @brief 파일 읽기를 듣는 쪽입니다 — 개발 도구가 디스크 흉내 · 읽기 기록으로 붙는다(`PlatformFileUtil::registerReadObserver`). 파일 계층은 이 인터페이스만 안다.
     * @details 부르는 스레드(게임 · 워커 · 비동기 IO 스레드)에서 불리므로 구현은 스레드 안전해야 합니다. 언리얼이 `IPlatformFile` 장식 층
     *          (`FLoggedPlatformFile` · 지연 흉내)을 개발 모듈에서 끼우는 모양과 같다 — 배포본에는 관찰자가 없어 읽기 길은 원자 읽기 하나다.
     */
    class SW_API IFileReadObserver
    {
    public:
        IFileReadObserver() = default;
        /** @brief 가상 소멸자입니다. */
        virtual ~IFileReadObserver() = default;

        /** @brief 복사를 금지합니다. */
        IFileReadObserver( const IFileReadObserver& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        IFileReadObserver& operator=( const IFileReadObserver& ) = delete;

        /** @brief 장치에서 @p byteCount 를 읽기 직전에 불립니다(위치 지정 읽기 · 비동기 IO 덩이). 구현이 이 스레드를 재우면 그만큼 읽기가 늦는다. */
        virtual void recordDeviceRead( size_t byteCount ) = 0;
        /** @brief 파일 하나를 읽으려고 연 뒤 불립니다. @p byteCount 는 읽은 바이트이고 핸들만 연 경우 0 입니다. */
        virtual void recordFileOpenedForRead( string_view filePath, size_t byteCount ) = 0;
    };

    /**
     * @brief 이 프로세스가 파일 내용 · 도장을 바꾼 것을 듣는 쪽입니다 — 파일 내용에서 뽑은 값을 (크기, 쓰기 시각) 도장으로 캐시하는 쪽(`ContentHashCache`)이 붙는다
     *        (`PlatformFileUtil::registerWriteObserver`). 파일 계층은 이 인터페이스만 안다.
     * @details 쓰기 시각은 시계 틱(Windows 약 16 ms) 단위라 같은 틱 안에 같은 크기로 다시 쓰면 도장이 같다. 자기 쓰기는 쓰기 경로가 이 알림으로 캐시를 그 자리에서 고치고,
     *          시간 창(막 쓴 파일은 캐시하지 않는다)은 바깥 도구(git · 외부 편집기 · 자식 프로세스)의 쓰기에만 남는다 — 언리얼 `FFileManagerGeneric` 위의 DDC 해시 캐시가
     *          자기 저장을 무효화 사건으로 받는 것과 같은 자리. 부르는 스레드에서 불리므로 구현은 스레드 안전해야 합니다. 관찰자가 없으면 쓰기 길은 원자 읽기 하나다.
     */
    class SW_API IFileWriteObserver
    {
    public:
        IFileWriteObserver() = default;
        /** @brief 가상 소멸자입니다. */
        virtual ~IFileWriteObserver() = default;

        /** @brief 복사를 금지합니다. */
        IFileWriteObserver( const IFileWriteObserver& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        IFileWriteObserver& operator=( const IFileWriteObserver& ) = delete;

        /**
         * @brief @p filePath 의 내용이나 도장을 이 프로세스가 바꾼 **뒤에** 불립니다(쓰기 · 덧붙이기 · 복사 대상 · 이름 바꾸기 대상 · 시각 바꾸기 · 지우기).
         * @details 경로는 쓰기 입구가 받은 그대로(구분자만 정규화)다. 폴더 지우기 · 옮기기는 폴더 경로 하나로 온다.
         */
        virtual void recordFileWritten( string_view filePath ) = 0;
    };

    /**
     * @struct PlatformFileUtil
     * @brief stdio 기반 플랫폼 파일 원시 연산 모음입니다.
     */
    struct SW_API PlatformFileUtil
    {
        /** @brief 오프셋 · 크기 질의가 실패했음을 나타내는 값입니다. */
        static constexpr int64 kInvalidOffset = constant::invalid_index::kInt64;

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
         * @brief @p pSourcePath 를 @p pTargetPath 경로로 옮기며, 이미 있으면 **한 번에** 바꿔치기합니다(같은 볼륨 안에서 원자적).
         * @details 저장은 임시 파일에 다 쓴 뒤 이것으로 바꿔 끼웁니다. 도중에 죽어도 원본은 온전하거나 새 파일이 온전하며, 반쯤 쓴 파일이
         *          남지 않습니다. Windows 는 `MoveFileExW( REPLACE_EXISTING | WRITE_THROUGH )`, POSIX 는 `rename` 입니다.
         * @return 성공하면 true 입니다. 다른 프로세스가 대상 파일을 삭제 공유 없이 열고 있으면 Windows 에서 실패할 수 있습니다.
         */
        static bool replaceFile( const utf8* pSourcePath, const utf8* pTargetPath );

        /** @brief 이 플랫폼에 OS 휴지통(`moveToRecycleBin`)이 있는지입니다. 없으면 호출하는 쪽이 지울지 남길지 정합니다. */
        static bool hasRecycleBin();

        /**
         * @brief 파일 · 폴더들을 OS 휴지통으로 한 번에 옮깁니다(사용자가 복원할 수 있는 삭제).
         * @details Windows 는 셸 파일 작업 하나(`SHFileOperationW` + `FOF_ALLOWUNDO`)로 목록 전체를 보내며 확인 창 · 오류 창을 띄우지 않습니다.
         *          휴지통 규약(freedesktop Trash)이 데스크톱마다 다른 Linux 는 지원하지 않아 아무것도 건드리지 않고 false 입니다(`hasRecycleBin`).
         * @param listPath UTF-8 경로들. 구분자는 `/` · `\` 모두 받습니다.
         * @return 모두 옮겼으면 true 입니다. 실패하거나 중단되면 false 이며, 일부만 옮겨졌을 수 있습니다.
         */
        [[nodiscard]] static bool moveToRecycleBin( const vector<string>& listPath );

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
        /**
         * @brief 쓰기 전용으로 만들거나 비우고 OS 핸들을 엽니다(다른 프로세스와 공유하지 않는다). **할당하지 않고** 로그를 쓰지 않습니다 - 크래시 경로(시그널 처리기 포함)가 씁니다.
         * @details 경로 변환은 스택에서 한다(`kMaxBuffer1024` 글자까지). 보통 쓰기는 `FileUtil::writeFile`(원자적 · 쓰기 문 · 로그)이다.
         * @return 열지 못했으면 `kInvalidNativeFileHandle` 입니다.
         */
        static NativeFileHandle createNativeFileForWrite( const utf8* pFilePath );
        /** @brief 열린 핸들에 @p size 바이트를 다 씁니다(끊긴 쓰기는 이어 쓴다). **할당하지 않습니다.** 다 쓰지 못하면 false 입니다. */
        [[nodiscard]] static bool writeNativeFile( NativeFileHandle handle, const void* pData, size_t size );
        /** @brief `openNativeFileForRead` · `createNativeFileForWrite` 로 연 핸들을 닫습니다. 잘못된 핸들이면 아무 일도 하지 않습니다. */
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

        /**
         * @brief 장치에서 @p byteCount 를 읽기 직전에 부릅니다 — 걸린 읽기 관찰자(`IFileReadObserver::recordDeviceRead`)에 알린다.
         * @details 읽기 경로(`readNativeFileAt`, 비동기 IO 백엔드의 덩이 읽기)가 부릅니다. 관찰자가 없으면(배포본은 늘) 아무것도 하지 않습니다.
         */
        static void recordDeviceRead( size_t byteCount );

        /**
         * @brief 파일 하나를 읽으려고 열 때 부릅니다(`FileUtil::readFile` · `readTextFile` · `readStreamTextFile`, 비동기 IO 의 `AsyncFileHandle::open`). 연 횟수를 세고
         *        걸린 읽기 관찰자에 알린다.
         * @param byteCount 읽은 바이트. 핸들만 연 경우(뒤에 위치 지정 읽기가 온다)는 0 입니다.
         */
        static void recordFileOpenedForRead( string_view filePath, size_t byteCount );

        /**
         * @brief 읽기 관찰자를 겁니다. 이미 다른 관찰자가 걸려 있으면 거절하고 false 입니다(관찰자는 하나다).
         * @param pObserver 뗄 때까지 살아 있어야 합니다.
         */
        [[nodiscard]] static bool registerReadObserver( IFileReadObserver* pObserver );
        /** @brief @p pObserver 가 걸린 관찰자이면 뗍니다. */
        static void unregisterReadObserver( IFileReadObserver* pObserver );

        /** @brief 이 프로세스가 @p filePath 를 바꾼 뒤 부릅니다 — 걸린 쓰기 관찰자(`IFileWriteObserver::recordFileWritten`)에 알린다. `FileUtil` 의 쓰기 입구가 부릅니다. */
        static void recordFileWritten( string_view filePath );
        /**
         * @brief 쓰기 관찰자를 겁니다. 이미 다른 관찰자가 걸려 있으면 거절하고 false 입니다(관찰자는 하나다 — 읽기 관찰자와 같은 모양).
         * @param pObserver 뗄 때까지 살아 있어야 합니다.
         */
        [[nodiscard]] static bool registerWriteObserver( IFileWriteObserver* pObserver );
        /** @brief @p pObserver 가 걸린 관찰자이면 뗍니다. */
        static void unregisterWriteObserver( IFileWriteObserver* pObserver );

        /**
         * @brief OS 경계에 들어온 경로가 엄격한 절대 경로인지 봅니다(R2 - 디스크 I/O 는 절대 경로만). 아니면 오류(`File.RelativePath`)를 남기고 false - 부르는 OS 호출은 실패한다.
         * @details 상대 경로는 실행 중 작업 폴더로 풀려 띄운 자리마다 다른 파일이 됐다. 경로를 받는 입구(명령줄 · 전역 변수 · 개발 명령 · 설정 · 루트)가 모두 받을 때 절대화하므로
         *          여기에 닿는 상대 경로는 결함이다. 파일 시스템 연산(`PlatformFileSystem` 의 경로 변환 길목)과 이 구조체의 여는 함수가 부른다.
         *          할당하므로 크래시 경로(`createNativeFileForWrite`)는 부르지 않는다. 빈 경로는 여기서 거절하지 않는다(부르는 쪽이 이미 실패로 다룬다).
         */
        [[nodiscard]] static bool canAcceptPathInput( string_view path );

#if defined( SW_PLATFORM_WINDOWS )
        /**
         * @brief UTF-8 절대 경로를 Win32 파일 API 가 받는 넓은 문자 경로로 바꿉니다(구분자 `\`). 파일을 여는 · 만드는 · 지우는 OS 호출이 모두 이 길로 간다.
         * @details 긴 경로(`kLongPathThreshold` 이상)는 `.` · `..` 를 편 뒤 확장 경로 접두(`\\?\`, UNC 는 `\\?\UNC\`)를 붙인다 — 접두 없이는 Win32 가
         *          옛 상한 260 자(MAX_PATH)에서 거절한다(작업 트리처럼 깊은 폴더의 테스트 임시 경로가 266 자라 열기가 실패했다). `longPathAware` 매니페스트는
         *          시스템 레지스트리(LongPathsEnabled)까지 켜져 있어야 들어 기계마다 달라진다. 언리얼 `FWindowsPlatformFile` 도 긴 경로에만 접두를 붙인다.
         *          작업 폴더 설정(`SetCurrentDirectoryW`)처럼 접두를 받지 않는 API 에는 쓰지 않는다. 펴기가 실패하면 빈 글이라 그 OS 호출이 실패한다.
         */
        [[nodiscard]] static wstring toWindowsFilePath( string_view path );
        /** @brief 이 길이(문자) 이상이면 확장 경로 접두를 붙입니다 — 폴더 만들기의 상한(MAX_PATH - 12 = 248)에 맞춘다. */
        static constexpr size_t kLongPathThreshold = 248;
#endif

        /** @brief 프로세스 시작부터 `recordFileOpenedForRead` 가 센 열기 횟수입니다(기동 보고 `startup … ms, … file opens`). */
        [[nodiscard]] static uint64 getFileOpenCount();
    };
} // namespace sw
