/**
 * @file AssetReloadCascade.h
 * @brief 에셋 하나가 바뀌었을 때 그것을 로드 시 펼쳐 넣은 에셋들을 의존 순서로 정합니다(연쇄 리로드의 계획). 캐시 · 씬을 모르는 순수 계산이라 테스트가 색인만으로 돌린다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    class EditorReferenceIndex;

    /**
     * @brief 리소스 id 가 "로드할 때 참조 대상의 내용을 펼쳐 넣는" 종류이면 true 입니다(`AssetReloadRoute::_referenceUse`). 연쇄는 이런 참조자만 따라간다.
     * @details 경로로만 들고 쓸 때 캐시에서 푸는 참조(메시 → 머티리얼, 스프라이트 → 텍스처)는 대상의 캐시 리로드로 이미 따라가므로 연쇄할 것이 없다.
     */
    using AssetInlinesReferencesFunc = bool ( * )( string_view assetID );

    /**
     * @struct AssetReloadCascade
     * @brief 연쇄 리로드의 계획입니다. 언리얼 에셋 레지스트리의 참조자 그래프(부모 머티리얼을 고치면 인스턴스가 다시 펼쳐진다) · 유니티의 의존 재임포트와 같은 일을 한다.
     * @details 바뀐 에셋 X 에서 시작해 역색인(`EditorReferenceIndex::findReferrers`)으로 X 를 `_base` 로 펼쳐 넣는 참조자를, 다시 그 참조자를 펼쳐 넣는 참조자를 따라간다(방문 집합으로 한 번씩).
     *          경로만 적은 칸(`EditorAssetReference::_bInlined` 가 false)은 따라가지 않는다 — 카탈로그 둘이 서로의 경로를 적는 일은 순환이 아니다.
     *          파일 단위 계획이라 두 카탈로그가 서로 다른 항목을 부모로 삼아도 순환이다. 항목 순환은 읽기(`DataCatalogInheritance`)가 따로 잡는다.
     *          중간 고리가 로드되지 않았어도 따라간다 — 손자는 사슬 전체를 디스크에서 펼쳐 읽었으므로 조부모가 바뀌면 다시 읽어야 한다. 로드됐는지는 실행하는 쪽(`AssetHotReload`)이 본다.
     *          순서는 위상 정렬(`TopologicalSortUtil`)이다 — 참조자는 자기가 펼쳐 넣는 대상보다 뒤에 온다(부모 → 자식 → 손자). 순환은 오류다.
     */
    struct SW_EDITOR_API AssetReloadCascade
    {
        /**
         * @brief @p changedID 가 바뀌었을 때 다시 읽을 참조자들을 의존 순서로 @p outListOrder 에 담습니다(@p changedID 자신은 넣지 않는다). 먼저 출력 목록을 비운다.
         * @param index 바뀐 파일을 이미 다시 스캔한 역색인(`EditorReferenceIndex::refreshFile`)입니다.
         * @param pfnInlinesReferences 참조자가 연쇄 대상인지 정하는 표의 칸입니다.
         * @return 순서를 정했으면 true 입니다. 참조자 사이에 순환이 있으면 오류를 남기고 false 이며 @p outListOrder 는 비어 있다(아무것도 다시 읽지 않는다).
         */
        [[nodiscard]] static bool planDependents( const EditorReferenceIndex& index, string_view changedID, AssetInlinesReferencesFunc pfnInlinesReferences, vector<string>& outListOrder );
    };
} // namespace sw::editor
