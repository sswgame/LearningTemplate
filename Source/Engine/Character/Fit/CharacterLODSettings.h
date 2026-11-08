/**
 * @file CharacterLODSettings.h
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
#include "Core/String/hashed_string.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct CharacterLODSectionRule;

    enum class XMLRootStatus : uint8;
} // namespace sw

namespace sw
{
    class Mesh;
    class XMLNode;

    /** @brief 단계 하나의 단순화 규칙입니다. 값은 늘 데이터(`engine/character/characterlod.xml`)가 정한다 — 셋 다 필수 속성이라 기본값이 없다. */
    REFLECT()
    struct SW_API CharacterLODLevelRule
    {
        REFLECT_BODY();

        PROPERTY( Name = "triangle_ratio", Required, Min = 0.0, Max = 1.0 )
        float32 _triangleRatio{ 0.0f }; ///< LOD 0 삼각형 수 대비 목표 비율(0..1, 단계마다 작아진다)
        PROPERTY( Name = "max_error", Required, Min = 0.0 )
        float32 _maxError{ 0.0f }; ///< 허용 오차(메시 크기 대비 비율, `meshopt_simplify` 의 target_error)
        PROPERTY( Name = "screen_size", Required, Min = 0.0, Max = 1.0 )
        float32 _screenSize{ 0.0f }; ///< 이 단계로 넘어가는 화면 크기 임계값(0..1, 단계마다 작아진다 — `MeshLODLevel::_screenSize`)
        PROPERTY( Name = "lock_border", Required )
        bool _bLockBorder{ false }; ///< 열린 가장자리(소매 · 머리카락 · 숨긴 몸의 경계)를 옮기지 않는다 — 가까운 단계에서 옷과 몸 사이 틈을 막는다(`meshopt_SimplifyLockBorder`)
        PROPERTY( Required )
        bool _bPermissive{ false }; ///< 이음매(UV · 각진 모서리의 갈린 정점)를 넘어 줄인다 — 먼 단계에서 정점을 줄인다(`meshopt_SimplifyPermissive`, 법선을 오차에 넣는다)
        PROPERTY( Required )
        bool _bPrune{ false }; ///< 작은 떨어진 조각(눈썹 등)을 지운다 — 가장 먼 단계(`meshopt_SimplifyPrune`)
    };

    /**
     * @struct CharacterLODSettings
     * @brief 조립한 캐릭터 메시의 LOD 표입니다(데이터 `engine/character/characterlod.xml`):
     *        `<CharacterLODSettings><Level triangle_ratio="0.5" max_error="0.01" screen_size="0.3" lock_border="true" permissive="false" prune="false"/>…</CharacterLODSettings>`.
     */
    struct SW_API CharacterLODSettings
    {
        vector<CharacterLODLevelRule> _listLevel{};

        /** @brief XML 을 읽습니다. 모르는 이름, 범위 밖 값, 앞 단계보다 크거나 같은 비율 · 임계값은 오류이고 false 입니다(표가 빈다). */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /** @brief 리소스 경로의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 엔진 기본 표(`EngineDefaultAssets::_characterLODSettings`)를 한 번 읽어 돌려줍니다. 읽지 못하면 오류를 한 번 남기고 빈 표입니다(LOD 0 만 그린다). 부품별 차이는 `CharacterLODSectionRule` 이다. */
        static const CharacterLODSettings& getDefault();

    private:
        /** @brief 찾은 루트를 읽습니다. 글 입구(`loadFromXMLText`)와 리소스 입구(`loadFromAsset` — `XMLReadUtil::parseResourceRoot` 로 층 · 다시 읽기 기록 · 접속 대조를 받는다)가 함께 쓴다. */
        [[nodiscard]] bool loadFromRoot( XMLRootStatus status, const XMLNode& root, string_view sourceLabel );
        [[nodiscard]] bool parseRoot( const XMLNode& root, string_view sourceLabel );

    public:
        // 조립한 캐릭터 메시의 LOD 만들기입니다(전부 static). */
        /**
         * @brief @p inoutMesh 에 표의 단계를 만들어 겁니다(`Mesh::setLODs`). 구간이 없으면 메시 전체가 구간 하나인 것처럼 단순화합니다.
         * @details 단계는 LOD 0 에서 따로 단순화한다(앞 단계에서 이어 줄이지 않는다). 구간 경계 · 열린 가장자리는 잠근다(`meshopt_SimplifyLockBorder`) — 옷과 몸 사이 틈이 벌어지지 않게.
         *          단계 정점은 LOD 0 정점의 부분 집합이고(단순화는 정점을 옮기지 않고 고른다) 처음 쓰인 순서로 모으며 스킨이 따라온다. 모프 차이는 싣지 않는다(`Mesh::setLODs`).
         *          @p listRule 은 구간 이름으로 찾는다 — 뺀 구간은 그 단계 범위가 비어 그리지 않는다(`MeshComponent::findDrawSection`).
         * @return 표가 비었으면 아무것도 하지 않고 true 입니다. 어느 단계가 삼각형을 줄이지 못하거나 `setLODs` 가 거절하면 오류를 남기고 LOD 없이 false 입니다.
         */
        static bool buildLODs( Mesh& inoutMesh, const CharacterLODSettings& settings, const vector<CharacterLODSectionRule>& listRule );
    };

    /**
     * @struct CharacterLODSectionRule
     * @brief 합친 메시의 구간 하나에 거는 LOD 규칙입니다(데이터 `ItemVisual` 의 `<Part>` · `<Section>` 의 `lodDetail` · `lodDropLevel`). 규칙이 없는 구간은 표 그대로입니다.
     * @details 언리얼 스켈레탈 메시는 LOD 마다 섹션을 끄거나(Disable Sections) 섹션별 감소 설정을 둔다(기억에 의존) — 얼굴은 늦게 줄이고 눈썹 · 장식은 먼저 없앤다.
     */
    struct CharacterLODSectionRule
    {
        hashed_string _section{};      ///< 합친 메시의 구간 이름
        float32       _detail{ 1.0f }; ///< 표의 `triangle_ratio` 에 곱하고(1 을 넘지 않는다) `max_error` 를 나눈다 — 1 보다 크면 늦게 줄인다
        uint32        _dropLevel{ 0 }; ///< 이 LOD 단계(1 부터)부터 삼각형을 모두 뺀다. 0 이면 빼지 않는다
    };

} // namespace sw
