/**
 * @file SceneCellSplitter.h
 * @brief 스트리밍하는 씬 문서(`SceneSettings::_bStreaming`)의 엔티티를 격자 셀로 나눕니다 — 소스 실행(`gv_sceneStreamSourceCells`) · 에디터 미리보기 · 쿠킹(DevTools `SceneCooker`)이 같은 나누기를 쓴다.
 * @details 언리얼 World Partition 의 런타임 해시 생성이 에디터 재생과 쿠킹에서 같은 함수인 자리다. 런타임이 소스 씬도 셀로 열어 엔진에 있고, 쿠커는 부르기만 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Scene/StreamingCellGrid.h"

namespace sw
{
    struct SceneDocument;
    struct SceneEngineServices;

    class Scene;
} // namespace sw

namespace sw
{
    /** @brief 셀 하나의 미리보기 — 쿠킹이 그 셀에 실을 엔티티(열린 씬의 오브젝트 id)입니다. */
    struct StreamingCellPreviewCell
    {
        StreamingCellCoord _cell{};
        vector<uint64>     _listObjectID;
    };
} // namespace sw

namespace sw
{
    /** @brief 열린 씬을 쿠커처럼 셀로 나눠 본 결과입니다. id 는 모두 미리 본 씬의 런타임 오브젝트 id 입니다. */
    struct StreamingCellPreview
    {
        vector<StreamingCellPreviewCell> _listCell;           ///< z, x 오름차순(쿠킹본과 같다)
        vector<uint64>                   _listAlwaysLoadedID; ///< 기본 목록(늘 올라 있거나 자리가 없는 엔티티)
        vector<StreamingCellReference>   _listViolation;      ///< 셀을 넘는 참조. 비어 있지 않으면 쿠킹이 실패한다. 씬에 없는 쪽(되돌릴 수 없는 파일 id)은 0 이다

        /**
         * @brief 열린 씬 @p scene 을 쿠커처럼 셀로 나눠 본 결과를 그 씬의 오브젝트 id 로 채웁니다(에디터 Streaming Cells 창 · 개발 명령 `world.cells`).
         * @details 씬을 문서로 쓰고(`Scene::serializeToDocument`) `SceneCellSplitter::split` 로 나눈 뒤, 파일 id 를 씬의 런타임 id 로 되돌린다
         *          (`Scene::collectSavedIDMap`) — 쿠킹이 실패할 참조를 쿠킹 전에 그 오브젝트로 짚게.
         * @return 셀을 넘는 참조 말고 다른 이유로 나누지 못했으면(스트리밍하지 않는 씬 · 문서로 쓰지 못함 · 격자 · 프리팹) false 이고 오류를 남긴다.
         *         셀을 넘는 참조가 있으면 true 이고 `_listViolation` 이 찬다(셀 목록은 비어 있다).
         */
        [[nodiscard]] SW_API static bool build( const Scene& scene, StreamingCellPreview& outPreview );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SceneCellSplitter
     * @brief 파일 머리말 참고.
     */
    struct SceneCellSplitter
    {
        /**
         * @brief 스트리밍하는 씬의 엔티티를 격자로 셀에 나눕니다. 늘 올라 있는 엔티티는 `_listSceneObjectNode` 에 남고 나머지는 `_listStreamingCell` 로 갑니다.
         * @details 문서를 한 번 지어 엔티티마다 자리(계층 루트의 primary 씬 컴포넌트) · 늘 올라 있음 · 다른 엔티티로의 참조(부착 · 핸들)를 읽는다.
         *          셀을 넘는 참조(`StreamingCellGrid::findCrossCellReferences`)나 프리팹을 찾지 못한 엔티티가 있으면 엔티티마다 오류를 남기고 false 이며 문서는 그대로다.
         *          스트리밍하지 않는 문서면 아무것도 하지 않고 true 입니다.
         * @param pOutListViolation 주면 셀을 넘는 참조를 파일 id 로 채웁니다(먼저 비운다). 다른 이유로 실패하면 빈 채로 false 다.
         */
        [[nodiscard]] SW_API static bool split( const SceneEngineServices& services, SceneDocument& inoutDoc, string_view sceneName,
                                                vector<StreamingCellReference>* pOutListViolation = nullptr );
    };
} // namespace sw
