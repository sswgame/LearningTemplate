/**
 * @file GeometryCut.h
 * @brief 형상 자르기 도우미 — 삼각형 마스크로 나누기 · 열린 경계 고리 찾기 · 구멍 막는 캡 만들기 · 닫힘 검사 · 이어 붙이기입니다.
 * @details 순수 형상 입력 → 형상 출력이라 캐릭터 절단(`DismembermentUtil`) · 찢김 · 병합이 같이 쓰고, 나중의 파괴 가능 메시(미리 쪼갠 조각)도
 *          그대로 씁니다. 정점 속성(법선 · UV · 스킨 · 정점 그룹 · 모프)은 따라갑니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"

#include "Engine/Character/Fit/CharacterGeometry.h"

namespace sw
{
    /** @brief 형상 자르기 함수 모음입니다(전부 static). */
    struct SW_API GeometryCutUtil
    {
        /**
         * @brief @p listTriangleSelected 가 0 이 아닌 삼각형만 담은 형상을 만듭니다. 그 삼각형들이 쓰는 정점만 남기고 번호를 다시 매깁니다.
         * @param pOutListVertexRemap 있으면 원본 정점 → 새 정점 번호(없으면 0xFFFFFFFF)를 채웁니다.
         */
        static void extractTriangles( const AppearanceGeometry& source, vector_reference<const uint8> listTriangleSelected, AppearanceGeometry& outGeometry,
                                      vector<uint32>* pOutListVertexRemap = nullptr );
        /** @brief 마스크가 0 인 삼각형은 @p outKept 로, 0 이 아닌 삼각형은 @p outSevered 로 나눕니다. */
        static void splitByTriangleMask( const AppearanceGeometry& source, vector_reference<const uint8> listTriangleSevered, AppearanceGeometry& outKept,
                                         AppearanceGeometry& outSevered );
        /** @brief @p source 를 @p inoutTarget 끝에 붙입니다(정점 그룹 · 모프는 이름으로 합침, 부품 번호는 @p trianglePart 로 채움). */
        static void appendGeometry( AppearanceGeometry& inoutTarget, const AppearanceGeometry& source, uint16 trianglePart = CharacterGeometryConstant::kNoPart );
        /**
         * @brief 열린 경계(삼각형 하나만 쓰는 모서리)를 고리로 잇습니다. 고리마다 정점 번호가 감긴 방향(삼각형 감음 순서)으로 나옵니다.
         */
        static void findBoundaryLoops( const AppearanceGeometry& geometry, vector<vector<uint32>>& outListLoop );
        /**
         * @brief 경계 고리마다 중심에서 부채꼴로 구멍을 막는 캡 형상을 만듭니다(고리 정점은 복사 — 캡은 따로 음영). 감음은 원래 면과 맞아 붙이면 닫힙니다.
         * @details 법선은 고리 평면의 바깥쪽, UV 는 고리 평면으로 [0, 1] 투영(상처 머티리얼), 스킨은 고리 정점 것을 복사하고 중심은 첫 정점 것입니다.
         * @param pVertexOnSeam 있으면 이 표시(정점마다)가 하나라도 선 고리만 막습니다 — 자른 자리의 구멍만, 원래 있던 구멍(목 · 소매 끝)은 그대로.
         * @return 막은 고리 수입니다.
         */
        static uint32 createCaps( const AppearanceGeometry& geometry, AppearanceGeometry& outCap, const vector<uint8>* pVertexOnSeam = nullptr );
        /** @brief 같은 자리 정점을 하나로 보고(용접), 모든 모서리를 정확히 두 삼각형이 나누면 true 입니다(닫힌 형상). */
        static bool isClosed( const AppearanceGeometry& geometry );
    };
} // namespace sw
