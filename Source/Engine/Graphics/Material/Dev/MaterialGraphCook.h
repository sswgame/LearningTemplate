/**
 * @file MaterialGraphCook.h
 * @brief 그래프를 생성물(`<도메인>/shaders/matgraph/<줄기>_<해시>.hlsl` · `<그래프 옆>.gen.material`)로 바꿔 소스 트리에 씁니다. 셰이더 쿠킹이 시작할 때와 에디터의 임포터가 부릅니다.
 * @details 생성물은 소스 트리에 둔다 — 그 뒤는 손으로 쓴 셰이더 · 머티리얼과 같은 쿠킹 입력이다(셰이더 스탬프가 내용 해시로 추적하고, 머티리얼은 요청 수집과 팩이 그대로 읽는다).
 *          런타임은 생성물만 읽고 배포에는 이 코드가 없다(`Dev/`). 언리얼은 파생 캐시에 두지만, 이 엔진은 셰이더 소스 해석이 리소스 경로 하나라 같은 길을 태우는 쪽이 작다(기억에 의존).
 *          **내용이 바뀌면 파일 이름이 바뀐다.** PSO · 리플렉션 캐시가 경로를 열쇠로 하므로, 새 내용을 새 경로로 내면 무효화 코드 없이 Debug 밖의 개발 구성에서도 핫 리로드가 된다(`ShaderRecompiler` 는 Debug 전용이다).
 *          쿠킹은 바이트가 같으면 쓰지 않는다 — 깨끗한 트리에서 쿠킹이 소스를 건드리지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class AssetPaths;
    struct MaterialGraphCompileResult;

    /** @brief 그래프 하나의 생성물 자리입니다(리소스 id). */
    struct MaterialGraphGeneratedPaths
    {
        string _shadersFolder; ///< 셰이더가 드는 폴더(`game/x/shaders/matgraph`)
        string _stem;          ///< 파일 이름 줄기 — `materials/` 아래 경로를 `_` 로 이은 것(바이너리 이름이 폴더를 보지 않아 겹치지 않게)
        string _materialPath;  ///< 베이스 머티리얼(`game/x/materials/a/b.gen.material`)
    };

    /**
     * @struct MaterialGraphCook
     * @brief 파일 머리말 참고.
     */
    struct SW_API MaterialGraphCook
    {
        /** @brief 생성된 머티리얼의 접미사입니다. */
        static constexpr string_view kGeneratedMaterialSuffix = ".gen.material";

        /** @brief 그래프 @p graphID 의 생성물 자리를 정합니다. 경로에 `/materials/` 칸이 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool makeGeneratedPaths( string_view graphID, MaterialGraphGeneratedPaths& outPaths );
        /** @brief 편집기 미리보기의 자리입니다 — 작업 도메인(`scratch`, 쿠킹이 훑지 않는다)이라 저장 전 편집이 콘텐츠를 건드리지 않습니다. */
        [[nodiscard]] static MaterialGraphGeneratedPaths makePreviewPaths( const MaterialGraphGeneratedPaths& paths );
        /** @brief 셰이더 파일의 리소스 id 입니다. */
        [[nodiscard]] static string makeShaderPath( const MaterialGraphGeneratedPaths& paths, const MaterialGraphCompileResult& result );
        /**
         * @brief 컴파일 결과를 @p paths 자리에 씁니다(바이트가 같으면 쓰지 않는다). 같은 줄기의 옛 해시 셰이더는 지웁니다.
         * @return 쓰지 못하면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool writeGenerated( const AssetPaths& assetPaths, string_view graphID, const MaterialGraphGeneratedPaths& paths, const MaterialGraphCompileResult& result, uint32* pOutWrittenCount );
        /** @brief 그래프 하나를 읽고 컴파일해 씁니다(하위 그래프는 혼자 머티리얼이 되지 않아 건너뛴다). 문제는 노드 이름과 함께 오류로 남기고 false 입니다. */
        [[nodiscard]] static bool refreshGraph( const AssetPaths& assetPaths, string_view graphID, uint32* pOutWrittenCount );
        /**
         * @brief @p contentRoot 아래 모든 그래프를 생성물로 맞춥니다. 하위 그래프가 바뀌면 쓰는 쪽도 달라지므로 늘 전부 봅니다(그래프 수가 적고 바뀌지 않으면 쓰지 않는다).
         * @return 하나라도 실패하면 false 입니다(나머지는 계속 본다). 쿠킹은 이것을 실패로 센다.
         */
        [[nodiscard]] static bool refreshAll( const AssetPaths& assetPaths, string_view contentRoot, uint32* pOutWrittenCount );
    };
} // namespace sw
