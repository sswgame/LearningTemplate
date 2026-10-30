#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/FileSystem/ContentSource.h"
#include "Engine/FileSystem/Pack/PackPatch.h"
#include "Engine/FileSystem/Pack/PackReader.h"

namespace sw
{
    /**
     * @brief 유료 DLC 소유권을 확인하는 델리게이트입니다(스팀 · 에픽 · 콘솔 플랫폼 서비스 연동용).
     * @param dlcAppID 확인할 DLC 식별자
     * @return 소유가 확인되면 true, 구매하지 않았으면 false
     */
    SW_DECLARE_DELEGATE( bool, DLCEntitlementDelegate, uint32 );

    /**
     * @struct MountedPack
     * @brief VFS 에 마운트된 팩 하나와 그 우선순위입니다.
     */
    struct MountedPack
    {
        string                 _domainName{};          ///< 팩이 적은 마운트 도메인(`engine` · `game/empty` · `dlc/winter`). 패치 팩은 빈 글이다(항목이 전체 경로)
        int32                  _priority{ 0 };         ///< 높을수록 우선 탐색
        shared_ptr<PackReader> _pReader;               ///< 읽는 쪽이 잠금 밖에서 쓰는 동안 언마운트돼도 살아 있게 함께 든다
        vector<uint64>         _listRemovedPathHash{}; ///< 패치 팩이 지운 전체 경로의 해시(정렬). 이 팩보다 낮은 우선순위의 같은 경로를 가린다(`mountPatchPack`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class PackManager
     * @brief 여러 .pack 파일을 우선순위 스택으로 마운트하고 O(1) VFS 읽기를 제공하는 매니저입니다.
     */
    class SW_API PackManager
    {
    public:
        PackManager();
        ~PackManager();

        PackManager( const PackManager& )            = delete;
        PackManager& operator=( const PackManager& ) = delete;

        PackManager( PackManager&& ) noexcept;
        PackManager& operator=( PackManager&& ) noexcept;

        /**
         * @brief .pack 파일을 VFS 에 마운트합니다.
         * @param packFilePath .pack 파일의 실제 경로
         * @param priority 우선순위 가중치(높을수록 먼저 찾습니다). 0 이하면 팩의 마운트 도메인과 검색 우선순위(`AssetPaths::getSearchPriority`)로 정합니다
         * @return 마운트에 성공하면 true 입니다(DLC 를 소유하지 않았으면 false).
         */
        [[nodiscard]] bool mountPack( string_view packFilePath, int32 priority = 0 );
        /**
         * @brief 패치 팩(팩 단위 차분 배포)을 마운트합니다. 팩이 담은 바뀐 파일이 아래 팩을 덮고, @p listRemovedPath 의 경로는 아래 팩에 있어도 없는 것으로 봅니다.
         * @details 지움 목록은 패치 목록 파일(`PackPatchManifest::_listRemovedPath`)에서 온다. 지운 경로는 전체 가상 경로이며 도메인 팩의 도메인을 뗀 항목도 가린다.
         *          낱개 파일(`setContentSource`)은 가리지 않는다 — 패치는 배포 구성(팩만 읽음)의 일이다.
         * @return 마운트에 성공하면 true 입니다(`mountPack` 과 같은 실패 규칙).
         */
        [[nodiscard]] bool mountPatchPack( string_view packFilePath, int32 priority, const vector<string>& listRemovedPath );

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
         * @brief 마운트된 팩 전체에서 @p relativeFolder 바로 아래 항목(하위 폴더 안은 빼고)의 리소스 경로를 @p outListPath 뒤에 덧붙입니다.
         * @param relativeFolder 가상 상대 폴더(구분자 '/', 끝 '/' 없음. 예: "common/data/appearance/presets")
         * @details 찾는 두 형식은 `hasFile` 과 같습니다 — 전체 경로로 담긴 항목, 그리고 도메인 팩(`engine.pack`)에 도메인을 떼고 담긴 항목.
         *          뒤의 것은 질의의 도메인을 다시 붙여 돌려주므로 결과는 그대로 `readFile` 에 넣을 수 있습니다. 중복 제거 · 정렬은 부르는 쪽(`AssetIO::collectAssets`)이 합니다.
         */
        void collectFolderEntries( string_view relativeFolder, vector<string>& outListPath ) const;

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
         * @brief 마운트된 팩을 우선순위 순서대로 찾아 그 항목을 @p io 로 비동기로 읽습니다(`PackReader::readFileAsync`).
         * @return 건 읽기의 핸들. 어느 팩에도 없으면 아무것도 걸지 않고 `isValid() == false` 인 핸들입니다(@p onComplete 는 불리지 않는다).
         */
        AsyncReadHandle readFileAsync( AsyncFileIO& io, string_view relativePath, AsyncIOPriority priority, const AssetReadCompleteDelegate& onComplete ) const;

        /**
         * @brief DLC 소유권 검증 콜백을 등록합니다.
         */
        void setDLCEntitlementValidator( DLCEntitlementDelegate validator );

        /** @brief 콘텐츠 정책을 정합니다. 기동 때 마운트 전에 한 번 정한다(`AssetManager::mountContent`). 만들 때의 값은 빌드 기본값(`kBuildContentSource`)이다. */
        void setContentSource( ContentSource source );
        /** @brief 콘텐츠 정책입니다. */
        ContentSource getContentSource() const;

        /** @brief 낱개 파일(소스 트리)을 읽는지 반환합니다(`ContentSource::Source`). */
        bool isAllowLooseFiles() const { return getContentSource() == ContentSource::Source; }

        /** @brief 현재 마운트된 팩 개수를 반환합니다. */
        size_t getMountedPackCount() const;

        /**
         * @brief 디렉터리 안의 .pack 파일을 훑어 표준 우선순위로 마운트하고, 패치 목록(`*.packpatch.xml`)이 있으면 그 패치를 사슬 순서로 적용합니다.
         * @param packsDirectory .pack 파일들이 있는 디렉터리(예: "Bin/Packs")
         * @return 팩을 하나 이상 마운트하면 true 입니다.
         * @details 패치 목록이 가리키는 팩은 기본 팩으로 마운트하지 않는다. 설치된 빌드는 같은 폴더의 `PackPatchUtil::kBuildRecordFileName` 이 밝히고, 거기서 이어지는 패치를
         *          모두 적용한다(`mountPatchChain`). 목록을 못 읽거나 · 빌드 기록이 없거나 · 사슬이 끊기거나 갈리거나 · 패치 팩이 없으면 이 호출이 마운트한 팩을 모두 해제하고 false 다 —
         *          패치가 덜 적용된 채 돌지 않는다.
         */
        bool scanAndMountPacks( string_view packsDirectory );

        /**
         * @brief 패치 목록의 서명을 마운트 전에 확인하게 합니다. `scanAndMountPacks` 보다 먼저, 기동 스레드에서 한 번 부릅니다.
         * @details 설정하면 `mountPatchChain` 이 팩을 하나도 올리기 전에 사슬의 모든 목록에서 서명 · 팩 해시를 확인하고, 서명이 없거나 하나라도 틀리면 실패한다
         *          (이 호출이 올린 기본 팩도 `scanAndMountPacks` 가 모두 내린다). 설정하지 않으면 서명을 보지 않는다 — 스팀처럼 스토어가 무결성을 맡는 채널의 기본이다.
         *          @p trust 의 제공자는 마운트가 끝날 때까지 살아 있어야 한다. 제공자가 없는 설정은 오류로 거절하고 false 다(서명을 요구하면서 확인하지 못하는 길을 만들지 않는다).
         */
        [[nodiscard]] bool setPatchTrust( const PackPatchTrust& trust );

    private:
        /**
         * @brief 사슬 순서 @p listOrder 의 패치마다 팩 파일을 읽어 서명을 확인합니다(신뢰 설정이 있을 때만 부른다). 하나라도 실패하면 이유를 남기고 false 입니다.
         * @details 팩 바이트는 확인한 그 읽기로만 쓰이지 않고 마운트가 다시 연다 — 확인과 마운트 사이에 파일이 바뀌는 틈(TOCTOU)은 패치 폴더가 앱 설치 폴더라는 가정이 막는다.
         */
        [[nodiscard]] bool verifyPatchSignatures( string_view packsDirectory, const vector<PackPatchManifest>& listManifest, const vector<size_t>& listOrder ) const;
        /**
         * @brief 폴더의 빌드 기록에서 설치된 빌드를 읽고 @p listManifest 의 패치를 그 빌드부터 이어지는 순서로 높은 우선순위에 마운트합니다.
         * @param listMountedPath 마운트한 패치 팩 경로를 뒤에 덧붙입니다(실패했을 때 호출자가 내린다).
         */
        [[nodiscard]] bool mountPatchChain( string_view packsDirectory, const vector<PackPatchManifest>& listManifest, vector<string>& listMountedPath );
        /** @brief 팩을 열어 지움 목록 해시(@p listRemovedPathHash, 정렬됨)와 함께 마운트합니다. `mountPack` 과 `mountPatchPack` 의 본문입니다. */
        [[nodiscard]] bool mountReader( string_view packFilePath, int32 priority, vector<uint64> listRemovedPathHash, bool bPatch );
        /**
         * @brief 경로를 가진 팩의 리더를 잠금 안에서 찾아 사본을 돌려줍니다. 읽기는 부르는 쪽이 잠금 밖에서 합니다 — 한 팩의 읽기가 다른 팩
         *        · 다른 스레드의 읽기를 줄 세우지 않는다.
         * @param outPathHash 그 팩 안에서 찾은 해시(전체 경로 또는 도메인을 뗀 경로)
         * @param outPathInPack 그 팩 안의 경로(사본 - 도메인 없는 키는 찾는 동안 만든 전체 키에서 나온다)
         */
        shared_ptr<PackReader> findReaderWithFile( string_view relativePath, uint64& outPathHash, string& outPathInPack ) const;

        mutable mutex                        _vfsMutex;
        vector<MountedPack> _listMountedPack SW_GUARDED_BY( _vfsMutex );
        DLCEntitlementDelegate               _dlcValidator;
        atomic<ContentSource>                _contentSource; ///< 기동 때 정하고 로드 스레드가 잠금 없이 읽는다 — 원자값
        PackPatchTrust                       _patchTrust;    ///< 기동 스레드가 마운트 전에 한 번 정한다(`setPatchTrust`). `_pProvider` 가 null 이면 서명을 보지 않는다
    };

} // namespace sw
