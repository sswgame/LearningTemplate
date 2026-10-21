#include "pch.h"

#include "Editor/Common/Asset/VrmRuntimeAssets.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Asset/VrmAvatarImporter.h"

#include "Engine/Animation/Rig/RigSpringChain.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @struct VrmRuntimeAssetsInternal
         * @brief 사슬 끊기, 물성 옮기기, JSON 쓰기를 담당합니다.
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

            static void writeFloat3( const JSONValue& target, const float3& value )
            {
                target.setArray();
                target.pushBack().setFloat( static_cast<float64>( value._x ) );
                target.pushBack().setFloat( static_cast<float64>( value._y ) );
                target.pushBack().setFloat( static_cast<float64>( value._z ) );
            }

            static const VrmExpression* findExpression( const VrmAvatarDef& def, string_view name )
            {
                for ( const VrmExpression& expression : def._listExpression )
                {
                    if ( expression._name == name )
                        return &expression;
                }
                return nullptr;
            }

            static void writePose( const JSONValue& entry, string_view name, const VrmExpression* pExpression )
            {
                entry.setObject();
                entry.set( "_name", false ).setString( name );
                if ( pExpression == nullptr || pExpression->_listBind.empty() )
                    return;
                const JSONValue list = entry.set( "_listTarget", false );
                list.setArray();
                for ( const VrmExpressionBind& bind : pExpression->_listBind )
                {
                    const JSONValue target = list.pushBack();
                    target.setObject();
                    target.set( "_target", false ).setString( bind._morphTarget );
                    target.set( "_weight", false ).setFloat( static_cast<float64>( bind._weight ) );
                }
            }

            /** @brief 스프링 하나의 관절을 스켈레톤 부모를 따라 사슬로 끊습니다(VrmRuntimeAssets::makeSpringRigJSON 의 규칙). */
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
                        // 가지 — 앞 사슬을 끝내고 이 관절의 부모(고정된 뿌리)에서 새 사슬을 시작한다.
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

    string VrmRuntimeAssets::makeFacialRigJSON( const VrmAvatarDef& def, vector<string>* pOutListIgnored )
    {
        using Internal = VrmRuntimeAssetsInternal;
        JSONDocument    document;
        const JSONValue root = document.makeObject();

        const JSONValue listExpression = root.set( "_listExpression", false );
        listExpression.setArray();
        for ( const VrmExpression& expression : def._listExpression )
        {
            Internal::writePose( listExpression.pushBack(), expression._name, &expression );
            const bool bOverrides = expression._overrideBlink != VrmExpressionOverride::None || expression._overrideLookAt != VrmExpressionOverride::None ||
                                    expression._overrideMouth != VrmExpressionOverride::None;
            if ( bOverrides )
                Internal::addIgnored( pOutListIgnored, "expression '" + expression._name + "' override (blink, lookAt, mouth)" );
            if ( expression._bBinary != SW_FALSE )
                Internal::addIgnored( pOutListIgnored, "expression '" + expression._name + "' isBinary" );
        }

        // 비즘 — 무음 하나와 VRM 입 모양 셋. 립싱크 표의 이름이다.
        const JSONValue listViseme = root.set( "_listViseme", false );
        listViseme.setArray();
        Internal::writePose( listViseme.pushBack(), kSilenceViseme, nullptr );
        for ( const VisemeMapping& mapping : kArrVisemeMapping )
        {
            const VrmExpression* pExpression = Internal::findExpression( def, mapping._vrmPreset );
            if ( pExpression != nullptr )
                Internal::writePose( listViseme.pushBack(), mapping._viseme, pExpression );
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
            const JSONValue blink = root.set( "_blink", false );
            blink.setObject();
            const JSONValue list = blink.set( "_listTarget", false );
            list.setArray();
            for ( const string& target : listBlinkTarget )
            {
                list.pushBack().setString( target );
            }
        }
        return document.dump( 1 );
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

    string VrmRuntimeAssets::makeSpringRigJSON( const VrmAvatarDef& def, const Skeleton& skeleton, vector<string>* pOutListIgnored )
    {
        using Internal = VrmRuntimeAssetsInternal;
        vector<Internal::SpringChainSpec> listChain;
        for ( uint32 springIndex = 0; springIndex < static_cast<uint32>( def._listSpring.size() ); ++springIndex )
        {
            Internal::splitSpring( def, springIndex, skeleton, listChain, pOutListIgnored );
        }

        // 물성 옮기기의 스텝은 리그 사슬의 고정 스텝 기본값 하나다(RigSpringSettings).
        const float32   step = RigSpringSettings{}._fixedStep;
        JSONDocument    document;
        const JSONValue root  = document.makeObject();
        const JSONValue nodes = root.set( "nodes", false );
        nodes.setArray();
        uint32 previousSpring = MathUtil::kMaxUInt32;
        uint32 partIndex      = 0;
        for ( const Internal::SpringChainSpec& chain : listChain )
        {
            const VrmSpring& spring = def._listSpring[chain._springIndex];
            partIndex               = ( chain._springIndex == previousSpring ) ? partIndex + 1 : 0;
            previousSpring          = chain._springIndex;

            // 물성은 첫 관절의 값이다 — 관절마다 다르면 무시 목록에 남긴다.
            const VrmSpringJoint& first = spring._listJoint.front();
            float32               particleRadius{ 0.0f };
            for ( const VrmSpringJoint& joint : spring._listJoint )
            {
                particleRadius        = MathUtil::max( particleRadius, joint._hitRadius );
                const bool bDifferent = MathUtil::abs( joint._stiffness - first._stiffness ) > 1e-4f || MathUtil::abs( joint._dragForce - first._dragForce ) > 1e-4f ||
                                        MathUtil::abs( joint._gravityPower - first._gravityPower ) > 1e-4f;
                if ( bDifferent )
                    Internal::addIgnored( pOutListIgnored, "springBone '" + spring._name + "' per-joint values (the first joint is used)" );
            }
            if ( spring._centerBone.empty() == false )
                Internal::addIgnored( pOutListIgnored, "springBone '" + spring._name + "' center" );

            const JSONValue node = nodes.pushBack();
            node.setObject();
            node.set( "type", false ).setString( "SpringChain" );
            node.set( "name", false ).setString( "Spring" + to_string( chain._springIndex ) + ( partIndex > 0 ? "_" + to_string( partIndex ) : string{} ) );
            const JSONValue bones = node.set( "bones", false );
            bones.setArray();
            for ( const string& bone : chain._listBone )
            {
                bones.pushBack().setString( bone );
            }
            node.set( "stiffness", false ).setFloat( static_cast<float64>( MathUtil::saturate( first._stiffness * step ) ) );
            node.set( "damping", false ).setFloat( static_cast<float64>( MathUtil::saturate( first._dragForce ) ) );
            // VRM 의 중력은 스텝마다 더하는 위치(속도)라 가속도로는 ÷ 스텝이다. 힘이 0 이면 0 벡터로 덮어 월드 중력을 받지 않는다(VRM 과 같다).
            Internal::writeFloat3( node.set( "gravity", false ), first._gravityDirection * ( first._gravityPower / step ) );
            node.set( "particle_radius", false ).setFloat( static_cast<float64>( particleRadius ) );

            vector<uint32> listCollider;
            for ( const uint32 groupIndex : spring._listColliderGroup )
            {
                for ( const uint32 colliderIndex : def._listColliderGroup[groupIndex]._listCollider )
                {
                    if ( VectorUtil::contains( listCollider, colliderIndex ) == false )
                        listCollider.push_back( colliderIndex );
                }
            }
            const JSONValue colliders = node.set( "colliders", false );
            colliders.setArray();
            for ( const uint32 colliderIndex : listCollider )
            {
                const VrmSpringCollider& collider = def._listCollider[colliderIndex];
                if ( skeleton.findBoneIndex( hashed_string( collider._bone ) ) < 0 )
                {
                    Internal::addIgnored( pOutListIgnored, "springBone collider on '" + collider._bone + "' (not a skeleton bone)" );
                    continue;
                }
                const JSONValue entry = colliders.pushBack();
                entry.setObject();
                entry.set( "bone", false ).setString( collider._bone );
                entry.set( "shape", false ).setString( collider._bCapsule != SW_FALSE ? "Capsule" : "Sphere" );
                Internal::writeFloat3( entry.set( "a", false ), collider._offset );
                if ( collider._bCapsule != SW_FALSE )
                    Internal::writeFloat3( entry.set( "b", false ), collider._tail );
                entry.set( "radius", false ).setFloat( static_cast<float64>( collider._radius ) );
            }
        }
        return document.dump( 1 );
    }
} // namespace sw::editor
