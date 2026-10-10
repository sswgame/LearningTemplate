#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Pack/ResourcePackReader.h"

namespace sw
{
    /**
     * @brief 유료 DLC 소유권을 확인하는 델리게이트입니다(스팀 · 에픽 · 콘솔 플랫폼 서비스 연동용).
     * @param dlcAppID 확인할 DLC 식별자
     * @return 소유가 확인되면 true, 구매하지 않았으면 false
     */
    SW_DECLARE_DELEGATE( bool, DlcEntitlementDelegate, uint32 );

    /**
     * @struct MountedPack
     * @brief VFS 에 마운트된 팩 하나와 그 우선순위입니다.
     */
    struct MountedPack
    {
        string                         _domainName{};  ///< 팩 도메인 식별자(예: "engine", "common", "game_empty")
        int32                          _priority{ 0 }; ///< 높을수록 우선 탐색
        shared_ptr<ResourcePackReader> _pReader;       ///< 읽는 쪽이 잠금 밖에서 쓰는 동안 언마운트돼도 살아 있게 함께 든다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ResourcePackManager
     * @brief 여러 .pack 파일을 우선순위 스택으로 마운트하고 O(1) VFS 읽기를 제공하는 매니저입니다.
     */
    class SW_API ResourcePackManager
    {
    public:
        ResourcePackManager();
        ~ResourcePackManager();

        ResourcePackManager( const ResourcePackManager& )            = delete;
        ResourcePackManager& operator=( const ResourcePackManager& ) = delete;

        ResourcePackManager( ResourcePackManager&& ) noexcept;
        ResourcePackManager& operator=( ResourcePackManager&& ) noexcept;

        /**
         * @brief .pack 파일을 VFS 에 마운트합니다.
         * @param packFilePath .pack 파일의 실제 경로
         * @param priority 우선순위 가중치(높을수록 먼저 찾습니다. 기본 0)
         * @return 마운트에 성공하면 true 입니다(DLC 를 소유하지 않았으면 false).
         */
        bool mountPack( string_view packFilePath, int32 priority = 0 );

        /**
         * @brief 마운트된 .pack 파일을 언마운트하고 닫습니다.
         */
        bool unmountPack( string_view packFilePath );

        /**
         * @brief 마운트된 팩을 모두 언마운트합니다.
         */
        void unmountAll();

        /**
         * @brief 마운트된 팩 전체에서 가상 상대 경로의 파일이 있는지 확인합니다.
         */
        bool hasFile( string_view relativePath ) const;

        /**
         * @brief 마운트된 팩을 우선순위 순서대로 찾아 파일의 바이너리 데이터를 로드합니다.
         * @param relativePath 가상 상대 경로(예: "maps/0.title.scene.xml")
         * @param outBytes 읽은 데이터 버퍼
         * @return 파일을 읽고 무결성 검증에 성공하면 true 입니다.
         */
        [[nodiscard]] bool readFile( string_view relativePath, vector<uint8>& outBytes ) const;

        /**
         * @brief 마운트된 팩에서 텍스트(UTF-8) 파일을 로드합니다.
         * @param relativePath 가상 상대 경로
         * @param outText 읽은 본문
         * @param pOutMountedPackPath 실제로 그 파일을 읽은 .pack 파일 경로(nullptr 가능)
         */
        [[nodiscard]] bool readTextFile( string_view relativePath, string& outText, string* pOutMountedPackPath = nullptr ) const;

        /**
         * @brief 마운트된 팩을 우선순위 순서대로 찾아 그 항목을 @p io 로 비동기로 읽습니다(`ResourcePackReader::readFileAsync`).
         * @return 건 읽기의 핸들. 어느 팩에도 없으면 아무것도 걸지 않고 `isValid() == false` 인 핸들입니다(@p onComplete 는 불리지 않는다).
         */
        AsyncReadHandle readFileAsync( AsyncFileIO& io, string_view relativePath, AsyncIOPriority priority, const ResourceReadCompleteDelegate& onComplete ) const;

        /**
         * @brief DLC 소유권 검증 콜백을 등록합니다.
         */
        void setDlcEntitlementValidator( DlcEntitlementDelegate validator );

        /**
         * @brief 낱개 파일(loose file)을 먼저 읽을지 설정합니다(모딩 · 개발 편의용).
         */
        void setAllowLooseFiles( bool bAllow );

        /** @brief 낱개 파일을 먼저 읽는지 반환합니다. */
        bool isAllowLooseFiles() const;

        /** @brief 현재 마운트된 팩 개수를 반환합니다. */
        size_t getMountedPackCount() const;

        /**
         * @brief 디렉터리 안의 .pack 파일을 훑어 표준 우선순위로 마운트합니다.
         * @param packsDirectory .pack 파일들이 있는 디렉터리(예: "Bin/Packs")
         * @param listPriority EngineConfig._listResourcePriority 우선순위 토큰 목록
         * @return 팩을 하나 이상 마운트하면 true 입니다.
         */
        bool scanAndMountPacks( string_view packsDirectory, const vector<string>& listPriority );

    private:
        /**
         * @brief 경로를 가진 팩의 리더를 잠금 안에서 찾아 사본을 돌려줍니다. 읽기는 부르는 쪽이 잠금 밖에서 합니다 — 한 팩의 읽기가 다른 팩
         *        · 다른 스레드의 읽기를 줄 세우지 않는다.
         * @param outPathHash 그 팩 안에서 찾은 해시(전체 경로 또는 도메인을 뗀 경로)
         * @param outPathInPack 그 팩 안의 경로(@p relativePath 를 가리킨다)
         */
        shared_ptr<ResourcePackReader> findReaderWithFile( string_view relativePath, uint64& outPathHash, string_view& outPathInPack ) const;

        mutable mutex          _vfsMutex;
        vector<MountedPack>    _listMountedPack;
        DlcEntitlementDelegate _dlcValidator;
        bool                   _bAllowLooseFiles;
    };

} // namespace sw
