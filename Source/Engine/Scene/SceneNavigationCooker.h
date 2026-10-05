/**
 * @file SceneNavigationCooker.h
 * @brief 내비메시 쿠킹 — 내비 표면(`NavMeshSurfaceComponent`)이 놓인 씬마다 `<씬 이름>.navmesh` 를 씁니다.
 * @details `App --cook-scenes` 단계가 씬 · 프리팹 · VAT 다음에 부릅니다. 씬을 소스 그대로 세우고(플레이는 시작하지 않는다 — 플레이가 세우는 것 ·
 *          장애물은 들지 않는다) 런타임 베이크와 같은 함수(`SceneNavigation::makeCookedEntry` → `collectGeometry` · `NavMeshBakeUtil::bakeAllTiles`)로
 *          베이크하므로 두 길의 타일 바이트가 같습니다. 배포본은 그것을 팩에서 읽고, Dev 는 쿠킹 폴더를 마운트하지 않아 처음 쓸 때 베이크합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class NavMeshAsset;

    /** @struct SceneNavigationCooker @brief 파일 머리말 참고. */
    struct SW_API SceneNavigationCooker
    {
        /**
         * @brief 씬 하나의 내비메시를 베이크해 @p outAsset 에 담습니다. 표면이 없으면 true 이고 항목이 비었습니다.
         * @return 씬을 읽지 못했거나 어느 종류를 베이크하지 못하면 false 입니다.
         */
        [[nodiscard]] static bool cookScene( string_view sceneResourcePath, NavMeshAsset& outAsset );
        /**
         * @brief @p resourceRoot 아래의 모든 씬(`*.scene.xml`) 가운데 표면이 있는 것을 `<cookedDir>/<씬 경로의 .navmesh>` 로 씁니다. 활성 게임이 아닌
         *        게임 팩의 씬은 건너뜁니다(배포본은 활성 게임의 팩만 연다).
         * @return 쓴 파일 수입니다. 실패는 @p outFailedCount 에 셉니다.
         */
        static uint32 cookAll( const string& resourceRoot, const string& cookedDir, uint32& outFailedCount );
    };
} // namespace sw
