/**
 * @file VrmAvatarImporter.h
 * @brief VRM 의 아바타 정보(humanoid 본 표 · 표정 · 스프링 본 · 1인칭)를 읽어 엔진 이름(본 · 모프 타깃)으로 풀고, 모델 옆 폴더의 `<이름>.avatar.json` 으로 씁니다.
 * @details 0.x(`extensions.VRM` 의 humanoid · blendShapeMaster · secondaryAnimation · firstPerson)와 1.0(`VRMC_vrm` · `VRMC_springBone`)을 한 모양으로 모은다.
 *          이름은 1.0 의 것을 쓴다(0.x 의 표정 프리셋 joy 는 happy, 엄지 Proximal 은 Metacarpal). 본은 glTF 노드 번호가 아니라 스켈레톤 본 이름이고,
 *          표정은 메시 · 타깃 번호가 아니라 엔진 메시의 모프 타깃 이름이다(`ModelImportUtil::makeMorphTargetName` 과 같은 규칙).
 *          런타임은 이 파일 대신 임포트가 옮긴 엔진 형식을 읽는다 — 표정은 얼굴 리그, 스프링 본은 후처리 리그(`VrmRuntimeAssets`). humanoid 본 표와 1인칭은
 *          이 파일에만 있다(1인칭 메시 숨김은 엔진 메시에 머티리얼 구간이 생겨야 한다 — 본 메시는 구간을 합친 것이다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class JSONValue;
} // namespace sw

namespace sw::editor
{
    /** @brief 표정이 다른 표정(눈 깜박임 · 시선 · 입)을 어떻게 누르는지입니다. VRM 1.0 의 overrideBlink · overrideLookAt · overrideMouth 입니다. */
    enum class VrmExpressionOverride : uint8
    {
        None,
        Block, ///< 이 표정이 켜지면 그쪽을 끈다
        Blend, ///< 이 표정의 가중치만큼 그쪽을 줄인다
    };

    /** @brief 1인칭 카메라에서 메시를 보일지입니다. VRM 의 firstPersonFlag(0.x) · meshAnnotations.type(1.0) 입니다. */
    enum class VrmFirstPersonMode : uint8
    {
        Auto,            ///< 머리에 묶인 부분만 1인칭에서 숨긴다
        Both,            ///< 두 시점 모두 보인다
        ThirdPersonOnly, ///< 3인칭에서만 보인다(머리카락, 얼굴)
        FirstPersonOnly, ///< 1인칭에서만 보인다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief humanoid 본 하나입니다. VRM 1.0 의 본 이름(`hips`, `leftUpperArm`)에서 스켈레톤 본 이름으로의 대응입니다. */
    struct VrmHumanBone
    {
        string _humanBone;
        string _bone;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 표정이 움직이는 모프 타깃 하나와 가중치(0..1)입니다. */
    struct VrmExpressionBind
    {
        string  _morphTarget;
        float32 _weight{ 1.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 표정 하나입니다. 프리셋이면 이름이 1.0 프리셋 이름(`happy`, `aa`, `blinkLeft`)이고, 아니면 원본의 이름입니다. */
    struct VrmExpression
    {
        string                    _name;
        vector<VrmExpressionBind> _listBind;
        VrmExpressionOverride     _overrideBlink{ VrmExpressionOverride::None };
        VrmExpressionOverride     _overrideLookAt{ VrmExpressionOverride::None };
        VrmExpressionOverride     _overrideMouth{ VrmExpressionOverride::None };
        uint8                     _bPreset{ SW_FALSE };
        uint8                     _bBinary{ SW_FALSE }; ///< 가중치를 0.5 에서 0 · 1 로 자른다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 스프링 본 충돌체 하나입니다(구, 또는 offset 에서 tail 까지의 캡슐). 위치는 본 기준이며 엔진 공간입니다. */
    struct VrmSpringCollider
    {
        string  _bone;
        float3  _offset{};
        float3  _tail{};
        float32 _radius{ 0.0f };
        uint8   _bCapsule{ SW_FALSE };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 스프링 하나가 함께 피하는 충돌체 묶음입니다. `_listCollider` 는 `VrmAvatarDef::_listCollider` 의 번호입니다. */
    struct VrmSpringColliderGroup
    {
        string         _name;
        vector<uint32> _listCollider;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 스프링의 관절 하나와 그 물리 값입니다(VRM 1.0 joints 의 값). 중력 방향은 엔진 공간입니다. */
    struct VrmSpringJoint
    {
        string  _bone;
        float3  _gravityDirection{ 0.0f, -1.0f, 0.0f };
        float32 _hitRadius{ 0.0f };
        float32 _stiffness{ 1.0f };
        float32 _gravityPower{ 0.0f };
        float32 _dragForce{ 0.5f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 흔들리는 사슬 하나입니다. 관절은 뿌리에서 끝으로 놓입니다.
     * @details 0.x 의 boneGroup 은 뿌리 본만 적고 그 아래 모든 자손이 흔들린다. 그래서 뿌리마다 사슬 하나를 만들고 자손을 깊이 우선 순서로 넣는다(각 관절의 부모는 스켈레톤이 안다).
     */
    struct VrmSpring
    {
        string                 _name;
        string                 _centerBone; ///< 비면 월드 기준이다
        vector<VrmSpringJoint> _listJoint;
        vector<uint32>         _listColliderGroup; ///< `VrmAvatarDef::_listColliderGroup` 의 번호
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 1인칭에서 보일지를 정한 glTF 메시 하나입니다. 메시 이름은 원본의 것입니다(본 메시는 메시들을 합치므로 런타임이 구간으로 다시 찾아야 한다). */
    struct VrmFirstPersonMesh
    {
        string             _mesh;
        VrmFirstPersonMode _mode{ VrmFirstPersonMode::Auto };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief VRM 모델 하나의 아바타 정보 전부입니다. VRM 이 아니면 비어 있습니다(`isEmpty`). */
    struct VrmAvatarDef
    {
        vector<VrmHumanBone>           _listHumanBone;
        vector<VrmExpression>          _listExpression;
        vector<VrmSpringCollider>      _listCollider;
        vector<VrmSpringColliderGroup> _listColliderGroup;
        vector<VrmSpring>              _listSpring;
        vector<VrmFirstPersonMesh>     _listFirstPersonMesh;
        string                         _firstPersonBone;         ///< 1인칭 카메라가 붙는 본(보통 머리)
        float3                         _firstPersonOffset{};     ///< 그 본 기준의 눈 위치(엔진 공간)
        uint8                          _sourceMajorVersion{ 0 }; ///< 원본 VRM 판(0 · 1)

        bool isEmpty() const { return _listHumanBone.empty() && _listExpression.empty() && _listSpring.empty() && _firstPersonBone.empty(); }
        /** @brief humanoid 본 이름(1.0)의 스켈레톤 본 이름입니다. 없으면 빈 문자열입니다. */
        string findBone( string_view humanBone ) const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief glTF 번호를 엔진 이름으로 푸는 표입니다. 모델 임포트가 cgltf 데이터에서 채우고, 테스트는 손으로 채웁니다.
     * @details 노드 이름은 `ModelImportUtil::makeNodeName`(스켈레톤 본 이름), 모프 타깃 이름은 `makeMorphTargetName` 과 같은 규칙이어야 한다.
     */
    struct VrmGltfNames
    {
        vector<string>         _listNodeName;
        vector<int32>          _listNodeMesh; ///< 노드의 메시 번호(-1 이면 없음)
        vector<vector<uint32>> _listNodeChild;
        vector<string>         _listMeshName;
        vector<vector<string>> _listMeshMorphName; ///< 메시마다 모프 타깃 이름(번호 순)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct VrmAvatarImporter
     * @brief VRM 아바타 정보 읽기와 `.avatar.json` 쓰기를 담당합니다. 모든 함수가 static 입니다.
     * @details 번호가 범위를 벗어나면(없는 노드 · 메시 · 모프 타깃) 오류다. 모르는 키와, 알지만 옮기지 않는 값(시선 · 머티리얼 표정 · humanoid 관절 한계)은
     *          @p pOutListIgnored 에 경로를 모은다. 호출하는 쪽이 경고로 알린다. 아바타 정보는 모델 없이도 그릴 수 있는 부가 정보라 모르는 키로 임포트를 막지 않는다.
     */
    struct VrmAvatarImporter
    {
        /** @brief 모델 옆 폴더에 쓰는 아바타 파일의 접미사입니다(`<이름>.avatar.json`). */
        static constexpr string_view kExtension = ".avatar.json";
        /** @brief 아바타 파일의 형식과 읽기 규칙 버전입니다. 바꾸면 올립니다. VRM 원본의 해시에 섞여 다시 임포트하게 합니다. */
        static constexpr uint32 kVersion = 2; // 2: 얼굴 리그 · 후처리 리그도 쓴다(VrmRuntimeAssets)

        /**
         * @brief VRM 0.x 의 `extensions.VRM` 을 읽습니다.
         * @details 0.x 확장의 벡터(충돌체 offset, gravityDir, firstPersonBoneOffset)는 유니티 공간이라 z 를 뒤집어 glTF 공간으로 옮긴 뒤(three-vrm 과 같다) 엔진 공간(X 뒤집기)으로 옮긴다.
         *          blendShape 가중치(0..100)는 0..1 로 바꾼다.
         * @return 번호가 범위를 벗어나면 false 이고 @p outError 에 이유를 씁니다.
         */
        [[nodiscard]] static bool readVrm0Avatar( const JSONValue& vrm, const VrmGltfNames& names, VrmAvatarDef& outDef, vector<string>* pOutListIgnored, string& outError );

        /**
         * @brief VRM 1.0 의 `extensions.VRMC_vrm` 과 `extensions.VRMC_springBone`(없으면 무효 값)을 읽습니다. 벡터는 glTF 공간이라 X 만 뒤집습니다.
         * @return 번호가 범위를 벗어나면 false 이고 @p outError 에 이유를 씁니다.
         */
        [[nodiscard]] static bool readVrm1Avatar( const JSONValue& vrmcVrm, const JSONValue& vrmcSpringBone, const VrmGltfNames& names, VrmAvatarDef& outDef,
                                                  vector<string>* pOutListIgnored, string& outError );

        /** @brief 아바타 정보를 `.avatar.json` 문자열로 만듭니다. 같은 값이면 같은 바이트입니다. */
        static string makeAvatarJSON( const VrmAvatarDef& def );
    };
} // namespace sw::editor
