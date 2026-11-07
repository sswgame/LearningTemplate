/**
 * @file ResourceRemap.h
 * @brief 리소스 경로 교체 표입니다. 안을 고칠 수 없는 바이너리(텍스처 · 오디오 · 메시)는 조건에 따라 다른 파일을 읽는다.
 * @details 표는 `<ResourceRemap><Remap id="common/ui/logo.dds" when="locale.ko" to="common/ui/logo_ko.dds" /></ResourceRemap>` 이다.
 *          같은 id 의 줄은 위에서부터 보고 처음 참인 줄의 `to` 를 읽는다. 조건은 리소스 조건(`ResourceTags::compileTest`)과 같은 식이다.
 *          리소스 입구(`AssetIO`)가 찾기 전에 한 번 바꾸므로 동기 · 비동기 · 있는지 묻기가 같은 파일을 본다. 쿠킹 작업 안에서는 바꾸지 않는다 — 교체는 읽을 때 정하고,
 *          쿠커가 기계의 언어로 다른 파일을 구우면 쿠킹본이 기계마다 달라진다. 두 파일 모두 팩에 든다(교체 대상도 평범한 리소스다).
 *          Godot 의 리소스 교체(Project Settings 의 Localization → Remaps, 언어별 경로)와 같은 자리이고(기억에 의존), 여기는 언어만이 아니라 모든 리소스 태그 축을 받는다.
 *          교체된 리소스가 로드 축 태그에 기대면 그 태그가 바뀔 때 쥔 공유 캐시가 다시 읽는다(`ResourceComposition`). GPU 캐시(텍스처)는 다음 읽기가 새 파일을 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"

namespace sw
{
    /**
     * @struct ResourceRemap
     * @brief 프로세스의 경로 교체 표입니다(게임 팩 설정 `PackEngineSettings::_resourceRemap` 이 기동 때 읽는다). 읽기는 로드 워커 · IO 스레드에서도 오므로 잠금으로 지킵니다.
     */
    struct SW_API ResourceRemap
    {
        static constexpr const utf8* kRootElement = "ResourceRemap";

        /**
         * @brief 표 글을 읽어 지금 표를 바꿉니다. 틀린 글(모르는 원소 · 빠진 칸 · 틀린 조건 · 자기 자신으로 바꾸기 · 바꾼 것을 또 바꾸기)이면 오류를 남기고 false 이며 표는 그대로입니다.
         * @details 사슬(바꾼 id 를 또 바꾸는 줄)을 받지 않는 까닭: 순서에 따라 답이 갈리고 순환을 찾아야 한다. 한 단계면 줄 하나가 곧 답이다.
         */
        [[nodiscard]] static bool loadFromXMLText( string_view text, string_view sourceName );
        /** @brief 리소스 @p assetID 의 표를 읽습니다(`loadFromXMLText`). 읽지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool loadFromAsset( const AssetID& assetID );
        /** @brief 표를 비웁니다(엔진 종료 · 테스트). */
        static void clear();

        /**
         * @brief 리소스 @p assetID 를 지금 태그로 바꿔야 하면 @p outAssetID 에 바꾼 id 를 쓰고 true 입니다. 바꾸지 않으면 false 입니다(표가 비면 잠금 없이 끝난다).
         * @details 로드 축 태그를 묻는 줄이 있는 id 는 그 태그를 다시 읽기 기록에 남긴다(`ResourceComposition::noteRemap`).
         */
        [[nodiscard]] static bool resolve( string_view assetID, string& outAssetID );
    };
} // namespace sw
