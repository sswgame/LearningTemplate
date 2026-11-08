#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/BodyChannel.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    float32 BodyChannelLayerDef::applyTo( float32 below ) const
    {
        switch ( _blend )
        {
            case BodyChannelBlend::Override:
                return _value;
            case BodyChannelBlend::Additive:
                return below + _value;
            case BodyChannelBlend::Max:
                return MathUtil::max( below, _value );
        }
        return _value;
    }

    void BodyChannelLayerDef::blendLayers( span<const BodyChannelLayerDef> listLayer, vector<BodyChannelValue>& inoutListValue )
    {
        // 안정 정렬 — 같은 우선순위는 적힌 순서를 지킨다(결정적).
        vector<const BodyChannelLayerDef*> listSorted;
        listSorted.reserve( listLayer.size() );
        for ( const BodyChannelLayerDef& layer : listLayer )
            listSorted.push_back( &layer );
        std::stable_sort( listSorted.begin(), listSorted.end(), []( const BodyChannelLayerDef* pLhs, const BodyChannelLayerDef* pRhs )
        { return pLhs->_priority < pRhs->_priority; } );
        for ( const BodyChannelLayerDef* pLayer : listSorted )
        {
            BodyChannelValue* pValue = VectorUtil::findBy<&BodyChannelValue::_channel>( inoutListValue, pLayer->_name );
            if ( pValue == nullptr )
            {
                inoutListValue.push_back( BodyChannelValue{ pLayer->_name, 0.0f } );
                pValue = &inoutListValue.back();
            }
            pValue->_value = pLayer->applyTo( pValue->_value );
        }
    }
} // namespace sw
