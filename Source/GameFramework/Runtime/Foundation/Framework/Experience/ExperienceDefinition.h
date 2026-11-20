/**
 * @file ExperienceDefinition.h
 * @brief Experience 데이터(`<팩>/experiences/<이름>.experience.xml`)입니다 — 허브에서 들어가는 미니게임 하나가 쓰는 맵 · 들어가는 방식 · 게임 모드 · 키트 · 입력 문맥 · UI · 카탈로그.
 * @details 언리얼 Lyra 의 `ULyraExperienceDefinition`(게임 모드 · 폰 데이터 · 켤 Game Feature · 액션 셋)과 같은 자리다. 키트는 모두 링크되어 있고 Experience 는
 *          그 등록만 켜고 끈다(런타임 모듈 로드 기각 — `docs/DesignBacklog.md` "허브 게임 · 미니게임 Experience"). 읽기는 루트 속성을 리플렉션 스키마로(`XMLFields::readProperties`),
 *          `<GameMode>` 원소는 손으로 읽는다(속성 이름이 게임 모드 형식의 칸이라 스키마가 하나로 정해지지 않는다).
 * @code
 *     <Experience name="kitdemo_actionplatformer" map="game/playground/maps/kitdemo_actionplatformer.scene.xml" entry="SceneSwitch"
 *                 kits="ActionPlatformer,Respawn" inputContexts="platformer" ui="game/playground/ui/platformer_hud.ui.xml" catalogs="actionplatformer">
 *       <GameMode type="ActionPlatformerDemoComponent" stage="demo-1"/>
 *     </Experience>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AssetID;
    class ExperienceKitRegistry;
    class TypeRegistry;
    class XMLCatalogRoot;
    class XMLNode;

    /** @brief Experience 에 들어가는 방식입니다. */
    ENUM()
    enum class ExperienceEntry : uint8
    {
        SubLevel,    ///< 허브 씬에 하위 레벨로 더한다(허브가 보이고 허브 틱만 멈춘다) — 하위 레벨 로더(허브 이전 ②)가 서야 들어갈 수 있다
        SceneSwitch, ///< 맵 씬으로 바꾼다(허브는 `GameStateSnapshot` 으로 찍어 두고 돌아올 때 복원 — 허브 이전 ②)
    };

    /** @brief 게임 모드 설정 한 칸입니다 — `<GameMode>` 의 `type` 밖 속성 하나(이름은 게임 모드 형식의 칸 속성 이름, `XMLFields::getAttributeName`). */
    struct ExperienceSetting
    {
        string _name;  ///< 칸 속성 이름
        string _value; ///< 적힌 글 그대로(적용할 때 그 칸의 리플렉션 읽기가 해석한다)
    };

    /** @brief Experience 의 게임 모드입니다 — 판을 돌리는 컴포넌트 형식(`GameModeComponent` 파생)과 그 칸 설정. */
    struct ExperienceGameMode
    {
        hashed_string             _type{};        ///< 리플렉션 형식 이름(`GameModeComponent` 의 만들 수 있는 파생)
        vector<ExperienceSetting> _listSetting{}; ///< 칸 설정(적은 순서)
    };

    /**
     * @brief Experience 정의 하나입니다. `loadFromAsset` · `loadFromXMLText` 가 읽고(모르는 속성 · 원소 · 깨진 값은 오류), `validate` 가 참조를 봅니다.
     * @details 읽기는 글만 보고, 참조(키트 이름 · 맵 · UI 문서 · 게임 모드 형식)는 `validate` 가 등록부 · 에셋으로 본다 — 쿠킹 · 테스트가 엔진 없이도 형식을 읽게 둘로 나눈다.
     */
    REFLECT()
    struct SW_GF_API ExperienceDefinition
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{}; ///< 파일 이름(`<이름>.experience.xml`)과 같아야 한다 — `experience.enter <이름>` 이 그 이름으로 찾는다
        PROPERTY( Required )
        DataPath _map; ///< 들어갈 맵 씬(리소스 id)
        PROPERTY( Required )
        ExperienceEntry _entry{ ExperienceEntry::SceneSwitch }; ///< 들어가는 방식
        PROPERTY( Name = "kits" )
        vector<hashed_string> _listKit{}; ///< 켤 키트 이름(`SW_EXPERIENCE_KIT` 의 이름, 적은 순서로 켜고 역순으로 끈다)
        PROPERTY( Name = "inputContexts" )
        vector<hashed_string> _listInputContext{}; ///< 켤 입력 맵 레이어(게임 입력 맵 묶음에 있어야 한다 — 켤 때 본다)
        PROPERTY( Name = "ui" )
        vector<string> _listUI{}; ///< 열 UI 문서(리소스 id). DataPath 목록은 리플렉션 글 처리기가 없어 글 목록이다 — 있는지는 `validate` 가 본다
        PROPERTY( Name = "catalogs" )
        vector<hashed_string>        _listCatalog{}; ///< 키트가 읽을 데이터 종류(`kDataKind`). 키트가 적지 않은 종류를 읽거나 적은 종류를 아무도 읽지 않으면 켜기가 진다
        optional<ExperienceGameMode> _gameMode;      ///< `<GameMode>`(없으면 게임 모드 없음 — 허브처럼 맵의 컴포넌트가 판을 돌린다)

        /** @brief 리소스 @p assetID 의 XML 을 읽습니다. 실패하면 이유를 알리고 false 이며, @p outDefinition 은 그대로입니다. */
        [[nodiscard]] static bool loadFromAsset( const AssetID& assetID, ExperienceDefinition& outDefinition );
        /** @brief `loadFromAsset` 의 글 버전입니다(테스트). @p sourceName 은 오류에 적는 이름입니다. */
        [[nodiscard]] static bool loadFromXMLText( string_view xmlText, string_view sourceName, ExperienceDefinition& outDefinition );

        /**
         * @brief 참조를 봅니다 — 키트 이름이 @p kits 에 있고 겹치지 않는지, 맵 · UI 문서가 있는지, 게임 모드 형식이 @p types 에 있는 만들 수 있는 `GameModeComponent` 파생이고 설정 이름이 그 칸인지.
         * @return 틀린 것을 모두 알리고(첫 오류에서 멈추지 않는다) 하나라도 있으면 false 입니다.
         */
        [[nodiscard]] bool validate( const ExperienceKitRegistry& kits, const TypeRegistry& types, string_view sourceName ) const;

    private:
        /** @brief 루트 읽기입니다(`GameDataXML::loadFromAsset` · `loadFromXMLText`). */
        [[nodiscard]] bool loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        /** @brief `<GameMode type="…" 칸="값" …/>` 하나를 읽습니다. 둘째 `<GameMode>` · 자식 원소 · 빈 `type` 은 오류입니다. */
        [[nodiscard]] bool loadGameMode( const XMLNode& node, string_view sourceName );
        /** @brief 게임 모드 형식과 설정 이름을 봅니다(`validate`). */
        [[nodiscard]] bool validateGameMode( const TypeRegistry& types, string_view sourceName ) const;
    };
} // namespace sw
