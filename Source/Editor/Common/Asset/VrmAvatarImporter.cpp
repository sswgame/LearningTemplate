/**
 * @file VrmAvatarImporter.cpp
 * @brief VRM 아바타 정보(humanoid · 표정 · 스프링 본 · 1인칭) 읽기와 `.avatar.json` 쓰기입니다.
 * @details 0.x 와 1.0 의 대응:
 *          - humanoid: 0.x `humanBones[] { bone, node }` · 1.0 `humanBones { <이름>: { node } }`. 0.x 엄지 `ThumbProximal` · `ThumbIntermediate` 는 1.0 의
 *            `ThumbMetacarpal` · `ThumbProximal` 이다. 유니티 Mecanim 근육 값(armStretch, min/max 등)은 엔진에 대응이 없어 옮기지 않는다.
 *          - 표정: 0.x `blendShapeGroups[] { presetName, binds[] { mesh, index, weight 0..100 } }` · 1.0 `expressions { preset, custom }`
 *            `{ morphTargetBinds[] { node, index, weight 0..1 } }`. 머티리얼 색 · 텍스처 표정은 옮기지 않는다(무시 목록).
 *          - 스프링 본: 0.x `secondaryAnimation.boneGroups`(뿌리 본 + 그룹 값) · 1.0 `VRMC_springBone.springs`(관절마다 값). 0.x 의 키 `stiffiness` 는 원본 철자다.
 *          - 1인칭: 0.x `firstPerson { firstPersonBone, firstPersonBoneOffset, meshAnnotations[] { mesh, firstPersonFlag } }` ·
 *            1.0 `firstPerson.meshAnnotations[] { node, type }` 와 `lookAt.offsetFromHeadBone`(본은 humanoid head). 시선(lookAt)의 나머지는 옮기지 않는다.
 */
#include "pch.h"

#include "Editor/Common/Asset/VrmAvatarImporter.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

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

            template <size_t N>
            static bool contains( const string_view ( &arrName )[N], string_view name )
            {
                for ( const string_view candidate : arrName )
                {
                    if ( candidate == name )
                        return true;
                }
                return false;
            }

            /** @brief @p object 의 키 가운데 @p arrKnown 과 공통 키가 아닌 것을 `<context>.<키>` 로 무시 목록에 모읍니다. */
            template <size_t N>
            static void collectUnknownKeys( const JSONValue& object, const string_view ( &arrKnown )[N], string_view context, vector<string>* pOutListIgnored )
            {
                if ( pOutListIgnored == nullptr || object.isObject() == false )
                    return;
                for ( const string& key : object.getMemberNames() )
                {
                    if ( contains( arrKnown, key ) || contains( kArrCommonKey, key ) )
                        continue;
                    pOutListIgnored->push_back( string( context ) + "." + key + " (unknown key)" );
                }
            }

            /** @brief 지원하지 않는 값의 경로를 무시 목록에 한 번만 넣습니다. */
            static void addIgnored( vector<string>* pOutListIgnored, const string& path )
            {
                if ( pOutListIgnored == nullptr )
                    return;
                if ( std::find( pOutListIgnored->begin(), pOutListIgnored->end(), path ) == pOutListIgnored->end() )
                    pOutListIgnored->push_back( path );
            }

            /** @brief 0.x 벡터 `{ x, y, z }` 나 1.0 벡터 `[ x, y, z ]` 를 읽습니다. 없으면 @p fallback 입니다. */
            static float3 readVector( const JSONValue& value, const float3& fallback )
            {
                if ( value.isObject() )
                    return float3{ readFloat( value, "x", fallback._x ), readFloat( value, "y", fallback._y ), readFloat( value, "z", fallback._z ) };
                if ( value.isArray() && value.size() >= 3 )
                {
                    return float3{ static_cast<float32>( value.at( 0 ).asFloat() ), static_cast<float32>( value.at( 1 ).asFloat() ),
                                   static_cast<float32>( value.at( 2 ).asFloat() ) };
                }
                return fallback;
            }

            /** @brief 0.x 확장의 벡터(유니티 공간)를 엔진 공간으로 옮깁니다. z 를 뒤집어 glTF 로, X 를 뒤집어 엔진으로 간다. */
            static float3 convertVrm0Vector( const float3& value ) { return float3{ -value._x, value._y, -value._z }; }
            /** @brief 1.0 확장의 벡터(glTF 공간)를 엔진 공간으로 옮깁니다. */
            static float3 convertVrm1Vector( const float3& value ) { return float3{ -value._x, value._y, value._z }; }

            static float32 readFloat( const JSONValue& object, string_view key, float32 fallback )
            {
                return static_cast<float32>( object.get( key, false ).asFloat( static_cast<float64>( fallback ) ) );
            }

            /** @brief 노드 번호를 본 이름으로 풉니다. 범위 밖이면 false 와 이유입니다. */
            static bool resolveNode( const VrmGltfNames& names, const JSONValue& value, string_view context, string& outName, string& outError )
            {
                const int64 node                  = value.asInt( -1 );
                const bool  bInvalidNodeReference = value.isNumber() == false || node < 0 || static_cast<uint64>( node ) >= names._listNodeName.size();
                if ( bInvalidNodeReference )
                {
                    outError = string( context ) + ": node " + value.dump() + " is not a node of the model";
                    return false;
                }
                outName = names._listNodeName[static_cast<size_t>( node )];
                return true;
            }

            /** @brief 메시 번호 · 타깃 번호를 모프 타깃 이름으로 풉니다. */
            static bool resolveMorphTarget( const VrmGltfNames& names, int64 mesh, int64 index, string_view context, string& outName, string& outError )
            {
                if ( mesh < 0 || static_cast<uint64>( mesh ) >= names._listMeshMorphName.size() )
                {
                    outError = string( context ) + ": mesh " + to_string( mesh ) + " is not a mesh of the model";
                    return false;
                }
                const vector<string>& listMorph = names._listMeshMorphName[static_cast<size_t>( mesh )];
                if ( index < 0 || static_cast<uint64>( index ) >= listMorph.size() )
                {
                    outError = string( context ) + ": morph target " + to_string( index ) + " is not a target of mesh " + to_string( mesh );
                    return false;
                }
                outName = listMorph[static_cast<size_t>( index )];
                return true;
            }

            static string makeMeshName( const VrmGltfNames& names, int64 mesh )
            {
                const bool bHasMeshName = 0 <= mesh && static_cast<uint64>( mesh ) < names._listMeshName.size() && names._listMeshName[static_cast<size_t>( mesh )].empty() == false;
                if ( bHasMeshName )
                    return names._listMeshName[static_cast<size_t>( mesh )];
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

            /** @brief @p root 와 그 자손을 깊이 우선 순서로 모읍니다(0.x 스프링 뿌리 아래 흔들리는 본들). 순환은 노드 수에서 끊습니다. */
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

            static void writeNumber( const JSONValue& target, float32 value )
            {
                target.setFloat( static_cast<float64>( std::llround( static_cast<float64>( value ) * kWrittenPrecision ) ) / kWrittenPrecision );
            }

            static void writeVector( const JSONValue& target, const float3& value )
            {
                target.setArray();
                writeNumber( target.pushBack(), value._x );
                writeNumber( target.pushBack(), value._y );
                writeNumber( target.pushBack(), value._z );
            }

            static void writeIndices( const JSONValue& target, const vector<uint32>& listIndex )
            {
                target.setArray();
                for ( const uint32 index : listIndex )
                {
                    target.pushBack().setUint( index );
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
                    if ( contains( kArrHumanBone, humanBone ) == false )
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
                    expression._bBinary      = group.get( "isBinary", false ).asBool( false ) ? SW_TRUE : SW_FALSE;
                    const JSONValue listBind = group.get( "binds", false );
                    for ( size_t bindIndex = 0; bindIndex < listBind.size(); ++bindIndex )
                    {
                        const JSONValue bindValue = listBind.at( bindIndex );
                        collectUnknownKeys( bindValue, kArrVrm0BindKey, "VRM.blendShapeMaster.blendShapeGroups.binds", pOutListIgnored );
                        VrmExpressionBind bind;
                        if ( resolveMorphTarget( names, bindValue.get( "mesh", false ).asInt( -1 ), bindValue.get( "index", false ).asInt( -1 ),
                                                 "VRM 0.x blend shape group '" + expression._name + "'", bind._morphTarget, outError ) == false )
                            return false;
                        bind._weight = readFloat( bindValue, "weight", 100.0f ) * kVrm0WeightScale;
                        expression._listBind.push_back( std::move( bind ) );
                    }
                    if ( group.get( "materialValues", false ).size() > 0 )
                        addIgnored( pOutListIgnored, "VRM.blendShapeMaster.blendShapeGroups.materialValues" );
                    inoutDef._listExpression.push_back( std::move( expression ) );
                }
                return true;
            }

            /** @brief 0.x 의 secondaryAnimation 을 읽습니다. 뿌리 본마다 사슬 하나입니다. */
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
                        collider._bone   = bone;
                        collider._offset = convertVrm0Vector( readVector( colliderValue.get( "offset", false ), float3{} ) );
                        collider._tail   = collider._offset;
                        collider._radius = readFloat( colliderValue, "radius", 0.0f );
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
                    // UniVRM 의 기본값이다(키가 없을 때).
                    VrmSpringJoint jointValue;
                    jointValue._stiffness        = readFloat( group, "stiffiness", 1.0f );
                    jointValue._gravityPower     = readFloat( group, "gravityPower", 0.0f );
                    jointValue._gravityDirection = convertVrm0Vector( readVector( group.get( "gravityDir", false ), float3{ 0.0f, -1.0f, 0.0f } ) );
                    jointValue._dragForce        = readFloat( group, "dragForce", 0.4f );
                    jointValue._hitRadius        = readFloat( group, "hitRadius", 0.02f );

                    string          centerBone;
                    const JSONValue center                = group.get( "center", false );
                    const bool      bCenterBoneUnresolved = center.isValid() && center.asInt( -1 ) >= 0 && resolveNode( names, center, context + " center", centerBone, outError ) == false;
                    if ( bCenterBoneUnresolved )
                        return false;
                    vector<uint32>  listColliderGroupIndex;
                    const JSONValue listGroupRef = group.get( "colliderGroups", false );
                    for ( size_t refIndex = 0; refIndex < listGroupRef.size(); ++refIndex )
                    {
                        const int64 ref = listGroupRef.at( refIndex ).asInt( -1 );
                        if ( ref < 0 || static_cast<uint64>( ref ) >= inoutDef._listColliderGroup.size() )
                        {
                            outError = context + ": collider group " + to_string( ref ) + " does not exist";
                            return false;
                        }
                        listColliderGroupIndex.push_back( static_cast<uint32>( ref ) );
                    }

                    const string    comment  = group.get( "comment", false ).asString();
                    const JSONValue listRoot = group.get( "bones", false );
                    for ( size_t rootIndex = 0; rootIndex < listRoot.size(); ++rootIndex )
                    {
                        string rootName;
                        if ( resolveNode( names, listRoot.at( rootIndex ), context + " bone", rootName, outError ) == false )
                            return false;
                        VrmSpring spring;
                        spring._name = comment;
                        if ( spring._name.empty() )
                            spring._name = "spring" + to_string( static_cast<uint64>( groupIndex ) );
                        if ( listRoot.size() > 1 )
                            spring._name += "_" + to_string( static_cast<uint64>( rootIndex ) );
                        spring._centerBone        = centerBone;
                        spring._listColliderGroup = listColliderGroupIndex;
                        collectDescendants( names, static_cast<uint32>( listRoot.at( rootIndex ).asUint() ), listChain );
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
                const JSONValue bone = firstPerson.get( "firstPersonBone", false );
                if ( bone.isValid() && bone.asInt( -1 ) >= 0 )
                {
                    if ( resolveNode( names, bone, "VRM 0.x firstPersonBone", inoutDef._firstPersonBone, outError ) == false )
                        return false;
                }
                else
                {
                    inoutDef._firstPersonBone = inoutDef.findBone( "head" );
                }
                inoutDef._firstPersonOffset    = convertVrm0Vector( readVector( firstPerson.get( "firstPersonBoneOffset", false ), float3{} ) );
                const JSONValue listAnnotation = firstPerson.get( "meshAnnotations", false );
                for ( size_t index = 0; index < listAnnotation.size(); ++index )
                {
                    const JSONValue annotation = listAnnotation.at( index );
                    collectUnknownKeys( annotation, kArrVrm0AnnotationKey, "VRM.firstPerson.meshAnnotations", pOutListIgnored );
                    VrmFirstPersonMesh mesh;
                    mesh._mesh        = makeMeshName( names, annotation.get( "mesh", false ).asInt( -1 ) );
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
                expression._name      = name;
                expression._bPreset   = bPreset ? SW_TRUE : SW_FALSE;
                expression._bBinary   = value.get( "isBinary", false ).asBool( false ) ? SW_TRUE : SW_FALSE;
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
                    const int64       node = bindValue.get( "node", false ).asInt( -1 );
                    const int64       mesh = ( 0 <= node && static_cast<uint64>( node ) < names._listNodeMesh.size() ) ? names._listNodeMesh[static_cast<size_t>( node )] : -1;
                    VrmExpressionBind bind;
                    if ( resolveMorphTarget( names, mesh, bindValue.get( "index", false ).asInt( -1 ), "VRM 1.0 expression '" + name + "' node " + to_string( node ),
                                             bind._morphTarget, outError ) == false )
                        return false;
                    bind._weight = readFloat( bindValue, "weight", 1.0f );
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
                    if ( sphere.isObject() )
                    {
                        collectUnknownKeys( sphere, kArrVrm1SphereKey, "VRMC_springBone.colliders.shape.sphere", pOutListIgnored );
                        collider._offset = convertVrm1Vector( readVector( sphere.get( "offset", false ), float3{} ) );
                        collider._tail   = collider._offset;
                        collider._radius = readFloat( sphere, "radius", 0.0f );
                    }
                    else if ( capsule.isObject() )
                    {
                        collectUnknownKeys( capsule, kArrVrm1CapsuleKey, "VRMC_springBone.colliders.shape.capsule", pOutListIgnored );
                        collider._offset   = convertVrm1Vector( readVector( capsule.get( "offset", false ), float3{} ) );
                        collider._tail     = convertVrm1Vector( readVector( capsule.get( "tail", false ), float3{} ) );
                        collider._radius   = readFloat( capsule, "radius", 0.0f );
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
                        const int64 ref = listColliderRef.at( refIndex ).asInt( -1 );
                        if ( ref < 0 || static_cast<uint64>( ref ) >= inoutDef._listCollider.size() )
                        {
                            outError = "VRM 1.0 collider group '" + group._name + "': collider " + to_string( ref ) + " does not exist";
                            return false;
                        }
                        group._listCollider.push_back( static_cast<uint32>( ref ) );
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
                        const int64 ref = listGroupRef.at( refIndex ).asInt( -1 );
                        if ( ref < 0 || static_cast<uint64>( ref ) >= inoutDef._listColliderGroup.size() )
                        {
                            outError = context + ": collider group " + to_string( ref ) + " does not exist";
                            return false;
                        }
                        spring._listColliderGroup.push_back( static_cast<uint32>( ref ) );
                    }
                    const JSONValue listJoint = springValue.get( "joints", false );
                    for ( size_t jointIndex = 0; jointIndex < listJoint.size(); ++jointIndex )
                    {
                        const JSONValue jointValue = listJoint.at( jointIndex );
                        collectUnknownKeys( jointValue, kArrVrm1JointKey, "VRMC_springBone.springs.joints", pOutListIgnored );
                        VrmSpringJoint joint;
                        if ( resolveNode( names, jointValue.get( "node", false ), context + " joint", joint._bone, outError ) == false )
                            return false;
                        joint._hitRadius        = readFloat( jointValue, "hitRadius", 0.0f );
                        joint._stiffness        = readFloat( jointValue, "stiffness", 1.0f );
                        joint._gravityPower     = readFloat( jointValue, "gravityPower", 0.0f );
                        joint._gravityDirection = convertVrm1Vector( readVector( jointValue.get( "gravityDir", false ), float3{ 0.0f, -1.0f, 0.0f } ) );
                        joint._dragForce        = readFloat( jointValue, "dragForce", 0.5f );
                        spring._listJoint.push_back( std::move( joint ) );
                    }
                    inoutDef._listSpring.push_back( std::move( spring ) );
                }
                return true;
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
            if ( Internal::contains( Internal::kArrHumanBone, humanBone ) == false )
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
            if ( Internal::contains( Internal::kArrVrm1Preset, name ) == false )
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
            const int64 node = annotation.get( "node", false ).asInt( -1 );
            if ( node < 0 || static_cast<uint64>( node ) >= names._listNodeMesh.size() )
            {
                outError = "VRM 1.0 meshAnnotations: node " + to_string( node ) + " is not a node of the model";
                return false;
            }
            VrmFirstPersonMesh mesh;
            mesh._mesh        = Internal::makeMeshName( names, names._listNodeMesh[static_cast<size_t>( node )] );
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
        outDef._firstPersonBone   = outDef.findBone( "head" );
        outDef._firstPersonOffset = Internal::convertVrm1Vector( Internal::readVector( lookAt.get( "offsetFromHeadBone", false ), float3{} ) );
        if ( lookAt.has( "type", false ) )
            Internal::addIgnored( pOutListIgnored, "VRMC_vrm.lookAt" );

        if ( vrmcSpringBone.isObject() )
            return Internal::readVrm1Springs( vrmcSpringBone, names, outDef, pOutListIgnored, outError );
        return true;
    }

    string VrmAvatarImporter::makeAvatarJSON( const VrmAvatarDef& def )
    {
        using Internal = VrmAvatarImporterInternal;
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        root.set( "_version", false ).setUint( kVersion );
        root.set( "_sourceMajorVersion", false ).setUint( def._sourceMajorVersion );
        {
            const JSONValue map = root.set( "_mapHumanBone", false );
            map.setObject();
            for ( const VrmHumanBone& bone : def._listHumanBone )
            {
                map.set( bone._humanBone, false ).setString( bone._bone );
            }
        }
        root.set( "_firstPersonBone", false ).setString( def._firstPersonBone );
        Internal::writeVector( root.set( "_firstPersonOffset", false ), def._firstPersonOffset );
        {
            const JSONValue list = root.set( "_listFirstPersonMesh", false );
            list.setArray();
            for ( const VrmFirstPersonMesh& mesh : def._listFirstPersonMesh )
            {
                const JSONValue entry = list.pushBack();
                entry.setObject();
                entry.set( "_mesh", false ).setString( mesh._mesh );
                entry.set( "_mode", false ).setString( Internal::makeFirstPersonModeName( mesh._mode ) );
            }
        }
        {
            const JSONValue list = root.set( "_listExpression", false );
            list.setArray();
            for ( const VrmExpression& expression : def._listExpression )
            {
                const JSONValue entry = list.pushBack();
                entry.setObject();
                entry.set( "_name", false ).setString( expression._name );
                entry.set( "_bPreset", false ).setBool( expression._bPreset == SW_TRUE );
                entry.set( "_bBinary", false ).setBool( expression._bBinary == SW_TRUE );
                entry.set( "_overrideBlink", false ).setString( Internal::makeOverrideName( expression._overrideBlink ) );
                entry.set( "_overrideLookAt", false ).setString( Internal::makeOverrideName( expression._overrideLookAt ) );
                entry.set( "_overrideMouth", false ).setString( Internal::makeOverrideName( expression._overrideMouth ) );
                const JSONValue listBind = entry.set( "_listBind", false );
                listBind.setArray();
                for ( const VrmExpressionBind& bind : expression._listBind )
                {
                    const JSONValue bindEntry = listBind.pushBack();
                    bindEntry.setObject();
                    bindEntry.set( "_morphTarget", false ).setString( bind._morphTarget );
                    Internal::writeNumber( bindEntry.set( "_weight", false ), bind._weight );
                }
            }
        }
        {
            const JSONValue list = root.set( "_listCollider", false );
            list.setArray();
            for ( const VrmSpringCollider& collider : def._listCollider )
            {
                const JSONValue entry = list.pushBack();
                entry.setObject();
                entry.set( "_bone", false ).setString( collider._bone );
                entry.set( "_bCapsule", false ).setBool( collider._bCapsule == SW_TRUE );
                Internal::writeNumber( entry.set( "_radius", false ), collider._radius );
                Internal::writeVector( entry.set( "_offset", false ), collider._offset );
                Internal::writeVector( entry.set( "_tail", false ), collider._tail );
            }
        }
        {
            const JSONValue list = root.set( "_listColliderGroup", false );
            list.setArray();
            for ( const VrmSpringColliderGroup& group : def._listColliderGroup )
            {
                const JSONValue entry = list.pushBack();
                entry.setObject();
                entry.set( "_name", false ).setString( group._name );
                Internal::writeIndices( entry.set( "_listCollider", false ), group._listCollider );
            }
        }
        {
            const JSONValue list = root.set( "_listSpring", false );
            list.setArray();
            for ( const VrmSpring& spring : def._listSpring )
            {
                const JSONValue entry = list.pushBack();
                entry.setObject();
                entry.set( "_name", false ).setString( spring._name );
                entry.set( "_centerBone", false ).setString( spring._centerBone );
                Internal::writeIndices( entry.set( "_listColliderGroup", false ), spring._listColliderGroup );
                const JSONValue listJoint = entry.set( "_listJoint", false );
                listJoint.setArray();
                for ( const VrmSpringJoint& joint : spring._listJoint )
                {
                    const JSONValue jointEntry = listJoint.pushBack();
                    jointEntry.setObject();
                    jointEntry.set( "_bone", false ).setString( joint._bone );
                    Internal::writeNumber( jointEntry.set( "_hitRadius", false ), joint._hitRadius );
                    Internal::writeNumber( jointEntry.set( "_stiffness", false ), joint._stiffness );
                    Internal::writeNumber( jointEntry.set( "_gravityPower", false ), joint._gravityPower );
                    Internal::writeVector( jointEntry.set( "_gravityDirection", false ), joint._gravityDirection );
                    Internal::writeNumber( jointEntry.set( "_dragForce", false ), joint._dragForce );
                }
            }
        }
        return document.dump( 1 );
    }
} // namespace sw::editor
