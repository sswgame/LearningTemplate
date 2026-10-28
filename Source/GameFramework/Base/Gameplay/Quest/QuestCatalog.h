/**
 * @file QuestCatalog.h
 * @brief 퀘스트 정의입니다. 선행 퀘스트와 레벨, 단계(목표, 다음 단계, 선택지 분기, 완료, 실패), 보상을 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/StatBlock.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;
} // namespace sw

namespace sw
{
    /**
     * @brief `<Quest>` 속성 중 기본값이 있는 필드의 스키마입니다(`GameDataXML::readFields`). 속성 이름은 필드 이름에서 `_` 를 뺀 것입니다.
     * @details `id`, `name`, `requires` 는 읽는 코드가 따로 읽는다(이름과 이름 목록). 적지 않은 필드는 여기 초기값입니다.
     */
    REFLECT()
    struct QuestRowDef
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _level{ 0 }; ///< 받을 수 있는 최소 레벨
        PROPERTY()
        bool _bRepeatable{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief `<Stage>` 속성 중 기본값이 있는 필드의 스키마입니다(`id` 와 `next` 는 이름으로 따로 읽는다). 범위 밖 값은 로드 오류입니다. */
    REFLECT()
    struct QuestStageRowDef
    {
        REFLECT_BODY();

        PROPERTY()
        string _text{};
        PROPERTY( Min = 0, Units = s )
        float32 _time{ 0.0f }; ///< 0 이면 제한이 없다
        PROPERTY()
        bool _bComplete{ false };
        PROPERTY()
        bool _bFail{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief `<Objective>` 속성 중 기본값이 있는 필드의 스키마입니다(`kind` 와 `target` 은 이름으로 따로 읽는다). */
    REFLECT()
    struct QuestObjectiveRowDef
    {
        REFLECT_BODY();

        PROPERTY()
        string _text{};
        PROPERTY( Min = 1 )
        int32 _count{ 1 };
        PROPERTY()
        bool _bOptional{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 보상의 `<Item>` 속성 스키마입니다. */
    REFLECT()
    struct QuestRewardItemRowDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _item{};
        PROPERTY( Min = 1 )
        int32 _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 목표 하나입니다. "무엇을(종류) 누구에게(대상) 몇 번" 하는지 담습니다. 종류와 대상은 문자열이므로 게임이 정합니다("Kill", "Collect", "Talk", "Reach"). */
    struct QuestObjective
    {
        hashed_string _kind{};
        hashed_string _target{};
        string        _text{};
        int32         _count{ 1 };
        uint8         _bOptional{ SW_FALSE }; ///< 끝내지 않아도 단계가 넘어간다. 보너스 보상이나 다른 결말의 조건에 쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 선택지 하나입니다. 고르면 그 단계로 넘어갑니다. */
    struct QuestBranch
    {
        hashed_string _choice{};
        hashed_string _nextStage{};
    };
} // namespace sw

namespace sw
{
    /** @brief 보상입니다. 아이템은 값 목록(`ItemStackList`)이고, 그 밖의 보상(경험치, 돈, 평판)은 이름과 수치의 쌍입니다. */
    struct QuestReward
    {
        ItemStackList _items{};
        StatBlock     _values{}; ///< `xp="100" gold="50" rep.town="10"` 형식이다. 이름의 뜻은 게임이 해석한다
    };
} // namespace sw

namespace sw
{
    /** @brief 단계 하나입니다. */
    struct QuestStage
    {
        hashed_string          _id{};
        hashed_string          _nextStage{}; ///< 목표를 다 하면 넘어갈 단계. 비어 있으면 완료나 실패 단계이거나 선택지 단계이다
        string                 _text{};      ///< 일지에 표시할 텍스트
        vector<QuestObjective> _listObjective{};
        vector<QuestBranch>    _listBranch{};      ///< 있으면 목표를 다 한 뒤 선택을 기다린다
        QuestReward            _reward{};          ///< 이 단계에 들어설 때 준다. 완료 단계이면 퀘스트 보상이다
        float32                _timeLimit{ 0.0f }; ///< 초 단위이며, 넘으면 실패한다. 0 이면 제한이 없다
        uint8                  _bComplete{ SW_FALSE };
        uint8                  _bFail{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 퀘스트 하나입니다. 첫 단계에서 시작합니다. */
    struct SW_GF_API QuestDef
    {
        hashed_string         _id{};
        string                _name{};
        vector<hashed_string> _listRequiredQuest{}; ///< 끝낸(완료한) 퀘스트
        vector<QuestStage>    _listStage{};
        int32                 _requiredLevel{ 0 };
        uint8                 _bRepeatable{ SW_FALSE };

        const QuestStage* findStage( const hashed_string& stageID ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class QuestCatalog
     * @brief XML 형식입니다.
     * @code
     *     <QuestCatalog>
     *       <Quest id="wolves" name="Wolf Trouble" level="2" requires="intro" repeatable="false">
     *         <Stage id="hunt" next="report" text="Kill wolves" time="0">
     *           <Objective kind="Kill" target="wolf" count="5" text="Wolves slain"/>
     *           <Objective kind="Collect" target="pelt" count="3" optional="true"/>
     *         </Stage>
     *         <Stage id="report"><Objective kind="Talk" target="elder"/><Branch choice="spare" next="mercy"/><Branch choice="kill" next="blood"/></Stage>
     *         <Stage id="mercy" complete="true"><Reward xp="100" rep.town="10"><Item item="ring" count="1"/></Reward></Stage>
     *         <Stage id="blood" complete="true"><Reward xp="150"/></Stage>
     *       </Quest>
     *     </QuestCatalog>
     * @endcode
     */
    class SW_GF_API QuestCatalog : public GameCatalog<QuestDef, XMLCatalog<QuestCatalog>>
    {
        friend class XMLCatalog<QuestCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "quests";

    private:
        static constexpr const utf8* kXMLRootName = "QuestCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw
