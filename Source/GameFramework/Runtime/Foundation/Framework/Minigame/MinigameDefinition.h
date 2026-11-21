/**
 * @file MinigameDefinition.h
 * @brief 미니게임 데이터(`<팩>/minigames/<이름>.minigame.xml`)입니다 — 미니게임 하나가 쓰는 맵 · 들어가는 방식 · 게임 기능 · 입력 문맥 · UI · 카탈로그.
 * @details 언리얼 Lyra 의 `ULyraExperienceDefinition`(켤 Game Feature · 액션 셋), 용과 같이 · GTA · 마리오 파티의 미니게임 하나와 같은 자리다. 키트는 모두 링크되어 있고 미니게임은
 *          그 게임 기능 등록만 켜고 끈다(런타임 모듈 로드 기각 — `docs/agents/Gameplay.md` "게임 기능 · 미니게임"). 판을 돌리는 컴포넌트(`GameModeComponent` 파생)는 맵에 둔다 —
 *          정의에 게임 모드 칸을 두지 않는다(어디에도 적용되지 않던 `<GameMode>` 원소를 지웠다). 읽기는 루트 속성을 리플렉션 스키마로(`XMLFields::readProperties`), 자식 원소는 없다.
 * @code
 *     <Minigame name="kitdemo_actionplatformer" map="game/playground/maps/kitdemo_actionplatformer.scene.xml" entry="SceneSwitch"
 *               features="ActionPlatformer" inputContexts="platformer" ui="game/playground/ui/platformer_hud.ui.xml" catalogs="actionplatformer"/>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AssetID;
    class GameFeatureRegistry;
    class XMLCatalogRoot;

    /** @brief 미니게임에 들어가는 방식입니다. */
    ENUM()
    enum class MinigameEntry : uint8
    {
        SubLevel,    ///< 들어갈 때의 활성 씬에, 진입을 건 사물의 변환으로 하위 레벨을 더한다(바탕 씬이 보이고 그대로 돈다 — 언리얼 레벨 인스턴스 액터 · 유니티 Additive 씬, `MinigameFlow`)
        SceneSwitch, ///< 맵 씬으로 바꾼다(들어갈 때의 활성 씬을 `GameStateSnapshot` 으로 찍어 두고 나올 때 복원 — `MinigameFlow`)
    };

    /**
     * @brief 미니게임 정의 하나입니다. `loadFromAsset` · `loadFromXMLText` 가 읽고(모르는 속성 · 원소 · 깨진 값은 오류), `validate` 가 참조를 봅니다.
     * @details 읽기는 글만 보고, 참조(게임 기능 이름 · 맵 · UI 문서)는 `validate` 가 등록부 · 에셋으로 본다 — 쿠킹 · 테스트가 엔진 없이도 형식을 읽게 둘로 나눈다.
     */
    REFLECT()
    struct SW_GF_API MinigameDefinition
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{}; ///< 파일 이름(`<이름>.minigame.xml`)과 같아야 한다 — `minigame.enter <이름>` 이 그 이름으로 찾는다
        PROPERTY( Required )
        DataPath _map; ///< 들어갈 맵 씬(리소스 id)
        PROPERTY( Required )
        MinigameEntry _entry{ MinigameEntry::SceneSwitch }; ///< 들어가는 방식
        PROPERTY( Name = "features" )
        vector<hashed_string> _listFeature{}; ///< 켤 게임 기능 이름(`SW_GAME_FEATURE` 의 이름, 적은 순서로 켜고 역순으로 끈다)
        PROPERTY( Name = "inputContexts" )
        vector<hashed_string> _listInputContext{}; ///< 켤 입력 맵 레이어(게임 입력 맵 묶음에 있어야 한다 — 켤 때 본다)
        PROPERTY( Name = "ui" )
        vector<string> _listUI{}; ///< 열 UI 문서(리소스 id). DataPath 목록은 리플렉션 글 처리기가 없어 글 목록이다 — 있는지는 `validate` 가 본다
        PROPERTY( Name = "catalogs" )
        vector<hashed_string> _listCatalog{}; ///< 기능이 읽을 데이터 종류(`kDataKind`). 기능이 적지 않은 종류를 읽거나 적은 종류를 아무도 읽지 않으면 켜기가 진다

        /** @brief 리소스 @p assetID 의 XML 을 읽습니다. 실패하면 이유를 알리고 false 이며, @p outDefinition 은 그대로입니다. */
        [[nodiscard]] static bool loadFromAsset( const AssetID& assetID, MinigameDefinition& outDefinition );
        /** @brief `loadFromAsset` 의 글 버전입니다(테스트). @p sourceName 은 오류에 적는 이름입니다. */
        [[nodiscard]] static bool loadFromXMLText( string_view xmlText, string_view sourceName, MinigameDefinition& outDefinition );

        /**
         * @brief 참조를 봅니다 — 게임 기능 이름이 @p features 에 있고 겹치지 않는지, 입력 문맥 · 카탈로그가 겹치지 않는지, 맵 · UI 문서가 있는지.
         * @return 틀린 것을 모두 알리고(첫 오류에서 멈추지 않는다) 하나라도 있으면 false 입니다.
         */
        [[nodiscard]] bool validate( const GameFeatureRegistry& features, string_view sourceName ) const;

    private:
        /** @brief 루트 읽기입니다(`GameDataXML::loadFromAsset` · `loadFromXMLText`). */
        [[nodiscard]] bool loadRoot( const XMLCatalogRoot& root, string_view sourceName );
    };
} // namespace sw
