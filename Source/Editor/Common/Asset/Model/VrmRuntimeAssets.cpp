#include "pch.h"

#include "Editor/Common/Asset/Model/VrmRuntimeAssets.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Asset/Model/VrmAvatarImporter.h"

#include "Engine/Animation/Rig/RigSpringChain.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @struct VrmRuntimeAssetsInternal
         * @brief 사슬 끊기, 물성 옮기기, XML 쓰기를 담당합니다.
         */
        struct VrmRuntimeAssetsInternal
        {
            /** @brief 사슬 하나와 그 물성의 원본(첫 관절)입니다. */
            struct SpringChainSpec
            {
                vector<string> _listBone;
                uint32         _springIndex{ 0 };
            };

            static void addIgnored( vector<string>* pOutListIgnored, const string& name )
            {
                if ( pOutListIgnored != nullptr && VectorUtil::contains( *pOutListIgnored, name ) == false )
                    pOutListIgnored->push_back( name );
            }

            /** @brief 벡터를 `x,y,z` 글로 만듭니다(후처리 리그 XML 의 벡터 표기). */
            static string makeFloat3Text( const float3& value ) { return XMLReadUtil::makeFloatListText( &value._x, 3 ); }

            static const VrmExpression* findExpression( const VrmAvatarDef& def, string_view name )
            {
                for ( const VrmExpression& expression : def._listExpression )
                {
                    if ( expression._name == name )
                        return &expression;
                }
                return nullptr;
            }

            static void writePose( const XMLNode& entry, string_view name, const VrmExpression* pExpression )
            {
                entry.appendAttribute( "_name", name );
                const XMLNode list = entry.appendChild( "_listTarget" );
                if ( pExpression == nullptr )
                    return;
                for ( const VrmExpressionBind& bind : pExpression->_listBind )
                {
                    const XMLNode target = list.appendChild( "FacialTargetWeight" );
                    target.appendAttribute( "_target", string_view{ bind._morphTarget } );
                    target.appendAttribute( "_weight", bind._weight );
                }
            }

            /** @brief 사슬 본 @p bone 의 관절입니다. 없으면(가지 사슬 루트가 이 스프링 밖일 때) 끝 본 @p tail 의 관절입니다 — 사슬은 관절 본으로만 만들어져 둘 중 하나는 있다. */
            static const VrmSpringJoint* findJoint( const VrmSpring& spring, const string& bone, const string& tail )
            {
                const VrmSpringJoint* pTail = nullptr;
                for ( const VrmSpringJoint& joint : spring._listJoint )
                {
                    if ( joint._bone == bone )
                        return &joint;
                    if ( joint._bone == tail )
                        pTail = &joint;
                }
                return pTail;
            }

            /** @brief 스프링 하나의 관절을 스켈레톤 부모를 따라 사슬로 끊습니다(VrmRuntimeAssets::makeSpringRigXML 의 규칙). */
            static void splitSpring( const VrmAvatarDef& def, uint32 springIndex, const Skeleton& skeleton, vector<SpringChainSpec>& inoutListChain,
                                     vector<string>* pOutListIgnored )
            {
                const VrmSpring& spring = def._listSpring[springIndex];
                SpringChainSpec  current;
                current._springIndex = springIndex;
                auto flush           = [&]()
                {
                    if ( current._listBone.size() >= 2 )
                        inoutListChain.push_back( current );
                    else if ( current._listBone.empty() == false )
                        addIgnored( pOutListIgnored, "springBone chain of one bone '" + current._listBone.front() + "'" );
                    current._listBone.clear();
                };
                int32 lastBone = -1;
                for ( const VrmSpringJoint& joint : spring._listJoint )
                {
                    const int32 bone = skeleton.findBoneIndex( hashed_string( joint._bone ) );
                    if ( bone < 0 )
                    {
                        addIgnored( pOutListIgnored, "springBone joint '" + joint._bone + "' (not a skeleton bone)" );
                        continue;
                    }
                    const int32 parent = skeleton.getBone( static_cast<uint32>( bone ) )._parentIndex;
                    if ( current._listBone.empty() == false && parent != lastBone )
                    {
                        // 가지 — 앞 사슬을 끝내고 이 관절의 부모(고정된 루트)에서 새 사슬을 시작한다.
                        flush();
                        if ( parent >= 0 )
                            current._listBone.push_back( string( skeleton.getBone( static_cast<uint32>( parent ) )._name.c_str() ) );
                    }
                    current._listBone.push_back( joint._bone );
                    lastBone = bone;
                }
                flush();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool VrmRuntimeAssets::hasFacialRig( const VrmAvatarDef& def )
    {
        return def._listExpression.empty() == false;
    }

    string VrmRuntimeAssets::makeFacialRigXML( const VrmAvatarDef& def, vector<string>* pOutListIgnored )
    {
        using Internal = VrmRuntimeAssetsInternal;
        XMLDocument   document;
        const XMLNode root = document.appendRoot( "FacialRig" );

        const XMLNode listExpression = root.appendChild( "_listExpression" );
        for ( const VrmExpression& expression : def._listExpression )
        {
            Internal::writePose( listExpression.appendChild( "FacialPose" ), expression._name, &expression );
            const bool bOverrides = expression._overrideBlink != VrmExpressionOverride::None || expression._overrideLookAt != VrmExpressionOverride::None ||
                                    expression._overrideMouth != VrmExpressionOverride::None;
            if ( bOverrides )
                Internal::addIgnored( pOutListIgnored, "expression '" + expression._name + "' override (blink, lookAt, mouth)" );
            if ( expression._bBinary != SW_FALSE )
                Internal::addIgnored( pOutListIgnored, "expression '" + expression._name + "' isBinary" );
        }

        // 비즘 — 무음 하나와 VRM 입 모양 셋. 립싱크 표의 이름이다.
        const XMLNode listViseme = root.appendChild( "_listViseme" );
        Internal::writePose( listViseme.appendChild( "FacialPose" ), kSilenceViseme, nullptr );
        for ( const VisemeMapping& mapping : kArrVisemeMapping )
        {
            const VrmExpression* pExpression = Internal::findExpression( def, mapping._vrmPreset );
            if ( pExpression != nullptr )
                Internal::writePose( listViseme.appendChild( "FacialPose" ), mapping._viseme, pExpression );
        }

        // 깜빡임 — blink, 없으면 blinkLeft + blinkRight 의 타깃. 얼굴 리그의 깜빡임 타깃은 가중치 1 이다.
        vector<string> listBlinkTarget;
        auto           collectBlink = [&]( string_view name )
        {
            const VrmExpression* pExpression = Internal::findExpression( def, name );
            if ( pExpression == nullptr )
                return;
            for ( const VrmExpressionBind& bind : pExpression->_listBind )
            {
                if ( VectorUtil::contains( listBlinkTarget, bind._morphTarget ) == false )
                    listBlinkTarget.push_back( bind._morphTarget );
                if ( MathUtil::abs( bind._weight - 1.0f ) > 1e-4f )
                    Internal::addIgnored( pOutListIgnored, "expression '" + string( name ) + "' weights (blink targets are weight 1)" );
            }
        };
        collectBlink( "blink" );
        if ( listBlinkTarget.empty() )
        {
            collectBlink( "blinkLeft" );
            collectBlink( "blinkRight" );
        }
        if ( listBlinkTarget.empty() == false )
        {
            const XMLNode blink = root.appendChild( "_blink" );
            const XMLNode list  = blink.appendChild( "_listTarget" );
            for ( const string& target : listBlinkTarget )
            {
                (void)list.appendChild( "item", string_view{ target } );
            }
        }
        return document.saveToString();
    }

    bool VrmRuntimeAssets::hasSpringRig( const VrmAvatarDef& def, const Skeleton& skeleton )
    {
        vector<VrmRuntimeAssetsInternal::SpringChainSpec> listChain;
        for ( uint32 springIndex = 0; springIndex < static_cast<uint32>( def._listSpring.size() ); ++springIndex )
        {
            VrmRuntimeAssetsInternal::splitSpring( def, springIndex, skeleton, listChain, nullptr );
        }
        return listChain.empty() == false;
    }

    string VrmRuntimeAssets::makeSpringRigXML( const VrmAvatarDef& def, const Skeleton& skeleton, vector<string>* pOutListIgnored )
    {
        using Internal = VrmRuntimeAssetsInternal;
        vector<Internal::SpringChainSpec> listChain;
        for ( uint32 springIndex = 0; springIndex < static_cast<uint32>( def._listSpring.size() ); ++springIndex )
        {
            Internal::splitSpring( def, springIndex, skeleton, listChain, pOutListIgnored );
        }

        // 물성 옮기기의 스텝은 리그 사슬의 고정 스텝 기본값 하나다(RigSpringSettings).
        const float32 step = RigSpringSettings{}._fixedStep;
        XMLDocument   document;
        const XMLNode root           = document.appendRoot( "RigAsset" );
        const XMLNode nodes          = root.appendChild( "nodes" );
        uint32        previousSpring = MathUtil::kMaxUInt32;
        uint32        partIndex      = 0;
        for ( const Internal::SpringChainSpec& chain : listChain )
        {
            const VrmSpring& spring = def._listSpring[chain._springIndex];
            partIndex               = ( chain._springIndex == previousSpring ) ? partIndex + 1 : 0;
            previousSpring          = chain._springIndex;

            if ( spring._centerBone.empty() == false )
                Internal::addIgnored( pOutListIgnored, "springBone '" + spring._name + "' center" );

            const XMLNode node = nodes.appendChild( "item" );
            node.appendAttribute( "type", "SpringChain" );
            node.appendAttribute( "name", string_view{ "Spring" + to_string( chain._springIndex ) + ( partIndex > 0 ? "_" + to_string( partIndex ) : string{} ) } );
            // VRM(three-vrm VRMSpringBoneJoint.update) 의 한 프레임: 다음 끝 = 끝 + (끝 − 이전 끝) × (1 − dragForce) + 뼈 방향 × stiffness × dt + gravityDir × gravityPower × dt,
            // 그 뒤 길이를 맞추고 충돌체(hitRadius + 반지름)에서 밀어낸다. 리그 사슬의 고정 스텝과 항마다 같다 — 감쇠 = dragForce, axis_pull = stiffness(m/s),
            // 중력 가속도 = gravityDir × gravityPower ÷ 스텝(스텝마다 가속도 × 스텝² = gravityPower × 스텝), 비율 당김(stiffness) 은 0, 반지름 = hitRadius.
            // 관절 j(본 j, 끝은 본 j + 1)의 값이 입자 j + 1 이다 — 사슬 본 k 의 관절 값이 joints[k] 다. 가지에서 새로 시작한 사슬의 루트(부모 본)는 이 스프링의 관절이다.
            const VrmSpringJoint& first = *Internal::findJoint( spring, chain._listBone[0], chain._listBone[1] );
            node.appendAttribute( "stiffness", 0.0f );
            node.appendAttribute( "axis_pull", first._stiffness );
            node.appendAttribute( "damping", MathUtil::saturate( first._dragForce ) );
            node.appendAttribute( "gravity", string_view{ Internal::makeFloat3Text( first._gravityDirection * ( first._gravityPower / step ) ) } );
            node.appendAttribute( "particle_radius", first._hitRadius );
            const XMLNode bones = node.appendChild( "bones" );
            for ( const string& bone : chain._listBone )
            {
                (void)bones.appendChild( "item", string_view{ bone } );
            }
            const XMLNode joints = node.appendChild( "joints" );
            for ( size_t boneIndex = 0; boneIndex + 1 < chain._listBone.size(); ++boneIndex )
            {
                const VrmSpringJoint& joint = *Internal::findJoint( spring, chain._listBone[boneIndex], chain._listBone[boneIndex + 1] );
                const XMLNode         entry = joints.appendChild( "item" );
                entry.appendAttribute( "stiffness", 0.0f );
                entry.appendAttribute( "axis_pull", joint._stiffness );
                entry.appendAttribute( "damping", MathUtil::saturate( joint._dragForce ) );
                entry.appendAttribute( "gravity", string_view{ Internal::makeFloat3Text( joint._gravityDirection * ( joint._gravityPower / step ) ) } );
                entry.appendAttribute( "radius", joint._hitRadius );
            }

            vector<uint32> listCollider;
            for ( const uint32 groupIndex : spring._listColliderGroup )
            {
                for ( const uint32 colliderIndex : def._listColliderGroup[groupIndex]._listCollider )
                {
                    if ( VectorUtil::contains( listCollider, colliderIndex ) == false )
                        listCollider.push_back( colliderIndex );
                }
            }
            const XMLNode colliders = node.appendChild( "colliders" );
            for ( const uint32 colliderIndex : listCollider )
            {
                const VrmSpringCollider& collider = def._listCollider[colliderIndex];
                if ( skeleton.findBoneIndex( hashed_string( collider._bone ) ) < 0 )
                {
                    Internal::addIgnored( pOutListIgnored, "springBone collider on '" + collider._bone + "' (not a skeleton bone)" );
                    continue;
                }
                const XMLNode entry = colliders.appendChild( "item" );
                entry.appendAttribute( "bone", string_view{ collider._bone } );
                entry.appendAttribute( "shape", collider._bCapsule != SW_FALSE ? "Capsule" : "Sphere" );
                entry.appendAttribute( "a", string_view{ Internal::makeFloat3Text( collider._offset ) } );
                if ( collider._bCapsule != SW_FALSE )
                    entry.appendAttribute( "b", string_view{ Internal::makeFloat3Text( collider._tail ) } );
                entry.appendAttribute( "radius", collider._radius );
            }
        }
        return document.saveToString();
    }
} // namespace sw::editor
