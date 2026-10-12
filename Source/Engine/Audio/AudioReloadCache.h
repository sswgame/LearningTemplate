/**
 * @file AudioReloadCache.h
 * @brief 오디오 데이터 파일(믹서 그래프 `*.audiomixer.xml`, 이벤트 라이브러리 `*.audioevents.xml`)을 에셋 캐시 레지스트리에 노출하는 어댑터입니다.
 *        에디터 핫 리로드가 다른 에셋과 같은 경로로 다시 읽게 합니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    /**
     * @class AudioReloadCache
     * @brief 상태가 없는 어댑터입니다. 그래프와 라이브러리는 오디오 서비스(`IAudioService`)가 가지고, 이 클래스는 바뀐 파일을 그 서비스에 넘깁니다(`LocalizationReloadCache` 와 같은 모양).
     * @details 에디터의 핫 리로드 경로 표가 이 파일의 접미사(`.audiomixer.xml`, `.audioevents.xml`)를 "경로를 든 캐시가 읽는다" 경로로 돌리면 `isCached` 를 아는 이 캐시가 받는다.
     *          `clear()` 는 아무것도 비우지 않는다 — 캐시 비우기(재초기화)가 소리 데이터를 지우면 안 되고 수명은 서비스가 정한다.
     */
    class SW_API AudioReloadCache final : public IAssetCache
    {
    public:
        /** @brief 레지스트리의 종류 이름("AudioData")입니다. */
        const utf8* getAssetKindName() const override { return "AudioData"; }
        /** @brief 그 경로가 지금 쓰는 믹서 그래프나 이벤트 라이브러리의 파일이면 true 입니다. */
        bool isCached( string_view relativePath ) const override;
        /** @brief 그 파일을 다시 읽어 바꿉니다(`IAudioService::reloadDataFile`). 실패하면 지금 것을 그대로 씁니다. 디바이스는 쓰지 않습니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /** @brief 서비스가 들고 있는 데이터 파일 수의 하한(믹서 그래프가 파일이면 1)입니다. */
        size_t getCachedCount() const override;
        /** @brief 아무것도 하지 않습니다(위 설명). */
        void clear() override {}
    };
} // namespace sw
