/**
 * @file AssetSourceLocator.h
 * @brief 임포터가 원본의 바이트를 어디서 읽는지 정하는 창구 하나입니다 — 서술(`.meta`) 곁의 파일이거나, 원본을 저장소 밖에 둔 내용 주소 저장소(`AssetSourceAddress`)의 파일.
 * @details 원본의 이름 · 규칙 · 결과 자리는 늘 리소스 트리의 경로(`models_raw/x.glb`)로 정하고, 바이트만 여기서 푼 자리에서 읽는다 — 언리얼 Virtual Assets 가 패키지는 두고
 *          페이로드만 내용 해시로 찾는 것과 같다. 저장소 배치는 Git LFS 의 `objects` 와 같다(`<뿌리>/<앞 두 자>/<다음 두 자>/<SHA-256>`) — LFS 저장소를 그대로 원본 저장소로 쓸 수 있다.
 *          내용이 주소와 맞는지는 저장소에 넣고 받는 도구가 본다(Git LFS 의 clean · smudge 와 같다). 로컬 저장소에 없으면 임포트 오류다 — 다른 원본이나 빈 결과로 대신하지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/AssetDatabase.h"

namespace sw::editor
{
    /**
     * @struct AssetSourceLocator
     * @brief 원본 자리를 푸는 창구입니다. 상태가 없어 워커에서 불러도 된다. 파일 머리말 참고.
     */
    struct AssetSourceLocator
    {
        /** @brief 저장소 설정 파일(프로젝트 루트 기준)입니다 — `_root`(`<프로젝트>/Saved` 아래) 또는 `_rootEnvironment`(절대 경로를 담은 환경 변수) 가운데 하나. */
        static constexpr const utf8* kConfigFile = "Config/Dev/SourceStore.json";

        /**
         * @brief 원본 @p sourcePath(디스크 경로)의 바이트가 있는 파일입니다 — 그 자리에 있으면 그 파일(설정을 읽지 않는다), 없고 서술이 저장소 주소를 적었으면 설정한 저장소의 파일.
         * @return 원본도 저장소 주소도 없거나, 저장소 설정이 틀렸거나, 로컬 저장소에 그 내용이 없으면 까닭입니다.
         */
        [[nodiscard]] static expected<DiskPath> resolve( const DiskPath& sourcePath );
        /** @brief `resolve` 를 저장소 뿌리 @p storeRoot 로 합니다. */
        [[nodiscard]] static expected<DiskPath> resolveInStore( const DiskPath& sourcePath, const AbsolutePath& storeRoot );
        /** @brief 원본 @p sourcePath 의 바이트를 읽습니다(`resolve`). 풀지 못했거나 읽지 못했거나 비었으면 false 입니다 — 저장소 주소를 풀지 못한 까닭은 오류로 남기고, 없는 파일은 부르는 쪽이 알린다. */
        [[nodiscard]] static bool readBytes( const DiskPath& sourcePath, vector<uint8>& outBytes );

        /** @brief `readBytes` 가 읽을 바이트의 해시입니다(`AssetImportStampUtil::computeFileHash`) — 바뀌지 않은 원본은 읽지 않는다. 풀지 못했거나 비었으면 0 이고 로그는 `readBytes` 와 같다. */
        [[nodiscard]] static uint64 computeHash( const DiskPath& sourcePath );

        /** @brief @p folder(하위까지)에서 파일은 없고 서술이 저장소 주소를 적은 원본의 디스크 경로를 @p inoutListSourcePath(그 폴더에서 찾은 파일들) 끝에 더합니다 — 원본을 훑는 쪽이 파일 목록에 더한다. */
        static void collectStoredSources( const AbsolutePath& folder, vector<AbsolutePath>& inoutListSourcePath );

        /** @brief 저장소 안의 파일 자리입니다(`<뿌리>/<앞 두 자>/<다음 두 자>/<SHA-256>`). */
        [[nodiscard]] static AbsolutePath makeStorePath( const AbsolutePath& storeRoot, const AssetSourceAddress& address );
        /** @brief 설정(`kConfigFile`)의 저장소 뿌리입니다. 설정이 없거나 틀렸거나 환경 변수가 비었으면 까닭입니다. */
        [[nodiscard]] static expected<AbsolutePath> loadStoreRoot();
    };
} // namespace sw::editor
