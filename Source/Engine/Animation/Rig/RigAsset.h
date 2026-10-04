/**
 * @file RigAsset.h
 * @brief 후처리 리그 에셋(`*.rig.json`) — 대상 목록과 **순서 있는** 노드 목록, 그리고 노드 종류 등록부(이름 → 만들기)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Rig/RigNode.h"

namespace sw
{
    /** @brief 노드 하나를 만드는 함수입니다. */
    using RigNodeFactory = unique_ptr<RigNode> ( * )();

    /**
     * @class RigNodeRegistry
     * @brief 노드 종류 이름 → 만들기 표입니다. 데이터의 `"type"` 이 여기서 고르고, 모르는 이름은 로드 오류입니다.
     * @details 엔진 노드(IK · 제약 · 스프링 · 포즈 구동 · 발 디딤)는 처음 쓸 때 등록됩니다. 게임 · 키트는 `registerNode` 로 더합니다.
     */
    class SW_API RigNodeRegistry
    {
    public:
        /** @brief 프로세스 하나의 등록부입니다(엔진 노드가 든 채로). */
        static RigNodeRegistry& getInstance();

        /** @brief 종류를 등록합니다. 같은 이름이 있으면 바꿉니다. */
        void registerNode( const hashed_string& typeName, RigNodeFactory factory );
        /** @brief 이름의 노드를 만듭니다. 모르는 이름이면 nullptr 입니다. */
        unique_ptr<RigNode> createNode( const hashed_string& typeName ) const;
        /** @brief 등록된 종류 이름들입니다(진단 · 에디터). */
        vector<hashed_string> getTypeNames() const;

    private:
        RigNodeRegistry() = default;

        unordered_map<hashed_string, RigNodeFactory> _mapFactory;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RigAsset
     * @brief 리그 하나 — 대상과 노드(원형)입니다. 노드 순서가 결과를 바꾸므로 파일 순서 그대로 평가합니다.
     * @details 형식(JSON, 모르는 키 · 종류 · 겹친 이름은 오류):
     *          @code
     *          { "planar": false,
     *            "targets": [ { "name": "Grip", "socket": "Grip", "unit": "Weapon", "space": "handslot.r" },
     *                         { "name": "Look", "object": "LookTarget" },
     *                         { "name": "Chest", "bone": "chest", "unit": "Body", "translation": [0,0,0], "rotation": [0,0,0] } ],
     *            "nodes": [ { "type": "TwoBoneIk", "name": "LeftHand", "weight": 1, "weight_curve": "HandIk", "weight_slot": "Grip",
     *                         "root": "upperarm.l", "mid": "lowerarm.l", "end": "hand.l", "target": "Grip", "match_rotation": true } ] }
     *          @endcode
     *          대상은 `bone` · `socket` · `object` 중 하나, `rotation` 은 도 단위 [피치, 요, 롤] 입니다. 노드 종류별 키는 `README.md` 표에 있습니다.
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

        /** @brief JSON 본문을 읽습니다. 틀리면 false 이고 내용은 비웁니다. */
        [[nodiscard]] bool parseJson( string_view json, string_view sourceLabel );
        /** @brief 리소스 경로(또는 절대 경로)의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 모두 비웁니다. */
        void clear();

        const vector<RigTargetDef>&        getTargets() const { return _listTarget; }
        const vector<unique_ptr<RigNode>>& getNodes() const { return _listNode; }
        /** @brief 2D(평면) 리그인지입니다. */
        bool isPlanar() const { return _bPlanar == SW_TRUE; }
        /** @brief 이름의 대상 번호입니다. 없으면 -1 입니다. */
        int32 findTargetIndex( const hashed_string& name ) const;

    private:
        [[nodiscard]] bool parseRoot( const JsonValue& root, string_view sourceLabel );
        [[nodiscard]] bool parseTarget( const JsonValue& value, string_view sourceLabel );
        [[nodiscard]] bool parseNode( const JsonValue& value, string_view sourceLabel );

        vector<RigTargetDef>        _listTarget;
        vector<unique_ptr<RigNode>> _listNode;
        uint8                       _bPlanar;
    };
} // namespace sw
