/**
 * @file SpriteClipCache.h
 * @brief 스프라이트 클립(`.sprite.json`)을 경로별로 나눠 주는 캐시입니다("SpriteClip").
 * @details 구현은 `SharedAssetCache` 하나다. 표는 프로세스에 하나이고 씬 로드 워커와 게임 모듈이 서비스 없이 부른다(`acquire`). 같은 클립을 쓰는
 *          스프라이트 백 개가 파일을 백 번 읽지 않는다. 약한 참조(`MeshUtil::acquirePrimitive` 와 같은 구조)라 마지막 사용자가 놓으면 사라진다.
 *
 *          `reloadShared` 는 **제자리로** 다시 읽어 그 클립을 가진 쪽 모두가 새 내용을 본다. 틱 밖(게임 스레드)에서만 부른다 — 틱 중의 스프라이트가 읽는
 *          내용을 바꾸기 때문이다. 클립에서 계산한 컴포넌트 상태(UV, 아틀라스)는 부르는 쪽이 `onPropertyChanged` 로 다시 맞춘다(에디터 핫 리로드가 그렇게
 *          한다). 아무도 쓰지 않으면 false 이고 다음 `acquire` 가 새 내용을 읽는다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include "Engine/Resource/Cache/SharedAssetCache.h"

namespace sw
{
    class SpriteClipAsset;

    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<SpriteClipAsset>;

    /** @brief 파일 머리말을 참고하세요. */
    using SpriteClipCache = SharedAssetCache<SpriteClipAsset>;
} // namespace sw
