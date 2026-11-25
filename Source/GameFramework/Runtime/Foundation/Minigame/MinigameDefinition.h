/**
 * @file MinigameDefinition.h
 * @brief 미니게임 데이터(`<팩>/minigames/<이름>.minigame.xml`)입니다 — 미니게임 하나가 쓰는 맵 · 들어가는 방식 · 게임 기능 · 입력 문맥 · UI · 카탈로그.
 * @details 언리얼 Lyra 의 `ULyraExperienceDefinition`(켤 Game Feature · 액션 셋), 용과 같이 · GTA · 마리오 파티의 미니게임 하나와 같은 자리다. 키트는 모두 링크되어 있고 미니게임은
 *          그 게임 기능 등록만 켜고 끈다(런타임 모듈 로드 기각 — `docs/agents/Gameplay.md` "게임 기능 · 미니게임"). 판을 돌리는 컴포넌트(`GameModeComponent` 파생)는 맵에 둔다 —
 *          정의에 게임 모드 칸을 두지 않는다(어디에도 적용되지 않던 `<GameMode>` 원소를 지웠다). 읽기는 루트 속성을 리플렉션 스키마로(`XMLFields::readProperties`), 자식 원소는 없다.
 * @code
 *     <Minigame name="kitdemo_actionplatformer" map="game/playground/maps/kitdemo_actionplatformer.scene.xml" entry="SceneSwitch" portalSlot="3"
 *               features="ActionPlatformer" inputContexts="platformer" ui="game/playground/ui/platformer_hud.ui.xml" catalogs="ActionPlatformerCatalog"/>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AssetID;
    class GameFeatureRegistry;
    class InputMap;
    class XMLCatalogRoot;

    /** @brief 미니게임에 들어가는 방식입니다. */
    ENUM()
    enum class MinigameEntry : uint8
    {
        SubLevel,    ///< 들어갈 때의 활성 씬에, 진입을 건 사물의 변환으로 하위 레벨을 더한다(바탕 씬이 보이고 그대로 돈다 — 언리얼 레벨 인스턴스 액터 · 유니티 Additive 씬, `MinigameFlow`)
        SceneSwitch, ///< 맵 씬으로 바꾼다(들어갈 때의 활성 씬을 `GameStateSnapshot` 으로 찍어 두고 나올 때 복원 — `MinigameFlow`)
    };

    /**
     * @brief 미니게임 안에서 세이브를 허락하는지입니다.
     * @details 언리얼 · 유니티에는 이 자리의 엔진 기능이 없고 게임마다 "세이브 가능?" 깃발을 둔다(RDR2 · 용과 같이는 미니게임 동안 막고, 농장 · 도시 건설처럼 긴 판은 안에서 저장한다) — 정의의 값으로 둔다.
     */
    ENUM()
    enum class MinigameSavePolicy : uint8
    {
        Allowed, ///< 안에서 저장한다 — 세이브가 이 미니게임과 덮인 바탕 씬을 함께 담고, 불러오면 이 미니게임 안으로 들어간다(`MinigameFlow::travelToSavedMinigame`)
        Blocked, ///< 판이 끝날 때까지 저장을 막는다(`SaveLock`) — 대전 · 오락기처럼 되돌려 다시 하면 안 되는 짧은 판
    };

    /**
     * @brief 미니게임 정의 하나입니다. `loadFromAsset` · `loadFromXMLText` 가 읽고(모르는 속성 · 원소 · 깨진 값은 오류), `validate` 가 참조를 봅니다.
     * @details 읽기는 글만 보고, 참조(게임 기능 이름 · 맵 · UI 문서)는 `validate` 가 등록부 · 에셋으로 본다 — 쿠킹 · 테스트가 엔진 없이도 형식을 읽게 둘로 나눈다.
     */
    REFLECT()
    struct SW_GF_API MinigameDefinition
    {
        REFLECT_BODY();

        /** @brief 허브 입구가 없는 미니게임의 `_portalSlot` 입니다(시나리오 · 실행 인자 · 씬에 놓은 입구로만 들어간다). */
        static constexpr int32 kNoPortalSlot = -1;

        PROPERTY( Required )
        hashed_string _name{}; ///< 파일 이름(`<이름>.minigame.xml`)과 같아야 한다 — `minigame.enter <이름>` 이 그 이름으로 찾는다
        PROPERTY( Required )
        DataPath _map; ///< 들어갈 맵 씬(리소스 id)
        PROPERTY( Required )
        MinigameEntry _entry{ MinigameEntry::SceneSwitch }; ///< 들어가는 방식
        PROPERTY()
        string _matchMode{}; ///< 매치형이면 매칭 모드(서버 매칭 키트 `MatchModeCatalog` 의 id) — 적으면 배정받은 매치로만 들어간다(`MinigameFlow::enterMatch`). 아래 둘과 함께 적는다
        PROPERTY()
        string _matchBoard{}; ///< 매치 서버가 결과를 낼 순위표(서버 순위표 키트 `LeaderboardCatalog` 의 id)
        PROPERTY()
        hashed_string _matchScore{}; ///< 순위표에 낼 결과 값 이름(디렉터의 `collectMinigameResult`) — 이 값은 바탕 판의 보상으로 넘기지 않는다
        PROPERTY( Name = "features" )
        vector<hashed_string> _listFeature{}; ///< 켤 게임 기능 이름(`SW_GAME_FEATURE` 의 이름, 적은 순서로 켜고 역순으로 끈다)
        PROPERTY( Name = "inputContexts" )
        vector<hashed_string> _listInputContext{}; ///< 켤 입력 맵 레이어(게임 입력 맵 묶음에 있어야 한다 — 켤 때 본다)
        PROPERTY( Name = "ui" )
        vector<string> _listUI{}; ///< 열 UI 문서(리소스 id). DataPath 목록은 리플렉션 글 처리기가 없어 글 목록이다 — 있는지는 `validate` 가 본다
        PROPERTY( Name = "catalogs" )
        vector<hashed_string> _listCatalog{}; ///< 기능이 읽을 데이터 종류(`kDataKind`). 기능이 적지 않은 종류를 읽거나 적은 종류를 아무도 읽지 않으면 켜기가 진다
        PROPERTY( Name = "save" )
        MinigameSavePolicy _savePolicy{ MinigameSavePolicy::Allowed }; ///< 안에서 세이브를 허락하는지
        PROPERTY( Name = "localPlayers", Min = 1, Max = 4 )
        int32 _localPlayerCount{ 1 }; ///< 한 화면에서 함께 하는 사람 수. 2 이상이면 켜는 동안 입력 사용자를 연다(`InputManager::openLocalUsers`)
        PROPERTY( Min = -1 )
        int32 _portalSlot{ kNoPortalSlot }; ///< 허브 입구 자리 번호(0 부터, 팩에서 겹치지 않는다) — 적으면 허브의 `MinigamePortalSpawnerComponent` 가 그 자리에 입구를 세운다
        PROPERTY()
        float3 _portalOffset{}; ///< 허브 입구에서 하위 레벨 맵 원점까지의 어긋남(미터) — 맵의 정문이 원점이 아닐 때 정문을 입구 앞에 맞춘다. 씬 전환 미니게임은 보지 않는다

        /** @brief 허브 입구를 가지면 true 입니다(`_portalSlot` 을 적었다). */
        bool hasPortal() const { return _portalSlot >= 0; }
        /** @brief 입구를 가진 정의를 자리 순 목록 @p inoutListPortal 의 제자리에 넣습니다. 자리가 이미 찼으면 오류를 남기고 넣지 않습니다(false). */
        [[nodiscard]] static bool insertPortal( vector<MinigameDefinition>& inoutListPortal, MinigameDefinition definition );

        /** @brief 매치형이면 true 입니다 — 매칭 줄에 서고 배정받아 매치 세션으로 들어간다. */
        bool isMatch() const { return _matchMode.empty() == false; }

        /** @brief 리소스 @p assetID 의 XML 을 읽습니다. 실패하면 이유를 알리고 false 이며, @p outDefinition 은 그대로입니다. */
        [[nodiscard]] static bool loadFromAsset( const AssetID& assetID, MinigameDefinition& outDefinition );
        /** @brief `loadFromAsset` 의 글 버전입니다(테스트). @p sourceName 은 오류에 적는 이름입니다. */
        [[nodiscard]] static bool loadFromXMLText( string_view xmlText, string_view sourceName, MinigameDefinition& outDefinition );

        /**
         * @brief 참조를 봅니다 — 게임 기능 이름이 @p features 에 있고 겹치지 않는지, 입력 문맥 · 카탈로그가 겹치지 않는지, 맵 · UI 문서가 있는지.
         * @return 틀린 것을 모두 알리고(첫 오류에서 멈추지 않는다) 하나라도 있으면 false 입니다.
         */
        [[nodiscard]] bool validate( const GameFeatureRegistry& features, string_view sourceName ) const;
        /**
         * @brief 입력 문맥을 게임 입력 맵 @p inputMap 에 견줘 봅니다 — 함께 켜면 한 입력이 두 입력이 되는 조합(가상 커서 레이어와 그 스틱 · 패드 버튼을 묶은 레이어)을 거절한다.
         * @return 틀린 것을 모두 알리고 하나라도 있으면 false 입니다. 없는 레이어는 켤 때 거절한다(`MinigameFlow`).
         */
        [[nodiscard]] bool validateInputLayers( const InputMap& inputMap, string_view sourceName ) const;

    private:
        /** @brief 루트 읽기입니다(`GameDataXML::loadFromAsset` · `loadFromXMLText`). */
        [[nodiscard]] bool loadRoot( const XMLCatalogRoot& root, string_view sourceName );
    };
} // namespace sw
