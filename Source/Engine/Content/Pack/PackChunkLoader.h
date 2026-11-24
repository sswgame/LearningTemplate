/**
 * @file PackChunkLoader.h
 * @brief 미뤄 둔 청크(`PackChunkManifest`)를 씬을 열 때 받고 마운트합니다 — 언리얼 청크 pak 설치(ChunkInstall) · 유니티 Addressables 의 번들 로드 자리.
 * @details 씬 열기(`SceneManager`)가 로드를 띄우기 전에 `prepareScene` 을 프레임마다 묻는다. 팩 파일이 디스크(데스크톱 · Android 의 팩 폴더)에 있으면 바로
 *          마운트할 차례이고, 없으면 받기(`IPackChunkSource` — 웹은 페이지 옆 `Packs/` 에서 fetch 해 가상 파일 시스템에 쓴다)를 건다. 마운트(팩 색인 읽기)는
 *          부르는 쪽이 작업 스레드에서 `mountPacks` 로 한다 — 게임 스레드가 파일을 읽지 않는다. 모두 마운트되면 `Ready` 다.
 *          한 번 마운트한 청크는 내리지 않는다(언리얼 pak 마운트와 같다 — 다시 여는 씬이 다시 받지 않는다).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    class PackManager;
} // namespace sw

namespace sw
{
    /** @brief 씬 하나의 청크 준비 상태입니다. */
    enum class PackChunkState : uint8
    {
        Ready,   ///< 그 씬이 쓰는 청크가 모두 마운트됐다(미룬 청크가 없는 씬 포함) — 로드를 띄운다
        Pending, ///< 받거나 마운트하는 중이다 — 다음 프레임에 다시 묻는다
        Failed,  ///< 팩을 받거나 마운트하지 못했다(이유는 로그) — 이번 요청은 열 수 없고, 다음 요청이 다시 받는다
    };

    /** @brief 받기 하나가 끝났습니다(성공이면 true). 아무 스레드에서나 올 수 있다. */
    SW_DECLARE_DELEGATE( void, PackChunkFetchDoneDelegate, bool );

    /**
     * @class IPackChunkSource
     * @brief 팩 폴더에 없는 청크 팩을 받아 그 자리에 씁니다. 웹만 있다(`createPlatformSource`) — 그 밖의 플랫폼은 팩이 늘 팩 폴더에 있다.
     */
    class SW_API IPackChunkSource
    {
    public:
        virtual ~IPackChunkSource() = default;
        /** @brief 팩 @p packFileName 을 받아 @p localPath 에 쓰고 @p onDone 을 부릅니다(받기 · 쓰기 실패면 이유를 남기고 false). */
        virtual void fetch( const string& packFileName, const AbsolutePath& localPath, PackChunkFetchDoneDelegate onDone ) = 0;
        /** @brief 이 플랫폼의 받기입니다. 받을 곳이 없는 플랫폼(팩이 디스크에 있다)은 nullptr 입니다. */
        static unique_ptr<IPackChunkSource> createPlatformSource();
    };

    /**
     * @class PackChunkLoader
     * @brief 파일 머리말 참고.
     */
    class SW_API PackChunkLoader
    {
    public:
        /** @param packs 청크 목록 · 팩 폴더를 읽은(`scanAndMountPacks`) 관리자 — 이것보다 오래 산다 */
        PackChunkLoader( PackManager& packs, unique_ptr<IPackChunkSource> pSource );

        /**
         * @brief 씬 @p sceneID 가 쓰는 미룬 청크를 준비합니다(게임 스레드). 받은 팩 가운데 이번에 마운트할 것을 @p outListToMount 에 담는다 — 부르는 쪽이 `mountPacks` 로 마운트한다.
         * @details 팩 폴더에 없고 받기도 없으면(데스크톱 패키지에 그 청크가 빠졌다) `Failed` 다.
         */
        [[nodiscard]] PackChunkState prepareScene( string_view sceneID, vector<string>& outListToMount );
        /** @brief `prepareScene` 이 넘긴 팩을 마운트합니다(작업 스레드). 마운트하지 못한 팩은 오류를 남기고 실패로 적는다(그 씬의 다음 준비가 `Failed`). */
        void mountPacks( const vector<string>& listPackFileName );

    private:
        enum class FileState : uint8
        {
            Unknown,     ///< 아직 보지 않았다
            Fetching,    ///< 받는 중
            Local,       ///< 팩 폴더에 있다 — 마운트할 차례
            MountQueued, ///< 작업 스레드가 마운트하는 중
            Mounted,
            Failed,
        };
        /** @brief 받기 콜백이 이 객체보다 늦게 올 수 있어 상태 표는 함께 든다. */
        struct SharedState
        {
            mutex                                      _mutex;
            unordered_map<string, FileState> _mapState SW_GUARDED_BY( _mutex ); ///< 팩 파일 이름 → 상태
        };

        PackManager&                 _packs;
        unique_ptr<IPackChunkSource> _pSource;
        shared_ptr<SharedState>      _pShared;
    };
} // namespace sw
