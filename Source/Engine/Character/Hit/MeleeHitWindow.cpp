#include "pch.h"

#include "Engine/Character/Hit/MeleeHitWindow.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        /** @brief 토막 하나를 쓸어 새로 걸린 오브젝트를 맞음으로 붙입니다(전달은 하지 않는다). */
        void sweepMeleeSegment( GameObject& attacker, const float3& from, const float3& to, const MeleeHitParams& params, vector<GameObjectHandle>& inoutListHitTarget,
                                vector<CharacterRayHit>& inoutListRayHit, vector<MeleeHit>& outListHit )
        {
            inoutListRayHit.clear();
            const GameObjectManager& manager = *attacker.getManager();
            if ( CharacterHitUtil::sweepAll( manager, from, to, params._radius, params._layerMask, attacker.getObjectID(), params._bIs2D, inoutListRayHit ) == 0 )
                return;
            for ( const CharacterRayHit& rayHit : inoutListRayHit )
            {
                GameObject* pTarget = rayHit._pObject;
                if ( pTarget == nullptr )
                    continue;
                // 공격자 계층(든 무기, 판정 자식)은 맞지 않는다. 바디 필터는 공격자 자신의 바디만 거르므로 계층은 여기서 거른다.
                const GameObjectHandle target         = pTarget->getHandle();
                const bool             bSkipCandidate = pTarget->isDescendantOf( &attacker ) || VectorUtil::contains( inoutListHitTarget, target );
                if ( bSkipCandidate )
                    continue;
                inoutListHitTarget.push_back( target );
                MeleeHit& melee  = outListHit.emplace_back();
                melee._target    = target;
                melee._hit       = CharacterHitUtil::makeHitInfo( rayHit, &attacker, to - from, params._damage, params._impulse );
                melee._hit._kind = params._kind;
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    MeleeHitAuthority MeleeHitWindowUtil::getProcessAuthority()
    {
        const GameNetSession* pSession = engine::findSubsystem<GameNetSession>();
        return pSession != nullptr && pSession->getRole() == GameNetRole::Client ? MeleeHitAuthority::Predicted : MeleeHitAuthority::Authority;
    }

    void MeleeHitWindowUtil::makeBlade( const float3& pointA, const float3& pointB, uint32 sampleCount, vector<float3>& outListPoint )
    {
        SW_ASSERT( sampleCount >= 2 );
        outListPoint.resize( sampleCount );
        for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
        {
            outListPoint[sampleIndex] = float3::lerp( pointA, pointB, static_cast<float32>( sampleIndex ) / static_cast<float32>( sampleCount - 1 ) );
        }
    }

    uint32 MeleeHitWindowUtil::sweep( GameObject& attacker, vector_reference<const float3> listPrevious, vector_reference<const float3> listCurrent, const MeleeHitParams& params,
                                      MeleeHitAuthority authority, vector<GameObjectHandle>& inoutListHitTarget, vector<MeleeHit>& outListHit )
    {
        GameObjectManager* pManager = attacker.getManager();
        if ( pManager == nullptr )
            return 0;
        const size_t            first = outListHit.size();
        vector<CharacterRayHit> inoutListRayHit;
        if ( listPrevious.size() == listCurrent.size() )
        {
            for ( size_t pointIndex = 0; pointIndex < listCurrent.size(); ++pointIndex )
            {
                sweepMeleeSegment( attacker, listPrevious[pointIndex], listCurrent[pointIndex], params, inoutListHitTarget, inoutListRayHit, outListHit );
            }
        }
        if ( listCurrent.size() >= 2 )
            sweepMeleeSegment( attacker, listCurrent.front(), listCurrent.back(), params, inoutListHitTarget, inoutListRayHit, outListHit );

        // 서버 권위: 클라이언트는 찾기만 한다. 전달은 모든 토막을 쓴 뒤에 한다(받는 쪽이 오브젝트를 만들고 지워도 된다).
        if ( authority == MeleeHitAuthority::Authority )
        {
            for ( size_t hitIndex = first; hitIndex < outListHit.size(); ++hitIndex )
            {
                GameObject* pTarget = pManager->resolveGameObject( outListHit[hitIndex]._target );
                if ( pTarget != nullptr )
                    CharacterHitUtil::deliverHit( *pTarget, outListHit[hitIndex]._hit );
            }
        }
        return static_cast<uint32>( outListHit.size() - first );
    }
} // namespace sw
