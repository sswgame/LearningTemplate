/**
 * @file MatchmakingKitConfig.h
 * @brief 매칭 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchModeCatalog.h"

namespace sw
{
    /** @brief 매칭 키트의 서버 설정입니다. 모드는 `_catalog` 와 `_listMode` 를 합쳐 하나는 있어야 합니다. */
    REFLECT()
    struct MatchmakingKitConfig
    {
        REFLECT_BODY();
        /** @brief 모드 정의(`<MatchModeCatalog>`)입니다 — 글 칸이었을 때는 같은 이름의 참조 칸과 GUID 이전 도구가 가르지 못했다. */
        PROPERTY()
        AssetRef<MatchModeCatalog> _catalog{};
        /** @brief 설정에 바로 적는 모드입니다. 데이터 파일 없이 도는 도구 · 테스트용이며 카탈로그와 같은 형식입니다. */
        PROPERTY()
        vector<MatchModeDef> _listMode{};
        /** @brief 파티 최대 인원입니다. */
        PROPERTY( Min = 1 )
        int32 _maxPartySize{ 4 };
        /** @brief 떠난 계정을 파티와 로비에 남겨 둘 밀리초입니다. 0 이면 바로 뺍니다. */
        PROPERTY( Required, Min = 0, Max = 3600000 )
        int32 _reconnectGraceMs{ 0 };
        /** @brief 빠지기가 권한 서버의 확인 답을 기다리는 밀리초입니다. 넘으면 권한 서버가 없는 것으로 보고 빠진 것으로 끝냅니다(감사 O-18). */
        PROPERTY( Required, Min = 1, Max = 60000 )
        int32 _cancelConfirmWaitMs{ 0 };
        /**
         * @brief 이 서버가 매칭이 배정하는 전용 게임 서버인지입니다.
         * @details 켜면 배정 주제(`mm.assign.<서버 id>`)를 받아 배정받은 계정만 게임 포트에 들입니다. 받아들이기 정책(`IConnectAdmission`)을 문맥에 내놓고 계정 키트의 `gameConnect` 가 씁니다.
         */
        PROPERTY()
        bool _bGameServer{ false };
    };
} // namespace sw
