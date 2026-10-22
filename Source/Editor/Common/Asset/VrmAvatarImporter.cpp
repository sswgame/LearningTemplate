/**
 * @file VrmAvatarImporter.cpp
 * @brief VRM 아바타 정보(humanoid · 표정 · 스프링 본 · 1인칭) 읽기와 `.avatar.xml` 쓰기입니다.
 * @details 0.x 와 1.0 의 대응:
 *          - humanoid: 0.x `humanBones[] { bone, node }` · 1.0 `humanBones { <이름>: { node } }`. 0.x 엄지 `ThumbProximal` · `ThumbIntermediate` 는 1.0 의
 *            `ThumbMetacarpal` · `ThumbProximal` 이다. 유니티 Mecanim 근육 값(armStretch, min/max 등)은 엔진에 대응이 없어 옮기지 않는다.
 *          - 표정: 0.x `blendShapeGroups[] { presetName, binds[] { mesh, index, weight 0..100 } }` · 1.0 `expressions { preset, custom }`
 *            `{ morphTargetBinds[] { node, index, weight 0..1 } }`. 머티리얼 색 · 텍스처 표정은 옮기지 않는다(무시 목록).
 *          - 스프링 본: 0.x `secondaryAnimation.boneGroups`(루트 본 + 그룹 값) · 1.0 `VRMC_springBone.springs`(관절마다 값). 0.x 의 키 `stiffiness` 는 원본 철자다.
 *          - 1인칭: 0.x `firstPerson { firstPersonBone, firstPersonBoneOffset, meshAnnotations[] { mesh, firstPersonFlag } }` ·
 *            1.0 `firstPerson.meshAnnotations[] { node, type }` 와 `lookAt.offsetFromHeadBone`(본은 humanoid head). 시선(lookAt)의 나머지는 옮기지 않는다.
 */
#include "pch.h"

#include "Editor/Common/Asset/VrmAvatarImporter.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    namespace
    {
        struct VrmAvatarImporterInternal
        {
            /** @brief 쓰는 실수를 이 단위로 반올림합니다(1 μm). float32 를 그대로 쓰면 0.06 이 0.0599999986… 이 된다. */
            static constexpr float64 kWrittenPrecision = 1.0e6;
            /** @brief 0.x 가중치(0..100)를 0..1 로 바꾸는 배율입니다. */
            static constexpr float32 kVrm0WeightScale = 0.01f;
            /** @brief 0.x 바인드에 가중치를 적지 않았을 때입니다(UniVRM, 0..100 단위). 1.0 은 `VrmExpressionBind::_weight` 의 초기값(1)이다. */
            static constexpr float32 kVrm0FullWeight = 100.0f;
            /** @brief 0.x 본 그룹에 적지 않았을 때의 끌림 · 충돌 반지름입니다(UniVRM). 나머지와 1.0 은 `VrmSpringJoint` 의 초기값이다. */
            static constexpr float32 kVrm0DragForce = 0.4f;
            static constexpr float32 kVrm0HitRadius = 0.02f;

            /** @brief 모든 객체에 올 수 있는 glTF 공통 키입니다. */
            static constexpr string_view kArrCommonKey[] = { "extensions", "extras" };

            /** @brief VRM 1.0 humanoid 본 이름 전부(55)입니다. */
            static constexpr string_view kArrHumanBone[] = {
                "hips", "spine", "chest", "upperChest", "neck", "head", "leftEye", "rightEye", "jaw",
                "leftUpperLeg", "leftLowerLeg", "leftFoot", "leftToes", "rightUpperLeg", "rightLowerLeg", "rightFoot", "rightToes",
                "leftShoulder", "leftUpperArm", "leftLowerArm", "leftHand", "rightShoulder", "rightUpperArm", "rightLowerArm", "rightHand",
                "leftThumbMetacarpal", "leftThumbProximal", "leftThumbDistal", "leftIndexProximal", "leftIndexIntermediate", "leftIndexDistal",
                "leftMiddleProximal", "leftMiddleIntermediate", "leftMiddleDistal", "leftRingProximal", "leftRingIntermediate", "leftRingDistal",
                "leftLittleProximal", "leftLittleIntermediate", "leftLittleDistal",
                "rightThumbMetacarpal", "rightThumbProximal", "rightThumbDistal", "rightIndexProximal", "rightIndexIntermediate", "rightIndexDistal",
                "rightMiddleProximal", "rightMiddleIntermediate", "rightMiddleDistal", "rightRingProximal", "rightRingIntermediate", "rightRingDistal",
                "rightLittleProximal", "rightLittleIntermediate", "rightLittleDistal" };

            /** @brief 0.x 의 엄지 이름이 1.0 에서 바뀐 것입니다(0.x 이름, 1.0 이름). */
            static constexpr string_view kArrVrm0ThumbRename[][2] = {
                {     "leftThumbProximal",  "leftThumbMetacarpal"},
                { "leftThumbIntermediate",    "leftThumbProximal"},
                {    "rightThumbProximal", "rightThumbMetacarpal"},
                {"rightThumbIntermediate",   "rightThumbProximal"}
            };

            /** @brief 0.x 표정 프리셋(presetName, 소문자)의 1.0 이름입니다. `unknown` 은 프리셋이 아니다(사용자 표정). */
            static constexpr string_view kArrVrm0Preset[][2] = {
                {  "neutral",    "neutral"},
                {        "a",         "aa"},
                {        "i",         "ih"},
                {        "u",         "ou"},
                {        "e",         "ee"},
                {        "o",         "oh"},
                {    "blink",      "blink"},
                {  "blink_l",  "blinkLeft"},
                {  "blink_r", "blinkRight"},
                {      "joy",      "happy"},
                {    "angry",      "angry"},
                {   "sorrow",        "sad"},
                {      "fun",    "relaxed"},
                {   "lookup",     "lookUp"},
                { "lookdown",   "lookDown"},
                { "lookleft",   "lookLeft"},
                {"lookright",  "lookRight"}
            };

            /** @brief VRM 1.0 표정 프리셋 이름입니다. */
            static constexpr string_view kArrVrm1Preset[] = { "happy", "angry", "sad", "relaxed", "surprised", "aa", "ih", "ou", "ee", "oh",
                                                              "blink", "blinkLeft", "blinkRight", "lookUp", "lookDown", "lookLeft", "lookRight", "neutral" };

            // 0.x 키
            static constexpr string_view kArrVrm0RootKey[]          = { "exporterVersion", "specVersion", "meta", "humanoid", "firstPerson", "blendShapeMaster",
                                                                        "secondaryAnimation", "materialProperties" };
            static constexpr string_view kArrVrm0HumanoidKey[]      = { "humanBones", "armStretch", "legStretch", "upperArmTwist", "lowerArmTwist",
                                                                        "upperLegTwist", "lowerLegTwist", "feetSpacing", "hasTranslationDoF" };
            static constexpr string_view kArrVrm0HumanBoneKey[]     = { "bone", "node", "useDefaultValues", "min", "max", "center", "axisLength" };
            static constexpr string_view kArrVrm0FirstPersonKey[]   = { "firstPersonBone", "firstPersonBoneOffset", "meshAnnotations", "lookAtTypeName",
                                                                        "lookAtHorizontalInner", "lookAtHorizontalOuter", "lookAtVerticalDown", "lookAtVerticalUp" };
            static constexpr string_view kArrVrm0AnnotationKey[]    = { "mesh", "firstPersonFlag" };
            static constexpr string_view kArrVrm0MasterKey[]        = { "blendShapeGroups" };
            static constexpr string_view kArrVrm0GroupKey[]         = { "name", "presetName", "binds", "materialValues", "isBinary" };
            static constexpr string_view kArrVrm0BindKey[]          = { "mesh", "index", "weight" };
            static constexpr string_view kArrVrm0SecondaryKey[]     = { "boneGroups", "colliderGroups" };
            static constexpr string_view kArrVrm0BoneGroupKey[]     = { "comment", "stiffiness", "gravityPower", "gravityDir", "dragForce", "center",
                                                                        "hitRadius", "bones", "colliderGroups" };
            static constexpr string_view kArrVrm0ColliderGroupKey[] = { "node", "colliders" };
            static constexpr string_view kArrVrm0ColliderKey[]      = { "offset", "radius" };

            // 1.0 키
            static constexpr string_view kArrVrm1RootKey[]        = { "specVersion", "meta", "humanoid", "firstPerson", "lookAt", "expressions" };
            static constexpr string_view kArrVrm1HumanoidKey[]    = { "humanBones" };
            static constexpr string_view kArrVrm1NodeKey[]        = { "node" };
            static constexpr string_view kArrVrm1FirstPersonKey[] = { "meshAnnotations" };
            static constexpr string_view kArrVrm1AnnotationKey[]  = { "node", "type" };
            static constexpr string_view kArrVrm1LookAtKey[]      = { "offsetFromHeadBone", "type", "rangeMapHorizontalInner", "rangeMapHorizontalOuter",
                                                                      "rangeMapVerticalDown", "rangeMapVerticalUp" };
            static constexpr string_view kArrVrm1ExpressionsKey[] = { "preset", "custom" };
            static constexpr string_view kArrVrm1ExpressionKey[]  = { "morphTargetBinds", "materialColorBinds", "textureTransformBinds", "isBinary",
                                                                      "overrideBlink", "overrideLookAt", "overrideMouth" };
            static constexpr string_view kArrVrm1BindKey[]        = { "node", "index", "weight" };
            static constexpr string_view kArrVrm1SpringRootKey[]  = { "specVersion", "colliders", "colliderGroups", "springs" };
            static constexpr string_view kArrVrm1ColliderKey[]    = { "node", "shape" };
            static constexpr string_view kArrVrm1ShapeKey[]       = { "sphere", "capsule" };
            static constexpr string_view kArrVrm1SphereKey[]      = { "offset", "radius" };
            static constexpr string_view kArrVrm1CapsuleKey[]     = { "offset", "radius", "tail" };
            static constexpr string_view kArrVrm1GroupKey[]       = { "name", "colliders" };
            static constexpr string_view kArrVrm1SpringKey[]      = { "name", "joints", "colliderGroups", "center" };
            static constexpr string_view kArrVrm1JointKey[]       = { "node", "hitRadius", "stiffness", "gravityPower", "gravityDir", "dragForce" };

            /** @brief @p object 의 키 가운데 @p arrKnown 과 공통 키가 아닌 것을 `<context>.<키>` 로 무시 목록에 모읍니다. */
            template <size_t N>
            static void collectUnknownKeys( const JSONValue& object, const string_view ( &arrKnown )[N], string_view context, vector<string>* pOutListIgnored )
            {
                if ( pOutListIgnored == nullptr || object.isObject() == false )
                    return;
                for ( const string& key : object.getMemberNames() )
                {
                    if ( VectorUtil::contains( arrKnown, key ) || VectorUtil::contains( kArrCommonKey, key ) )
                        continue;
                    pOutListIgnored->push_back( string( context ) + "." + key + " (unknown key)" );
                }
            }

            /** @brief 지원하지 않는 값의 경로를 무시 목록에 한 번만 넣습니다. */
            static void addIgnored( vector<string>* pOutListIgnored, const string& path )
            {
                if ( pOutListIgnored == nullptr )
                    return;
                if ( VectorUtil::contains( *pOutListIgnored, path ) == false )
                    pOutListIgnored->push_back( path );
            }

            /**
             * @brief 0.x 벡터 `{ x, y, z }` 나 1.0 벡터 `[ x, y, z ]` 를 읽습니다(원본 공간). 없으면 @p inoutValue 를 그대로 둡니다(정의의 초기값).
             * @return 벡터 모양이 아니거나 칸이 숫자가 아니면 이유와 false 입니다.
             */
            [[nodiscard]] static bool readVector( const JSONValue& value, string_view context, float3& inoutValue, string& outError )
            {
                if ( value.isValid() == false || value.isNull() )
                    return true;
                if ( value.isObject() )
                    return readFloat( value, "x", context, inoutValue._x, outError ) && readFloat( value, "y", context, inoutValue._y, outError ) &&
                           readFloat( value, "z", context, inoutValue._z, outError );
                float32* const arrAxis[3] = { &inoutValue._x, &inoutValue._y, &inoutValue._z };
                const bool     bArray     = value.isArray() && value.size() >= 3;
                for ( uint32 axis = 0; bArray && axis < 3; ++axis )
                {
                    const std::optional<float32> number = value.at( axis ).read<float32>();
                    if ( number.has_value() == false )
                    {
                        outError = string( context ) + ": vector " + value.dump() + " has a value that is not a number";
                        return false;
                    }
                    *arrAxis[axis] = *number;
                }
                if ( bArray )
                    return true;
                outError = string( context ) + ": vector " + value.dump() + " must be { x, y, z } or [ x, y, z ]";
                return false;
            }

            /** @brief 0.x 확장의 벡터(유니티 공간)를 엔진 공간으로 옮깁니다. z 를 뒤집어 glTF 로, X 를 뒤집어 엔진으로 간다. */
            static float3 convertVrm0Vector( const float3& value ) { return float3{ -value._x, value._y, -value._z }; }
            /** @brief 1.0 확장의 벡터(glTF 공간)를 엔진 공간으로 옮깁니다. */
            static float3 convertVrm1Vector( const float3& value ) { return float3{ -value._x, value._y, value._z }; }

            /** @brief 없어도 되는 숫자 @p key 를 읽습니다. 없으면 @p inoutValue 를 그대로 두고(정의의 초기값), 숫자가 아니면 이유와 false 입니다. */
            [[nodiscard]] static bool readFloat( const JSONValue& object, string_view key, string_view context, float32& inoutValue, string& outError )
            {
                const JSONValue value = object.get( key, false );
                if ( value.isValid() == false || value.isNull() )
                    return true;
                const std::optional<float32> number = value.read<float32>();
                if ( number.has_value() == false )
                {
                    outError = string( context ) + ": " + string( key ) + " " + value.dump() + " is not a number";
                    return false;
                }
                inoutValue = *number;
                return true;
            }

            /** @brief 없어도 되는 불리언 @p key 를 읽습니다. 없으면 @p inoutFlag 를 그대로 두고, 불리언이 아니면 이유와 false 입니다. */
            [[nodiscard]] static bool readFlag( const JSONValue& object, string_view key, string_view context, uint8& inoutFlag, string& outError )
            {
                const JSONValue value = object.get( key, false );
                if ( value.isValid() == false || value.isNull() )
                    return true;
                const std::optional<bool> flag = value.read<bool>();
                if ( flag.has_value() == false )
                {
                    outError = string( context ) + ": " + string( key ) + " " + value.dump() + " is not a boolean";
                    return false;
                }
                inoutFlag = *flag ? SW_TRUE : SW_FALSE;
                return true;
            }

            /** @brief 번호 @p value 가 [0, @p count) 안의 정수이면 @p outIndex 에 넣습니다. 아니면 `<context>: <what> <값> <reason>` 과 false 입니다. */
            [[nodiscard]] static bool readIndex( const JSONValue& value, size_t count, string_view context, string_view what, string_view reason, uint32& outIndex,
                                                 string& outError )
            {
                const std::optional<uint32> index = value.read<uint32>();
                if ( index.has_value() == false || *index >= count )
                {
                    outError = string( context ) + ": " + string( what ) + " " + value.dump() + " " + string( reason );
                    return false;
                }
                outIndex = *index;
                return true;
            }

            /** @brief 노드 번호를 본 이름으로 풉니다. 번호가 아니거나 범위 밖이면 false 와 이유입니다. */
            static bool resolveNode( const VrmGltfNames& names, const JSONValue& value, string_view context, string& outName, string& outError )
            {
                uint32 node{ 0 };
                if ( readIndex( value, names._listNodeName.size(), context, "node", "is not a node of the model", node, outError ) == false )
                    return false;
                outName = names._listNodeName[node];
                return true;
            }

            /** @brief 0.x 의 "없음" 이 -1 인 노드 필드(center · firstPersonBone)가 노드를 가리키는지 봅니다. 없거나 음수면 false 이고, 정수가 아니면 @p outbInvalid 입니다. */
            static bool refersToNode( const JSONValue& value, bool& outbInvalid )
            {
                outbInvalid = false;
                if ( value.isValid() == false || value.isNull() )
                    return false;
                const std::optional<int64> node = value.read<int64>();
                outbInvalid                     = node.has_value() == false;
                return node.has_value() && *node >= 0;
            }

            /** @brief 메시 번호 · 타깃 번호를 모프 타깃 이름으로 풉니다. 번호가 아니거나 범위 밖이면 false 와 이유입니다. */
            static bool resolveMorphTarget( const VrmGltfNames& names, uint32 mesh, const JSONValue& indexValue, string_view context, string& outName, string& outError )
            {
                if ( mesh >= names._listMeshMorphName.size() )
                {
                    outError = string( context ) + ": mesh " + to_string( mesh ) + " is not a mesh of the model";
                    return false;
                }
                const vector<string>& listMorph = names._listMeshMorphName[mesh];
                uint32                index{ 0 };
                if ( readIndex( indexValue, listMorph.size(), context, "morph target", "is not a target of mesh " + to_string( mesh ), index, outError ) == false )
                    return false;
                outName = listMorph[index];
                return true;
            }

            static string makeMeshName( const VrmGltfNames& names, uint32 mesh )
            {
                const bool bHasMeshName = mesh < names._listMeshName.size() && names._listMeshName[mesh].empty() == false;
                if ( bHasMeshName )
                    return names._listMeshName[mesh];
                return "mesh" + to_string( mesh );
            }

            [[nodiscard]] static bool parseFirstPersonMode( const string& text, VrmFirstPersonMode& outMode )
            {
                static constexpr string_view kArrName[] = { "auto", "both", "thirdPersonOnly", "firstPersonOnly" };
                for ( size_t index = 0; index < sizeof( kArrName ) / sizeof( kArrName[0] ); ++index )
                {
                    if ( StringUtil::equals( text, kArrName[index], true ) )
                    {
                        outMode = static_cast<VrmFirstPersonMode>( index );
                        return true;
                    }
                }
                return false;
            }

            [[nodiscard]] static bool parseOverride( const JSONValue& value, VrmExpressionOverride& outOverride )
            {
                if ( value.isValid() == false )
                    return true;
                static constexpr string_view kArrName[] = { "none", "block", "blend" };
                const string                 text       = value.asString();
                for ( size_t index = 0; index < sizeof( kArrName ) / sizeof( kArrName[0] ); ++index )
                {
                    if ( text == kArrName[index] )
                    {
                        outOverride = static_cast<VrmExpressionOverride>( index );
                        return true;
                    }
                }
                return false;
            }

            /** @brief @p root 와 그 자손을 깊이 우선 순서로 모읍니다(0.x 스프링 루트 아래 흔들리는 본들). 순환은 노드 수에서 끊습니다. */
            static void collectDescendants( const VrmGltfNames& names, uint32 root, vector<uint32>& outListNode )
            {
                outListNode.clear();
                vector<uint32> listStack{ root };
                while ( listStack.empty() == false && outListNode.size() <= names._listNodeName.size() )
                {
                    const uint32 node = listStack.back();
                    listStack.pop_back();
                    outListNode.push_back( node );
                    if ( node >= names._listNodeChild.size() )
                        continue;
                    const vector<uint32>& listChild = names._listNodeChild[node];
                    for ( size_t childIndex = listChild.size(); childIndex > 0; --childIndex )
                    {
                        if ( listChild[childIndex - 1] < names._listNodeName.size() )
                            listStack.push_back( listChild[childIndex - 1] );
                    }
                }
            }

            /** @brief 실수를 1 μm 로 반올림해 글로 만듭니다(`kWrittenPrecision`). 같은 값이면 같은 글입니다. */
            static string makeNumberText( float32 value )
            {
                return to_string( static_cast<float64>( std::llround( static_cast<float64>( value ) * kWrittenPrecision ) ) / kWrittenPrecision );
            }

            /** @brief 벡터를 `x,y,z` 글로 만듭니다(성분마다 `makeNumberText`). */
            static string makeVectorText( const float3& value )
            {
                return makeNumberText( value._x ) + "," + makeNumberText( value._y ) + "," + makeNumberText( value._z );
            }

            static string_view makeBoolName( bool bValue ) { return bValue ? "true" : "false"; }

            /** @brief 번호 목록을 @p target 요소 안에 `<item>` 으로 씁니다. */
            static void writeIndices( const XMLNode& target, const vector<uint32>& listIndex )
            {
                for ( const uint32 index : listIndex )
                {
                    (void)target.appendChild( "item", index );
                }
            }

            static string_view makeOverrideName( VrmExpressionOverride value )
            {
                switch ( value )
                {
                    case VrmExpressionOverride::None:
                        return "None";
                    case VrmExpressionOverride::Block:
                        return "Block";
                    case VrmExpressionOverride::Blend:
                        return "Blend";
                }
                return "None";
            }

            static string_view makeFirstPersonModeName( VrmFirstPersonMode value )
            {
                switch ( value )
                {
                    case VrmFirstPersonMode::Auto:
                        return "Auto";
                    case VrmFirstPersonMode::Both:
                        return "Both";
                    case VrmFirstPersonMode::ThirdPersonOnly:
                        return "ThirdPersonOnly";
                    case VrmFirstPersonMode::FirstPersonOnly:
                        return "FirstPersonOnly";
                }
                return "Auto";
            }

            /** @brief 0.x 의 humanoid 를 읽습니다. */
            [[nodiscard]] static bool readVrm0Humanoid( const JSONValue& humanoid, const VrmGltfNames& names, VrmAvatarDef& inoutDef, vector<string>* pOutListIgnored, string& outError )
            {
                collectUnknownKeys( humanoid, kArrVrm0HumanoidKey, "VRM.humanoid", pOutListIgnored );
                const JSONValue listBone = humanoid.get( "humanBones", false );
                for ( size_t index = 0; index < listBone.size(); ++index )
                {
                    const JSONValue entry = listBone.at( index );
                    collectUnknownKeys( entry, kArrVrm0HumanBoneKey, "VRM.humanoid.humanBones", pOutListIgnored );
                    string humanBone = entry.get( "bone", false ).asString();
                    for ( const string_view( &arrRename )[2] : kArrVrm0ThumbRename )
                    {
                        if ( humanBone == arrRename[0] )
                        {
                            humanBone = string( arrRename[1] );
                            break;
                        }
                    }
                    if ( VectorUtil::contains( kArrHumanBone, humanBone ) == false )
                    {
                        outError = "VRM 0.x humanoid: unknown human bone '" + humanBone + "'";
                        return false;
                    }
                    VrmHumanBone bone;
                    bone._humanBone = humanBone;
                    if ( resolveNode( names, entry.get( "node", false ), "VRM 0.x humanoid bone '" + humanBone + "'", bone._bone, outError ) == false )
                        return false;
                    inoutDef._listHumanBone.push_back( std::move( bone ) );
                }
                return true;
            }

            /** @brief 0.x 의 blendShapeMaster 를 읽습니다. */
            [[nodiscard]] static bool readVrm0Expressions( const JSONValue& master, const VrmGltfNames& names, VrmAvatarDef& inoutDef, vector<string>* pOutListIgnored, string& outError )
            {
                collectUnknownKeys( master, kArrVrm0MasterKey, "VRM.blendShapeMaster", pOutListIgnored );
                const JSONValue listGroup = master.get( "blendShapeGroups", false );
                for ( size_t groupIndex = 0; groupIndex < listGroup.size(); ++groupIndex )
                {
                    const JSONValue group = listGroup.at( groupIndex );
                    collectUnknownKeys( group, kArrVrm0GroupKey, "VRM.blendShapeMaster.blendShapeGroups", pOutListIgnored );
                    VrmExpression expression;
                    expression._name        = group.get( "name", false ).asString();
                    const string presetName = group.get( "presetName", false ).asString();
                    if ( presetName.empty() == false && StringUtil::equals( presetName, "unknown", true ) == false )
                    {
                        bool bKnown = false;
                        for ( const string_view( &arrPreset )[2] : kArrVrm0Preset )
                        {
                            if ( StringUtil::equals( presetName, arrPreset[0], true ) )
                            {
                                expression._name    = string( arrPreset[1] );
                                expression._bPreset = SW_TRUE;
                                bKnown              = true;
                                break;
                            }
                        }
                        if ( bKnown == false )
                        {
                            outError = "VRM 0.x blend shape group '" + expression._name + "': unknown presetName '" + presetName + "'";
                            return false;
                        }
                    }
                    const string groupContext = "VRM 0.x blend shape group '" + expression._name + "'";
                    if ( readFlag( group, "isBinary", groupContext, expression._bBinary, outError ) == false )
                        return false;
                    const JSONValue listBind = group.get( "binds", false );
                    for ( size_t bindIndex = 0; bindIndex < listBind.size(); ++bindIndex )
                    {
                        const JSONValue bindValue = listBind.at( bindIndex );
                        collectUnknownKeys( bindValue, kArrVrm0BindKey, "VRM.blendShapeMaster.blendShapeGroups.binds", pOutListIgnored );
                        VrmExpressionBind bind;
                        uint32            mesh{ 0 };
                        float32           weight{ kVrm0FullWeight };
                        const bool        bBindRead = readIndex( bindValue.get( "mesh", false ), names._listMeshMorphName.size(), groupContext, "mesh", "is not a mesh of the model", mesh,
                                                                 outError ) &&
                                               resolveMorphTarget( names, mesh, bindValue.get( "index", false ), groupContext, bind._morphTarget, outError ) &&
                                               readFloat( bindValue, "weight", groupContext, weight, outError );
                        if ( bBindRead == false )
                            return false;
                        bind._weight = weight * kVrm0WeightScale;
                        expression._listBind.push_back( std::move( bind ) );
                    }
                    if ( group.get( "materialValues", false ).size() > 0 )
                        addIgnored( pOutListIgnored, "VRM.blendShapeMaster.blendShapeGroups.materialValues" );
                    inoutDef._listExpression.push_back( std::move( expression ) );
                }
                return true;
            }

            /** @brief 0.x 의 secondaryAnimation 을 읽습니다. 루트 본마다 사슬 하나입니다. */
            [[nodiscard]] static bool readVrm0Springs( const JSONValue& secondary, const VrmGltfNames& names, VrmAvatarDef& inoutDef, vector<string>* pOutListIgnored, string& outError )
            {
                collectUnknownKeys( secondary, kArrVrm0SecondaryKey, "VRM.secondaryAnimation", pOutListIgnored );
                const JSONValue listColliderGroup = secondary.get( "colliderGroups", false );
                for ( size_t groupIndex = 0; groupIndex < listColliderGroup.size(); ++groupIndex )
                {
                    const JSONValue group = listColliderGroup.at( groupIndex );
                    collectUnknownKeys( group, kArrVrm0ColliderGroupKey, "VRM.secondaryAnimation.colliderGroups", pOutListIgnored );
                    string bone;
                    if ( resolveNode( names, group.get( "node", false ), "VRM 0.x collider group " + to_string( static_cast<uint64>( groupIndex ) ), bone, outError ) == false )
                        return false;
                    VrmSpringColliderGroup colliderGroup;
                    colliderGroup._name               = "colliderGroup" + to_string( static_cast<uint64>( groupIndex ) );
                    const JSONValue listColliderValue = group.get( "colliders", false );
                    for ( size_t colliderIndex = 0; colliderIndex < listColliderValue.size(); ++colliderIndex )
                    {
                        const JSONValue colliderValue = listColliderValue.at( colliderIndex );
                        collectUnknownKeys( colliderValue, kArrVrm0ColliderKey, "VRM.secondaryAnimation.colliderGroups.colliders", pOutListIgnored );
                        VrmSpringCollider collider;
                        collider._bone               = bone;
                        const string colliderContext = "VRM 0.x collider group " + to_string( static_cast<uint64>( groupIndex ) );
                        float3       offset{};
                        if ( readVector( colliderValue.get( "offset", false ), colliderContext, offset, outError ) == false ||
                             readFloat( colliderValue, "radius", colliderContext, collider._radius, outError ) == false )
                            return false;
                        collider._offset = convertVrm0Vector( offset );
                        collider._tail   = collider._offset;
                        colliderGroup._listCollider.push_back( static_cast<uint32>( inoutDef._listCollider.size() ) );
                        inoutDef._listCollider.push_back( std::move( collider ) );
                    }
                    inoutDef._listColliderGroup.push_back( std::move( colliderGroup ) );
                }

                const JSONValue listBoneGroup = secondary.get( "boneGroups", false );
                vector<uint32>  listChain;
                for ( size_t groupIndex = 0; groupIndex < listBoneGroup.size(); ++groupIndex )
                {
                    const JSONValue group = listBoneGroup.at( groupIndex );
                    collectUnknownKeys( group, kArrVrm0BoneGroupKey, "VRM.secondaryAnimation.boneGroups", pOutListIgnored );
                    const string context = "VRM 0.x bone group " + to_string( static_cast<uint64>( groupIndex ) );
                    // 키가 없으면 UniVRM 의 기본값이다. 끌림 · 충돌 반지름만 1.0 의 초기값과 다르다. 중력 방향은 원본 공간에서 읽고 옮긴다.
                    VrmSpringJoint jointValue;
                    jointValue._dragForce = kVrm0DragForce;
                    jointValue._hitRadius = kVrm0HitRadius;
                    float3     gravityDirection{ convertVrm0Vector( jointValue._gravityDirection ) };
                    const bool bJointRead = readFloat( group, "stiffiness", context, jointValue._stiffness, outError ) &&
                                            readFloat( group, "gravityPower", context, jointValue._gravityPower, outError ) &&
                                            readVector( group.get( "gravityDir", false ), context, gravityDirection, outError ) &&
                                            readFloat( group, "dragForce", context, jointValue._dragForce, outError ) &&
                                            readFloat( group, "hitRadius", context, jointValue._hitRadius, outError );
                    if ( bJointRead == false )
                        return false;
                    jointValue._gravityDirection = convertVrm0Vector( gravityDirection );

                    string          centerBone;
                    const JSONValue center         = group.get( "center", false );
                    bool            bCenterInvalid = false;
                    const bool      bHasCenter     = refersToNode( center, bCenterInvalid );
                    if ( bCenterInvalid || ( bHasCenter && resolveNode( names, center, context + " center", centerBone, outError ) == false ) )
                    {
                        if ( bCenterInvalid )
                            outError = context + ": center " + center.dump() + " is not a node number";
                        return false;
                    }
                    vector<uint32>  listColliderGroupIndex;
                    const JSONValue listGroupRef = group.get( "colliderGroups", false );
                    for ( size_t refIndex = 0; refIndex < listGroupRef.size(); ++refIndex )
                    {
                        uint32 ref{ 0 };
                        if ( readIndex( listGroupRef.at( refIndex ), inoutDef._listColliderGroup.size(), context, "collider group", "does not exist", ref, outError ) == false )
                            return false;
                        listColliderGroupIndex.push_back( ref );
                    }

                    const string    comment  = group.get( "comment", false ).asString();
                    const JSONValue listRoot = group.get( "bones", false );
                    for ( size_t rootIndex = 0; rootIndex < listRoot.size(); ++rootIndex )
                    {
                        uint32 rootNode{ 0 };
                        if ( readIndex( listRoot.at( rootIndex ), names._listNodeName.size(), context + " bone", "node", "is not a node of the model", rootNode, outError ) == false )
                            return false;
                        VrmSpring spring;
                        spring._name = comment;
                        if ( spring._name.empty() )
                            spring._name = "spring" + to_string( static_cast<uint64>( groupIndex ) );
                        if ( listRoot.size() > 1 )
                            spring._name += "_" + to_string( static_cast<uint64>( rootIndex ) );
                        spring._centerBone        = centerBone;
                        spring._listColliderGroup = listColliderGroupIndex;
                        collectDescendants( names, rootNode, listChain );
                        for ( const uint32 node : listChain )
                        {
                            VrmSpringJoint joint = jointValue;
                            joint._bone          = names._listNodeName[node];
                            spring._listJoint.push_back( std::move( joint ) );
                        }
                        inoutDef._listSpring.push_back( std::move( spring ) );
                    }
                }
                return true;
            }

            /** @brief 0.x 의 firstPerson 을 읽습니다. 본이 없으면(-1) humanoid head 입니다. */
            [[nodiscard]] static bool readVrm0FirstPerson( const JSONValue& firstPerson, const VrmGltfNames& names, VrmAvatarDef& inoutDef, vector<string>* pOutListIgnored, string& outError )
            {
                collectUnknownKeys( firstPerson, kArrVrm0FirstPersonKey, "VRM.firstPerson", pOutListIgnored );
                const JSONValue bone         = firstPerson.get( "firstPersonBone", false );
                bool            bBoneInvalid = false;
                const bool      bHasBone     = refersToNode( bone, bBoneInvalid );
                if ( bBoneInvalid )
                {
                    outError = "VRM 0.x firstPersonBone " + bone.dump() + " is not a node number";
                    return false;
                }
                if ( bHasBone )
                {
                    if ( resolveNode( names, bone, "VRM 0.x firstPersonBone", inoutDef._firstPersonBone, outError ) == false )
                        return false;
                }
                else
                {
                    inoutDef._firstPersonBone = inoutDef.findBone( "head" );
                }
                float3 offset{};
                if ( readVector( firstPerson.get( "firstPersonBoneOffset", false ), "VRM 0.x firstPersonBoneOffset", offset, outError ) == false )
                    return false;
                inoutDef._firstPersonOffset    = convertVrm0Vector( offset );
                const JSONValue listAnnotation = firstPerson.get( "meshAnnotations", false );
                for ( size_t index = 0; index < listAnnotation.size(); ++index )
                {
                    const JSONValue annotation = listAnnotation.at( index );
                    collectUnknownKeys( annotation, kArrVrm0AnnotationKey, "VRM.firstPerson.meshAnnotations", pOutListIgnored );
                    VrmFirstPersonMesh          mesh;
                    const std::optional<uint32> meshIndex = annotation.get( "mesh", false ).read<uint32>();
                    if ( meshIndex.has_value() == false )
                    {
                        outError = "VRM 0.x meshAnnotations: mesh " + annotation.get( "mesh", false ).dump() + " is not a mesh number";
                        return false;
                    }
                    mesh._mesh        = makeMeshName( names, *meshIndex );
                    const string flag = annotation.get( "firstPersonFlag", false ).asString();
                    if ( parseFirstPersonMode( flag, mesh._mode ) == false )
                    {
                        outError = "VRM 0.x meshAnnotations: unknown firstPersonFlag '" + flag + "'";
                        return false;
                    }
                    inoutDef._listFirstPersonMesh.push_back( std::move( mesh ) );
                }
                if ( firstPerson.has( "lookAtTypeName", false ) )
                    addIgnored( pOutListIgnored, "VRM.firstPerson.lookAt*" );
                return true;
            }

            /** @brief 1.0 표정 하나를 읽습니다. */
            [[nodiscard]] static bool readVrm1Expression( const JSONValue& value, const string& name, bool bPreset, const VrmGltfNames& names, VrmAvatarDef& inoutDef,
                                                          vector<string>* pOutListIgnored, string& outError )
            {
                collectUnknownKeys( value, kArrVrm1ExpressionKey, "VRMC_vrm.expressions." + name, pOutListIgnored );
                VrmExpression expression;
                expression._name    = name;
                expression._bPreset = bPreset ? SW_TRUE : SW_FALSE;
                if ( readFlag( value, "isBinary", "VRM 1.0 expression '" + name + "'", expression._bBinary, outError ) == false )
                    return false;
                const bool bOverrides = parseOverride( value.get( "overrideBlink", false ), expression._overrideBlink ) &&
                                        parseOverride( value.get( "overrideLookAt", false ), expression._overrideLookAt ) &&
                                        parseOverride( value.get( "overrideMouth", false ), expression._overrideMouth );
                if ( bOverrides == false )
                {
                    outError = "VRM 1.0 expression '" + name + "': override must be none, block or blend";
                    return false;
                }
                const JSONValue listBind = value.get( "morphTargetBinds", false );
                for ( size_t index = 0; index < listBind.size(); ++index )
                {
                    const JSONValue bindValue = listBind.at( index );
                    collectUnknownKeys( bindValue, kArrVrm1BindKey, "VRMC_vrm.expressions." + name + ".morphTargetBinds", pOutListIgnored );
                    const string      context = "VRM 1.0 expression '" + name + "'";
                    uint32            node{ 0 };
                    VrmExpressionBind bind;
                    if ( readIndex( bindValue.get( "node", false ), names._listNodeMesh.size(), context, "node", "is not a node of the model", node, outError ) == false )
                        return false;
                    const int64 mesh = names._listNodeMesh[node];
                    if ( mesh < 0 )
                    {
                        outError = context + ": node " + to_string( node ) + " has no mesh";
                        return false;
                    }
                    const bool bBindRead = resolveMorphTarget( names, static_cast<uint32>( mesh ), bindValue.get( "index", false ), context + " node " + to_string( node ),
                                                               bind._morphTarget, outError ) &&
                                           readFloat( bindValue, "weight", context, bind._weight, outError );
                    if ( bBindRead == false )
                        return false;
                    expression._listBind.push_back( std::move( bind ) );
                }
                if ( value.get( "materialColorBinds", false ).size() > 0 )
                    addIgnored( pOutListIgnored, "VRMC_vrm.expressions.materialColorBinds" );
                if ( value.get( "textureTransformBinds", false ).size() > 0 )
                    addIgnored( pOutListIgnored, "VRMC_vrm.expressions.textureTransformBinds" );
                inoutDef._listExpression.push_back( std::move( expression ) );
                return true;
            }

            /** @brief 1.0 VRMC_springBone 을 읽습니다. */
            [[nodiscard]] static bool readVrm1Springs( const JSONValue& springBone, const VrmGltfNames& names, VrmAvatarDef& inoutDef, vector<string>* pOutListIgnored, string& outError )
            {
                collectUnknownKeys( springBone, kArrVrm1SpringRootKey, "VRMC_springBone", pOutListIgnored );
                const JSONValue listCollider = springBone.get( "colliders", false );
                for ( size_t index = 0; index < listCollider.size(); ++index )
                {
                    const JSONValue colliderValue = listCollider.at( index );
                    collectUnknownKeys( colliderValue, kArrVrm1ColliderKey, "VRMC_springBone.colliders", pOutListIgnored );
                    VrmSpringCollider collider;
                    if ( resolveNode( names, colliderValue.get( "node", false ), "VRM 1.0 collider " + to_string( static_cast<uint64>( index ) ), collider._bone, outError ) ==
                         false )
                        return false;
                    const JSONValue shape   = colliderValue.get( "shape", false );
                    const JSONValue sphere  = shape.get( "sphere", false );
                    const JSONValue capsule = shape.get( "capsule", false );
                    collectUnknownKeys( shape, kArrVrm1ShapeKey, "VRMC_springBone.colliders.shape", pOutListIgnored );
                    const string colliderContext = "VRM 1.0 collider " + to_string( static_cast<uint64>( index ) );
                    float3       offset{};
                    float3       tail{};
                    if ( sphere.isObject() )
                    {
                        collectUnknownKeys( sphere, kArrVrm1SphereKey, "VRMC_springBone.colliders.shape.sphere", pOutListIgnored );
                        if ( readVector( sphere.get( "offset", false ), colliderContext, offset, outError ) == false ||
                             readFloat( sphere, "radius", colliderContext, collider._radius, outError ) == false )
                            return false;
                        collider._offset = convertVrm1Vector( offset );
                        collider._tail   = collider._offset;
                    }
                    else if ( capsule.isObject() )
                    {
                        collectUnknownKeys( capsule, kArrVrm1CapsuleKey, "VRMC_springBone.colliders.shape.capsule", pOutListIgnored );
                        const bool bCapsuleRead = readVector( capsule.get( "offset", false ), colliderContext, offset, outError ) &&
                                                  readVector( capsule.get( "tail", false ), colliderContext, tail, outError ) &&
                                                  readFloat( capsule, "radius", colliderContext, collider._radius, outError );
                        if ( bCapsuleRead == false )
                            return false;
                        collider._offset   = convertVrm1Vector( offset );
                        collider._tail     = convertVrm1Vector( tail );
                        collider._bCapsule = SW_TRUE;
                    }
                    else
                    {
                        outError = "VRM 1.0 collider " + to_string( static_cast<uint64>( index ) ) + " has neither a sphere nor a capsule shape";
                        return false;
                    }
                    inoutDef._listCollider.push_back( std::move( collider ) );
                }

                const JSONValue listGroup = springBone.get( "colliderGroups", false );
                for ( size_t index = 0; index < listGroup.size(); ++index )
                {
                    const JSONValue groupValue = listGroup.at( index );
                    collectUnknownKeys( groupValue, kArrVrm1GroupKey, "VRMC_springBone.colliderGroups", pOutListIgnored );
                    VrmSpringColliderGroup group;
                    group._name                     = groupValue.get( "name", false ).asString();
                    const JSONValue listColliderRef = groupValue.get( "colliders", false );
                    for ( size_t refIndex = 0; refIndex < listColliderRef.size(); ++refIndex )
                    {
                        uint32 ref{ 0 };
                        if ( readIndex( listColliderRef.at( refIndex ), inoutDef._listCollider.size(), "VRM 1.0 collider group '" + group._name + "'", "collider", "does not exist", ref,
                                        outError ) == false )
                            return false;
                        group._listCollider.push_back( ref );
                    }
                    inoutDef._listColliderGroup.push_back( std::move( group ) );
                }

                const JSONValue listSpring = springBone.get( "springs", false );
                for ( size_t index = 0; index < listSpring.size(); ++index )
                {
                    const JSONValue springValue = listSpring.at( index );
                    collectUnknownKeys( springValue, kArrVrm1SpringKey, "VRMC_springBone.springs", pOutListIgnored );
                    VrmSpring spring;
                    spring._name            = springValue.get( "name", false ).asString();
                    const string    context = "VRM 1.0 spring '" + spring._name + "'";
                    const JSONValue center  = springValue.get( "center", false );
                    if ( center.isValid() && resolveNode( names, center, context + " center", spring._centerBone, outError ) == false )
                        return false;
                    const JSONValue listGroupRef = springValue.get( "colliderGroups", false );
                    for ( size_t refIndex = 0; refIndex < listGroupRef.size(); ++refIndex )
                    {
                        uint32 ref{ 0 };
                        if ( readIndex( listGroupRef.at( refIndex ), inoutDef._listColliderGroup.size(), context, "collider group", "does not exist", ref, outError ) == false )
                            return false;
                        spring._listColliderGroup.push_back( ref );
                    }
                    const JSONValue listJoint = springValue.get( "joints", false );
                    for ( size_t jointIndex = 0; jointIndex < listJoint.size(); ++jointIndex )
                    {
                        const JSONValue jointValue = listJoint.at( jointIndex );
                        collectUnknownKeys( jointValue, kArrVrm1JointKey, "VRMC_springBone.springs.joints", pOutListIgnored );
                        VrmSpringJoint joint;
                        if ( resolveNode( names, jointValue.get( "node", false ), context + " joint", joint._bone, outError ) == false )
                            return false;
                        // 적지 않은 칸은 `VrmSpringJoint` 의 초기값(1.0 명세의 기본값)이다. 중력 방향은 원본 공간에서 읽고 옮긴다.
                        float3     gravityDirection{ convertVrm1Vector( joint._gravityDirection ) };
                        const bool bJointRead = readFloat( jointValue, "hitRadius", context, joint._hitRadius, outError ) &&
                                                readFloat( jointValue, "stiffness", context, joint._stiffness, outError ) &&
                                                readFloat( jointValue, "gravityPower", context, joint._gravityPower, outError ) &&
                                                readVector( jointValue.get( "gravityDir", false ), context, gravityDirection, outError ) &&
                                                readFloat( jointValue, "dragForce", context, joint._dragForce, outError );
                        if ( bJointRead == false )
                            return false;
                        joint._gravityDirection = convertVrm1Vector( gravityDirection );
                        spring._listJoint.push_back( std::move( joint ) );
                    }
                    inoutDef._listSpring.push_back( std::move( spring ) );
                }
                return true;
            }

            /** @brief 표정 목록(`_listExpression`)을 @p root 에 씁니다. */
            static void writeExpressions( const XMLNode& root, const VrmAvatarDef& def )
            {
                const XMLNode list = root.appendChild( "_listExpression" );
                for ( const VrmExpression& expression : def._listExpression )
                {
                    const XMLNode entry = list.appendChild( "VrmExpression" );
                    entry.appendAttribute( "_name", string_view{ expression._name } );
                    entry.appendAttribute( "_bPreset", makeBoolName( expression._bPreset == SW_TRUE ) );
                    entry.appendAttribute( "_bBinary", makeBoolName( expression._bBinary == SW_TRUE ) );
                    entry.appendAttribute( "_overrideBlink", makeOverrideName( expression._overrideBlink ) );
                    entry.appendAttribute( "_overrideLookAt", makeOverrideName( expression._overrideLookAt ) );
                    entry.appendAttribute( "_overrideMouth", makeOverrideName( expression._overrideMouth ) );
                    const XMLNode listBind = entry.appendChild( "_listBind" );
                    for ( const VrmExpressionBind& bind : expression._listBind )
                    {
                        const XMLNode bindEntry = listBind.appendChild( "VrmExpressionBind" );
                        bindEntry.appendAttribute( "_morphTarget", string_view{ bind._morphTarget } );
                        bindEntry.appendAttribute( "_weight", string_view{ makeNumberText( bind._weight ) } );
                    }
                }
            }

            /** @brief 스프링 본(콜라이더 · 콜라이더 그룹 · 스프링)을 @p root 에 씁니다. 그룹과 스프링은 앞 목록의 번호로 가리킵니다. */
            static void writeSprings( const XMLNode& root, const VrmAvatarDef& def )
            {
                {
                    const XMLNode list = root.appendChild( "_listCollider" );
                    for ( const VrmSpringCollider& collider : def._listCollider )
                    {
                        const XMLNode entry = list.appendChild( "VrmSpringCollider" );
                        entry.appendAttribute( "_bone", string_view{ collider._bone } );
                        entry.appendAttribute( "_bCapsule", makeBoolName( collider._bCapsule == SW_TRUE ) );
                        entry.appendAttribute( "_radius", string_view{ makeNumberText( collider._radius ) } );
                        entry.appendAttribute( "_offset", string_view{ makeVectorText( collider._offset ) } );
                        entry.appendAttribute( "_tail", string_view{ makeVectorText( collider._tail ) } );
                    }
                }
                {
                    const XMLNode list = root.appendChild( "_listColliderGroup" );
                    for ( const VrmSpringColliderGroup& group : def._listColliderGroup )
                    {
                        const XMLNode entry = list.appendChild( "VrmSpringColliderGroup" );
                        entry.appendAttribute( "_name", string_view{ group._name } );
                        writeIndices( entry.appendChild( "_listCollider" ), group._listCollider );
                    }
                }
                {
                    const XMLNode list = root.appendChild( "_listSpring" );
                    for ( const VrmSpring& spring : def._listSpring )
                    {
                        const XMLNode entry = list.appendChild( "VrmSpring" );
                        entry.appendAttribute( "_name", string_view{ spring._name } );
                        entry.appendAttribute( "_centerBone", string_view{ spring._centerBone } );
                        writeIndices( entry.appendChild( "_listColliderGroup" ), spring._listColliderGroup );
                        const XMLNode listJoint = entry.appendChild( "_listJoint" );
                        for ( const VrmSpringJoint& joint : spring._listJoint )
                        {
                            const XMLNode jointEntry = listJoint.appendChild( "VrmSpringJoint" );
                            jointEntry.appendAttribute( "_bone", string_view{ joint._bone } );
                            jointEntry.appendAttribute( "_hitRadius", string_view{ makeNumberText( joint._hitRadius ) } );
                            jointEntry.appendAttribute( "_stiffness", string_view{ makeNumberText( joint._stiffness ) } );
                            jointEntry.appendAttribute( "_gravityPower", string_view{ makeNumberText( joint._gravityPower ) } );
                            jointEntry.appendAttribute( "_gravityDirection", string_view{ makeVectorText( joint._gravityDirection ) } );
                            jointEntry.appendAttribute( "_dragForce", string_view{ makeNumberText( joint._dragForce ) } );
                        }
                    }
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    string VrmAvatarDef::findBone( string_view humanBone ) const
    {
        for ( const VrmHumanBone& bone : _listHumanBone )
        {
            if ( bone._humanBone == humanBone )
                return bone._bone;
        }
        return {};
    }

    bool VrmAvatarImporter::readVrm0Avatar( const JSONValue& vrm, const VrmGltfNames& names, VrmAvatarDef& outDef, vector<string>* pOutListIgnored, string& outError )
    {
        using Internal             = VrmAvatarImporterInternal;
        outDef                     = VrmAvatarDef{};
        outDef._sourceMajorVersion = 0;
        Internal::collectUnknownKeys( vrm, Internal::kArrVrm0RootKey, "VRM", pOutListIgnored );
        const JSONValue humanoid = vrm.get( "humanoid", false );
        if ( humanoid.isObject() && Internal::readVrm0Humanoid( humanoid, names, outDef, pOutListIgnored, outError ) == false )
            return false;
        const JSONValue master = vrm.get( "blendShapeMaster", false );
        if ( master.isObject() && Internal::readVrm0Expressions( master, names, outDef, pOutListIgnored, outError ) == false )
            return false;
        const JSONValue secondary = vrm.get( "secondaryAnimation", false );
        if ( secondary.isObject() && Internal::readVrm0Springs( secondary, names, outDef, pOutListIgnored, outError ) == false )
            return false;
        const JSONValue firstPerson = vrm.get( "firstPerson", false );
        if ( firstPerson.isObject() )
            return Internal::readVrm0FirstPerson( firstPerson, names, outDef, pOutListIgnored, outError );
        outDef._firstPersonBone = outDef.findBone( "head" );
        return true;
    }

    bool VrmAvatarImporter::readVrm1Avatar( const JSONValue& vrmcVrm, const JSONValue& vrmcSpringBone, const VrmGltfNames& names, VrmAvatarDef& outDef,
                                            vector<string>* pOutListIgnored, string& outError )
    {
        using Internal             = VrmAvatarImporterInternal;
        outDef                     = VrmAvatarDef{};
        outDef._sourceMajorVersion = 1;
        Internal::collectUnknownKeys( vrmcVrm, Internal::kArrVrm1RootKey, "VRMC_vrm", pOutListIgnored );

        const JSONValue humanoid = vrmcVrm.get( "humanoid", false );
        Internal::collectUnknownKeys( humanoid, Internal::kArrVrm1HumanoidKey, "VRMC_vrm.humanoid", pOutListIgnored );
        const JSONValue humanBones = humanoid.get( "humanBones", false );
        for ( const string& humanBone : humanBones.getMemberNames() )
        {
            if ( VectorUtil::contains( Internal::kArrHumanBone, humanBone ) == false )
            {
                outError = "VRM 1.0 humanoid: unknown human bone '" + humanBone + "'";
                return false;
            }
            const JSONValue entry = humanBones.get( humanBone, false );
            Internal::collectUnknownKeys( entry, Internal::kArrVrm1NodeKey, "VRMC_vrm.humanoid.humanBones." + humanBone, pOutListIgnored );
            VrmHumanBone bone;
            bone._humanBone = humanBone;
            if ( Internal::resolveNode( names, entry.get( "node", false ), "VRM 1.0 humanoid bone '" + humanBone + "'", bone._bone, outError ) == false )
                return false;
            outDef._listHumanBone.push_back( std::move( bone ) );
        }

        const JSONValue expressions = vrmcVrm.get( "expressions", false );
        Internal::collectUnknownKeys( expressions, Internal::kArrVrm1ExpressionsKey, "VRMC_vrm.expressions", pOutListIgnored );
        const JSONValue preset = expressions.get( "preset", false );
        for ( const string& name : preset.getMemberNames() )
        {
            if ( VectorUtil::contains( Internal::kArrVrm1Preset, name ) == false )
            {
                outError = "VRM 1.0 expressions: unknown preset '" + name + "'";
                return false;
            }
            if ( Internal::readVrm1Expression( preset.get( name, false ), name, true, names, outDef, pOutListIgnored, outError ) == false )
                return false;
        }
        const JSONValue custom = expressions.get( "custom", false );
        for ( const string& name : custom.getMemberNames() )
        {
            if ( Internal::readVrm1Expression( custom.get( name, false ), name, false, names, outDef, pOutListIgnored, outError ) == false )
                return false;
        }

        const JSONValue firstPerson = vrmcVrm.get( "firstPerson", false );
        Internal::collectUnknownKeys( firstPerson, Internal::kArrVrm1FirstPersonKey, "VRMC_vrm.firstPerson", pOutListIgnored );
        const JSONValue listAnnotation = firstPerson.get( "meshAnnotations", false );
        for ( size_t index = 0; index < listAnnotation.size(); ++index )
        {
            const JSONValue annotation = listAnnotation.at( index );
            Internal::collectUnknownKeys( annotation, Internal::kArrVrm1AnnotationKey, "VRMC_vrm.firstPerson.meshAnnotations", pOutListIgnored );
            uint32 node{ 0 };
            if ( Internal::readIndex( annotation.get( "node", false ), names._listNodeMesh.size(), "VRM 1.0 meshAnnotations", "node", "is not a node of the model", node, outError ) == false )
                return false;
            const int64 nodeMesh = names._listNodeMesh[node];
            if ( nodeMesh < 0 )
            {
                outError = "VRM 1.0 meshAnnotations: node " + to_string( node ) + " has no mesh";
                return false;
            }
            VrmFirstPersonMesh mesh;
            mesh._mesh        = Internal::makeMeshName( names, static_cast<uint32>( nodeMesh ) );
            const string type = annotation.get( "type", false ).asString();
            if ( Internal::parseFirstPersonMode( type, mesh._mode ) == false )
            {
                outError = "VRM 1.0 meshAnnotations: unknown type '" + type + "'";
                return false;
            }
            outDef._listFirstPersonMesh.push_back( std::move( mesh ) );
        }

        // 1.0 의 1인칭 본은 humanoid head 이고, 눈 위치는 lookAt.offsetFromHeadBone 이다.
        const JSONValue lookAt = vrmcVrm.get( "lookAt", false );
        Internal::collectUnknownKeys( lookAt, Internal::kArrVrm1LookAtKey, "VRMC_vrm.lookAt", pOutListIgnored );
        outDef._firstPersonBone = outDef.findBone( "head" );
        float3 offset{};
        if ( Internal::readVector( lookAt.get( "offsetFromHeadBone", false ), "VRM 1.0 lookAt.offsetFromHeadBone", offset, outError ) == false )
            return false;
        outDef._firstPersonOffset = Internal::convertVrm1Vector( offset );
        if ( lookAt.has( "type", false ) )
            Internal::addIgnored( pOutListIgnored, "VRMC_vrm.lookAt" );

        if ( vrmcSpringBone.isObject() )
            return Internal::readVrm1Springs( vrmcSpringBone, names, outDef, pOutListIgnored, outError );
        return true;
    }

    string VrmAvatarImporter::makeAvatarXML( const VrmAvatarDef& def )
    {
        using Internal = VrmAvatarImporterInternal;
        XMLDocument   document;
        const XMLNode root = document.appendRoot( "VrmAvatar" );
        root.appendAttribute( "_version", kVersion );
        root.appendAttribute( "_sourceMajorVersion", static_cast<uint32>( def._sourceMajorVersion ) );
        root.appendAttribute( "_firstPersonBone", string_view{ def._firstPersonBone } );
        root.appendAttribute( "_firstPersonOffset", string_view{ Internal::makeVectorText( def._firstPersonOffset ) } );
        {
            // humanoid 이름은 속성 이름이다(`hips="J_Bip_C_Hips"`) — 이름이 값마다 다른 맵이라 칸 이름을 고정할 수 없다.
            const XMLNode map = root.appendChild( "_mapHumanBone" );
            for ( const VrmHumanBone& bone : def._listHumanBone )
            {
                map.appendAttribute( bone._humanBone.c_str(), string_view{ bone._bone } );
            }
        }
        {
            const XMLNode list = root.appendChild( "_listFirstPersonMesh" );
            for ( const VrmFirstPersonMesh& mesh : def._listFirstPersonMesh )
            {
                const XMLNode entry = list.appendChild( "VrmFirstPersonMesh" );
                entry.appendAttribute( "_mesh", string_view{ mesh._mesh } );
                entry.appendAttribute( "_mode", Internal::makeFirstPersonModeName( mesh._mode ) );
            }
        }
        Internal::writeExpressions( root, def );
        Internal::writeSprings( root, def );
        return document.saveToString();
    }
} // namespace sw::editor
