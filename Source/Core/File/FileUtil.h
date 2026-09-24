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

    SW_DECLARE_DELEGATE( void, FileDialogDelegate, const vector<string>& fileName );

    /**
     * @brief 파일 하나의 크기와 마지막 쓰기 시각입니다. "지난번에 본 그 파일 그대로인가" 를 파일을 열지 않고 확인할 때 씁니다.
     * @details 시각은 플랫폼 고유의 단위 그대로입니다(Windows 는 100 ns, 그 밖은 `std::filesystem` 파일 시계). 같은 기계 ·
     *          같은 실행 안에서 **같은지만** 비교합니다. `getFileTimestamp` 는 초 단위라 1초 안에 일어난 편집을 놓칩니다.
     */
    struct FileStamp
    {
        uint64 _size{ 0 };      ///< 바이트 수
        uint64 _writeTime{ 0 }; ///< 마지막 쓰기 시각(플랫폼 고유 단위)

        /** @brief 크기와 시각이 모두 같은지 확인합니다. */
        bool operator==( const FileStamp& other ) const { return _size == other._size && _writeTime == other._writeTime; }
        /** @brief 크기나 시각이 다른지 확인합니다. */
        bool operator!=( const FileStamp& other ) const { return ( *this == other ) == false; }
    };

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
        /** @brief 경로가 절대 경로인지 확인합니다. */
        static bool isAbsolutePath( string_view path );
        /**
         * @brief 비교 · 맵 키용으로 경로를 정규화합니다(`\` 를 `/` 로 바꾸고 소문자로).
         * @note Linux · macOS 의 파일 I/O 에는 쓰지 마십시오. 파일을 열 때는 normalizeSeparators 나 실제 파일 시스템 경로를 씁니다.
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
         * @brief 대상 파일이 들어갈 상위 디렉터리를 만듭니다.
         * @param filePath 대상 파일 경로(상위 디렉터리가 없으면 재귀적으로 만듭니다)
         */
        static void createParentDirectory( string_view filePath );
        /** @brief 디렉터리 경로 자체를 만듭니다(필요하면 상위 디렉터리도). */
        static void ensureDirectoryExists( string_view directoryPath );
        /** @brief 경로에 일반 파일이 있는지 반환합니다. */
        static bool fileExists( string_view fileName );
        /** @brief 경로에 디렉터리가 있는지 반환합니다. */
        static bool directoryExists( string_view path );
        /** @brief 현재 작업 디렉터리를 반환합니다. */
        static string getCurrentPath();
        /** @brief 실행 파일의 경로를 반환합니다. */
        static string getExecutablePath();

        /** @brief 파일의 수정 시각을 초 단위로 반환합니다. */
        static uint64 getFileTimestamp( string_view fileName );
        /**
         * @brief 지금 시각을 `getFileTimestamp` 와 **같은 시계 · 같은 단위**로 반환합니다.
         * @details 파일 시각은 `std::filesystem` 의 파일 시계로 나옵니다. `system_clock` 과 기준점이 다를 수 있어 그쪽의 `now()`
         *          와 비교하면 틀립니다. "이 시각 이후 바뀐 파일" 을 고를 때는 이 함수를 쓰십시오.
         */
        static uint64 getCurrentFileTimestamp();
        /** @brief 파일 크기를 반환합니다. */
        static uint64 getFileSize( string_view fileName );
        /**
         * @brief 파일의 크기와 마지막 쓰기 시각을 **한 번의 조회**로 얻습니다. 파일이 없거나 읽을 수 없으면 false 입니다.
         * @details 파일 내용에서 뽑은 값(해시 등)을 캐시해 두고 "그 뒤로 파일이 바뀌었나" 만 확인할 때 씁니다. 파일을 여는 것보다
         *          쌉니다(이 PC 에서 열기는 ~200 us, 이 조회는 ~75 us 입니다. 둘 다 필터 드라이버를 거칩니다).
         */
        static bool getFileStamp( string_view fileName, FileStamp& outStamp );
        /** @brief 파일을 복사합니다. */
        static bool copyFile( string_view source, string_view destination );
        /** @brief 파일을 삭제합니다. 없었거나 삭제했으면 true 입니다. */
        static bool removeFile( string_view path );
        /** @brief 디렉터리를 재귀적으로 삭제합니다. 없었거나 삭제했으면 true 입니다. */
        static bool removeDirectory( string_view path );
        /** @brief 시스템 임시 디렉터리 경로를 반환합니다. */
        static string getTempDirectory();
        /** @brief 바이너리 데이터를 파일에 씁니다. */
        static bool writeFile( string_view fileName, const uint8* pData, uint64 size );
        /** @brief 파일을 읽어 outBytes 에 담습니다. */
        static bool readFile( string_view fileName, vector<uint8>& outBytes, uint32 offset = 0, uint32 maxReadCount = MathUtil::MaxUInt32 );
        /** @brief 파일 전체를 UTF-8 텍스트로 읽습니다(UTF-8 BOM 은 자동으로 뗍니다). */
        static bool readTextFile( string_view fileName, string& outText );
        /** @brief UTF-8 텍스트를 파일로 씁니다. */
        static bool writeTextFile( string_view fileName, string_view text );
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
         *          예전에는 그 스레드에서 곧바로 델리게이트를 불렀습니다. 그러면 콜백이 씬 · 컴포넌트 · 에디터 상태처럼 메인
         *          스레드가 매 프레임 만지는 것들을 **동시에** 고치게 됩니다(실제로 씬 직렬화와 컴포넌트 역직렬화가 그 스레드에서
         *          돌고 있었습니다). 호출하는 곳마다 큐를 하나씩 두는 대신 여기서 한 번에 막습니다.
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
         * @brief 디렉터리에서 확장자 필터에 맞는 파일을 모읍니다.
         * @details 실제 파일 시스템을 훑어 얻은 경로라 **대소문자를 그대로 돌려줍니다**(구분자만 `/` 로 바꿉니다). 그대로 열 수
         *          있는 경로여야 하기 때문입니다. 대소문자를 구분하는 파일 시스템에서는 소문자로 바꾼 경로가 곧 "파일 없음" 입니다.
         *          맵 키가 필요하면 받는 쪽에서 normalizePath 하십시오.
         */
        static bool collectFiles( string_view directory, string_view filterExtension, vector<string>& outListFilePath, bool bRecursive );
        /** @brief 디렉터리 아래의 폴더를 모읍니다(경로 규칙은 collectFiles 와 같습니다). */
        static bool collectFolders( string_view directory, vector<string>& outListFolder, bool bRecursive );

        /** @brief 플랫폼의 공유 라이브러리 접두어(예: lib)를 반환합니다. */
        static string_view getSharedLibraryPrefix();
        /** @brief 플랫폼의 공유 라이브러리 확장자(예: .dll)를 반환합니다. */
        static string_view getSharedLibraryExtension();
        /** @brief baseName 에 접두어와 확장자를 붙여 공유 라이브러리 이름을 만듭니다. */
        static string formatSharedLibraryName( string_view baseName );
        /**
         * @brief 라이브러리에 대응하는 별도 디버그 심볼 파일의 경로를 반환합니다.
         * @note Windows: `.pdb` / macOS: `.dSYM` / Linux: `.debug`(없으면 DWARF 가 .so 안에 들어 있는 경우가 많습니다)
         */
        static string getDebugSymbolPath( string_view libraryPath );

        /** @brief 동적 라이브러리를 로드합니다. */
        static void* loadDynamicLibrary( string_view libraryName );
        /** @brief 동적 라이브러리에서 심볼 주소를 찾습니다. */
        static void* getDynamicSymbol( void* pHandle, string_view symbolName );
        /** @brief 로드한 동적 라이브러리를 메모리에서 내립니다. */
        static void unloadDynamicLibrary( void* pHandle );
        /**
         * @brief 주소 @p pAddressInside 를 담은 실행 이미지(exe · DLL · SO)가 메모리에서 차지하는 범위를 찾습니다.
         * @details Windows 는 이미지 기준 주소 + `SizeOfImage`, 리눅스는 그 이미지의 적재 세그먼트(PT_LOAD) 전체입니다. 핫 리로드가
         *          "이 델리게이트 · 함수 포인터가 내리려는 모듈의 코드인가" 를 가리는 데 씁니다.
         * @return 찾지 못하면 false 입니다(그 외 플랫폼 포함).
         */
        static bool findLoadedImageRange( const void* pAddressInside, const void*& pOutBegin, const void*& pOutEnd );
        /** @brief `loadDynamicLibrary` 가 준 핸들의 이미지 범위를 찾습니다(`findLoadedImageRange` 와 같다). */
        static bool findDynamicLibraryRange( void* pHandle, const void*& pOutBegin, const void*& pOutEnd );
    };
} // namespace sw
