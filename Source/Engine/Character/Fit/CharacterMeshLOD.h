/**
 * @file CharacterMeshLOD.h
 * @brief 실행 중에 조립한 캐릭터 메시(합친 외형)의 낮은 상세 단계(LOD)를 만듭니다 — 단계마다 구간을 그대로 두고 구간마다 따로 단순화합니다.
 * @details 외형은 부품을 굽고 합쳐 실행 중에 만들어지므로(`CharacterMeshUtil::mergeMeshes`) 임포트가 만든 LOD 를 쓸 수 없다. 합친 결과에서 한 번 만들고 합친 메시와 함께 캐시한다.
 *          언리얼 스켈레탈 메시 LOD 처럼 단계마다 섹션 목록이 LOD 0 과 같고(`MeshLODSectionRange`), 구간마다 단순화해 머티리얼 경계가 섞이지 않는다.
 *          단계는 그 단계가 쓰는 정점만 가진다 — 고른 단계 하나만 스키닝하므로(`GPUMeshMorphPool`) 먼 캐릭터일수록 스키닝 · 그리기가 준다.
 *          표(단계마다 삼각형 비율 · 허용 오차 · 화면 크기)는 데이터(`engine/character/characterlod.xml`)다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"

namespace sw
{
    class Mesh;
    class XMLNode;

    /** @brief 단계 하나의 단순화 규칙입니다. */
    struct CharacterLODLevelRule
    {
        float32 _triangleRatio{ 0.5f }; ///< LOD 0 삼각형 수 대비 목표 비율(0..1, 단계마다 작아진다)
        float32 _maxError{ 0.01f };     ///< 허용 오차(메시 크기 대비 비율, `meshopt_simplify` 의 target_error)
        float32 _screenSize{ 0.3f };    ///< 이 단계로 넘어가는 화면 크기 임계값(0..1, 단계마다 작아진다 — `MeshLODLevel::_screenSize`)
    };

    /**
     * @struct CharacterLODSettings
     * @brief 조립한 캐릭터 메시의 LOD 표입니다(데이터 `engine/character/characterlod.xml`):
     *        `<CharacterLODSettings><Level triangle_ratio="0.5" max_error="0.01" screen_size="0.3"/>…</CharacterLODSettings>`.
     */
    struct SW_API CharacterLODSettings
    {
        vector<CharacterLODLevelRule> _listLevel{};

        /** @brief XML 을 읽습니다. 모르는 이름, 범위 밖 값, 앞 단계보다 크거나 같은 비율 · 임계값은 오류이고 false 입니다(표가 빈다). */
        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        /** @brief 리소스 경로의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 엔진 기본 표(`EngineDefaultAssets::_characterLODSettings`)를 한 번 읽어 돌려줍니다. 읽지 못하면 오류를 한 번 남기고 빈 표입니다(LOD 0 만 그린다). */
        static const CharacterLODSettings& getDefault();

    private:
        [[nodiscard]] bool parseRoot( const XMLNode& root, string_view sourceLabel );
    };

    /** @brief 조립한 캐릭터 메시의 LOD 만들기입니다(전부 static). */
    struct SW_API CharacterMeshLOD
    {
        /**
         * @brief @p inoutMesh 에 표의 단계를 만들어 겁니다(`Mesh::setLODs`). 구간이 없으면 메시 전체가 구간 하나인 것처럼 단순화합니다.
         * @details 단계는 LOD 0 에서 따로 단순화한다(앞 단계에서 이어 줄이지 않는다). 구간 경계 · 열린 가장자리는 잠근다(`meshopt_SimplifyLockBorder`) — 옷과 몸 사이 틈이 벌어지지 않게.
         *          단계 정점은 LOD 0 정점의 부분 집합이고(단순화는 정점을 옮기지 않고 고른다) 처음 쓰인 순서로 모으며 스킨이 따라온다. 모프 차이는 싣지 않는다(`Mesh::setLODs`).
         * @return 표가 비었으면 아무것도 하지 않고 true 입니다. 어느 단계가 삼각형을 줄이지 못하거나 `setLODs` 가 거절하면 오류를 남기고 LOD 없이 false 입니다.
         */
        static bool buildLODs( Mesh& inoutMesh, const CharacterLODSettings& settings );
    };
} // namespace sw
