/**
 * @file SceneCooker.h
 * @brief 씬 XML 을 런타임이 바로 읽는 SCN1 바이너리로 쿠킹합니다(엔티티 상태까지 바이너리로).
 * @details 스트리밍하는 씬(`SceneSettings::_bStreaming`)은 엔티티를 격자로 셀에 나눠 같은 쿠킹본에 싣는다(`SceneDocument::_listStreamingCell`). 자리는 계층 루트의
 *          primary 씬 컴포넌트이고, 늘 올라 있는 오브젝트(`GameObject::isAlwaysLoaded`)와 자리가 없는 오브젝트는 기본 목록에 남는다. 셀을 넘는 참조는 쿠킹 실패다.
 *          셀마다 파일을 따로 두지 않는 이유: 쿠킹 기록 · DDC(`CookStore`)는 작업 하나에 출력 하나이고, 기본 파일과 셀 파일이 따로 굽히면 서로 다른
 *          쿠킹의 것이 섞여 실린다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Scene/StreamingCellGrid.h"

namespace sw
{
    struct CookJobDesc;
    struct SceneDocument;
    struct SceneEngineServices;

    class CookStore;
    class Scene;
    class TypeRegistry;

    /** @brief 셀 하나의 미리보기 — 쿠킹이 그 셀에 실을 엔티티(열린 씬의 오브젝트 id)입니다. */
    struct StreamingCellPreviewCell
    {
        StreamingCellCoord _cell{};
        vector<uint64>     _listObjectID;
    };
} // namespace sw

namespace sw
{
    /** @brief `SceneCooker::previewStreamingCells` 의 결과입니다. id 는 모두 미리 본 씬의 런타임 오브젝트 id 입니다. */
    struct StreamingCellPreview
    {
        vector<StreamingCellPreviewCell> _listCell;           ///< z, x 오름차순(쿠킹본과 같다)
        vector<uint64>                   _listAlwaysLoadedID; ///< 기본 목록(늘 올라 있거나 자리가 없는 엔티티)
        vector<StreamingCellReference>   _listViolation;      ///< 셀을 넘는 참조. 비어 있지 않으면 쿠킹이 실패한다. 씬에 없는 쪽(되돌릴 수 없는 파일 id)은 0 이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class SceneCooker
     * @brief 씬 문서의 엔티티 상태를 리플렉션 바이너리로 쿠킹하는 오프라인 단계입니다.
     *
     * @details **왜 C++ 에 있는가.** 쿠킹은 모두 엔진 안이다(팩은 `PackCooker`). 특히
     *          엔티티 상태를 바이너리로 만들려면 리플렉션(`TypeInfo` · 프로퍼티 표)이 있어야 하고,
     *          그것은 엔진 안에만 있습니다. 그래서 이 단계만 `App.exe --cook-scenes` 로 넘어옵니다.
     *          셰이더가 `--cook-shaders` 로 넘어오는 것과 같은 이유, 같은 모양입니다.
     *
     *          `.scene.bin` 이 XML 문자열을 담으면 런타임이 엔티티마다 XML 을 다시 파싱합니다. 바이너리 상태는 엔티티당
     *          4254 ns -> 1602 ns 입니다.
     *
     *          엔진 서비스는 부르는 쪽이 묶음(`SceneEngineServices`)으로 넘깁니다 — 쿠킹용 씬도 런타임 씬과 같은 묶음으로 짓고, 전역 표를 다시 찾지 않습니다.
     *          타입 표(`_pTypeRegistry`)가 비어 있으면 쿠킹하지 않고 실패합니다.
     */
    class SceneCooker
    {
    public:
        /**
         * @brief 문서의 각 엔티티 XML 을 바이너리 상태로 쿠킹합니다.
         * @param inoutDoc 대상 문서. 성공한 엔티티는 `_embeddedStateBytes` 가 차고 `_embeddedXML` 이 비워집니다.
         * @param pOutMissingComponentCount 주면, 모르는 타입으로 지어진 컴포넌트(`MissingComponent`) 수를 받습니다. 하나마다 오류 로그를 남깁니다.
         * @return 바이너리로 바꾼 엔티티 수입니다.
         *
         * @details **왕복으로 검증하고 나서만 바꿉니다.** 쿠킹된 바이트를 즉시 되읽어 엔티티 상태 전체가 같은지 본 뒤에 XML 을 버립니다.
         *          모르는 컴포넌트 타입(이 프로세스에 올라오지 않은 모듈의 것 · 이름이 바뀐 타입)은 `MissingComponent` 가 원문을 맡아 왕복을
         *          통과하므로 검증으로는 걸러지지 않습니다 — 그 수를 @p pOutMissingComponentCount 로 받아 쿠킹을 실패시킵니다(`cookAllScenes`).
         */
        SW_API static uint32 cookEntityState( const SceneEngineServices& services, SceneDocument& inoutDoc, uint32* pOutMissingComponentCount = nullptr );

        /**
         * @brief 스트리밍하는 씬의 엔티티를 격자로 셀에 나눕니다. 늘 올라 있는 엔티티는 `_listSceneObjectNode` 에 남고 나머지는 `_listStreamingCell` 로 갑니다.
         * @details 문서를 한 번 지어 엔티티마다 자리(계층 루트의 primary 씬 컴포넌트) · 늘 올라 있음 · 다른 엔티티로의 참조(부착 · 핸들)를 읽는다.
         *          셀을 넘는 참조(`StreamingCellGrid::findCrossCellReferences`)나 프리팹을 찾지 못한 엔티티가 있으면 엔티티마다 오류를 남기고 false 이며 문서는 그대로다.
         *          스트리밍하지 않는 문서면 아무것도 하지 않고 true 입니다.
         * @param pOutListViolation 주면 셀을 넘는 참조를 파일 id 로 채웁니다(먼저 비운다). 다른 이유로 실패하면 빈 채로 false 다.
         */
        [[nodiscard]] SW_API static bool splitStreamingCells( const SceneEngineServices& services, SceneDocument& inoutDoc, string_view sceneName,
                                                              vector<StreamingCellReference>* pOutListViolation = nullptr );

        /**
         * @brief 열린 씬 @p scene 을 쿠커처럼 셀로 나눠 본 결과를 그 씬의 오브젝트 id 로 채웁니다(에디터 Streaming Cells 창 · 개발 명령 `world.cells`).
         * @details 씬을 문서로 쓰고(`Scene::serializeToDocument`) `splitStreamingCells` 로 나눈 뒤, 파일 id 를 씬의 런타임 id 로 되돌린다
         *          (`Scene::collectSavedIDMap`) — 쿠킹이 실패할 참조를 쿠킹 전에 그 오브젝트로 짚게.
         * @return 셀을 넘는 참조 말고 다른 이유로 나누지 못했으면(스트리밍하지 않는 씬 · 문서로 쓰지 못함 · 격자 · 프리팹) false 이고 오류를 남긴다.
         *         셀을 넘는 참조가 있으면 true 이고 `_listViolation` 이 찬다(셀 목록은 비어 있다).
         */
        [[nodiscard]] SW_API static bool previewStreamingCells( const Scene& scene, StreamingCellPreview& outPreview );

        /**
         * @brief @p sourceRoot 아래의 모든 `*.scene.xml` 을 `<cookedDir>/<상대경로>/<이름>.scene.bin` 으로 쿠킹합니다(이름은 `AssetCookPath`).
         * @param sourceRoot 리소스 루트(절대 경로)입니다. 쿠킹본의 상대 경로가 여기서 정해집니다.
         * @param cookedDir 산출물 스테이징 디렉터리(절대 경로). 비어 있으면 아무것도 하지 않고 실패 하나로 셉니다. 모든 타입 공급자가 등록을 끝내기
         *                  전(`TypeRegistry::areAllModuleTypesRegistered` — 기동 단계 `ModuleLoadStage` 전)에 불러도 그렇습니다.
         * @param outFailedCount 읽거나 쓰지 못한 씬 수입니다. 모르는 타입의 컴포넌트가 든 씬도 쓰지 않고 셉니다. 하나라도 있으면 배포본에 그 씬이 없다 —
         *                       쿠킹은 실패다. 다만 활성 게임이 아닌 게임 팩(`game/<다른 게임>/`)의 씬이 등록되지 않은 컴포넌트를 쓰면 세지 않고 건너뜁니다 —
         *                       그 게임 모듈은 이 빌드에 없고, 배포본은 활성 게임의 팩만 엽니다.
         * @return 기록한 씬 파일 수입니다. 씬마다 옆에 프리로드 목록(`<씬>.preload.json`)도 굽지만 이 수에는 세지 않는다.
         *
         * @param store 기록과 DDC 입니다. 씬마다 작업(`CookJob`) 하나로 베이크하고, 기록이 새것이면 DDC 의 출력을 다시 씁니다(`CookStore::cookFile`).
         * @details 산출물을 소스 옆에 두지 않습니다 — 소스가 옮겨진 뒤 낡은 `.bin` 이 남아 Dev 런타임이 그것으로 물러나 실패를 가립니다.
         *          프리팹은 같은 실행에서 `PrefabCache::cookAllPrefabs` 가 쿠킹합니다. 실패를 세지 않으면 배포본이 그 씬을 열 때에야
         *          "Shipping requires cooked binary scene" 으로 멈춘다.
         *          씬 작업의 입력은 씬이 지어지며 읽은 모든 것(씬, 프리팹, 그 프리팹의 중첩 원본 …)이다. 프리팹 하나를 고치면 그것을 쓰는 씬만 다시 베이크한다.
         */
        SW_API static uint32 cookAllScenes( const SceneEngineServices& services, string_view sourceRoot, string_view cookedDir, const CookStore& store,
                                            uint32& outFailedCount );

        /**
         * @brief 씬 쿠킹 작업의 키 값입니다. 설정 해시는 @p registry 의 타입 · 프로퍼티 · 열거자 지문입니다(쿠킹된 상태의 바이트가 그 표로 정해진다).
         */
        SW_API static CookJobDesc makeCookJobDesc( const TypeRegistry& registry );

        /**
         * @brief 씬 프리로드 목록 쿠킹 작업의 키 값입니다(`ScenePreloadList`). 씬 쿠킹과 따로 도는 작업이라 키가 따로다.
         * @details 설정 해시는 0 이다 — 목록은 씬이 지어질 때 읽은 입력(씬 · 프리팹 · 컴포넌트 경로)으로만 정해진다. 형식 버전은 `ScenePreloadList::kVersion`.
         */
        SW_API static CookJobDesc makePreloadCookJobDesc();

        /**
         * @brief 씬 쿠커 버전입니다. 쿠킹된 씬의 형식이나 출력을 바꾸는 코드(기본값 · 검증)를 바꾸면 올립니다 — 모든 씬이 다시 베이크됩니다.
         * @details 2: 월드 설정과 스트리밍 셀(SCN1 판 4).
         */
        static constexpr uint32 kCookVersion = 2;
    };

} // namespace sw
