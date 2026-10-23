/**
 * @file PackPatchBuilder.h
 * @brief 두 빌드의 팩에서 패치(바뀐 파일만 담은 팩 + 패치 목록)를 만들고, 빌드의 내용 목록(`base.packbuild.xml`)을 남깁니다.
 * @details 패치 형식과 적용 규칙은 `PackPatch.h` · `ResourcePakManager::scanAndMountPacks` 이고, 이 파일은 만드는 쪽이다. 설계는 docs/plans/PatchDeltaDelivery.md 이다.
 *          - 내용 목록은 소스 트리가 아니라 **베이크한 팩**에서 센다. 팩이 배포되는 바이트라 쿠킹 증분 기록(팩을 다시 쓰지 않은 경우)과 상관없이 같은 값이 나온다.
 *          - 항목의 경로는 전체 가상 경로(`engine/ui/a.xml`, `game/empty/maps/a.scene.xml`)다. 팩 안에는 도메인을 뗀 경로로 들어 있고(`engine.pack` 의 `ui/a.xml`),
 *            패치 팩은 전체 경로로 담는다 — 리더가 전체 경로 해시를 먼저 찾으므로 아래 도메인 팩의 같은 파일을 덮는다.
 *          - 폴더에 패치 목록이 있으면 기본 팩의 폴더가 아니다 — 거절한다(이미 패치된 폴더에서 다음 빌드를 만들지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Resource/Pack/PackPatch.h"
#include "Engine/Resource/Pack/ResourcePakWriter.h"

namespace sw
{
    /** @struct PackPatchBuildParams @brief 패치 하나를 만드는 입력입니다. 모두 필수입니다. */
    struct PackPatchBuildParams
    {
        string                _packsDirectory{};     ///< 새 빌드의 기본 팩 폴더(`App --cook-packs` 가 쓴 `-pack-dir`)
        string                _previousRecordPath{}; ///< 이전 빌드의 내용 목록 파일(`*.packbuild.xml`)
        string                _newBuild{};           ///< 새 빌드 번호. 파일 이름에 들어가므로 글자 · 숫자 · `.` · `_` · `-` 만 쓴다
        string                _outputDirectory{};    ///< 패치 팩 · 패치 목록 · 새 빌드의 내용 목록을 쓸 폴더
        ResourcePakWriterDesc _writerDesc{};         ///< 패치 팩의 압축(팩 쿠킹 설정과 같은 값을 쓴다)
    };
} // namespace sw

namespace sw
{
    /** @brief 패치 만들기입니다. 상태가 없습니다. */
    struct SW_API PackPatchBuilder
    {
        /** @brief 패치 팩 파일 이름입니다(`patch_<빌드>.pack`). 기본 팩 우선순위 규칙의 `patch_` 접두사와 같다. */
        static string makePatchPackFileName( string_view build );
        /** @brief 패치 목록 파일 이름입니다(`patch_<빌드>.packpatch.xml`). */
        static string makeManifestFileName( string_view build );
        /** @brief 빌드 번호로 쓸 수 있는 글인지 봅니다(비어 있지 않고 글자 · 숫자 · `.` · `_` · `-` 만). */
        static bool isValidBuildName( string_view build );

        /**
         * @brief @p packsDirectory 의 모든 기본 팩이 담은 파일의 전체 가상 경로와 내용 해시를 모읍니다(경로 사전순).
         * @return 폴더에 패치 목록이 있거나, 팩을 열지 못했거나, 같은 전체 경로가 두 팩에 있으면 이유를 남기고 false 입니다.
         */
        [[nodiscard]] static bool collectBuildEntries( string_view packsDirectory, vector<PackContentEntry>& outListEntry );

        /** @brief @p packsDirectory 의 팩 내용 목록을 빌드 @p build 의 기록으로 @p recordPath 에 씁니다(기본 팩 폴더에는 `PackPatchUtil::kBuildRecordFileName`). */
        [[nodiscard]] static bool writeBuildRecord( string_view packsDirectory, string_view build, string_view recordPath );
        /** @brief 패치를 쓰지 않는 빌드의 기록(`patching="none"`, 번호 · 내용 목록 없음)을 @p recordPath 에 씁니다. 빌드 번호를 주지 않은 쿠킹이 쓴다 — 기록이 없는 것으로 대신하지 않는다. */
        [[nodiscard]] static bool writeNoPatchingRecord( string_view recordPath );

        /**
         * @brief 이전 빌드 기록과 새 팩의 차이로 패치 팩 · 패치 목록 · 새 빌드의 내용 목록을 `_outputDirectory` 에 씁니다.
         * @details 바뀐 파일이 없고 지운 파일도 없으면 거절한다(빈 패치는 배포할 이유가 없다). 새 빌드 번호가 이전과 같아도 거절한다.
         */
        [[nodiscard]] static bool makePatch( const PackPatchBuildParams& params );
    };
} // namespace sw
