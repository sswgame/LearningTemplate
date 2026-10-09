/**
 * @file FileUtil.h
 * @brief 경로 · 파일 I/O · 다이얼로그 · 동적 라이브러리 유틸리티입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/MathUtil.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) FileDialogParams — 열기/저장, 필터 · 시작 폴더 · 다중 선택
    // ------------------------------------------------------------------------------
    /** @brief 네이티브 파일 열기 · 저장 다이얼로그의 매개변수입니다. */
    struct FileDialogParams
    {
        /** @brief 열기인지 저장인지 나타냅니다. */
        enum class Type : uint8
        {
            Open,
            Save,
        };

        Type           _type{ Type::Open };         ///< Open 또는 Save
        string         _title;                      ///< 다이얼로그 창 제목(비어 있으면 OS 기본값)
        string         _description;                ///< 필터 설명 문자열
        vector<string> _listFilterExtension;        ///< 허용 확장자 목록(예: ".png", "hlsl")
        string         _initialDirectory;           ///< 시작 폴더(비어 있으면 OS 기본값)
        bool           _bEnableMultiselect{ true }; ///< 다중 선택 허용(열기에서만)
    };
} // namespace sw

namespace sw
{
    SW_DECLARE_DELEGATE( void, FileDialogDelegate, const vector<string>& fileName );

    /**
     * @brief 파일 하나의 크기와 마지막 쓰기 시각입니다. "지난번에 본 그 파일 그대로인가" 를 파일을 열지 않고 확인할 때 씁니다.
     * @details 시각은 플랫폼 고유의 단위 그대로입니다(Windows 는 100 ns, 리눅스는 `stat` 의 ns). 같은 기계 ·
     *          같은 실행 안에서 **같은지만** 비교합니다. `getFileTimestamp` 는 초 단위라 1초 안에 일어난 편집을 놓칩니다.
     */
    struct FileStamp
    {
        uint64 _size{ 0 };      ///< 바이트 수
        uint64 _writeTime{ 0 }; ///< 마지막 쓰기 시각(플랫폼 고유 단위 — Windows 100 ns, 리눅스 ns. 같은지만 비교한다)

        /** @brief 크기와 시각이 모두 같은지 확인합니다. */
        bool operator==( const FileStamp& other ) const { return _size == other._size && _writeTime == other._writeTime; }
        /** @brief 크기나 시각이 다른지 확인합니다. */
        bool operator!=( const FileStamp& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 디렉터리 순회가 항목마다 넘기는 값입니다.
     * @details `_path` 는 **콜백 안에서만** 유효합니다(순회가 버퍼 하나를 다시 씁니다). 남기려면 `string` 으로 복사하십시오.
     */
    struct DirectoryEntry
    {
        string_view _path;                ///< 순회 루트로 시작하는 `/` 구분 UTF-8 경로(대소문자는 디스크 그대로)
        bool        _bDirectory{ false }; ///< 디렉터리면 true(링크는 가리키는 쪽 기준)
    };

    /** @brief 디렉터리 순회 콜백입니다. false 를 돌려주면 순회를 멈춥니다. */
    using DirectoryEntryVisitFn = bool ( * )( void* pContext, const DirectoryEntry& entry );
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) FileUtil — 경로 분해 · 정규화 · 존재 확인 · I/O · 다이얼로그 · DLL
    //    전부 static. 맵 키에는 normalizePath, 파일 열기에는 normalizeSeparators 를 쓴다
    // ------------------------------------------------------------------------------
    /** @brief 경로 · 파일 · 공유 라이브러리를 다루는 정적 유틸리티입니다. */
    struct SW_API FileUtil
    {
        /** @brief 전체 경로를 디렉터리와 파일 이름 뷰로 나눕니다(할당 없음). */
        static void splitPath( string_view fullPath, string_view& outDirectoryPath, string_view& outFileName );
        /** @brief 경로에서 파일 이름만 반환합니다. */
        static string getFileNamePart( string_view fullPath );
        /** @brief 경로의 파일 이름 부분을 뷰로 돌려줍니다(할당 없음). */
        static void getFileNamePart( string_view fullPath, string_view& outFileName );
        /** @brief 경로에서 디렉터리만 반환합니다. */
        static string getDirectoryPart( string_view fullPath );
        /** @brief 경로의 디렉터리 부분을 뷰로 돌려줍니다(할당 없음). */
        static void getDirectoryPart( string_view fullPath, string_view& outDirectoryPath );
        /** @brief 마지막 확장자(예: ".hlsl")를 점까지 포함해 반환합니다. 없으면 빈 문자열입니다. */
        static string getExtension( string_view fileName );
        /** @brief 마지막 확장자를 뷰로 돌려줍니다(할당 없음). */
        static void getExtension( string_view fileName, string_view& outExtension );
        /** @brief 경로가 지정한 확장자로 끝나는지 대소문자 구분 없이 확인합니다(앞의 '.' 는 생략해도 됩니다). */
        static bool hasExtension( string_view fileName, string_view extension );
        /** @brief 지정한 확장자 중 하나와 대소문자 구분 없이 일치하는지 확인합니다. */
        static bool hasAnyExtension( string_view fileName, std::initializer_list<string_view> listExtension );
        /** @brief 확장자를 바꿉니다. */
        static string replaceExtension( string_view fileName, string_view extension );
        /** @brief 확장자를 뗍니다. */
        static string removeExtension( string_view fileName );
        /** @brief 확장자를 뗀 부분을 뷰로 돌려줍니다(할당 없음). */
        static void removeExtension( string_view fileName, string_view& outFileName );

        /** @brief rootDir 기준 상대 경로를 만듭니다. */
        static bool makeRelativePath( string_view rootDir, string_view path, string& outResult );
        /** @brief 절대 경로를 만듭니다. */
        static bool makeAbsolutePath( string_view path, string& outResult );
        /** @brief 비교 · 맵 키가 아니라 **실제** 경로를 만듭니다(링크 · `.` · `..` · 8.3 짧은 이름을 풀어서). 경로가 없으면 false 입니다. 구분자는 `/` 입니다. */
        [[nodiscard]] static bool makeCanonicalPath( string_view path, string& outResult );
        /** @brief 경로가 절대 경로인지 확인합니다. */
        static bool isAbsolutePath( string_view path );
        /**
         * @brief 비교 · 맵 키용으로 경로를 정규화합니다(`\` 를 `/` 로 바꾸고 소문자로).
         * @note Linux 의 파일 I/O 에는 쓰지 마십시오. 파일을 열 때는 normalizeSeparators 나 실제 파일 시스템 경로를 씁니다.
         */
        static string normalizePath( string_view path );
        /** @brief I/O 용으로 구분자만 정규화합니다(`\` 를 `/` 로). 대소문자는 그대로 둡니다. */
        static string normalizeSeparators( string_view path );
        /** @brief OS 고유의 경로 구분자로 바꿉니다(Windows: `\`, POSIX: `/`). */
        static string toNativeSeparators( string_view path );
        /** @brief normalizePath 기준으로 두 경로가 같은지 비교합니다. */
        static bool pathsEqualNormalized( string_view lhs, string_view rhs );
        /**
         * @brief 경로 끝의 `/` · `\` 를 뗍니다(루트 `/` 는 남깁니다).
         */
        static string trimTrailingSlashes( string_view path );
        /**
         * @brief 루트와 상대 경로를 `/` 로 이어 붙입니다.
         * @param root 절대 · 상대 루트(끝 슬래시와 구분자는 정규화합니다)
         * @param relative 루트 아래 상대 경로(앞 슬래시 허용, 비어 있으면 root 만)
         * @return `root` 또는 `root/relative`. root 가 비어 있으면 빈 문자열입니다.
         */
        static string joinPath( string_view root, string_view relative );
        /**
         * @brief `path` 가 `component` 자체이거나 `component/` 로 시작하는지 확인합니다(경로 구간 경계 기준).
         * @note `games` 와 `gamesfoo` 처럼 접두어만 같은 경우를 맞다고 하지 않습니다.
         */
        static bool startsWithPathComponent( string_view path, string_view component );
        /**
         * @brief `component/` 뒤의 상대 경로를 반환합니다(`component` 만 있으면 빈 문자열).
         */
        static string suffixAfterPathComponent( string_view path, string_view component );
        /**
         * @brief 대상 파일이 들어갈 상위 디렉터리가 있게 합니다(없으면 재귀적으로 만듭니다). 폴더 부분이 없는 이름이면 할 일이 없어 true 입니다.
         * @return 디렉터리가 있으면 true. 만들지 못했으면 경로와 이유를 알리고 false 입니다.
         */
        static bool ensureParentDirectoryExists( string_view filePath );
        /**
         * @brief 디렉터리가 있게 합니다(필요하면 상위 디렉터리도).
         * @details 만들지 못하면 여기서 알린다 — 말없이 넘어가면 뒤따르는 쓰기가 "열 수 없다" 로만 실패해 어느 폴더가 왜 막혔는지 모른다.
         * @return 디렉터리가 있으면 true. 만들지 못했으면 경로와 이유를 알리고 false 입니다.
         */
        static bool ensureDirectoryExists( string_view directoryPath );
        /** @brief 경로에 **항목(파일 또는 디렉터리)** 이 있는지 반환합니다. 디렉터리인지는 `isDirectory` 로 묻습니다. */
        static bool exists( string_view path );
        /** @brief 경로에 디렉터리가 있는지 반환합니다. */
        static bool isDirectory( string_view path );
        /**
         * @brief 경로에 **일반 파일**이 있는지 반환합니다(디렉터리 · 없는 경로는 false, 링크는 가리키는 쪽 기준).
         * @details 후보 경로 여럿 중 처음 있는 것을 골라 파일로 읽는 곳(설정 · 리소스 낱개 · 셰이더 include · 폰트 찾기)이 씁니다 — 같은 이름의 폴더가
         *          있으면 `exists` 는 그것을 골라 다음 후보로 넘어가지 못합니다.
         */
        static bool isRegularFile( string_view path );
        /**
         * @brief 파일이 있고 쓰기가 막혀 있으면(읽기 전용 속성 · 소유자 쓰기 권한 없음) true 입니다. 없는 파일은 false 입니다.
         * @details 버전 관리의 잠금(git LFS lockable 파일은 잠그기 전까지 읽기 전용이다)을 에디터가 보여 주는 데 씁니다.
         */
        static bool isReadOnlyFile( string_view fileName );
        /**
         * @brief 쓰기 권한을 바꿉니다. false 면 모든 쓰기 권한을 떼고(Windows 는 읽기 전용 속성), true 면 소유자 쓰기를 더합니다. 디렉터리에도 됩니다.
         * @return 바꾸지 못했으면 경로와 이유를 알리고 false 입니다.
         */
        [[nodiscard]] static bool setWritable( string_view path, bool bWritable );
        /** @brief 현재 작업 디렉터리를 반환합니다. */
        static string getCurrentPath();
        /** @brief 실행 파일의 경로를 반환합니다. */
        static string getExecutablePath();
        /**
         * @brief 빌드 산출물 폴더(`Bin`)를 반환합니다 — 실행 파일 폴더이고, 실행 파일이 시험 폴더(`TestBin`)에 있으면 그 옆의 `Bin` 입니다.
         * @details 모듈(`Bin/Modules`) · 서드파티 DLL · 셰이더 컴파일러 · 설정처럼 "실행 파일 옆" 에 놓이는 것은 이 폴더 기준으로 찾습니다.
         *          시험 실행 파일은 `TestBin` 에 있지만 그것들은 `Bin` 에 있습니다(`cmake/Engine/TestTargets.cmake`).
         */
        static string getBinaryDirectory();

        /** @brief 파일의 수정 시각을 초 단위로 반환합니다. */
        static uint64 getFileTimestamp( string_view fileName );
        /**
         * @brief 지금 시각을 `getFileTimestamp` 와 **같은 시계 · 같은 단위**로 반환합니다.
         * @details 파일 시각은 파일 시계(Windows 1601 년 기준)로 나옵니다. `system_clock` 과 기준점이 다를 수 있어 그쪽의 `now()`
         *          와 비교하면 틀립니다. "이 시각 이후 바뀐 파일" 을 고를 때는 이 함수를 쓰십시오.
         */
        static uint64 getCurrentFileTimestamp();
        /** @brief `getFileWriteTime` · `setFileWriteTime` 의 눈금입니다(100 ns). 1 초가 이만큼입니다. */
        static constexpr int64 kFileTimeTicksPerSecond = 10'000'000;
        /** @brief 마지막 쓰기 시각을 파일 시계의 100 ns 눈금으로 얻습니다. 없거나 읽지 못하면 false 입니다. 같은 기계 · 같은 플랫폼 안에서만 견줍니다. */
        [[nodiscard]] static bool getFileWriteTime( string_view path, int64& outTicks );
        /** @brief 마지막 쓰기 시각을 바꿉니다(`getFileWriteTime` 과 같은 눈금). 바꾸지 못했으면 경로와 이유를 알리고 false 입니다. */
        [[nodiscard]] static bool setFileWriteTime( string_view path, int64 ticks );
        /** @brief 지금 시각을 `getFileWriteTime` 과 같은 시계 · 눈금으로 반환합니다. */
        static int64 getCurrentFileWriteTime();
        /** @brief 파일 크기를 반환합니다. */
        static uint64 getFileSize( string_view fileName );
        /**
         * @brief 파일의 크기와 마지막 쓰기 시각을 **한 번의 조회**로 얻습니다. 파일이 없거나 읽을 수 없으면 false 입니다.
         * @details 파일 내용에서 뽑은 값(해시 등)을 캐시해 두고 "그 뒤로 파일이 바뀌었나" 만 확인할 때 씁니다. 파일을 여는 것보다
         *          쌉니다(이 PC 에서 열기는 ~200 us, 이 조회는 ~75 us 입니다. 둘 다 필터 드라이버를 거칩니다).
         */
        static bool getFileStamp( string_view fileName, FileStamp& outStamp );
        /** @brief 파일을 복사합니다(대상이 있으면 덮어씁니다). 실패하면 로그를 남기고 false 입니다. */
        [[nodiscard]] static bool copyFile( string_view source, string_view destination );
        /** @brief 파일을 삭제합니다. 없었거나 삭제했으면 true 이고, 지우지 못했으면 경로와 이유를 알리고 false 입니다. */
        [[nodiscard]] static bool removeFile( string_view path );
        /**
         * @brief 파일 삭제를 시도만 합니다 — 지우지 못해도 알리지 않습니다. 없었거나 삭제했으면 true 입니다.
         * @details 실패가 예상된 정리에만 씁니다(아직 매핑된 모듈 DLL 의 그림자 사본 — 다음 시작 · 종료의 정리가 지운다). 그 밖은 `removeFile`.
         */
        [[nodiscard]] static bool tryRemoveFile( string_view path );
        /** @brief 디렉터리를 재귀적으로 삭제합니다. 없었거나 삭제했으면 true 입니다. */
        [[nodiscard]] static bool removeDirectory( string_view path );
        /** @brief 시스템 임시 디렉터리 경로를 반환합니다. */
        static string getTempDirectory();
        /**
         * @brief 바이너리 데이터를 파일에 씁니다. **원자적입니다**: 같은 폴더의 임시 파일에 다 쓰고 결과를 확인한 뒤 원본과 바꿔 끼웁니다.
         * @details 실패하면(디스크 가득 · 잠김 · 도중 종료) false 이고 원본은 그대로 남습니다. 반쯤 쓴 파일이 남지 않습니다.
         */
        [[nodiscard]] static bool writeFile( string_view fileName, const uint8* pData, uint64 size );
        /**
         * @brief 파일의 [offset, offset + maxReadCount) 를 outBytes 에 담습니다.
         * @details 잰 크기만큼 읽지 못하면(읽기 오류 · 읽는 도중 파일이 줄어듦) false 입니다. 인자는 64비트라 4 GB 이상의 파일도 다룹니다.
         */
        [[nodiscard]] static bool readFile( string_view fileName, vector<uint8>& outBytes, uint64 offset = 0, uint64 maxReadCount = MathUtil::kMaxUInt64 );
        /** @brief 파일 전체를 UTF-8 텍스트로 읽습니다(UTF-8 BOM 은 자동으로 뗍니다). 잘린 읽기는 false 입니다. */
        [[nodiscard]] static bool readTextFile( string_view fileName, string& outText );
        /** @brief UTF-8 텍스트를 파일로 씁니다. `writeFile` 과 같이 원자적이고, 쓰기 · 닫기 결과를 확인합니다. */
        [[nodiscard]] static bool writeTextFile( string_view fileName, string_view text );
        /** @brief 문자열이 UTF-8 BOM(0xEF, 0xBB, 0xBF)으로 시작하면 그것을 건너뛴 string_view 를 반환합니다. */
        static string_view skipUtf8Bom( string_view text );
        /** @brief 버퍼가 UTF-8 BOM(0xEF, 0xBB, 0xBF)으로 시작하면 포인터와 크기를 3바이트 건너뛰도록 고칩니다. */
        static void skipUtf8Bom( const uint8*& pData, size_t& size );

        /**
         * @brief 네이티브 파일 다이얼로그를 엽니다. **결과 델리게이트는 메인 스레드에서 불립니다.**
         * @details 다이얼로그 자체는 분리된(detached) 스레드가 띄웁니다. 네이티브 다이얼로그는 사용자가 닫을 때까지 돌아오지
         *          않으므로 그 자리에서 기다리면 프레임이 멈추기 때문입니다. 그래서 **결과만** 큐에 담고, `pumpFileDialogResults`
         *          가 메인 스레드에서 꺼내 델리게이트를 부릅니다.
         *
         *          그 스레드에서 곧바로 델리게이트를 부르면 콜백이 씬 · 컴포넌트 · 에디터 상태처럼 메인 스레드가 매 프레임
         *          만지는 것들을 **동시에** 고치게 됩니다(씬 직렬화 · 컴포넌트 역직렬화 등). 호출하는 곳마다 큐를 하나씩 두는 대신
         *          여기서 한 번에 막습니다.
         * @param params 다이얼로그 설정
         * @param onSuccess 사용자가 파일을 골랐을 때 **메인 스레드에서** 불릴 델리게이트(취소하면 불리지 않습니다)
         */
        static void openFileDialog( const FileDialogParams& params, FileDialogDelegate onSuccess );
        /**
         * @brief 완료된 파일 다이얼로그 결과를 처리합니다. **메인 스레드에서 프레임마다** 부릅니다.
         * @details 아무도 부르지 않으면 결과가 전달되지 않을 뿐 경합은 없습니다(헤드리스 · 도구 실행이 그렇습니다).
         */
        static void pumpFileDialogResults();
        /**
         * @brief 아직 전달하지 않은 파일 다이얼로그 결과를 버립니다. 종료할 때나 모듈을 언로드할 때 부릅니다.
         * @details 델리게이트는 로드 가능한 모듈(EditorModule 등) 안의 코드를 가리킬 수 있고, 델리게이트는 대상이 살아 있는지
         *          확인하지 않습니다. 그래서 그 모듈이 내려가기 전에 이 함수를 불러 연결을 끊어야 합니다. 이미 열려 있는
         *          다이얼로그의 결과도 세대 번호로 함께 버려집니다.
         */
        static void cancelFileDialogResults();
        /**
         * @brief 디렉터리를 훑으며 항목마다 @p visit 를 부릅니다. 들어갈 수 없는 하위 폴더는 건너뜁니다. 예외를 던지지 않습니다.
         * @details 순회가 도중에 멈추면(폴더가 순회 중에 지워짐 등) 경고하고 거기까지로 끝납니다. 항목 경로는 버퍼 하나를 다시 써서 넘깁니다.
         * @return 디렉터리를 열지 못했으면 false 입니다(콜백이 멈춘 것은 실패가 아닙니다).
         */
        [[nodiscard]] static bool forEachDirectoryEntry( string_view directory, bool bRecursive, DirectoryEntryVisitFn visit, void* pContext );
        /** @brief 람다 · 함수 객체를 받는 판입니다. 할당하지 않습니다(함수 객체의 주소를 문맥으로 넘긴다). `func` 는 `bool( const DirectoryEntry& )` 입니다. */
        template <typename Func>
        [[nodiscard]] static bool forEachDirectoryEntry( string_view directory, bool bRecursive, Func&& func )
        {
            using FuncType = std::remove_reference_t<Func>;
            return forEachDirectoryEntry( directory, bRecursive, []( void* pContext, const DirectoryEntry& entry ) -> bool
            { return ( *static_cast<FuncType*>( pContext ) )( entry ); },
                                          const_cast<void*>( static_cast<const void*>( std::addressof( func ) ) ) );
        }
        /**
         * @brief 디렉터리에서 확장자 필터에 맞는 파일을 모읍니다.
         * @details 실제 파일 시스템을 훑어 얻은 경로라 **대소문자를 그대로 돌려줍니다**(구분자만 `/` 로 바꿉니다). 그대로 열 수
         *          있는 경로여야 하기 때문입니다. 대소문자를 구분하는 파일 시스템에서는 소문자로 바꾼 경로가 곧 "파일 없음" 입니다.
         *          맵 키가 필요하면 받는 쪽에서 normalizePath 하십시오.
         */
        static bool collectFiles( string_view directory, string_view filterExtension, vector<string>& outListFilePath, bool bRecursive );
        /** @brief 디렉터리 아래의 폴더를 모읍니다(경로 규칙은 collectFiles 와 같습니다). */
        static bool collectFolders( string_view directory, vector<string>& outListFolder, bool bRecursive );
    };
} // namespace sw
