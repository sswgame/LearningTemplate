/**
 * @file RTSGameModeComponent.h
 * @brief 실시간 전략 키트(`GF_RealTimeStrategy`)의 디렉터 바탕 — 유닛 카탈로그 · 시작 광물(씬 데이터)을 들고, 화면이 읽을 월드 · 이 화면의 플레이어를 냅니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 모습과 입력은 클라이언트 모듈(`GF_Client_RealTimeStrategy`)이
 *          이 바탕의 읽기 API 와 파생 디렉터의 명령으로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          파생은 둘이다: 1 대 1 한 판(`SkirmishGameModeComponent`, 자기 맵 · 지갑)과, 도시 건설 미니게임이 공유 땅 · 지갑 위에 얹는 디렉터(게임 몫 — 두 키트를 섞는다).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

#include <initializer_list>

namespace sw
{
    class RTSWorld;

    /**
     * @class RTSGameModeComponent
     * @brief RTS 월드 하나를 돌리는 디렉터의 공통 몫입니다. 월드 · 명령 · 상태는 파생이 듭니다.
     */
    REFLECT( Abstract, Category = "RealTimeStrategy", DisplayName = "RTS Game Mode", Tooltip = "Base of the game modes that run one RTS world" )
    class SW_KIT_API RTSGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        RTSGameModeComponent();
        virtual ~RTSGameModeComponent() override;

        // ---- 읽기(클라이언트 모듈) ----
        virtual const RTSWorld& getWorld() const = 0;
        /** @brief 지금 쥔 유닛 카탈로그입니다. 판을 열기 전에는 nullptr 입니다. */
        const RTSCatalog* findCatalog() const { return _pCatalog.get(); }
        /** @brief 이 화면이 맡은 플레이어입니다. */
        virtual int32 getLocalPlayer() const { return 0; }
        /** @brief 이 화면의 플레이어를 사람이 맡았으면 true 입니다 — 안개가 남의 유닛을 가리고 명령을 낼 수 있다. 구경(모두 컴퓨터)이면 false 입니다. */
        virtual bool isLocalPlayerHuman() const { return true; }

    protected:
        /** @brief 씬이 적은 유닛 카탈로그를 받습니다. 비었거나 읽지 못하면 오류를 남기고 false 입니다(판을 열지 않는다). */
        [[nodiscard]] bool acquireCatalog();
        /** @brief 씬 데이터가 준 유닛 id 가 모두 카탈로그에 있으면 true 입니다. 비었거나 없는 것을 모두 오류로 남긴다 — C++ 에 기본 id 가 없다. `acquireCatalog` 뒤에 부른다. */
        [[nodiscard]] bool hasUnits( std::initializer_list<const CatalogKey<RTSUnitDef>*> listUnitID ) const;
        /** @brief 새 판이 시작할 때의 광물입니다(카탈로그 규칙의 `mineralCurrency`). */
        int32 getStartMinerals() const { return _startMinerals; }

    private:
        PROPERTY( Category = "Data", DisplayName = "Unit Data", Tooltip = "Unit catalog XML" )
        AssetRef<RTSCatalog> _unitDataPath;
        PROPERTY( Category = "Rules", DisplayName = "Start Minerals", Tooltip = "Minerals a fresh game starts with (the currency of the unit catalog rules)", Min = 0 )
        int32 _startMinerals;

        SharedAssetRef<RTSCatalog> _pCatalog;
    };
} // namespace sw
