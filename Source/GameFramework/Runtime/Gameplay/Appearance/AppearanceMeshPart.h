/**
 * @file AppearanceMeshPart.h
 * @brief 외형의 스킨드 부품(몸 · 옷)을 피팅하고 체형 모프와 함께 베이크한 메시 사본으로 만듭니다. 씬 · 컴포넌트를 모르는 함수라 테스트에서 그대로 돌린다.
 * @details 순서: 부품 메시를 위치로 이은 형상으로 바꾸고(`CharacterMeshUtil::makeWeldedGeometry`) 체형 모프를 CPU 에 걸어, 피팅 표 · 부품 피팅 데이터로 푼다(`FitSolver`).
 *          몸은 해석의 숨김 영역(`ResolvedAppearance::_listHiddenRegion`)도 뺀다. 결과는 부품마다 베이크한 사본이다(`CharacterMeshUtil::createBakedMesh` — 덮인 삼각형을 빼고
 *          피팅 델타와 체형 모프를 레스트에 베이크한다). 그 위의 병합(`makeMergePart` · `mergeParts` — 구간 있는 메시 하나와 낮은 상세 단계)도 여기 있다.
 *          언리얼 Mutable 의 런타임 조립(부품 → 체형 → 병합 → LOD, 결과 캐시)과 같은 단계이고, 외형 컴포넌트는 입력을 모아 워커에 넘기고 결과를 걸기만 한다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/IdentityToken.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CharacterBoneArray;
    struct CharacterLODSectionRule;
    struct CharacterLODSettings;
    struct FitResult;
    struct MeshMergePart;
    struct ResolvedAppearance;
    struct ResolvedPart;

    class FitPartDef;
    class FitTables;
    class Mesh;

    /** @brief 조립할 스킨드 부품 하나입니다. */
    struct SW_GF_API AppearanceMeshPart
    {
        hashed_string     _name{};              ///< 보고에 쓰는 부품 이름
        hashed_string     _owner{};             ///< 해석의 주인(슬롯 · 꾸미기 매개변수). 비어 있으면 몸이다
        const Mesh*       _pMesh{ nullptr };    ///< 읽은 원본 메시(CPU 정점 · 스킨 · 모프)
        const FitPartDef* _pFitData{ nullptr }; ///< 피팅 데이터. 없으면 피팅하지 않고 체형 모프만 베이크한다

        // 스킨드 부품의 베이크한 메시를 만듭니다(전부 static).
        /**
         * @brief 부품마다 베이크한 메시를 @p outListMesh(부품 순서)에 냅니다. 바꿀 것이 없는 부품(걸 모프 · 피팅 없음)은 nullptr — 원본을 그대로 그린다.
         * @param pTables 피팅 표입니다. nullptr 이면 피팅하지 않습니다(피팅 데이터를 가진 부품이 있으면 오류).
         * @param bindBones 몸 스켈레톤의 바인드 본입니다. 조임 고리 · 영역이 본 이름으로 찾습니다.
         * @param pOutFitResult 있으면 푼 피팅 결과(부품 순서는 피팅 데이터를 가진 부품만, 보고 포함)를 담습니다 — 캐릭터 편집 창의 피팅 확인이 읽는다. 풀 것이 없으면 빈 결과입니다.
         * @return 이름이 표에 없거나 피팅을 풀 수 없으면 까닭을 @p outReport 에 담고 false 입니다(그때 @p outListMesh 는 비어 있다).
         */
        [[nodiscard]] static bool assemble( const ResolvedAppearance& resolved, vector_reference<const AppearanceMeshPart> listPart, const FitTables* pTables,
                                            const CharacterBoneArray& bindBones, vector<shared_ptr<Mesh>>& outListMesh, string& outReport, FitResult* pOutFitResult = nullptr );

        /** @brief 부품의 구간 이름입니다 — 몸(주인이 빈 것)은 부품 이름, 다른 부품은 `주인.부품` 이다(차림 안에서 겹치지 않는다). */
        static hashed_string makeSectionName( const ResolvedPart& part );
        /**
         * @brief 부품을 병합에 넣는 모양으로 만듭니다 — `<Section>` 이 없으면 부품의 구간 이름(`makeSectionName`) 조각 하나, 있으면 적은 구간마다 조각 하나다(적지 않은 구간은 빠진다).
         * @param mesh 그릴 메시(베이크한 사본이거나 원본)입니다. 조각이 가리키므로 병합이 끝날 때까지 살아 있어야 합니다.
         * @details 몸(@p bBody)의 조각은 원본 구간 이름 그대로다 — 몸의 주 메시와 몸 구간 컴포넌트가 이미 그 이름으로 그려, 병합을 걸고 풀 때 이름을 다시 매기지 않는다.
         *          다른 부품의 조각은 부품의 구간 이름 뒤에 `/구간` 을 붙여 차림 안에서 겹치지 않게 한다.
         */
        static MeshMergePart makeMergePart( const ResolvedPart& part, const Mesh& mesh, bool bBody );
        /**
         * @brief 병합 조각(@p merge — `makeMergePart` 의 결과)마다 합친 메시 LOD 규칙을 덧붙입니다. 조각 이름이 합친 메시의 구간 이름이다(훅 없는 병합).
         *        `<Section>` 이 적은 값이 먼저이고, 적지 않으면 부품의 값입니다.
         */
        static void appendLODRules( const ResolvedPart& part, const MeshMergePart& merge, vector<CharacterLODSectionRule>& inoutListRule );
        /**
         * @brief 조각들(몸이 먼저)을 구간 있는 메시 하나로 합치고 낮은 상세 단계를 만듭니다(`CharacterMeshUtil::mergeMeshes` · `CharacterLODSettings::buildLODs`).
         * @details LOD 를 만들지 못하면 오류를 남기고 LOD 0 만 가진 메시를 돌려준다(그려지기는 한다). 씬 · 서브시스템을 보지 않아 워커에서 부른다.
         * @return 합치지 못하면 까닭(`mergeMeshes` 의 글)을 담은 오류입니다.
         */
        [[nodiscard]] static expected<shared_ptr<Mesh>> mergeParts( vector_reference<const MeshMergePart> listMergePart, const CharacterLODSettings& lodSettings,
                                                                    const vector<CharacterLODSectionRule>& listLODRule );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AppearanceBakedMeshCache
     * @brief 같은 차림(키 — 해석의 메시 해시 · 부품 · 원본 내용 토큰의 키 · 데이터 판)의 베이크한 메시를 나눠 줍니다. 약한 참조라 그 메시를 그리는 유닛이 없으면 사라집니다.
     * @details 같은 차림의 NPC 가 한 번 베이크한 메시를 같이 그린다 — 베이크하는 비용이 한 번이고, 스킨 원본이 같은 객체라 군중 포즈 묶음도 나눌 수 있다(언리얼 Mutable 의 결과 캐시).
     *          표는 GameFramework 의 모듈 서브시스템이 갖는다(`AppearanceBakedMeshTable` — 게임 서비스가 없는 실행에는 없어 매번 굽는다). 게임 스레드에서 부른다.
     */
    struct SW_GF_API AppearanceBakedMeshCache
    {
        /** @brief 키의 메시들이 모두 살아 있으면 @p outListMesh 에 채우고 true 입니다(원본을 그리는 부품은 nullptr). */
        [[nodiscard]] static bool tryGet( uint64 key, vector<shared_ptr<Mesh>>& outListMesh );
        /**
         * @brief 키의 베이크한 메시들을 남깁니다. 사라진 항목은 이때 치웁니다.
         * @param listSourceContent 키에 섞은 원본 내용 토큰들 — 항목이 들고 있어, 원본이 지워져도 그 토큰 주소가 다른 원본의 키로 다시 쓰여 낡은 베이크를 내주지 않는다.
         */
        static void store( uint64 key, const vector<shared_ptr<Mesh>>& listMesh, const vector<IdentityToken>& listSourceContent );
    };
} // namespace sw
