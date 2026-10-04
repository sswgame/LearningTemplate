/**
 * @file SceneCooker.h
 * @brief 씬 XML 을 런타임이 바로 읽는 SCN1 바이너리로 쿠킹합니다(엔티티 상태까지 바이너리로).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    struct SceneDocument;

    /**
     * @class SceneCooker
     * @brief 씬 문서의 엔티티 상태를 리플렉션 바이너리로 쿠킹하는 오프라인 단계입니다.
     *
     * @details **왜 C++ 에 있는가.** 나머지 쿠킹은 `Scripts/generate/CookAssets.py` 가 합니다. 그런데
     *          엔티티 상태를 바이너리로 만들려면 리플렉션(`TypeInfo` · 프로퍼티 표)이 있어야 하고,
     *          그것은 엔진 안에만 있습니다. 그래서 이 단계만 `App.exe --cook-scenes` 로 넘어옵니다.
     *          셰이더가 `--cook-shaders` 로 넘어오는 것과 같은 이유, 같은 모양입니다.
     *
     *          `.scene.bin` 이 XML 문자열을 담으면 런타임이 엔티티마다 XML 을 다시 파싱합니다. 바이너리 상태는 엔티티당
     *          4254 ns -> 1602 ns 입니다.
     */
    class SceneCooker
    {
    public:
        /**
         * @brief 문서의 각 엔티티 XML 을 바이너리 상태로 쿠킹합니다.
         * @param inoutDoc 대상 문서. 성공한 엔티티는 `_embeddedStateBytes` 가 차고 `_embeddedXml` 이 비워집니다.
         * @param pOutMissingComponentCount 주면, 모르는 타입으로 지어진 컴포넌트(`MissingComponent`) 수를 받습니다. 하나마다 오류 로그를 남깁니다.
         * @return 바이너리로 바꾼 엔티티 수입니다.
         *
         * @details **왕복으로 검증하고 나서만 바꿉니다.** 쿠킹된 바이트를 즉시 되읽어 엔티티 상태 전체가 같은지 본 뒤에 XML 을 버립니다.
         *          모르는 컴포넌트 타입(이 프로세스에 올라오지 않은 모듈의 것 · 이름이 바뀐 타입)은 `MissingComponent` 가 원문을 맡아 왕복을
         *          통과하므로 검증으로는 걸러지지 않습니다 — 그 수를 @p pOutMissingComponentCount 로 받아 쿠킹을 실패시킵니다(`cookAllScenes`).
         */
        SW_API static uint32 cookEntityState( SceneDocument& inoutDoc, uint32* pOutMissingComponentCount = nullptr );

        /**
         * @brief @p sourceRoot 아래의 모든 `*.scene.xml` 을 `<cookedDir>/<상대경로>/<이름>.scene.bin` 으로 쿠킹합니다(이름은 `AssetCookPath`).
         * @param sourceRoot 리소스 루트(절대 경로)입니다. 쿠킹본의 상대 경로가 여기서 정해집니다.
         * @param cookedDir 산출물 스테이징 디렉터리(절대 경로). 비어 있으면 아무것도 하지 않고 실패 하나로 셉니다. 모든 타입 공급자가 등록을 끝내기
         *                  전(`TypeRegistry::areAllModuleTypesRegistered` — 기동 단계 `ModuleTypes` 전)에 불러도 그렇습니다.
         * @param outFailedCount 읽거나 쓰지 못한 씬 수입니다. 모르는 타입의 컴포넌트가 든 씬도 쓰지 않고 셉니다. 하나라도 있으면 배포본에 그 씬이 없다 —
         *                       쿠킹은 실패다. 다만 활성 게임이 아닌 게임 팩(`game/<다른 게임>/`)의 씬이 등록되지 않은 컴포넌트를 쓰면 세지 않고 건너뜁니다 —
         *                       그 게임 모듈은 이 빌드에 없고, 배포본은 활성 게임의 팩만 엽니다.
         * @return 기록한 씬 파일 수입니다.
         *
         * @details 산출물을 소스 옆에 두지 않습니다 — 소스가 옮겨진 뒤 낡은 `.bin` 이 남아 Dev 런타임이 그것으로 물러나 실패를 가립니다.
         *          프리팹은 같은 실행에서 `PrefabCache::cookAllPrefabs` 가 쿠킹합니다. 실패를 세지 않으면 배포본이 그 씬을 열 때에야
         *          "Shipping requires cooked binary scene" 으로 멈춘다.
         */
        SW_API static uint32 cookAllScenes( string_view sourceRoot, string_view cookedDir, uint32& outFailedCount );
    };

} // namespace sw
