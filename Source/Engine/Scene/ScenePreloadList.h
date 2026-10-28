/**
 * @file ScenePreloadList.h
 * @brief 씬이 쓰는 에셋 경로 목록(`<씬>.preload.json`)의 수집 · 읽고 쓰기입니다. 쿠킹이 굽고, 레벨 전환이 `PreloadSet` 으로 열어 파일을 미리 읽는다.
 * @details **수집은 씬을 지어서 한다.** 씬 문서를 `Scene::instantiate` 로 지으면(프리팹 스폰 · 중첩까지 런타임과 같은 길) 모든 오브젝트의 컴포넌트가 생긴다. 그 컴포넌트의
 *          에셋 경로 프로퍼티(`PROPERTY( AssetPath )` · `AssetType = …`, `ComponentAssetPathUtil::readAssetPath`)를 리플렉션으로 읽어 모은다 — 컴포넌트마다 목록을 코드로
 *          적지 않아, 새 컴포넌트가 경로 프로퍼티만 달면 자동으로 든다. 머티리얼이 쓰는 텍스처는 싣지 않는다(텍스처는 그것을 쓰는 머티리얼의 고정이 붙든다 — `PreloadSet`).
 *          확장자가 없는 값(`cube` 같은 내장 메시 id)과 씬 경로(`.scene.xml` · `.scene.bin`, 다음 씬으로의 이음이지 이 씬의 에셋이 아니다)는 뺀다.
 *          목록은 경로 오름차순이고 중복이 없다 — 같은 씬은 늘 같은 바이트로 구워져 쿠킹 기록(`CookStore`)이 재사용한다.
 *
 *          형식: `{ "version": 1, "paths": [ "game/x/models/a.mesh", … ] }`. 경로는 리소스 id(`/` 구분)다.
 *          쿠킹은 씬 하나마다 작업 둘을 돈다(`SceneCooker::cookAllScenes`) — 쿠킹 기록은 작업 하나에 출력 하나라, 씬 쿠킹본(SCN1)에 목록을 싣는 대신 옆 파일을 따로 굽는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Asset/AssetID.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 씬 프리로드 목록의 파일 형식과 수집입니다(전부 static). */
    struct SW_API ScenePreloadList
    {
        static constexpr string_view kExtension = ".preload.json";
        static constexpr uint32      kVersion   = 1;

        /**
         * @brief 씬 파일 경로(`….scene.xml` 또는 `….scene.bin`)의 프리로드 목록 경로(`….preload.json`)를 만듭니다. 씬 파일이 아니면 빈 문자열입니다.
         */
        static string makeListPath( string_view scenePath );

        /** @brief @p objects 의 모든 오브젝트 컴포넌트에서 에셋 경로를 모아 @p outListPath 에 오름차순 · 중복 없이 채웁니다(먼저 비운다). */
        static void collect( const GameObjectManager& objects, vector<string>& outListPath );

        /** @brief 목록을 디스크 절대 경로 @p diskPath 에 씁니다(쿠킹 출력). */
        [[nodiscard]] static bool write( string_view diskPath, const vector<string>& listPath );
        /**
         * @brief 리소스 id @p listAssetID 의 목록을 읽습니다(팩 · 낱개 파일). 파일이 없거나 형식이 틀리면 오류를 남기고 false 입니다.
         * @details 파일이 **없는** 씬(쿠킹 전 Dev 실행)은 부르는 쪽이 `AssetIO::hasAsset` 로 먼저 묻는다 — 없는 파일을 오류로 세지 않는다.
         */
        [[nodiscard]] static bool read( const AssetID& assetID, vector<string>& outListPath );
    };
} // namespace sw
