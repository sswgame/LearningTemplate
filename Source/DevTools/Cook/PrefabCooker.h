/**
 * @file PrefabCooker.h
 * @brief 저작 프리팹을 배포본이 읽는 쿠킹본(`.prefab.bin`)으로 굽습니다(`App --cook-scenes` · `--cook-all`).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Content/CookJob.h"

namespace sw
{
    class CookStore;

    /** @struct PrefabCooker @brief 파일 머리말 참고. */
    struct PrefabCooker
    {
        /**
         * @brief @p sourceRoot 아래(하위 폴더 포함)의 저작 프리팹(`*.prefab.xml`, `*.prefab.json`)을 `<cookedDir>/<상대 경로>/<이름>.prefab.bin` 으로 쿠킹합니다.
         * @return 기록한 쿠킹본 수입니다. 읽지 못한 것과, 앞의 소스와 같은 쿠킹본을 쓰게 되는 소스(`x.prefab.xml` 과 `x.prefab.json`)는
         *         @p outFailedCount 로 세고 경고합니다. Shipping 은 쿠킹본 하나만 읽으므로 어느 것이 이길지 정해 두면 안 됩니다.
         * @details 씬처럼 엔진이 쿠킹한다(`App --cook-scenes` 가 리소스 루트로 함께 부른다. 언리얼의 쿡 커맨드렛에 해당한다). 형식을 쓰는 곳은
         *          `PrefabAsset::saveToBinaryFile` 하나다. 중첩 인스턴스는 펼쳐서 싣는다(`flattenNestedInstances`). 펼치지 못한 프리팹(원본 없음 · 순환)은
         *          쓰지 않고 실패로 센다 — 배포본이 부품 없이 스폰하는 것을 쿠킹에서 막는다.
         *          프리팹마다 작업(`CookJob`) 하나로 베이크하고 @p store 의 기록이 새것이면 DDC 의 출력을 다시 쓴다(`CookStore::cookFile`). 중첩 원본도 그 작업의 입력이다.
         */
        static uint32 cookAll( const AbsolutePath& sourceRoot, string_view cookedDir, const CookStore& store, uint64 reflectionFingerprint, uint32& outFailedCount );
        /** @brief 프리팹 쿠킹 작업의 키 값입니다. 설정 해시는 리플렉션 지문(`CookReflectionFingerprint`)입니다. */
        static CookJobDesc makeCookJobDesc( uint64 reflectionFingerprint );
    };
} // namespace sw
