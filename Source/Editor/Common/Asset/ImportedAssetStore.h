/**
 * @file ImportedAssetStore.h
 * @brief 임포트 결과를 내용 해시로 한 벌만 두는 공유 저장소(`<도메인>/models/_shared/<종류>/<키>.<확장자>`)입니다.
 * @details 임포트 결과는 사람이 차이를 적을 수 없으므로 내용으로 합친다(언리얼 Derived Data Cache · 유니티 Library 의 내용 키와 같다). 키는 결과 바이트에 그 결과를 만든 설정을 섞은 값이다 —
 *          설정이 다르면 지금 바이트가 같아도 다른 결과다. 저장소는 도메인마다 하나다. 도메인을 넘어 합치지 않는다 — 임포트는 자기 도메인(곧 자기 팩)에만 쓴다(규칙: docs/agents/NamingScripts.md).
 *          원본 하나의 임포트는 자기 결과 자리에만 쓴다는 `IRawAssetImporter` 규칙의 유일한 예외다. 이름이 내용에서 나오므로 두 원본이 같은 자리를 쓰면 바이트도 같고, 쓰기는 한 잠금 아래에서 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct ImportedAssetStore
     * @brief 공유 저장소의 키 · 경로 · 쓰기 · 참조 찾기 · 안 쓰는 파일 지우기입니다.
     */
    struct ImportedAssetStore
    {
        /** @brief 저장소 폴더 이름입니다. `models/` 아래라 Git 무시 · 쿠킹 대상이 다른 임포트 결과와 같다. `models_raw/_shared.*` 원본은 이 이름과 겹치므로 두지 않는다. */
        static constexpr string_view kStoreFolderName = "_shared";

        /** @brief 임포트한 모델이 있는 폴더 이름입니다. */
        static constexpr string_view kModelFolderName = "models";

        /** @brief 결과 바이트와 설정 글로 키(소문자 16 자리 16 진수)를 만듭니다. 설정 글이 다르면 다른 키입니다. */
        [[nodiscard]] static string makeKey( const void* pBytes, size_t byteCount, string_view settingsText );

        /** @brief @p sourcePath 의 도메인 폴더입니다(`<x>/models_raw/<y>` → `<x>`). `models_raw/` 구간이 없으면 빈 글입니다. */
        [[nodiscard]] static string findDomainFolder( string_view sourcePath );

        /**
         * @brief 디스크 경로 @p diskPath 에 쓸 임포트 결과 글 @p text 에, 그 리소스 id 에서 결정적으로 만든 GUID 머리(`<?asset guid="…"?>`)를 씌운 글입니다.
         *        우리 형식 글(`.material` · `*.xml`) 결과는 모두 이것(또는 `writeTextWithIdentity`)을 지나 쓴다 — 머리 없는 결과는 에셋 색인이 결함으로 센다.
         * @details 결과는 커밋하지 않으므로 무작위 GUID 를 둘 곳이 없다 — 다시 임포트해도 · 다른 컴퓨터에서도 같도록 id 에서 만든다(`AssetGUID::fromName`).
         *          결과 경로가 이미 (원본 자리, 결과 종류, 결과 안 이름) · 공유 저장소는 (도메인, 종류, 키)에서 정해지므로 id 하나가 같은 정보를 담고, 공유 결과처럼
         *          원본이 하나가 아닌 것도 같은 규칙이다. 유니티가 임포트한 하위 에셋의 fileID 를 이름에서 결정적으로 만드는 것과 같은 자리다.
         *          원본을 옮기면 결과 경로와 함께 GUID 도 바뀐다 — 결과를 가리키는 참조가 아직 경로라 지금은 잃는 것이 없다.
         * @return @p diskPath 가 리소스 루트 · 붙인 도메인 밖이라 id 가 없으면 오류를 남기고 없음입니다(빈 id 로 만든 GUID 는 모든 결과가 겹친다).
         */
        [[nodiscard]] static optional<string> makeTextWithIdentity( const AssetPaths& assetPaths, string_view diskPath, string_view text );

        /** @brief `makeTextWithIdentity` 의 글을 @p diskPath 에 씁니다(부모 폴더를 만든다). 실패하면 false 입니다 — 쓰기 실패 오류는 부르는 쪽이 자기 결과 이름으로 남긴다. */
        [[nodiscard]] static bool writeTextWithIdentity( const AssetPaths& assetPaths, string_view diskPath, string_view text );

        /** @brief 저장소 파일 경로입니다(`<x>/models/_shared/<kind>/<key><extension>`). */
        [[nodiscard]] static string makeStorePath( string_view domainFolder, string_view kind, string_view key, string_view extension );

        /**
         * @brief 저장소 파일을 씁니다. 이미 있으면 바이트가 같은지만 봅니다(병렬 임포트가 같은 키를 동시에 써도 한 벌이다).
         * @return 쓰기에 실패했거나 같은 이름의 파일 바이트가 다르면(해시 충돌 · 손댄 파일) 오류를 남기고 false 입니다 — 다른 모델이 쓰는 파일을 덮어쓰지 않는다.
         */
        [[nodiscard]] static bool writeIfAbsent( string_view storePath, const void* pBytes, size_t byteCount );

        /** @brief 글 @p text 에서 저장소를 가리키는 리소스 경로(`"…/models/_shared/…"` 따옴표 안)를 모두 찾아 @p outListPath 에 더합니다. */
        static void collectReferences( string_view text, vector<string>& outListPath );

        /** @brief 저장소 경로를 가리킬 수 있는 임포트 결과인지 봅니다(`.material` · `.animset.xml` · `.prefab.xml`). */
        [[nodiscard]] static bool isReferrer( string_view path );

        /**
         * @brief @p importedPath(리소스 루트 아래 디스크 경로)가 가리키는 저장소 파일들의 바이트 해시를 @p hash 에 섞습니다(`ModelImporter::computeImportedHash`).
         * @details 공유 파일은 옆 폴더 밖이라 옆 폴더 해시에 들지 않는다. 지우거나 손대면 어긋남으로 잡히게 참조를 따라가 섞는다.
         */
        [[nodiscard]] static uint64 mixReferencedFiles( string_view importedPath, string_view text, uint64 hash );

        /**
         * @brief @p resourceRoot 아래 모든 저장소에서 같은 도메인의 임포트 결과가 가리키지 않는 파일을 지웁니다(원본을 고치면 옛 키의 파일이 남는다).
         * @details 범위를 정한 임포트(핫 리로드)는 부르지 않는다 — 전체 임포트(`ModelImporter::importAllModels`)가 끝난 뒤 직렬로 부른다.
         * @return 지운 파일 수입니다.
         */
        static uint32 removeUnreferenced( string_view resourceRoot, vector<string>& inoutListProblem );
    };
} // namespace sw::editor
