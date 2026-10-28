/**
 * @file SocketSet.h
 * @brief 소켓 에셋(`*.sockets.xml`)입니다. 본과 메시와 따로 둔 원본 데이터이며, 소켓 종류 테이블(`SocketKindTable`)과 가상 본도 여기에 있습니다.
 * @details 임포트 산출물(`.mesh`, 스켈레톤)에 소켓을 넣으면 재임포트가 지우므로, 소켓은 사람이 고치는 별도 파일이고 임포트가 덮어쓰지 않는다
 *          (`SocketImportUtil` 은 파일이 없을 때만 처음 한 번 쓴다). 층은 스켈레톤, 메시(부품), 외형 순서이고, 위층이 같은 이름의 항목을
 *          **적은 필드만** 덮어쓴다(`applyOverride`). 2D 도 같은 에셋이며, 본이 Z 축으로만 돌 뿐이다.
 *
 *          XML 예:
 *          @code
 *          <SocketSet>
 *            <VirtualBone name="AimRef" from="hand_r" to="hand_l" weight="0.5"/>
 *            <Socket name="Muzzle" parent="barrel" kind="Attach" translation="0 0 0.42" rotation="0 0 0" preview="engine/models/cube.mesh"/>
 *            <Socket name="Scabbard" parent="spine_01" kind="Attach" translation="0.15 0 0" fallback="Belt.Hook"/>
 *            <Socket name="Belly" parent="spine_01" kind="HitboxCenter" anchor="Surface" translation="0 0 0.12"/>
 *          </SocketSet>
 *          @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/CharacterIssue.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Resource/ResourceID.h"

namespace sw
{
    struct CharacterBoneArray;

    class CharacterDataReader;
    class XMLNode;

    /**
     * @brief 소켓 종류 테이블입니다(부착 소켓, 접지점, 히트박스 중심, 락온 지점 등). 종류는 코드가 아니라 데이터 항목 하나입니다.
     * @details XML 형식은 `<SocketKinds><Kind name="Attach"/><Kind name="GroundPoint"/></SocketKinds>` 이다. 엔진 기본은 `engine/character/default.socketkinds.xml` 이다.
     */
    class SW_API SocketKindTable
    {
    public:
        /** @brief XML 텍스트에서 읽어 현재 테이블에 추가합니다. 모르는 속성이나 원소, 겹친 이름은 오류이며 false 를 반환합니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( const ResourceID& resourceID );
        /**
         * @brief 엔진 기본 종류 테이블을 읽습니다. 경로의 원본은 엔진 기본 에셋 표(`EngineDefaultAssets::_defaultSocketKinds`)입니다.
         * @details 표가 없으면 대신 쓸 경로가 없어 서브시스템 조회가 멈춘다. 읽지 못했으면 false 이고 오류는 로그에 남는다.
         */
        [[nodiscard]] bool loadDefault();
        /** @brief 종류 하나를 더합니다(이미 있으면 그대로). */
        void addKind( const hashed_string& name );
        /** @brief 그 종류가 테이블에 있으면 true 를 반환합니다. */
        bool hasKind( const hashed_string& name ) const;
        /** @brief 테이블의 종류들을 반환합니다. */
        const vector<hashed_string>& getKinds() const { return _listKind; }

    private:
        void readRoot( const XMLNode& root, CharacterDataReader& reader );

    private:
        vector<hashed_string> _listKind;
    };
} // namespace sw

namespace sw
{
    /** @brief 소켓이 무엇을 따라 움직이는가입니다. */
    ENUM()
    enum class SocketAnchor : uint8
    {
        Bone = 0, ///< 부모 본(또는 루트)을 따른다. 본 비율 보정(`BoneProportion`)이 그대로 옮긴다.
        Surface,  ///< 바인드할 때 가장 가까운 표면 점에 묶이고, 체형 모프가 표면을 옮기면 같이 옮겨진다.
    };

    /** @brief 덮어쓰기 층에서 "이 필드를 적었다"를 나타내는 비트입니다. */
    struct SocketFieldBit
    {
        static constexpr uint16 kParent      = 1u << 0;
        static constexpr uint16 kKind        = 1u << 1;
        static constexpr uint16 kTranslation = 1u << 2;
        static constexpr uint16 kRotation    = 1u << 3;
        static constexpr uint16 kScale       = 1u << 4;
        static constexpr uint16 kPreview     = 1u << 5;
        static constexpr uint16 kFallback    = 1u << 6;
        static constexpr uint16 kAnchor      = 1u << 7;
        static constexpr uint16 kAll         = 0xFFu;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 소켓 항목 하나입니다. 부모가 비면 메시(유닛) 루트입니다.
     * @details 후보 목록(`_listFallback`)이 있으면 해석할 때 그 이름들 중 처음 있는 소켓으로 간다(벨트의 걸이가 없으면 자기 위치로 간다). 후보가 없으면 자기 위치를 쓴다.
     */
    struct SW_API SocketDef
    {
        hashed_string         _name{};
        hashed_string         _parent{};
        hashed_string         _kind{};
        string                _previewMesh{};
        vector<hashed_string> _listFallback{};
        quaternion            _rotation{};
        float3                _translation{};
        float3                _scale{ 1.0f };
        uint16                _fieldMask{ SocketFieldBit::kAll };
        SocketAnchor          _anchor{ SocketAnchor::Bone };

        /** @brief 부모 기준 로컬 변환입니다. */
        float4x4 makeLocalTransform() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 두 본 사이를 따라가는 가상 본입니다(IK와 조준 기준이며 언리얼의 Virtual Bone 에 해당합니다). 0 이면 `_from`, 1 이면 `_to` 입니다. */
    struct SW_API VirtualBoneDef
    {
        hashed_string _name{};
        hashed_string _from{};
        hashed_string _to{};
        float32       _weight{ 0.5f };
    };
} // namespace sw

namespace sw
{
    /** @brief 소켓 에셋 하나입니다. 소켓 항목과 가상 본을 담습니다. */
    class SW_API SocketSet
    {
    public:
        /** @brief 소켓 에셋 파일의 확장자입니다(편집 창이 문서를 고르는 판정). */
        static constexpr const utf8* kExtension = ".sockets.xml";

        /**
         * @brief XML 텍스트에서 읽습니다(현재 내용은 비웁니다). 모르는 속성이나 원소, 종류(@p kinds 에 없음), 겹친 이름은 오류입니다.
         * @param pBones 있으면 부모 본 이름도 대조합니다(없는 본은 오류입니다).
         */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, const SocketKindTable& kinds, const CharacterBoneArray* pBones = nullptr );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( const ResourceID& resourceID, const SocketKindTable& kinds, const CharacterBoneArray* pBones = nullptr );
        /** @brief XML 텍스트로 씁니다(적은 필드만 씁니다). */
        string saveToXMLText() const;

        /** @brief 항목을 더합니다. 같은 이름이 있으면 바꿉니다. */
        void addSocket( const SocketDef& socket );
        /** @brief 가상 본을 더합니다. 같은 이름이 있으면 바꿉니다. */
        void addVirtualBone( const VirtualBoneDef& virtualBone );
        /** @brief 이름의 항목입니다. 없으면 nullptr 입니다. */
        const SocketDef* findSocket( const hashed_string& name ) const;
        /** @brief 이름의 가상 본입니다. 없으면 nullptr 입니다. */
        const VirtualBoneDef* findVirtualBone( const hashed_string& name ) const;
        /** @brief 항목들입니다. */
        const vector<SocketDef>& getSockets() const { return _listSocket; }
        /** @brief 가상 본들입니다. */
        const vector<VirtualBoneDef>& getVirtualBones() const { return _listVirtualBone; }
        /** @brief 모두 비웁니다. */
        void clear();

        /**
         * @brief 위층(@p upper)을 이름으로 덮어씁니다. 같은 이름은 위층이 **적은 필드만** 바꾸고, 새 이름은 추가합니다. 가상 본은 통째로 바꿉니다.
         * @details 스켈레톤 층에 메시 층, 그 위에 외형 층을 차례로 호출한다.
         */
        void applyOverride( const SocketSet& upper );
        /**
         * @brief 부모 이름(본 또는 이 에셋의 가상 본)과 가상 본의 두 본이 @p bones 에 있는지 검사합니다. 없으면 문제(종류 id 와 글)를 @p pOutIssues 에 추가하고 false 를 반환합니다.
         */
        bool validateBones( const CharacterBoneArray& bones, CharacterIssueList* pOutIssues ) const;
        /**
         * @brief 항목의 유닛 공간 변환을 본 변환에서 계산합니다(부모 본, 가상 본, 루트).
         * @return 부모를 찾지 못하면 false 입니다.
         */
        bool computeSocketTransform( const SocketDef& socket, const CharacterBoneArray& bones, float4x4& outUnitTransform ) const;
        /** @brief 가상 본의 유닛 공간 변환입니다. 두 본 중 하나라도 없으면 false 입니다. */
        static bool computeVirtualBoneTransform( const VirtualBoneDef& virtualBone, const CharacterBoneArray& bones, float4x4& outUnitTransform );

    private:
        void readRoot( const XMLNode& root, const SocketKindTable& kinds, CharacterDataReader& reader );
        void readSocket( const XMLNode& node, const SocketKindTable& kinds, CharacterDataReader& reader );
        void readVirtualBone( const XMLNode& node, CharacterDataReader& reader );

    private:
        vector<SocketDef>      _listSocket;
        vector<VirtualBoneDef> _listVirtualBone;
    };
} // namespace sw
