#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Resource/ResourcePackTypes.h"

namespace sw
{
	/**
	 * @class ResourcePackReader
	 * @brief 단일 .pack(SWPK) 바이너리 아카이브를 열고 64비트 해시 기반 O(1) 파일 읽기를 수행하는 VFS 리더
	 */
	class SW_API ResourcePackReader
	{
	public:
		ResourcePackReader();
		~ResourcePackReader();

		ResourcePackReader( const ResourcePackReader& )			   = delete;
		ResourcePackReader& operator=( const ResourcePackReader& ) = delete;

		ResourcePackReader( ResourcePackReader&& other ) noexcept;
		ResourcePackReader& operator=( ResourcePackReader&& other ) noexcept;

		/**
		 * @brief .pack 파일을 열고 헤더와 FAT 인덱스 테이블을 메모리에 로드합니다.
		 * @param packFilePath .pack 파일의 물리 경로
		 * @return 유효한 SWPK 아카이브이고 인덱스 로드 성공 시 true
		 */
		bool open( string_view packFilePath );

		/**
		 * @brief 열려 있는 팩 파일을 닫고 인덱스 메모리를 해제합니다.
		 */
		void close();

		/** @brief 현재 팩 파일이 열려있는지 여부 */
		bool isOpen() const;

		/** @brief 64비트 경로 해시로 파일 존재 여부 확인 (O(1)) */
		bool hasFile( uint64 pathHash ) const;

		/** @brief 가상 상대 경로로 파일 존재 여부 확인 (O(1)) */
		bool hasFile( string_view relativePath ) const;

		/** @brief 파일 엔트리 메타데이터 조회 */
		bool getFileEntry( uint64 pathHash, PackFileEntry& outEntry ) const;
		bool getFileEntry( string_view relativePath, PackFileEntry& outEntry ) const;

		/**
		 * @brief 팩 내 파일 데이터를 읽고 CRC32 검증 및 압축을 해제하여 반환합니다.
		 * @param pathHash 64비트 경로 해시
		 * @param outBytes 압축 해제된 원본 데이터 버퍼
		 * @return 파일 읽기 및 CRC32 무결성 검증 성공 시 true
		 */
		bool readFile( uint64 pathHash, vector<uint8>& outBytes ) const;

		/** @brief 가상 상대 경로로 파일 읽기 */
		bool readFile( string_view relativePath, vector<uint8>& outBytes ) const;

		/** @brief 가상 상대 경로로 텍스트 파일(UTF-8) 읽기 */
		bool readTextFile( string_view relativePath, string& outText ) const;

		/** @brief 팩 헤더 정보 반환 */
		const PackHeader& getHeader() const;

		/** @brief DLC 식별자 (0 = 본편, >0 = DLC AppID) */
		uint32 getDlcAppId() const;

		/** @brief 팩에 포함된 파일 총 개수 */
		uint32 getFileCount() const;

		/** @brief 마운트된 .pack 파일의 물리 경로 */
		const string& getPackPath() const;

		/** @brief 팩 내 모든 FAT 엔트리 맵 반환 (디버깅/테스트용) */
		const unordered_map<uint64, PackFileEntry>& getMapEntry() const;

	private:
		bool loadIndexTable();
		bool decompressData( PackCompressionType type, const uint8* pSrc, size_t srcSize, void* pDst, size_t dstSize ) const;

	private:
		mutable mutex						 _fileMutex;
		void*								 _pFileHandle; ///< 내부 FILE* 포인터
		string								 _packFilePath;
		PackHeader							 _header;
		unordered_map<uint64, PackFileEntry> _mapEntry;
		vector<utf8>						 _stringPoolBytes;
	};

} // namespace sw
