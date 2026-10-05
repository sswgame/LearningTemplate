/**
 * @file InteractionCatalog.h
 * @brief 상호작용 정의(데이터) — 종류마다 안내 문구 키 · 입력 방식(누름 · 누르고 있기 · 연타) · 단계 · 거리 · 시야각 · 시야 · 쿨다운 · 태그 조건 ·
 *        맞춤 지점(모션 워핑 마커) · 강조 · 권한, 그리고 스마트 오브젝트의 자리 목록입니다.
 * @details XML 모양(`Resource/common/data/interactions/default.interactions.xml`):
 * @code
 *     <Interactions>
 *       <Interaction id="Open" prompt="ui.interact.open" mode="Press" maxDistance="2" maxAngle="70" lineOfSight="true" cooldown="0.5"
 *                    requiredTags="" forbiddenTags="State.Stunned" alignment="Front" highlight="Outline" authority="Server"/>
 *       <Interaction id="Revive" prompt="ui.interact.revive" mode="Hold" duration="3" maxParticipants="2"/>
 *       <Interaction id="Craft" maxDistance="1.5">
 *         <Step prompt="ui.craft.gather" mode="Hold" duration="1"/>
 *         <Step prompt="ui.craft.hammer" mode="Mash" presses="6" decay="0.5"/>
 *       </Interaction>
 *       <SmartObject id="Bench"><Slot id="Left" offset="-0.5 0 0" yaw="180" tags="Activity.Sit"/></SmartObject>
 *     </Interactions>
 * @endcode
 *          단계가 없으면 `<Interaction>` 의 mode · prompt · duration · presses · decay 가 한 단계입니다. 각은 도로 적고 정의는 라디안입니다.
 *          모르는 속성 · 원소 · 열거자 이름은 경고하고 읽기는 실패입니다(`ResourceDataSchemaTest`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 단계 하나를 끝내는 입력 방식입니다. */
    ENUM()
    enum class InteractionInputMode : uint8
    {
        Press = 0, ///< 한 번 누르면 끝
        Hold,      ///< 누르고 있는 동안 차오른다(떼면 처음부터 — `InteractionProgress`)
        Mash       ///< 누를 때마다 차오르고 쉬면 줄어든다
    };

    /** @brief 고른 대상을 렌더러가 어떻게 강조할지입니다(렌더러가 나중에 읽는 깃발). */
    ENUM()
    enum class InteractionHighlight : uint8
    {
        None = 0,
        Outline, ///< 외곽선
        Sense    ///< 투시 감각 모드(벽 너머도 보인다)
    };

    /** @brief 누가 결과를 정하는가입니다. `Server` 는 게임이 건 권한 훅(`IInteractionAuthority`)이 시작을 허락해야 합니다. */
    ENUM()
    enum class InteractionAuthority : uint8
    {
        Local = 0,
        Server
    };
} // namespace sw

namespace sw
{
    /** @brief 단계 하나입니다. */
    struct InteractionStepDef
    {
        hashed_string        _prompt{};         ///< 안내 문구 키(지역화 표의 키)
        float32              _duration{ 1.0f }; ///< Hold — 혼자 차오르는 초
        float32              _decay{ 0.5f };    ///< Mash — 쉬는 동안 초당 줄어드는 진행(0..1)
        int32                _presses{ 5 };     ///< Mash — 끝내는 누름 수
        int32                _maxParticipants{ 1 };
        InteractionInputMode _mode{ InteractionInputMode::Press };
    };
} // namespace sw

namespace sw
{
    /** @brief 상호작용 종류 하나입니다. */
    struct SW_GF_API InteractionDef
    {
        hashed_string              _id{};
        vector<InteractionStepDef> _listStep{};
        TagContainer               _requiredTags{};    ///< 하는 쪽이 모두 가져야 하는 태그(열쇠 · 능력)
        TagContainer               _forbiddenTags{};   ///< 하는 쪽이 하나라도 가지면 안 되는 태그(기절)
        hashed_string              _alignmentMarker{}; ///< 하는 쪽이 맞춰 설 마커 이름(모션 워핑 — 소켓 · 마커 에셋의 이름)
        float32                    _maxDistance{ 2.0f };
        float32                    _maxAngle{ 0.0f }; ///< 시선과 대상 사이 허용 각(라디안, 0 이면 보지 않는다)
        float32                    _cooldown{ 0.0f };
        InteractionHighlight       _highlight{ InteractionHighlight::Outline };
        InteractionAuthority       _authority{ InteractionAuthority::Local };
        uint8                      _bLineOfSight{ SW_TRUE };

        /** @brief 하는 쪽의 태그가 조건을 만족하는가입니다. */
        bool allowsInteractor( const TagContainer& interactorTags ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 스마트 오브젝트의 자리 하나입니다(벤치 자리 · 엄폐 지점 · 작업대 앞). */
    struct SmartObjectSlotDef
    {
        hashed_string _id{};
        TagContainer  _tags{};      ///< 자리가 하는 일(Activity.Sit · Cover.Low)
        float3        _offset{};    ///< 오브젝트 로컬 자리
        float32       _yaw{ 0.0f }; ///< 오브젝트 로컬 방향(라디안)
    };
} // namespace sw

namespace sw
{
    /** @brief 스마트 오브젝트 하나 — 자리 목록입니다. 플레이어와 AI 가 같은 정의로 자리를 차지합니다. */
    struct SmartObjectDef
    {
        hashed_string              _id{};
        vector<SmartObjectSlotDef> _listSlot{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class InteractionCatalog
     * @brief `<Interactions>` 의 상호작용 · 스마트 오브젝트 정의입니다. `findShared` 는 경로마다 한 번 읽어 나눠 씁니다(컴포넌트들이 같은 표를 본다).
     */
    class SW_GF_API InteractionCatalog
    {
    public:
        /** @brief 컴포넌트가 따로 정하지 않으면 읽는 표입니다. */
        static constexpr const utf8* kDefaultPath = "common/data/interactions/default.interactions.xml";

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               addInteraction( const InteractionDef& def ) { (void)_interactionCatalog.add( def ); }
        void               addSmartObject( const SmartObjectDef& def ) { (void)_smartObjectCatalog.add( def ); }

        const InteractionDef*         findInteraction( const hashed_string& id ) const { return _interactionCatalog.find( id ); }
        const SmartObjectDef*         findSmartObject( const hashed_string& id ) const { return _smartObjectCatalog.find( id ); }
        const vector<InteractionDef>& getInteractions() const { return _interactionCatalog.getAll(); }
        const vector<SmartObjectDef>& getSmartObjects() const { return _smartObjectCatalog.getAll(); }

        /**
         * @brief 경로의 표를 한 번 읽어 나눠 씁니다. 읽지 못했으면 nullptr 입니다(다음 호출이 다시 읽지 않는다 — 오류는 한 번).
         * @details 여러 스레드에서 불려도 됩니다. 돌려준 표는 바뀌지 않고 GameFramework 모듈이 내릴 때까지 삽니다. 파일을 고치면 에디터 핫 리로드가
         *          캐시("InteractionCatalog", `GameDataCache`)로 새 표를 읽고 `getSharedReloadCount` 를 올립니다 — 쓰는 쪽은 그때 정의를 다시 찾습니다.
         */
        static const InteractionCatalog* findShared( string_view path );
        /** @brief `findShared` 의 표를 다시 읽은 횟수입니다. */
        static uint32 getSharedReloadCount();

    private:
        [[nodiscard]] bool loadRoot( const XmlNode& root, string_view sourceName );
        [[nodiscard]] bool readInteraction( const XmlNode& node, InteractionDef& outDef, string_view sourceName ) const;
        [[nodiscard]] bool readSmartObject( const XmlNode& node, SmartObjectDef& outDef, string_view sourceName ) const;

        GameCatalog<InteractionDef> _interactionCatalog{};
        GameCatalog<SmartObjectDef> _smartObjectCatalog{};
    };
} // namespace sw
