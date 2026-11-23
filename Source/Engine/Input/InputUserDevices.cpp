#include "pch.h"

#include "Engine/Input/InputUserDevices.h"

namespace sw
{
    void InputUserDevices::pairDevices( vector<InputUserDevices>& inoutListUser, const vector<uint32>& listConnectedPad )
    {
        for ( const uint32 padIndex : listConnectedPad )
        {
            InputUserDevices* pUnpaired = nullptr;
            bool              bTaken    = false;
            for ( size_t index = 0; index < inoutListUser.size(); ++index )
            {
                InputUserDevices& user = inoutListUser[index];
                if ( user._padIndex.has_value() )
                    bTaken = bTaken || *user._padIndex == padIndex;
                else if ( pUnpaired == nullptr || pUnpaired == &inoutListUser.front() )
                    pUnpaired = &user; // 사용자 0 은 키보드 · 마우스가 있으므로 맨 뒤다
            }
            if ( bTaken == false && pUnpaired != nullptr )
                pUnpaired->_padIndex = padIndex;
        }
    }
} // namespace sw
