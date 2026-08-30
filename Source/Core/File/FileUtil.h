/**
 * @file FileUtil.h
 * @brief 경로·파일 I/O·다이얼로그·동적 라이브러리 유틸
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{

	// ------------------------------------------------------------------------------
	// 1) FileDialogParams — Open/Save, 필터·시작 폴더·다중 선택
	// ------------------------------------------------------------------------------
	/** @brief 네이티브 파일 열기/저장 다이얼로그 파라미터 */
	struct FileDialogParams
	{
		/** @brief 열기 또는 저장입니다. */
		enum class Type : uint8
		{
			Open,
			Save,
		};

		Type		   _type{ Type::Open };			///< Open 또는 Save
		string		   _title;						///< 다이얼로그 창 제목 (비어 있으면 OS 기본)
		string		   _description;				///< 필터 설명 문자열
		vector<string> _listFilterExtension;		///< 허용 확장자 목록 (예: ".png", "hlsl")
		string		   _initialDirectory;			///< 시작 폴더 (비어 있으면 OS 기본)
		bool		   _bEnableMultiselect{ true }; ///< 다중 선택 허용 (Open만)
	};

	SW_DECLARE_DELEGATE( void, FileDialogDelegate, const vector<string>& fileName );

	// ------------------------------------------------------------------------------
	// 2) FileUtil — 경로 분해·정규화 · 존재/I/O · 다이얼로그 · DLL
	//    전부 static. 맵 키는 normalizePath, open 은 normalizeSeparators
	// ------------------------------------------------------------------------------
	/** @brief 경로·파일·클립보드·공유 라이브러리 정적 유틸 */
	struct SW_API FileUtil
	{
		/** @brief 현재 시각을 문자열로 반환합니다. */
		static string getCurrentDateTimeAsString();

		/** @brief 전체 경로를 디렉터리와 파일명으로 나눕니다. */
		static void splitPath( string_view fullPath, string& outDirectoryPath, string& outFileName );
		/** @brief 전체 경로를 디렉터리와 파일명 뷰로 나눕니다 (Zero Allocation). */
		static void splitPath( string_view fullPath, string_view& outDirectoryPath, string_view& outFileName );
		/** @brief 경로에서 파일명만 반환합니다. */
		static string getFileNamePart( string_view fullPath );
		/** @brief 경로에서 파일명 부분의 뷰를 출력 매개변수로 반환합니다 (Zero Allocation). */
		static void getFileNamePart( string_view fullPath, string_view& outFileName );
		/** @brief 경로에서 파일명 문자열을 출력 매개변수로 반환합니다. */
		static void getFileNamePart( string_view fullPath, string& outFileName );
		/** @brief 경로에서 디렉터리만 반환합니다. */
		static string getDirectoryPart( string_view fullPath );
		/** @brief 경로에서 디렉터리 부분의 뷰를 출력 매개변수로 반환합니다 (Zero Allocation). */
		static void getDirectoryPart( string_view fullPath, string_view& outDirectoryPath );
		/** @brief 경로에서 디렉터리 문자열을 출력 매개변수로 반환합니다. */
		static void getDirectoryPart( string_view fullPath, string& outDirectoryPath );
		/** @brief 파일명의 확장자 토큰을 수집합니다. */
		static void getExtensionPart( string_view fileName, vector<string>& outListPart );
		/** @brief 파일 경로의 마지막 확장자(예: ".hlsl")를 포함하여 반환합니다. 없을 경우 빈 문자열입니다. */
		static string getExtension( string_view fileName );
		/** @brief 파일 경로의 마지막 확장자 뷰를 출력 매개변수로 반환합니다 (Zero Allocation). */
		static void getExtension( string_view fileName, string_view& outExtension );
		/** @brief 파일 경로의 마지막 확장자 문자열을 출력 매개변수로 반환합니다. */
		static void getExtension( string_view fileName, string& outExtension );
		/** @brief 경로가 지정된 확장자로 끝나는지 대소문자 구분 없이 확인합니다. (앞에 '.' 생략 가능) */
		static bool hasExtension( string_view fileName, string_view extension );
		/** @brief 지정된 확장자 중 하나와 대소문자 무시로 일치하는지 확인합니다. */
		static bool hasAnyExtension( string_view fileName, std::initializer_list<string_view> listExtension );
		/** @brief 경로가 지정된 접미사(다중 확장자 포함, 예: ".prefab.xml")로 끝나는지 대소문자 구분 없이 확인합니다. */
		static bool endsWithIgnoreCase( string_view path, string_view suffix );
		/** @brief 지정된 접미사 중 하나와 대소문자 무시로 끝나는지 확인합니다. */
		static bool endsWithAnyIgnoreCase( string_view path, std::initializer_list<string_view> listSuffix );
		/** @brief 확장자를 교체합니다. */
		static string replaceExtension( string_view fileName, string_view extension );
		/** @brief 확장자를 제거합니다. */
		static string removeExtension( string_view fileName );
		/** @brief 확장자를 제거한 본문 뷰를 출력 매개변수로 반환합니다 (Zero Allocation). */
		static void removeExtension( string_view fileName, string_view& outFileName );
		/** @brief 확장자를 제거한 본문 문자열을 출력 매개변수로 반환합니다. */
		static void removeExtension( string_view fileName, string& outFileName );

		/** @brief rootDir 기준 상대 경로로 만듭니다. */
		static bool makePathRelative( string_view rootDir, string_view path, string& outResult );
		/** @brief 절대 경로로 만듭니다. */
		static bool makePathAbsolute( string_view path, string& outResult );
		/**
		 * @brief 비교/맵 키용 경로 정규화 (`\`→`/` + 소문자).
		 * @note Linux/macOS I/O에는 사용하지 말 것. open에는 normalizeSeparators 또는 실제 FS 경로를 쓴다.
		 */
		static string normalizePath( string_view path );
		/** @brief I/O용 구분자만 정규화 (`\`→`/`). 대소문자는 유지한다. */
		static string normalizeSeparators( string_view path );
		/** @brief OS 네이티브 경로 구분자로 변환합니다 (Windows: `\`, POSIX: `/`). */
		static string toNativeSeparators( string_view path );
		/** @brief normalizePath 기준 경로 동등 비교 */
		static bool pathsEqualNormalized( string_view lhs, string_view rhs );
		/**
		 * @brief 경로 끝의 `/`·`\` 를 제거합니다 (루트 `/` 는 유지).
		 */
		static string trimTrailingSlashes( string_view path );
		/**
		 * @brief 루트와 상대 경로를 `/` 로 이어 붙입니다.
		 * @param root 절대/상대 루트 (끝 슬래시·구분자는 정규화)
		 * @param relative 루트 아래 상대 경로 (선행 슬래시 허용, 비우면 root만)
		 * @return `root` 또는 `root/relative`. root가 비면 empty.
		 */
		static string joinPath( string_view root, string_view relative );
		/**
		 * @brief `path`가 `component` 자체이거나 `component/` 로 시작하는지 (경로 세그먼트 경계).
		 * @note `games` vs `gamesfoo` 같은 접두어 오탐을 막습니다.
		 */
		static bool startsWithPathComponent( string_view path, string_view component );
		/**
		 * @brief `component/` 뒤의 상대 경로를 반환합니다 (`component`만 있으면 empty).
		 */
		static string suffixAfterPathComponent( string_view path, string_view component );
		/**
		 * @brief 파일 경로의 상위 디렉터리를 생성합니다.
		 * @note `path`가 파일이면 부모만 만듭니다. 폴더 자체를 만들려면 ensureDirectoryExists.
		 */
		static void createDirectory( string_view path );
		/** @brief 디렉터리 경로 자체를 생성합니다(필요 시 상위 포함). */
		static void ensureDirectoryExists( string_view directoryPath );
		/** @brief 경로에 일반 파일이 존재하는지 여부를 반환합니다. */
		static bool fileExists( string_view fileName );
		/** @brief 경로에 디렉터리가 존재하는지 여부를 반환합니다. */
		static bool directoryExists( string_view path );
		/** @brief 현재 작업 디렉터리를 반환합니다. */
		static string getCurrentPath();
		/** @brief 실행 파일 경로를 반환합니다. */
		static string getExecutablePath();

		/** @brief 파일 수정 시각(타임스탬프)을 반환합니다. */
		static uint64 getFileTimestamp( string_view fileName );
		/** @brief 파일 크기를 반환합니다. */
		static uint64 getFileSize( string_view fileName );
		/** @brief 파일을 복사합니다. */
		static bool copyFile( string_view source, string_view destination );
		/** @brief 파일을 삭제합니다. 없거나 삭제되면 true. */
		static bool removeFile( string_view path );
		/** @brief 디렉터리를 재귀적으로 삭제합니다. 없거나 삭제되면 true. */
		static bool removeDirectory( string_view path );
		/** @brief 시스템 임시 디렉터리 경로를 반환합니다. */
		static string getTempDirectory();
		/** @brief 바이너리 데이터를 파일에 씁니다. */
		static bool writeFile( string_view fileName, const uint8* pData, uint64 size );
		/** @brief 파일을 읽어 outData에 담습니다. */
		static bool readFile( string_view fileName, vector<uint8>& outData, uint32 offset = 0, uint32 maxReadCount = invalid_index::kUint32 );
		/** @brief 파일 전체를 텍스트(UTF-8)로 읽어 반환합니다 (UTF-8 BOM 자동 제거). */
		static bool readTextFile( string_view fileName, string& outText );
		/** @brief 텍스트(UTF-8) 데이터를 파일로 씁니다. */
		static bool writeTextFile( string_view fileName, string_view text );
		/** @brief UTF-8 BOM(0xEF, 0xBB, 0xBF)이 문자열 시작 부분에 포함되어 있다면 이를 건너뛴 string_view를 반환합니다. */
		static string_view skipUtf8Bom( string_view text );
		/** @brief UTF-8 BOM(0xEF, 0xBB, 0xBF)이 버퍼 시작 부분에 포함되어 있다면 포인터와 크기를 3바이트 건너뛰도록 조정합니다. */
		static void skipUtf8Bom( const uint8*& pData, size_t& size );

		/** @brief 네이티브 파일 다이얼로그를 엽니다. */
		static void openFileDialog( const FileDialogParams& params, FileDialogDelegate onSuccess );
		/** @brief 디렉터리에서 확장자 필터에 맞는 파일을 수집합니다. */
		static bool collectFiles( string_view directory, string_view filterExtension, vector<string>& outListFilePath, bool bRecursive, bool bNormalizePath = true );
		/** @brief 디렉터리 하위의 폴더를 수집합니다. */
		static bool collectFolders( string_view directory, vector<string>& outListFolder, bool bRecursive, bool bNormalizePath = true );

		/** @brief 클립보드 텍스트를 반환합니다. */
		static string getClipboardText();
		/** @brief 클립보드 텍스트를 설정합니다. */
		static bool setClipboardText( string_view str );

		/** @brief 플랫폼 공유 라이브러리 접두사(예: lib)를 반환합니다. */
		static string_view getSharedLibraryPrefix();
		/** @brief 플랫폼 공유 라이브러리 확장자(예: .dll)를 반환합니다. */
		static string_view getSharedLibraryExtension();
		/** @brief baseName에 접두사·확장자를 붙여 공유 라이브러리 이름을 만듭니다. */
		static string formatSharedLibraryName( string_view baseName );
		/**
		 * @brief 라이브러리에 대응하는 사이드카 디버그 심볼 경로를 반환합니다.
		 * @note Windows: `.pdb` / macOS: `.dSYM` / Linux: `.debug` (없으면 DWARF가 .so에 내장된 경우가 많음)
		 */
		static string getDebugSymbolPath( string_view libraryPath );

		/** @brief 동적 라이브러리를 로드합니다. */
		static void* loadDynamicLibrary( string_view libraryName );
		/** @brief 동적 심볼 주소를 조회합니다. */
		static void* getDynamicSymbol( void* pHandle, string_view symbolName );
		/** @brief 이전에 로드된 동적 라이브러리를 메모리에서 해제합니다. */
		static void unloadDynamicLibrary( void* pHandle );
	};
} // namespace sw
