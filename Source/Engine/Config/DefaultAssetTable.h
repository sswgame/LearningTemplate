/**
 * @file DefaultAssetTable.h
 * @brief 기본 에셋 표(`DataPath` 칸만 가진 리플렉션 구조체)를 읽는 공통 몸입니다 — 엔진의 `EngineDefaultAssets` 와 에디터의 `EditorDefaultAssets` 가 함께 쓴다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class AssetID;
    struct TypeInfo;

    /** @struct DefaultAssetTable @brief 파일 머리말 참고. */
    struct SW_API DefaultAssetTable
    {
        /**
         * @brief 리소스 @p assetID(XML)를 엄격하게 읽어 표 @p pTable(형식 @p typeInfo)을 채우고, 필수 칸마다 값이 있고 그 파일이 있는지 봅니다.
         * @details 필수 칸은 헤더에 값이 없다 — 데이터가 기준이다. 빠졌거나 가리키는 파일이 없으면 다른 경로로 대신하지 않고 모두 오류로 남긴 뒤 실패한다(기동 · 쿠킹이 멈춘다).
         *          팩에는 셰이더 원본이 없다(`PackConfig::isRawShaderSource`) — 그 칸은 쿠킹 판정(`--verify-shaders`)이 소스와 바이너리의 짝을 지키므로 쿠킹본을 읽을 때는 보지 않는다.
         *          이 호스트가 읽지 않는 에셋 종류(전용 서버의 텍스처 · 셰이더 · 글꼴)도 팩에 없어 보지 않는다.
         */
        [[nodiscard]] static bool load( const AssetID& assetID, void* pTable, const TypeInfo& typeInfo );
    };
} // namespace sw
