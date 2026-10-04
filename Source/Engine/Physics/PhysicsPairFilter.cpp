#include "pch.h"

#include "Engine/Physics/PhysicsPairFilter.h"

namespace sw
{
    namespace
    {
        struct PhysicsPairFilterInternal
        {
            static void removeValue( vector<uint64>& inoutListValue, uint64 value )
            {
                for ( size_t valueIndex = 0; valueIndex < inoutListValue.size(); ++valueIndex )
                {
                    if ( inoutListValue[valueIndex] != value )
                        continue;
                    inoutListValue[valueIndex] = inoutListValue.back();
                    inoutListValue.pop_back();
                    return;
                }
            }

            static bool containsValue( const vector<uint64>& listValue, uint64 value )
            {
                for ( const uint64 item : listValue )
                {
                    if ( item == value )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PhysicsPairFilter::PhysicsPairFilter()
        : _mapBodyToDisabled{}
    {
    }

    void PhysicsPairFilter::setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide )
    {
        if ( bodyA == bodyB || bodyA.isValid() == false || bodyB.isValid() == false )
            return;
        const uint64 keyA = bodyA.packed();
        const uint64 keyB = bodyB.packed();
        if ( bCollide )
        {
            unordered_map<uint64, vector<uint64>>::iterator iterA = _mapBodyToDisabled.find( keyA );
            if ( iterA != _mapBodyToDisabled.end() )
            {
                PhysicsPairFilterInternal::removeValue( iterA->second, keyB );
                if ( iterA->second.empty() )
                    _mapBodyToDisabled.erase( iterA );
            }
            unordered_map<uint64, vector<uint64>>::iterator iterB = _mapBodyToDisabled.find( keyB );
            if ( iterB != _mapBodyToDisabled.end() )
            {
                PhysicsPairFilterInternal::removeValue( iterB->second, keyA );
                if ( iterB->second.empty() )
                    _mapBodyToDisabled.erase( iterB );
            }
            return;
        }
        vector<uint64>& listA = _mapBodyToDisabled[keyA];
        if ( PhysicsPairFilterInternal::containsValue( listA, keyB ) == false )
            listA.push_back( keyB );
        vector<uint64>& listB = _mapBodyToDisabled[keyB];
        if ( PhysicsPairFilterInternal::containsValue( listB, keyA ) == false )
            listB.push_back( keyA );
    }

    bool PhysicsPairFilter::canCollide( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB ) const
    {
        if ( _mapBodyToDisabled.empty() )
            return true;
        unordered_map<uint64, vector<uint64>>::const_iterator iter = _mapBodyToDisabled.find( bodyA.packed() );
        if ( iter == _mapBodyToDisabled.end() )
            return true;
        return PhysicsPairFilterInternal::containsValue( iter->second, bodyB.packed() ) == false;
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
                PhysicsPairFilterInternal::removeValue( iterOther->second, body.packed() );
                if ( iterOther->second.empty() )
                    _mapBodyToDisabled.erase( iterOther );
            }
        }
    }
} // namespace sw
