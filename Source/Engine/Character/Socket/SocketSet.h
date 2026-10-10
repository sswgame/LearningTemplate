/**
 * @file SocketSet.h
 * @brief 소켓 에셋(`*.sockets.xml`) — 본 · 메시와 따로 둔 원본 데이터입니다. 소켓 종류 표(`SocketKindTable`)와 가상 본도 여기 있습니다.
 * @details 임포트 산출물(`.mesh` · 스켈레톤)에 소켓을 넣으면 재임포트가 지우므로, 소켓은 사람이 고치는 별도 파일이고 임포트가 덮어쓰지 않습니다
 *          (`SocketImportUtil` 은 파일이 없을 때만 처음 한 번 씁니다). 층은 스켈레톤 몫 → 메시(부품) 몫 → 외형 몫이고, 위층이 같은 이름의 항목을
 *          **적은 칸만** 덮어씁니다(`applyOverride`). 2D 도 같은 에셋입니다 — 본이 Z 축으로만 돌 뿐입니다.
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

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct CharacterBoneArray;

    class CharacterDataReader;
    class XMLNode;

    /**
     * @brief 소켓 종류 표입니다(부착 소켓 · 접지점 · 히트박스 중심 · 락온 지점 …). 종류는 코드가 아니라 데이터 한 줄입니다.
     * @details XML: `<SocketKinds><Kind name="Attach"/><Kind name="GroundPoint"/></SocketKinds>`. 엔진 기본은 `engine/character/default.socketkinds.xml`.
     */
    class SW_API SocketKindTable
    {
    public:
        /** @brief 엔진 기본 종류 표의 리소스 경로입니다. */
        static constexpr string_view kDefaultPath = "engine/character/default.socketkinds.xml";

        /** @brief XML 텍스트에서 읽습니다(지금 표에 더합니다). 모르는 속성 · 원소 · 겹친 이름은 오류이고 false 입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 종류 하나를 더합니다(이미 있으면 그대로). */
        void addKind( const hashed_string& name );
        /** @brief 그 종류가 표에 있으면 true 입니다. */
        bool hasKind( const hashed_string& name ) const;
        /** @brief 표의 종류들입니다. */
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
        Bone = 0, ///< 부모 본(또는 뿌리)을 따른다 — 본 비율 보정(`BoneProportion`)이 그대로 옮긴다
        Surface,  ///< 바인드 때 가장 가까운 표면 점에 묶이고 체형 모프가 표면을 옮기면 같이 옮긴다
    };

    /** @brief 덮어쓰기 층에서 "이 칸을 적었다" 를 나타내는 비트입니다. */
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
     * @brief 소켓 항목 하나입니다. 부모가 비면 메시(유닛) 뿌리입니다.
     * @details 후보 목록(`_listFallback`)이 있으면 해석 때 그 이름들 중 처음 있는 소켓으로 가고(벨트의 걸이 → 없으면 자기 자리), 없으면 자기 자리입니다.
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
    /** @brief 두 본 사이를 따르는 가상 본입니다(IK · 조준 기준 — 언리얼 Virtual Bone). 0 이면 `_from`, 1 이면 `_to` 입니다. */
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
    /** @brief 소켓 에셋 하나 — 소켓 항목과 가상 본입니다. */
    class SW_API SocketSet
    {
    public:
        /**
         * @brief XML 텍스트에서 읽습니다(지금 내용을 비우고). 모르는 속성 · 원소 · 종류(@p kinds 에 없음) · 겹친 이름은 오류입니다.
         * @param pBones 있으면 부모 본 이름도 대조합니다(없는 본은 오류).
         */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, const SocketKindTable& kinds, const CharacterBoneArray* pBones = nullptr );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path, const SocketKindTable& kinds, const CharacterBoneArray* pBones = nullptr );
        /** @brief XML 텍스트로 씁니다(적은 칸만). */
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
         * @brief 위층(@p upper)을 이름으로 덮어씁니다 — 같은 이름은 위층이 **적은 칸만** 바꾸고, 새 이름은 더합니다. 가상 본은 통째로 바꿉니다.
         * @details 스켈레톤 몫에 메시 몫, 그 위에 외형 몫을 차례로 부릅니다.
         */
        void applyOverride( const SocketSet& upper );
        /**
         * @brief 부모 이름(본 · 이 에셋의 가상 본)과 가상 본의 두 본이 @p bones 에 있는지 봅니다. 없으면 오류 글을 @p pOutError 에 더하고 false 입니다.
         */
        bool validateBones( const CharacterBoneArray& bones, string* pOutError ) const;
        /**
         * @brief 항목의 유닛 공간 변환을 본 변환에서 계산합니다(부모 본 · 가상 본 · 뿌리).
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
