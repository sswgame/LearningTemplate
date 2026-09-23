/**
 * @file SceneCooker.h
 * @brief 씬 XML 을 런타임이 바로 읽는 SCN1 바이너리로 굽습니다(엔티티 상태까지 바이너리로).
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
     * @brief 씬 문서의 엔티티 상태를 리플렉션 바이너리로 굽는 오프라인 단계입니다.
     *
     * @details **왜 C++ 에 있는가.** 나머지 쿠킹은 `Scripts/generate/CookAssets.py` 가 합니다. 그런데
     *          엔티티 상태를 바이너리로 만들려면 리플렉션(`TypeInfo` · 프로퍼티 표)이 있어야 하고,
     *          그것은 엔진 안에만 있습니다. 그래서 이 단계만 `App.exe --cook-scenes` 로 넘어옵니다.
     *          셰이더가 `--bake-shaders` 로 넘어오는 것과 같은 이유, 같은 모양입니다.
     *
     *          굽기 전에는 쿠킹된 `.scene.bin` 이 XML 문자열을 그대로 담고 있어서, 런타임이
     *          엔티티마다 XML 을 다시 파싱했습니다. 바이너리 상태는 엔티티당 4254 ns -> 1602 ns 입니다.
     */
    class SceneCooker
    {
    public:
        /**
         * @brief 문서의 각 엔티티 XML 을 바이너리 상태로 굽습니다.
         * @param inoutDoc 대상 문서. 성공한 엔티티는 `_embeddedStateBytes` 가 차고 `_embeddedXml` 이 비워집니다.
         * @return 바이너리로 바꾼 엔티티 수입니다.
         *
         * @details **왕복으로 검증하고 나서만 바꿉니다.** 구운 바이트를 즉시 되읽어 컴포넌트 구성이
         *          같은지 본 뒤에 XML 을 버립니다. 모르는 컴포넌트 타입(예: 이 프로세스에 올라오지 않은
         *          게임 모듈의 것)이 섞이면 검증이 실패하고 그 엔티티는 **XML 그대로 남습니다.**
         *          쿠킹이 조용히 컴포넌트를 떨어뜨리는 일은 없습니다.
         */
        SW_API static uint32 cookEntityState( SceneDocument& inoutDoc );

        /**
         * @brief 리소스 트리의 모든 `*.scene.xml` 을 `<cookedDir>/<상대경로>/<이름>.scene.bin` 으로 굽습니다.
         * @param cookedDir 산출물 스테이징 디렉터리(절대 경로). 비어 있으면 아무것도 하지 않습니다.
         * @return 기록한 씬 파일 수입니다.
         *
         * @details 산출물을 소스 옆에 두지 않는 이유는 `CookAssets.py::cookedOutputPathInternal` 의
         *          주석과 같습니다. 소스가 옮겨진 뒤 낡은 `.bin` 이 남아 실패를 가립니다.
         */
        SW_API static uint32 cookAllScenes( string_view cookedDir );
    };

} // namespace sw
