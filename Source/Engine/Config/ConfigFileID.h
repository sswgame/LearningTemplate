/**
 * @file ConfigFileID.h
 * @brief 자리가 정해진 설정 파일 하나입니다 — 프로젝트 루트 기준 경로와 그 파일이 담는 리플렉션 형식을 함께 듭니다. 읽는 길은 `ConfigManager::readConfig` 하나다.
 * @details 경로 글과 형식이 따로 있으면 부르는 쪽이 JSON 을 손으로 열어 아는 칸만 읽는다 — 모르는 키(오타)가 조용히 지나가고 범위 검사가 자리마다 다르다.
 *          유니티 `[CreateAssetMenu] ScriptableObject` · Godot `Resource` 처럼 파일이 곧 형식이다. 명령줄 · 서버 배포가 경로를 고르는 설정(`readConfigFileInto`)은 이것이 아니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class ConfigFileID
     * @brief 설정 파일의 선언입니다(`static constexpr ConfigFileID<T> kConfigFile{ "Config/…json" }` — @p T 의 멤버로 둔다).
     */
    template <typename T>
    class ConfigFileID
    {
    public:
        /** @param pPath 프로젝트 루트 기준 경로(글 상수)입니다. */
        constexpr explicit ConfigFileID( const utf8* pPath )
            : _pPath{ pPath }
        {
        }

        /** @brief 로그 · 안내 글에 적을 경로입니다. 읽기는 `ConfigManager::readConfig` 로 한다. */
        constexpr const utf8* getPath() const { return _pPath; }

    private:
        const utf8* _pPath;
    };
} // namespace sw
