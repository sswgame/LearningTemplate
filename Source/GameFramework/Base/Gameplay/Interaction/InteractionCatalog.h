/**
 * @file InteractionCatalog.h
 * @brief 상호작용 정의(데이터)입니다. 종류마다 안내 문구 키, 입력 방식(누름, 누르고 있기, 연타), 단계, 거리, 시야각, 시야, 쿨다운, 태그 조건,
 *        맞춤 지점(모션 워핑 마커), 강조, 권한을 정의하고, 스마트 오브젝트의 슬롯 목록도 담습니다.
 * @details XML 형식은 다음과 같다(`Resource/common/data/interactions/default.interactions.xml`).
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
 *          단계가 없으면 `<Interaction>` 의 mode, prompt, duration, presses, decay 가 한 단계가 된다. 각도는 도 단위로 적고, 정의에는 라디안으로 저장한다.
 *          알 수 없는 속성, 원소, 열거자 이름은 경고를 남기고 읽기에 실패한다(`ResourceDataSchemaTest`).
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

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 단계 하나를 끝내는 입력 방식입니다. */
    ENUM()
    enum class InteractionInputMode : uint8
    {
        Press = 0, ///< 한 번 누르면 끝
        Hold,      ///< 누르고 있는 동안 차오른다. 떼면 처음부터 다시 시작한다(`InteractionProgress`)
        Mash       ///< 누를 때마다 차오르고, 쉬면 줄어든다
    };

    /** @brief 고른 대상을 렌더러가 어떻게 강조할지 나타냅니다. 렌더러가 나중에 읽는 플래그입니다. */
    ENUM()
    enum class InteractionHighlight : uint8
    {
        None = 0,
        Outline, ///< 외곽선
        Sense    ///< 투시 감각 모드(벽 너머도 보인다)
    };

    /** @brief 누가 결과를 정하는지 나타냅니다. `Server` 는 게임이 등록한 권한 훅(`IInteractionAuthority`)이 시작을 허락해야 합니다. */
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
        hashed_string        _prompt{};         ///< 안내 문구 키(지역화 테이블의 키)
        hashed_string        _montage{};        ///< 이 단계가 시작될 때 하는 쪽의 애니메이터(`SkeletalAnimatorComponent`)가 한 번 재생하는 상태 · 클립 이름. 비면 재생하지 않는다
        float32              _duration{ 1.0f }; ///< Hold 에서 쓴다. 혼자 차오르는 데 걸리는 초
        float32              _decay{ 0.5f };    ///< Mash 에서 쓴다. 쉬는 동안 초당 줄어드는 진행량(0..1)
        int32                _presses{ 5 };     ///< Mash 에서 쓴다. 끝내는 데 필요한 누름 횟수
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
        TagContainer               _requiredTags{};    ///< 하는 쪽이 모두 가져야 하는 태그(열쇠, 능력)
        TagContainer               _forbiddenTags{};   ///< 하는 쪽이 하나라도 가지면 안 되는 태그(기절)
        hashed_string              _alignmentMarker{}; ///< 하는 쪽이 맞춰 설 마커 이름. 모션 워핑에 쓰며 소켓과 마커 에셋의 이름이다
        float32                    _maxDistance{ 2.0f };
        float32                    _maxAngle{ 0.0f }; ///< 시선과 대상 사이의 허용 각(라디안). 0 이면 검사하지 않는다
        float32                    _cooldown{ 0.0f };
        InteractionHighlight       _highlight{ InteractionHighlight::Outline };
        InteractionAuthority       _authority{ InteractionAuthority::Local };
        uint8                      _bLineOfSight{ SW_TRUE };

        /** @brief 하는 쪽의 태그가 조건을 만족하면 true 입니다. */
        bool allowsInteractor( const TagContainer& interactorTags ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 스마트 오브젝트의 슬롯 하나입니다(벤치 자리, 엄폐 지점, 작업대 앞). */
    struct SmartObjectSlotDef
    {
        hashed_string _id{};
        TagContainer  _tags{};      ///< 슬롯이 하는 일(Activity.Sit, Cover.Low)
        float3        _offset{};    ///< 오브젝트 로컬 위치
        float32       _yaw{ 0.0f }; ///< 오브젝트 로컬 방향(라디안)
    };
} // namespace sw

namespace sw
{
    /** @brief 스마트 오브젝트 하나입니다. 슬롯 목록을 담으며, 플레이어와 AI 가 같은 정의로 슬롯을 차지합니다. */
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
     * @brief `<Interactions>` 의 상호작용과 스마트 오브젝트 정의입니다. `findShared` 는 경로마다 한 번 읽어서 공유하므로, 컴포넌트들이 같은 테이블을 봅니다.
     */
    class SW_GF_API InteractionCatalog : public XMLCatalog<InteractionCatalog>
    {
        friend class XMLCatalog<InteractionCatalog>;

    public:
        /** @brief 컴포넌트가 따로 정하지 않으면 읽는 테이블입니다. */
        static constexpr const utf8* kDefaultPath = "common/data/interactions/default.interactions.xml";

        void addInteraction( const InteractionDef& def ) { (void)_interactionCatalog.add( def ); }
        void addSmartObject( const SmartObjectDef& def ) { (void)_smartObjectCatalog.add( def ); }

        const InteractionDef*         findInteraction( const hashed_string& id ) const { return _interactionCatalog.find( id ); }
        const SmartObjectDef*         findSmartObject( const hashed_string& id ) const { return _smartObjectCatalog.find( id ); }
        const vector<InteractionDef>& getInteractions() const { return _interactionCatalog.getAll(); }
        const vector<SmartObjectDef>& getSmartObjects() const { return _smartObjectCatalog.getAll(); }

        /**
         * @brief 경로의 테이블을 한 번 읽어서 공유합니다. 읽지 못했으면 nullptr 입니다. 다음 호출이 다시 읽지 않으므로 오류도 한 번만 남습니다.
         * @details 여러 스레드에서 불러도 된다. 반환한 테이블은 바뀌지 않고 GameFramework 모듈을 언로드할 때까지 산다. 파일을 고치면 에디터 핫 리로드가
         *          캐시("InteractionCatalog", `GameDataCache`)로 새 테이블을 읽고 `getSharedReloadCount` 를 늘린다. 쓰는 쪽은 그때 정의를 다시 찾는다.
         */
        static const InteractionCatalog* findShared( string_view path );
        /** @brief `findShared` 의 테이블을 다시 읽은 횟수입니다. */
        static uint32 getSharedReloadCount();

    private:
        static constexpr const utf8* kXMLRootName = "Interactions"; ///< 루트 원소(`XMLCatalog`)

        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool readInteraction( const XMLNode& node, InteractionDef& outDef, string_view sourceName ) const;
        [[nodiscard]] bool readSmartObject( const XMLNode& node, SmartObjectDef& outDef, string_view sourceName ) const;

        GameCatalog<InteractionDef> _interactionCatalog{};
        GameCatalog<SmartObjectDef> _smartObjectCatalog{};
    };
} // namespace sw
