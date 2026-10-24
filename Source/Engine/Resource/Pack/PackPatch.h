/**
 * @file PackPatch.h
 * @brief 팩 단위 차분 배포(패치)의 형식입니다 — 두 빌드의 내용 목록에서 바뀐 · 지운 경로를 셈하고, 패치 목록 파일(`*.packpatch.xml`)을 읽고 쓰고,
 *        설치된 빌드에서 목표 빌드까지 이어지는 패치 순서를 정합니다.
 * @details 패치 하나는 팩 하나(바뀐 · 새 파일)와 목록 하나(어느 빌드에서 어느 빌드로, 지운 경로)다. 런타임은 패치 팩을 기본 팩보다 높은 우선순위로
 *          지움 목록과 함께 마운트한다(`ResourcePakManager::mountPatchPack`). 설계와 남은 단위는 docs/plans/PatchDeltaDelivery.md 이다.
 *          빌드 번호는 목록에 적힌 글 그대로 비교한다 — 대소 비교를 하지 않는다(순서는 목록의 from → to 사슬이 정한다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 빌드 하나의 팩 내용 항목입니다. 경로는 가상 상대 경로이고 해시는 압축 전 내용의 해시입니다(쿠커가 채운다). */
    struct PackContentEntry
    {
        string _path{};
        uint64 _contentHash{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 두 빌드 사이의 차이입니다. 두 목록 모두 경로 사전순으로 정렬돼 있습니다. */
    struct PackDelta
    {
        vector<string> _listChangedPath{}; ///< 새로 생겼거나 내용이 바뀐 경로(패치 팩에 담는다)
        vector<string> _listRemovedPath{}; ///< 목표 빌드에 없는 경로(패치 목록의 지움으로 적는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 설치 빌드가 패치를 받는 방식입니다. 빌드 기록(`base.packbuild.xml`)에 `patching="chain|none"` 으로 늘 적히고, 빠져 있으면 읽기가 거절한다(빈 값을 `none` 으로 보지 않는다).
     * @details 기록이 팩 안이 아니라 팩 폴더 옆 파일인 것이 이 결정의 핵심이다 — 팩을 마운트하기 전에 읽어야 하기 때문이다(결정 기록 5-9).
     */
    enum class PackPatching : uint8
    {
        Chain = 0, ///< 목록 파일의 사슬로 패치를 얹는다(스팀 밖 채널 · 콘텐츠 핫픽스)
        None       ///< 패치를 쓰지 않는다. 스토어가 새 빌드를 통째로 준다(스팀). 이 폴더에 패치 목록이 있으면 오류다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 빌드 하나의 내용 목록 파일(`<PackBuild build="…"><Entry path="…" hash="…"/></PackBuild>`)입니다.
     * @details 기본 팩을 구울 때 팩 폴더에 `PackPatchUtil::kBuildRecordFileName` 으로 남기고(설치된 빌드의 출처), 다음 빌드의 패치를 만들 때 이전 빌드의 목록으로 읽는다.
     */
    struct PackBuildRecord
    {
        string                   _build{};                         ///< 빌드 번호(글 그대로 비교한다). `None` 이면 비어 있다
        vector<PackContentEntry> _listEntry{};                     ///< 전체 가상 경로 + 내용 해시(경로 사전순). `None` 이면 비어 있다
        PackPatching             _patching{ PackPatching::Chain }; ///< 이 빌드가 패치 사슬을 쓰는가. 파일에는 늘 명시한다
    };
} // namespace sw

namespace sw
{
    /** @brief 패치 목록 파일 하나(`<PackPatch from="…" to="…" pack="…"><Removed path="…"/></PackPatch>`)입니다. */
    struct PackPatchManifest
    {
        string         _fromBuild{};       ///< 이 패치를 얹을 수 있는 빌드
        string         _toBuild{};         ///< 얹은 뒤의 빌드
        string         _packFileName{};    ///< 패치 팩 파일 이름(목록 파일과 같은 폴더)
        vector<string> _listRemovedPath{}; ///< 지운 경로(가상 상대 경로)
    };
} // namespace sw

namespace sw
{
    /** @brief 패치 형식의 셈 · 읽기 · 쓰기입니다. 상태가 없습니다. */
    struct SW_API PackPatchUtil
    {
        static constexpr string_view kManifestExtension    = ".packpatch.xml";     ///< 패치 목록 파일의 이름 끝
        static constexpr string_view kBuildRecordFileName  = "base.packbuild.xml"; ///< 팩 폴더에서 설치된(기본) 빌드를 밝히는 파일
        static constexpr string_view kBuildRecordExtension = ".packbuild.xml";     ///< 빌드 내용 목록 파일의 이름 끝(이전 빌드의 목록을 보관할 때)

        /** @brief 내용 해시 하나를 팩 항목의 바이트에서 셉니다(크기 · 경로 순서와 무관, 바이트 그대로 — CR 도 센다). */
        static uint64 computeEntryHash( const vector<uint8>& bytes );
        /** @brief 빌드 내용 목록 XML 을 읽습니다. `build` · `Entry` 의 `path` · `hash`(16 자리 16 진수)는 필수이고 같은 경로가 두 번이면 거절합니다. */
        [[nodiscard]] static bool parseBuildRecord( string_view xmlText, string_view sourceName, PackBuildRecord& outRecord );
        /** @brief 빌드 내용 목록을 XML 글로 만듭니다(경로 사전순, `parseBuildRecord` 가 다시 읽는 형식). */
        static string makeBuildRecordText( const PackBuildRecord& record );
        /**
         * @brief 설치된 빌드 @p installedBuild 에서 시작해 이어지는 모든 패치의 순서와 마지막 빌드를 정합니다. 목록이 비어 있으면 빈 순서이고 마지막 빌드는 설치된 빌드입니다.
         * @details 갈림(한 빌드에서 시작하는 패치가 둘 이상) · 고리 · 설치된 빌드에서 닿지 않는 패치(사슬 밖에 남음)는 오류를 남기고 false 다 — 닿지 않는 패치를 조용히 버리지 않는다.
         */
        [[nodiscard]] static bool orderPatchChainToLatest( string_view installedBuild, const vector<PackPatchManifest>& listManifest, vector<size_t>& outListOrder, string& outLatestBuild );
        /**
         * @brief 기본 빌드 @p listBase 에서 목표 빌드 @p listTarget 로 가는 차이를 셉니다.
         * @details 한 목록 안에 같은 경로가 두 번 있으면 오류를 남기고 false 다(어느 쪽이 맞는지 정할 수 없다). 결과는 경로 사전순이다(같은 입력이면 같은 패치).
         */
        [[nodiscard]] static bool computeDelta( const vector<PackContentEntry>& listBase, const vector<PackContentEntry>& listTarget, PackDelta& outDelta );
        /**
         * @brief 패치 목록 XML 을 읽습니다. `from` · `to` · `pack` 은 필수이고 `from` 과 `to` 가 같으면 거절합니다. `Removed` 의 `path` 도 필수입니다.
         * @param sourceName 오류에 적을 이름(파일 경로)입니다.
         */
        [[nodiscard]] static bool parseManifest( string_view xmlText, string_view sourceName, PackPatchManifest& outManifest );
        /** @brief 패치 목록을 XML 글로 만듭니다(`parseManifest` 가 다시 읽는 형식). */
        static string makeManifestText( const PackPatchManifest& manifest );
        /**
         * @brief 설치된 빌드 @p installedBuild 에서 @p targetBuild 까지 얹을 패치의 순서(@p listManifest 의 인덱스)를 정합니다.
         * @details 빌드마다 그 빌드에서 시작하는 패치가 정확히 하나여야 한다. 없으면(사슬이 끊김) · 둘 이상이면(갈림) · 같은 빌드로 돌아오면(고리) 오류를 남기고 false 다.
         *          설치된 빌드가 이미 목표면 빈 순서로 true 다.
         */
        [[nodiscard]] static bool orderPatchChain( string_view installedBuild, string_view targetBuild, const vector<PackPatchManifest>& listManifest, vector<size_t>& outListOrder );
    };
} // namespace sw
