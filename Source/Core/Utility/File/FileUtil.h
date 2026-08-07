#pragma once
/**
 * @file FileUtil.h
 * @brief 경로·파일 I/O·다이얼로그·동적 라이브러리 유틸
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Utility/Delegate/Delegate.h"

namespace sw
{

	/** @brief 네이티브 파일 열기/저장 다이얼로그 파라미터 */
	struct FileDialogParams
	{
		enum class Type : uint8
		{
			Open,
			Save,
		};

		Type					 _type = Type::Open;			///< Open 또는 Save
		std::string				 _description;					///< 필터 설명 문자열
		std::vector<std::string> _filterExtensionList;			///< 허용 확장자 목록
		bool					 _bEnableMultiselect = true;	///< 다중 선택 허용
	};

	SW_DECLARE_DELEGATE( void, FileDialogDelegate, const std::vector<std::string>& fileName );

	/** @brief 경로·파일·클립보드·공유 라이브러리 정적 유틸 */
	struct SW_API FileUtil
	{
		/** @brief 현재 시각을 문자열로 반환합니다. */
		static std::string getCurrentDateTimeAsString();

		/** @brief 전체 경로를 디렉터리와 파일명으로 나눕니다. */
		static void		   splitPath( const std::string_view fullPath, std::string& outDirectoryPath, std::string& outFileName );
		/** @brief 경로에서 파일명만 반환합니다. */
		static std::string getFileNamePart( const std::string_view fullPath );
		/** @brief 경로에서 디렉터리만 반환합니다. */
		static std::string getDirectoryPart( const std::string_view fullPath );
		/** @brief 파일명의 확장자 토큰을 수집합니다. */
		static void		   getExtensionPart( const std::string_view fileName, std::vector<std::string>& outPartList );
		/** @brief 확장자를 교체합니다. */
		static std::string replaceExtension( const std::string_view fileName, const std::string_view extension );
		/** @brief 확장자가 없으면 붙이고, 있으면 교체합니다. */
		static std::string forceExtension( const std::string_view fileName, const std::string_view extension );
		/** @brief 확장자를 제거합니다. */
		static std::string removeExtension( const std::string_view fileName );

		/** @brief rootDir 기준 상대 경로로 만듭니다. */
		static bool		   makePathRelative( const std::string_view rootDir, const std::string_view path, std::string& outResult );
		/** @brief 절대 경로로 만듭니다. */
		static bool		   makePathAbsolute( const std::string_view path, std::string& outResult );
		/** @brief 경로 구분자를 정규화합니다. */
		static std::string normalizePath( const std::string_view path );
		/** @brief 디렉터리를 생성합니다(필요 시 상위 포함). */
		static void		   createDirectory( const std::string_view path );
		/** @brief 파일 존재 여부를 반환합니다. */
		static bool		   isFileExist( const std::string_view fileName );
		/** @brief 디렉터리 존재 여부를 반환합니다. */
		static bool		   isDirectoryExist( const std::string_view fileName );
		/** @brief 현재 작업 디렉터리를 반환합니다. */
		static std::string getCurrentPath();
		/** @brief 실행 파일 경로를 반환합니다. */
		static std::string getExecutablePath();

		/** @brief 파일 수정 시각(타임스탬프)을 반환합니다. */
		static uint64 getFileTimestamp( const std::string_view fileName );
		/** @brief 파일 크기를 반환합니다. */
		static uint32 getFileSize( const std::string_view fileName );
		/** @brief 파일을 복사합니다. */
		static bool	  copyFile( const std::string_view source, const std::string_view destination );
		/** @brief 바이너리 데이터를 파일에 씁니다. */
		static bool	  writeFile( const std::string_view fileName, const uint8* data, uint64 size );
		/** @brief 파일을 읽어 outData에 담습니다. */
		static bool	  readFile( const std::string_view fileName, std::vector<uint8>& outData, uint32 offset = 0, uint32 maxReadCount = invalid_index::kUint32 );

		/** @brief 네이티브 파일 다이얼로그를 엽니다. */
		static void openFileDialog( const FileDialogParams& params, FileDialogDelegate onSuccess );
		/** @brief 디렉터리에서 확장자 필터에 맞는 파일을 수집합니다. */
		static bool collectFiles( const std::string_view directory, const std::string_view filterExtension, std::vector<std::string>& outFilePathList, bool bRecursive, bool bNormalizePath = true );
		/** @brief 디렉터리 하위의 폴더를 수집합니다. */
		static bool collectFolders( const std::string_view directory, std::vector<std::string>& outFolderList, bool bRecursive, bool bNormalizePath = true );

		/** @brief 클립보드 텍스트를 반환합니다. */
		static std::string getClipboardText();
		/** @brief 클립보드 텍스트를 설정합니다. */
		static bool		   setClipboardText( const std::string& str );

		/** @brief 플랫폼 공유 라이브러리 접두사(예: lib)를 반환합니다. */
		static std::string_view getSharedLibraryPrefix();
		/** @brief 플랫폼 공유 라이브러리 확장자(예: .dll)를 반환합니다. */
		static std::string_view getSharedLibraryExtension();
		/** @brief baseName에 접두사·확장자를 붙여 공유 라이브러리 이름을 만듭니다. */
		static std::string		formatSharedLibraryName( const std::string& baseName );
		/** @brief 라이브러리에 대응하는 디버그 심볼 경로를 반환합니다. */
		static std::string		getDebugSymbolPath( const std::string_view libraryPath );

		/** @brief 동적 라이브러리를 로드합니다. */
		static void* loadDynamicLibrary( const std::string& libraryName );
		/** @brief 동적 심볼 주소를 조회합니다. */
		static void* getDynamicSymbol( void* handle, const std::string& symbolName );
		/** @brief 동적 라이브러리를 해제합니다. */
		static void	 freeDynamicLibrary( void* handle );
	};
}
