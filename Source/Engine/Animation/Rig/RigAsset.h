/**
 * @file RigAsset.h
 * @brief 후처리 리그 에셋(`*.rig.json`)입니다. 대상 목록과 **순서 있는** 노드 목록, 그리고 노드 종류 레지스트리(이름에서 생성 함수로)를 담습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Rig/RigNode.h"

namespace sw
{
    /** @brief 노드 하나를 만드는 함수입니다. */
    using RigNodeFactory = unique_ptr<RigNode> ( * )();

    /**
     * @class RigNodeRegistry
     * @brief 노드 종류 이름에서 생성 함수로 가는 레지스트리입니다. 데이터의 `"type"` 이 여기서 고르고, 모르는 이름은 로드 오류입니다.
     * @details 엔진 노드(IK, 제약, 스프링, 포즈 구동, 발 디딤)는 처음 쓸 때 등록된다. 게임과 키트는 `registerNode` 로 추가한다.
     */
    class SW_API RigNodeRegistry
    {
    public:
        /** @brief 프로세스 하나의 레지스트리입니다(엔진 노드가 든 채로). */
        static RigNodeRegistry& getInstance();

        /** @brief 종류를 등록합니다. 같은 이름이 있으면 바꿉니다. */
        void registerNode( const hashed_string& typeName, RigNodeFactory factory );
        /** @brief 이름의 노드를 만듭니다. 모르는 이름이면 nullptr 입니다. */
        unique_ptr<RigNode> createNode( const hashed_string& typeName ) const;
        /** @brief 등록된 종류 이름들을 등록 순서로 반환합니다(진단과 에디터용). */
        vector<hashed_string> getTypeNames() const;

    private:
        RigNodeRegistry() = default;

        NameRegistry<RigNodeFactory> _registry; ///< 종류 이름 → 생성 함수(등록 순서)
    };
} // namespace sw

namespace sw
{
    /** @brief 에디터가 리그 파일에 되써 넣는 노드 하나의 값입니다(`RigAsset::rewriteSettings`). 노드는 이름으로 찾습니다(이름은 파일 안에서 하나다). */
    struct RigNodeSaveParams
    {
        hashed_string _name{};
        float32       _weight{ 1.0f };
        bool          _bEnabled{ true };
    };
} // namespace sw

namespace sw
{
    /** @brief 에디터가 리그 파일에 되써 넣는 대상 하나의 오프셋입니다(`translation` · `rotation`, 대상의 본 · 소켓 공간). 대상은 이름으로 찾습니다. 스케일은 쓰지 않습니다. */
    struct RigTargetSaveParams
    {
        hashed_string _name{};
        float3        _translation{};
        quaternion    _rotation{};
    };
} // namespace sw

namespace sw
{
    /** @brief 리그 파일 되쓰기 한 번의 값입니다. 빈 목록은 그 종류를 건드리지 않습니다. */
    struct RigSaveParams
    {
        vector<RigNodeSaveParams>   _listNode{};
        vector<RigTargetSaveParams> _listTarget{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class RigAsset
     * @brief 리그 하나입니다. 대상과 노드(원형)를 담습니다. 노드 순서가 결과를 바꾸므로 파일 순서 그대로 평가합니다.
     * @details 형식은 JSON 이며, 모르는 키, 종류, 겹친 이름은 오류다.
     *          @code
     *          { "planar": false,
     *            "targets": [ { "name": "Grip", "socket": "Grip", "unit": "Weapon", "space": "handslot.r" },
     *                         { "name": "Look", "object": "LookTarget" },
     *                         { "name": "Chest", "bone": "chest", "unit": "Body", "translation": [0,0,0], "rotation": [0,0,0] } ],
     *            "nodes": [ { "type": "TwoBoneIK", "name": "LeftHand", "weight": 1, "weight_curve": "HandIK", "weight_slot": "Grip",
     *                         "root": "upperarm.l", "mid": "lowerarm.l", "end": "hand.l", "target": "Grip", "match_rotation": true } ] }
     *          @endcode
     *          대상은 `bone`, `socket`, `object` 중 하나를 쓰고, `rotation` 은 도 단위 [피치, 요, 롤] 이다. 노드 종류별 키는 `README.md` 의 테이블에 있다.
     */
    class SW_API RigAsset
    {
    public:
        /** @brief 리그 에셋 확장자입니다. */
        static constexpr string_view kExtension = ".rig.json";

        RigAsset();
        ~RigAsset();
        RigAsset( const RigAsset& )            = delete;
        RigAsset& operator=( const RigAsset& ) = delete;
        RigAsset( RigAsset&& ) noexcept;
        /** @brief 캐시가 제자리에서 다시 읽을 때 씁니다. 쓰는 쪽은 내용 번호가 바뀐 것을 보고 다시 바인딩합니다. */
        RigAsset& operator=( RigAsset&& ) noexcept;

        /** @brief JSON 본문을 읽습니다. 틀리면 false 이고 내용은 비웁니다. */
        [[nodiscard]] bool parseJSON( string_view json, string_view sourceLabel );
        /** @brief 리소스 경로(또는 절대 경로)의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 모두 비웁니다. */
        void clear();
        /**
         * @brief 리그 파일 글(@p jsonText)의 노드 `weight` · `enabled` 와 대상 `translation` · `rotation` 을 @p params 의 값으로 바꾼 글을 @p outJSONText 에 씁니다
         *        (리그 디버그 창의 "Save to rig" 와 대상 오프셋 기즈모).
         * @details 다른 키는 그대로 둔다 — 쓰는 쪽이 노드 · 대상 종류를 몰라도 된다. `enabled` 는 끈 노드이거나 파일에 이미 있을 때만, `translation` · `rotation` 은
         *          0 · 단위가 아니거나 파일에 이미 있을 때만 쓴다(적지 않음 = 기본). `rotation` 은 도 단위 [피치, 요, 롤]이다(읽기와 같은 배치).
         *          이름의 노드 · 대상이 없거나 결과가 리그 규칙에 맞지 않으면(다시 읽어 본다) 오류를 남기고 false 이며 @p outJSONText 는 그대로다.
         */
        [[nodiscard]] static bool rewriteSettings( string_view jsonText, string_view sourceLabel, const RigSaveParams& params, string& outJSONText );

        const vector<RigTargetDef>&        getTargets() const { return _listTarget; }
        const vector<unique_ptr<RigNode>>& getNodes() const { return _listNode; }
        /** @brief 내용 번호입니다. 읽을 때마다 프로세스 고유의 새 번호를 받습니다(핫 리로드 감지). */
        uint64 getContentID() const { return _contentID; }
        /** @brief 2D(평면) 리그인지입니다. */
        bool isPlanar() const { return _bPlanar == SW_TRUE; }
        /** @brief 이름의 대상 번호입니다. 없으면 -1 입니다. */
        int32 findTargetIndex( const hashed_string& name ) const;

    private:
        [[nodiscard]] bool parseRoot( const JSONValue& root, string_view sourceLabel );
        [[nodiscard]] bool parseTarget( const JSONValue& value, string_view sourceLabel );
        [[nodiscard]] bool parseNode( const JSONValue& value, string_view sourceLabel );

        vector<RigTargetDef>        _listTarget;
        vector<unique_ptr<RigNode>> _listNode;
        uint64                      _contentID;
        uint8                       _bPlanar;
    };
} // namespace sw
