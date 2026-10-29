/**
 * @file RigAsset.h
 * @brief 후처리 리그 에셋(`*.rig.xml`)입니다. 대상 목록과 **순서 있는** 노드 목록, 그리고 노드 종류 레지스트리(이름에서 생성 함수로)를 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/FileSystem/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"

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
    /**
     * @class RigAsset
     * @brief 리그 하나입니다. 대상과 노드(원형)를 담습니다. 노드 순서가 결과를 바꾸므로 파일 순서 그대로 평가합니다.
     * @details 형식은 XML 이며, 모르는 속성 · 자식, 종류, 겹친 이름은 오류다. 값은 속성이고, 벡터는 `x,y,z`, 이름 목록과 객체 목록은 칸 이름의 요소 안에 `<item>` 으로 늘어놓는다.
     *          @code
     *          <RigAsset planar="false">
     *            <targets>
     *              <item name="Grip" socket="Grip" unit="Weapon" space="handslot.r" />
     *              <item name="Look" object="LookTarget" />
     *              <item name="Chest" bone="chest" unit="Body" translation="0,0,0" rotation="0,0,0" />
     *            </targets>
     *            <nodes>
     *              <item type="TwoBoneIK" name="LeftHand" weight="1" weight_curve="HandIK" weight_slot="Grip"
     *                    root="upperarm.l" mid="lowerarm.l" end="hand.l" target="Grip" match_rotation="true" />
     *            </nodes>
     *          </RigAsset>
     *          @endcode
     *          대상은 `bone`, `socket`, `object` 중 하나를 쓰고, `rotation` 은 도 단위 [피치, 요, 롤] 이다. 노드 종류별 키는 `README.md` 의 테이블에 있다.
     */
    class SW_API RigAsset
    {
    public:
        /** @brief 리그 에셋 확장자입니다. */
        static constexpr string_view kExtension = ".rig.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `RigAsset.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "RigAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        RigAsset();
        ~RigAsset();
        RigAsset( const RigAsset& )            = delete;
        RigAsset& operator=( const RigAsset& ) = delete;
        RigAsset( RigAsset&& ) noexcept;
        /** @brief 캐시가 제자리에서 다시 읽을 때 씁니다. 쓰는 쪽은 내용 번호가 바뀐 것을 보고 다시 바인딩합니다. */
        RigAsset& operator=( RigAsset&& ) noexcept;

        /** @brief XML 본문을 읽습니다. 틀리면 false 이고 내용은 비웁니다. */
        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        /** @brief 리소스 경로(또는 절대 경로)의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 모두 비웁니다. */
        void clear();

        const vector<RigTargetDef>&        getTargets() const { return _listTarget; }
        const vector<unique_ptr<RigNode>>& getNodes() const { return _listNode; }
        /** @brief 내용 번호입니다. 읽을 때마다 프로세스 고유의 새 번호를 받습니다(핫 리로드 감지). */
        uint64 getContentID() const { return _contentID; }
        /** @brief 2D(평면) 리그인지입니다. */
        bool isPlanar() const { return _bPlanar == SW_TRUE; }
        /** @brief 이름의 대상 번호입니다. 없으면 -1 입니다. */
        int32 findTargetIndex( const hashed_string& name ) const;

    private:
        [[nodiscard]] bool parseRoot( const XMLNode& root, string_view sourceLabel );
        [[nodiscard]] bool parseTarget( const XMLNode& value, string_view sourceLabel );
        [[nodiscard]] bool parseNode( const XMLNode& value, string_view sourceLabel );

        vector<RigTargetDef>        _listTarget;
        vector<unique_ptr<RigNode>> _listNode;
        uint64                      _contentID;
        uint8                       _bPlanar;
    };
} // namespace sw
