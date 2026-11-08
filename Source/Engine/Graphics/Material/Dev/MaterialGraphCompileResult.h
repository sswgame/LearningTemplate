/**
 * @file MaterialGraphCompileResult.h
 * @brief 머티리얼 그래프를 HLSL 과 머티리얼 칸으로 바꾸는 컴파일러입니다: 위상 정렬 → 형식 검사 → HLSL 조각 펼치기. 개발 전용(`Dev/`)이며 로그를 남기지 않고 문제를 값으로 돌려줍니다.
 * @details 낸 HLSL 은 손으로 쓴 머티리얼 셰이더와 같은 길(순열 · `SW_MATERIAL` · 리플렉션 · 네 백엔드 교차 컴파일)을 지난다. 언리얼도 그래프를 HLSL 로 번역해 셰이더 컴파일러에 넘긴다(기억에 의존).
 *          편집기는 고칠 때마다 부르므로 문제를 로그로 내지 않고 노드에 붙인다 — 로그는 쿠킹 · 저장(`MaterialGraphGeneratedPaths`)이 남긴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/Material/Dev/MaterialGraph.h"
#include "Engine/Graphics/Material/Dev/MaterialGraphNodeRegistry.h"

namespace sw
{
    /** @brief 컴파일 문제 하나입니다. 어느 노드인지 늘 적습니다(하위 그래프 안이면 `바깥/안`). */
    struct MaterialGraphIssue
    {
        string _nodeName;
        string _id;      ///< 로그 · 테스트가 쓰는 짧은 이름(`Cycle` · `MissingPin` · `TypeMismatch` …)
        string _message; ///< 사람이 읽는 글(영어 한 줄)
    };

    /** @brief 생성된 머티리얼 칸 하나(파라미터 노드 · 텍스처 노드)입니다. */
    struct MaterialGraphField
    {
        string  _name;
        string  _hlslType;
        string  _propertyType;
        string  _defaultValue;
        string  _assetPath;
        float32 _min{ 0.0f };
        float32 _max{ 1.0f };
        bool    _bSRGB{ true }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다)
    };

    /** @brief 편집기가 그리는 핀 하나입니다. */
    struct MaterialGraphPinInfo
    {
        string                 _name;
        MaterialGraphValueType _type{ MaterialGraphValueType::Invalid };
    };

    /** @brief 컴파일 결과입니다. `_listIssue` 가 비어야 `_hlsl` 을 쓸 수 있습니다. */
    struct SW_API MaterialGraphCompileResult
    {
        string                     _hlsl;
        vector<MaterialGraphField> _listField;
        vector<MaterialGraphIssue> _listIssue;
        uint32                     _contentHash{ 0 };      ///< `_hlsl` 의 해시 — 셰이더 파일 이름에 든다. 새 내용은 새 경로라 옛 PSO · 리플렉션이 섞이지 않는다
        bool                       _bEmissive{ false };    ///< NOLINT(sw-bool-flag): 집합체다. 자체 발광을 쓴다 — G버퍼에 칸이 없어 포워드로 그린다(`MATERIAL_FORWARD_ONLY`)
        bool                       _bHair{ false };        ///< NOLINT(sw-bool-flag): 집합체다. 셰이딩 모델 Hair(`SW_MATERIAL_GRAPH_HAIR`) — G버퍼에 칸이 없어 포워드로 그린다
        bool                       _bPostProcess{ false }; ///< NOLINT(sw-bool-flag): 집합체다. 출력이 `PostProcessOutput` 이다 — PostProcess 영역(post_process_material_graph.hlsli · `MATERIAL_DOMAIN_POST_PROCESS`)

        // 파일 머리말 참고.
        /**
         * @brief @p graph 를 컴파일합니다. 순환 · 끊긴 필수 핀 · 형식 불일치 · 모르는 종류 · 없는 노드는 노드 이름과 함께 @p outResult 의 문제가 됩니다.
         * @param sourceName 생성물 머리말에 적을 그래프의 리소스 id 입니다(내용 해시에 든다).
         * @return 문제가 없으면 true 입니다.
         */
        [[nodiscard]] static bool compile( const MaterialGraphDef& graph, string_view sourceName, MaterialGraphCompileResult& outResult );
        /** @brief @p graph 의 출력 노드가 `PostProcessOutput` 이면 true 입니다 — 후처리 머티리얼(화면 입력을 읽고 풀스크린으로 그린다)이다. */
        [[nodiscard]] static bool isPostProcessGraph( const MaterialGraphDef& graph );
        /** @brief @p graph 가 `Output` 없이 `GraphOutput` 만 가지면(하위 그래프) true 입니다. 혼자서는 머티리얼이 되지 않습니다. */
        [[nodiscard]] static bool isSubgraph( const MaterialGraphDef& graph );
        /** @brief 생성된 베이스 머티리얼의 글입니다(`.gen.material`). */
        [[nodiscard]] static string makeMaterialText( const MaterialGraphCompileResult& result, string_view materialName, string_view shaderPath, string_view sourceName );
        /**
         * @brief 노드 @p node 의 핀을 모읍니다. 등록표의 선언이고, 하위 그래프 노드는 그 그래프의 `GraphInput` · `GraphOutput` 입니다(그 파일을 읽는다).
         * @return 종류를 모르거나 하위 그래프를 읽지 못하면 false 입니다(읽기가 오류를 남긴다).
         */
        [[nodiscard]] static bool collectNodePins( const MaterialGraphNodeDef& node, vector<MaterialGraphPinInfo>& outListInput, vector<MaterialGraphPinInfo>& outListOutput );
    };

} // namespace sw
