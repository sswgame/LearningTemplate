/**
 * @file CharacterMeshUtil.h
 * @brief 메시 에셋(`Mesh`)과 외형 계산의 중립 형상(`AppearanceGeometry`) 사이의 다리입니다. 피팅은 형상에서 풀고, 결과는 베이크한 메시 사본으로 돌아갑니다.
 * @details 결정: 체형 모프와 피팅 델타는 **사본의 레스트 정점에 베이크한다**(GPU 모프 가중치로 매 프레임 걸지 않는다). 피팅은 그 체형을 적용한 형상에서 풀므로 체형이 바뀌면
 *          어차피 다시 풀어야 하고, 구우면 매 프레임 가중치를 내는 작업이 없어 같은 차림의 캐릭터가 군중 포즈를 나눌 수 있다(Mutable 이 결과 메시를 베이크하는 것과 같다).
 *          베이크한 모프 타깃은 사본에서 빠진다 — 커브 · 얼굴이 같은 이름에 가중치를 내도 두 번 걸리지 않는다. 베이크하지 않은 타깃(얼굴)은 그대로 남아 GPU 모프 풀이 건다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct AppearanceGeometry;
    struct BodyMorphWeight;
    struct FitPartResult;

    class Mesh;

    /**
     * @struct CharacterMeshUtil
     * @brief 메시 ↔ 형상 변환입니다(전부 static).
     */
    struct SW_API CharacterMeshUtil
    {
        /**
         * @brief 메시의 삼각형 목록을 위치로 이은 형상으로 만듭니다. 위치가 10 마이크로미터 안으로 같은 모서리는 한 정점이 됩니다(UV · 노멀 이음매를 넘어 잇는다).
         * @details 삼각형 i 는 메시의 삼각형 i 이고 `_listIndex[c]` 가 메시 정점 c 의 형상 정점이다. 정점의 노멀 · UV · 스킨은 그 위치의 첫 모서리 것이고, 모프 타깃은
         *          이름마다 형상 정점의 첫 모서리 델타다. 피팅 연산이 이음매 양쪽을 같이 움직여 틈이 생기지 않는다. 결과는 결정적이라 보정 조각의 정점 번호가 이 형상을 가리킨다.
         */
        static void makeWeldedGeometry( const Mesh& mesh, AppearanceGeometry& outGeometry );
        /**
         * @brief 베이크한 메시 사본을 만듭니다 — 원본 정점에 모프 가중치를 더하고, 피팅 결과가 있으면 형상 정점의 델타를 더하고 보이지 않는 삼각형을 뺍니다.
         * @param listMorphWeight 구울 모프 가중치입니다. 메시에 없는 이름은 건너뛰고, 가중치가 0 이 아닌 타깃은 사본에서 뺍니다.
         * @param pWelded @p source 로 `makeWeldedGeometry` 가 만든 형상입니다. 피팅 결과가 없으면 nullptr 입니다.
         * @param pFit 그 형상의 피팅 결과입니다(삼각형 보임 · 정점 델타, 바인드 공간). nullptr 이면 모프만 베이크합니다.
         * @return 형상 · 결과의 크기가 원본과 맞지 않거나 구간을 옮기지 못하면 오류를 남기고 nullptr 입니다. 남은 스킨 · 모프 · 구간은 남은 정점 번호로 옮깁니다.
         */
        static shared_ptr<Mesh> createBakedMesh( const Mesh& source, vector_reference<const BodyMorphWeight> listMorphWeight, const AppearanceGeometry* pWelded,
                                                 const FitPartResult* pFit );
        /**
         * @brief 몸의 체형 모프와 스킨 가중치를 장비 메시로 옮깁니다(임포트 · 쿠킹 단계 — 장비는 기본 체형에 한 번 만든다). 장비 정점마다 몸의 가장 가까운 삼각형에 묶는다.
         * @details 옮긴 모프는 장비의 같은 이름 타깃을 바꾸고, 장비에만 있는 타깃은 남긴다. 스킨은 몸 스켈레톤(본 수)을 따른다. 묶이지 않은 정점(@p maxDistance 밖)은
         *          장비의 원래 스킨(없으면 첫 본)과 모프 0 이다 — 그 수를 돌려주며, 0 이 아니면 부르는 쪽이 데이터 오류로 알린다(장비가 몸에서 너무 떨어졌다).
         * @return 몸에 스킨이 없으면 오류를 남기고 장비를 바꾸지 않은 채 장비 정점 수를 돌려줍니다(아무것도 묶지 못했다).
         */
        static uint32 transferBodySurface( const Mesh& body, Mesh& inoutGarment, float32 maxDistance );
    };
} // namespace sw
