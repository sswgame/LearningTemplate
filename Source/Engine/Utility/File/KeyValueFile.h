/**
 * @file KeyValueFile.h
 * @brief 공유 key=value 텍스트 테이블 파싱/로드 (세이브 슬롯, 에디터 ini)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

namespace sw
{
	/**
	 * @brief 투명 std::less<> (C++14/17) — 임시 키 없이 find(string_view/const utf8*).
	 * @note C++20 unordered_map 이종 조회는 C++17 빌드 때문에 쓰지 않습니다.
	 */
	using KeyValueMap = map<string, string, std::less<>>;

	// ------------------------------------------------------------------------------
	// 1) KeyValueParseOptions — 주석/섹션 스킵
	// ------------------------------------------------------------------------------
	struct KeyValueParseOptions
	{
		utf8				   _commentChar;
		uint8				   _bSkipSemicolonComments : 1; ///< ';'로 시작하는 줄도 스킵
		uint8				   _bSkipBracketSections   : 1; ///< "[section]" 줄 스킵
		[[maybe_unused]] uint8 _reserved			   : 6;

		/** @brief 세미콜론 주석과 브래킷 섹션을 스킵하는 기본값. */
		KeyValueParseOptions() noexcept
			: _commentChar{ '#' }
			, _bSkipSemicolonComments{ SW_TRUE }
			, _bSkipBracketSections{ SW_TRUE }
			, _reserved{ 0 } {}
	};

	/**
	 * @class KeyValueFile
	 * @brief `key=value` 줄을 파싱하고 절대/리소스 상대 경로에서 로드합니다
	 */
	class SW_API KeyValueFile
	{
	public:
		// ------------------------------------------------------------------------------
		// 2) 파싱 · 로드
		// ------------------------------------------------------------------------------
		/** @brief 텍스트를 out에 파싱합니다 (먼저 clear). */
		static bool parse( string_view text, KeyValueMap& mapOut, KeyValueParseOptions opt = {} );

		/** @brief 절대 경로를 읽고 파싱합니다. */
		static bool loadFile( string_view absPath, KeyValueMap& mapOut, KeyValueParseOptions opt = {} );

		/** @brief ResourceUtil로 해석한 뒤 loadFile합니다. */
		static bool loadResource( string_view relativePath, KeyValueMap& mapOut, KeyValueParseOptions opt = {},
								  string* pOutAbsPath = nullptr );

		/**
		 * @brief 절대/작업 경로가 있으면 loadFile, 없으면 loadResource.
		 */
		static bool loadPath( string_view path, KeyValueMap& mapOut, KeyValueParseOptions opt = {},
							  string* pOutAbsPath = nullptr );

		// ------------------------------------------------------------------------------
		// 3) 조회
		// ------------------------------------------------------------------------------
		/** @brief 키의 문자열 값을 반환합니다. 없으면 fallback. */
		static const utf8* get( const KeyValueMap& mapData, string_view key, const utf8* pFallback = "" );
		/** @brief 키의 int32 값을 반환합니다. 없으면 fallback. */
		static int32 getInt( const KeyValueMap& mapData, string_view key, int32 fallback = 0 );
		/** @brief 키의 float32 값을 반환합니다. 없으면 fallback. */
		static float32 getFloat( const KeyValueMap& mapData, string_view key, float32 fallback = 0.0f );
		/** @brief 키의 bool 값을 반환합니다 (1/true/yes/on). */
		static bool getBool( const KeyValueMap& mapData, string_view key, bool fallback = false );

		// ------------------------------------------------------------------------------
		// 4) 쓰기
		// ------------------------------------------------------------------------------
		/**
		 * @brief key=value 텍스트를 만듭니다.
		 * @param headerComment 비어 있지 않으면 `# ` 접두(이미 #이면 그대로) 뒤 개행.
		 * @param sectionName 비어 있지 않으면 `[sectionName]` 줄을 넣습니다.
		 */
		static string dump( const KeyValueMap& mapData, string_view headerComment = {}, string_view sectionName = {} );
		/** @brief dump 결과를 절대 경로에 씁니다. */
		static bool saveFile( string_view absPath, const KeyValueMap& mapData, string_view headerComment = {},
							  string_view sectionName = {} );

		/**
		 * @brief 비어 있지 않고 주석이 아닌 각 줄에 fn(trimmedLine)을 호출합니다.
		 * @note 브래킷 섹션 헤더는 그대로 넘깁니다 (fn이 결정).
		 */
		template <typename Fn>
		static void forEachContentLine( string_view text, Fn&& fn, utf8 commentChar = '#' )
		{
			size_t begin{ 0 };
			while ( begin <= text.size() )
			{
				size_t end = text.find( '\n', begin );
				if ( end == string_view::npos )
					end = text.size();

				string_view line = text.substr( begin, end - begin );
				if ( line.empty() == false && line.back() == '\r' )
					line.remove_suffix( 1 );

				// 줄마다 할당하지 않도록 인라인 trim
				while ( line.empty() == false && ( line.front() == ' ' || line.front() == '\t' ) )
				{
					line.remove_prefix( 1 );
				}
				while ( line.empty() == false && ( line.back() == ' ' || line.back() == '\t' ) )
				{
					line.remove_suffix( 1 );
				}

				if ( line.empty() == false && line.front() != commentChar )
					fn( line );

				if ( end == text.size() )
					break;
				begin = end + 1;
			}
		}
	};
} // namespace sw
