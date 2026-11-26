/**
 * @file SceneHLODMeshCooker.h
 * @brief 스트리밍 씬의 합친 HLOD 대리(`HLODMeshComponent`)를 쿠킹 때 합쳐 씬마다 메시 파일 하나(`<씬>.hlod.mesh`)로 굽습니다.
 * @details 소스 실행은 대리가 부품 메시를 읽어 실행 때 합친다(게임 스레드). 쿠킹본은 대리마다 구간 하나인 메시를 읽기만 한다 — 첫 로드의 합치기도, 부품 메시 읽기도 없다
 *          (언리얼 HLOD 빌더 커맨드릿이 프록시 메시를 굽는 자리). 구간의 정점은 대리의 자리(부품들의 중심) 둘레라 파일 전체의 경계도 셀 하나 크기다.
 *          씬 쿠킹(`SceneCooker`)과 따로 도는 작업이다 — 쿠킹 기록은 작업 하나에 출력 하나다. 두 작업은 같은 문서(묶기 → 셀 나누기)에서 대리를 만들고 구간 이름(`makeSectionName`)으로만 만난다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

namespace sw
{
    struct MeshAssetData;
    struct SceneDocument;
    struct SceneEngineServices;

    /** @struct SceneHLODMeshCooker @brief 파일 머리말 참고(전부 static). */
    struct SceneHLODMeshCooker
    {
        /** @brief 합치기를 켠 씬의 글에 있는 설정 칸 이름입니다 — 글에 없으면 합칠 대리가 없다(씬을 짓지 않고 거른다). */
        static constexpr string_view kSettingMarker = "_hlodMergeMaxInstanceCount";

        /** @brief 쿠킹한 씬 경로(`*.scene.bin`) 옆의 합친 메시 경로입니다. 쿠킹한 씬 경로가 아니면 빈 글입니다. */
        static string makeMeshPath( string_view cookedScenePath );
        /** @brief 대리 @p proxyFileID 의 컴포넌트 @p localID 가 그리는 구간 이름입니다(파일 안에서 하나뿐이다). */
        static hashed_string makeSectionName( uint64 proxyFileID, uint64 localID );

        /**
         * @brief 나눈 문서의 합친 대리를 @p meshID 의 구간을 그리는 `InstancedMeshComponent` 로 바꿉니다. 자리 · 머티리얼은 그대로입니다.
         * @return 바꾼 컴포넌트 수입니다. 대리의 상태를 읽지 못하면 오류를 남기고 nullopt 이며 문서는 그대로입니다.
         */
        [[nodiscard]] static optional<uint32> replaceMergedProxies( SceneDocument& inoutDoc, string_view meshID );
        /**
         * @brief 나눈 문서의 합친 대리마다 부품을 합쳐 구간 하나로 @p outData 에 담습니다. 합친 대리가 없으면 true 이고 정점이 비어 있습니다.
         * @return 부품 메시를 읽지 못했거나 구간이 없으면 오류를 남기고 false 입니다 — 빠진 대리를 싣지 않는다.
         */
        [[nodiscard]] static bool buildMesh( const SceneEngineServices& services, const SceneDocument& doc, string_view scenePath, MeshAssetData& outData );
    };
} // namespace sw
