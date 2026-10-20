#include "pch.h"

#include "Engine/Physics/Collision/PhysicsPairFilter.h"

#include "Core/Container/VectorUtil.h"

namespace sw
{
    PhysicsPairFilter::PhysicsPairFilter()
        : _mapBodyToDisabled{}
    {
    }

    void PhysicsPairFilter::setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide )
    {
        const bool bInvalidPair = bodyA == bodyB || bodyA.isValid() == false || bodyB.isValid() == false;
        if ( bInvalidPair )
            return;
        const uint64 keyA = bodyA.packed();
        const uint64 keyB = bodyB.packed();
        if ( bCollide )
        {
            unordered_map<uint64, vector<uint64>>::iterator iterA = _mapBodyToDisabled.find( keyA );
            if ( iterA != _mapBodyToDisabled.end() )
            {
                (void)VectorUtil::removeSingleSwap( iterA->second, keyB ); // 없으면 지울 것이 없다
                if ( iterA->second.empty() )
                    _mapBodyToDisabled.erase( iterA );
            }
            unordered_map<uint64, vector<uint64>>::iterator iterB = _mapBodyToDisabled.find( keyB );
            if ( iterB != _mapBodyToDisabled.end() )
            {
                (void)VectorUtil::removeSingleSwap( iterB->second, keyA ); // 없으면 지울 것이 없다
                if ( iterB->second.empty() )
                    _mapBodyToDisabled.erase( iterB );
            }
            return;
        }
        vector<uint64>& listA = _mapBodyToDisabled[keyA];
        if ( VectorUtil::contains( listA, keyB ) == false )
            listA.push_back( keyB );
        vector<uint64>& listB = _mapBodyToDisabled[keyB];
        if ( VectorUtil::contains( listB, keyA ) == false )
            listB.push_back( keyA );
    }

    bool PhysicsPairFilter::canCollide( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB ) const
    {
        if ( _mapBodyToDisabled.empty() )
            return true;
        unordered_map<uint64, vector<uint64>>::const_iterator iter = _mapBodyToDisabled.find( bodyA.packed() );
        if ( iter == _mapBodyToDisabled.end() )
            return true;
        return VectorUtil::contains( iter->second, bodyB.packed() ) == false;
    }

    void PhysicsPairFilter::removeBodies( span<const PhysicsBodyHandle> listBody )
    {
        for ( const PhysicsBodyHandle& body : listBody )
        {
            unordered_map<uint64, vector<uint64>>::iterator iter = _mapBodyToDisabled.find( body.packed() );
            if ( iter == _mapBodyToDisabled.end() )
                continue;
            const vector<uint64> listOther = iter->second;
            _mapBodyToDisabled.erase( iter );
            for ( const uint64 other : listOther )
            {
                unordered_map<uint64, vector<uint64>>::iterator iterOther = _mapBodyToDisabled.find( other );
                if ( iterOther == _mapBodyToDisabled.end() )
                    continue;
                (void)VectorUtil::removeSingleSwap( iterOther->second, body.packed() ); // 없으면 지울 것이 없다
                if ( iterOther->second.empty() )
                    _mapBodyToDisabled.erase( iterOther );
            }
        }
    }
} // namespace sw
