#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/ResourcePackReader.h"

namespace sw
{
	/**
	 * @brief 유료 DLC 소유권 검증 델리게이트 (스팀/에픽/콘솔 플랫폼 서비스 연동용)
	 * @param dlcAppId 검증 대상 DLC 식별자
	 * @return 정품 소유권이 확인되면 true, 미구매 시 false
	 */
	SW_DECLARE_DELEGATE( bool, DlcEntitlementDelegate, uint32 );

	/**
	 * @struct MountedPack
	 * @brief VFS에 마운트된 개별 팩 및 우선순위 정보
	 */
	struct MountedPack
	{
		int32						   _priority{ 0 }; ///< 높을수록 우선 탐색
		unique_ptr<ResourcePackReader> _pReader;
	};

	/**
	 * @class ResourcePackManager
	 * @brief 여러 개의 .pack 파일을 우선순위 스택으로 마운트 관리하고 O(1) VFS 읽기를 제공하는 매니저
	 */
	class SW_API ResourcePackManager
	{
	public:
		ResourcePackManager();
		~ResourcePackManager();

		ResourcePackManager( const ResourcePackManager& )			 = delete;
		ResourcePackManager& operator=( const ResourcePackManager& ) = delete;

		ResourcePackManager( ResourcePackManager&& ) noexcept;
		ResourcePackManager& operator=( ResourcePackManager&& ) noexcept;

		/**
		 * @brief .pack 파일을 VFS에 마운트합니다.
		 * @param packFilePath .pack 파일 물리 경로
		 * @param priority 우선순위 가중치 (높을수록 먼저 탐색, 기본 0)
		 * @return 마운트 성공 시 true (DLC 미소유 시 false)
		 */
		bool mountPack( string_view packFilePath, int32 priority = 0 );

		/**
		 * @brief 마운트된 .pack 파일을 언마운트하고 닫습니다.
		 */
		bool unmountPack( string_view packFilePath );

		/**
		 * @brief 모든 마운트된 팩을 언마운트합니다.
		 */
		void unmountAll();

		/**
		 * @brief 마운트된 팩들 전체에서 가상 상대 경로 파일의 존재 여부를 확인합니다.
		 */
		bool hasFile( string_view relativePath ) const;

		/**
		 * @brief 마운트된 팩들의 우선순위 순서대로 파일을 탐색하여 바이너리 데이터를 로드합니다.
		 * @param relativePath 가상 상대 경로 (예: "maps/0.title.scene.xml")
		 * @param outBytes 읽은 데이터 버퍼
		 * @return 파일 로드 및 무결성 검증 성공 시 true
		 */
		bool readFile( string_view relativePath, vector<uint8>& outBytes ) const;

		/**
		 * @brief 마운트된 팩들에서 텍스트(UTF-8) 파일을 로드합니다.
		 * @param relativePath 가상 상대 경로
		 * @param outText 읽은 본문
		 * @param pOutMountedPackPath 실제 해당 파일을 로드한 .pack 파일 경로 (nullable)
		 */
		bool readTextFile( string_view relativePath, string& outText, string* pOutMountedPackPath = nullptr ) const;

		/**
		 * @brief DLC 소유권 검증 콜백을 등록합니다.
		 */
		void setDlcEntitlementValidator( DlcEntitlementDelegate validator );

		/**
		 * @brief 낱개 파일(Loose File) 우선 로드 허용 여부 설정 (모딩/개발 편의용)
		 */
		void setAllowLooseFiles( bool bAllow );

		/** @brief 낱개 파일 우선 로드 허용 여부 */
		bool isAllowLooseFiles() const;

		/** @brief 현재 마운트된 팩 개수 */
		size_t getMountedPackCount() const;

		/**
		 * @brief 지정된 디렉터리 내의 .pack 파일들을 스캔하여 표준 우선순위에 따라 자동 마운트합니다.
		 * @param packsDirectory .pack 파일들이 위치한 디렉터리 (예: "Bin/Packs")
		 * @param listPriority EngineConfig._listResourcePriority 우선순위 토큰 목록
		 * @return 1개 이상의 팩이 성공적으로 마운트되면 true
		 */
		bool scanAndMountPacks( string_view packsDirectory, const vector<string>& listPriority );

	private:
		mutable mutex		   _vfsMutex;
		vector<MountedPack>	   _listMountedPack;
		DlcEntitlementDelegate _dlcValidator;
		bool				   _bAllowLooseFiles;
	};

} // namespace sw
