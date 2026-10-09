#include "pch.h"

#include "Engine/UI/Animation/UiStyleTransition.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/UI/Animation/UiAnimatedProperty.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Style/UiStylePass.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    namespace
    {
        struct UiStyleTransitionInternal
        {
            static constexpr utf8 kAllName[] = "all";

            /** @brief 칸의 성분 수입니다(보간되지 않는 칸은 0). */
            static uint32 findComponentCount( UiStyleField field )
            {
                switch ( field )
                {
                    case UiStyleField::BackgroundColor:
                    case UiStyleField::CornerRadius:
                    case UiStyleField::BorderColor:
                    case UiStyleField::ShadowColor:
                    case UiStyleField::Padding:
                    case UiStyleField::TextColor:
                    case UiStyleField::TextOutlineColor:
                    case UiStyleField::FocusRingColor:
                        return 4;
                    case UiStyleField::ShadowOffset:
                        return 2;
                    case UiStyleField::BorderWidth:
                    case UiStyleField::ShadowBlur:
                    case UiStyleField::FontSize:
                    case UiStyleField::TextOutlineWidth:
                    case UiStyleField::Opacity:
                        return 1;
                    case UiStyleField::Font:
                    case UiStyleField::Transition:
                    case UiStyleField::Count:
                        return 0;
                }
                return 0;
            }

            static void readFloat4( const float4& value, float32 ( &outArr )[4] )
            {
                outArr[0] = value._x;
                outArr[1] = value._y;
                outArr[2] = value._z;
                outArr[3] = value._w;
            }

            /** @brief 칸의 값을 성분으로 읽습니다. */
            static void readField( const WidgetStyle& style, UiStyleField field, float32 ( &outArr )[4] )
            {
                switch ( field )
                {
                    case UiStyleField::BackgroundColor:
                    {
                        readFloat4( style._backgroundColor, outArr );
                        break;
                    }
                    case UiStyleField::CornerRadius:
                    {
                        readFloat4( style._cornerRadius, outArr );
                        break;
                    }
                    case UiStyleField::BorderColor:
                    {
                        readFloat4( style._borderColor, outArr );
                        break;
                    }
                    case UiStyleField::ShadowColor:
                    {
                        readFloat4( style._shadowColor, outArr );
                        break;
                    }
                    case UiStyleField::Padding:
                    {
                        readFloat4( style._padding, outArr );
                        break;
                    }
                    case UiStyleField::TextColor:
                    {
                        readFloat4( style._textColor, outArr );
                        break;
                    }
                    case UiStyleField::TextOutlineColor:
                    {
                        readFloat4( style._textOutlineColor, outArr );
                        break;
                    }
                    case UiStyleField::FocusRingColor:
                    {
                        readFloat4( style._focusRingColor, outArr );
                        break;
                    }
                    case UiStyleField::ShadowOffset:
                    {
                        outArr[0] = style._shadowOffset._x;
                        outArr[1] = style._shadowOffset._y;
                        break;
                    }
                    case UiStyleField::BorderWidth:
                    {
                        outArr[0] = style._borderWidth;
                        break;
                    }
                    case UiStyleField::ShadowBlur:
                    {
                        outArr[0] = style._shadowBlur;
                        break;
                    }
                    case UiStyleField::FontSize:
                    {
                        outArr[0] = style._fontSize;
                        break;
                    }
                    case UiStyleField::TextOutlineWidth:
                    {
                        outArr[0] = style._textOutlineWidth;
                        break;
                    }
                    case UiStyleField::Opacity:
                    {
                        outArr[0] = style._opacity;
                        break;
                    }
                    case UiStyleField::Font:
                    case UiStyleField::Transition:
                    case UiStyleField::Count:
                    {
                        break;
                    }
                }
            }

            /** @brief 성분을 칸에 씁니다. */
            static void writeField( WidgetStyle& style, UiStyleField field, const float32 ( &arr )[4] )
            {
                const float4 value{ arr[0], arr[1], arr[2], arr[3] };
                switch ( field )
                {
                    case UiStyleField::BackgroundColor:
                    {
                        style._backgroundColor = value;
                        break;
                    }
                    case UiStyleField::CornerRadius:
                    {
                        style._cornerRadius = value;
                        break;
                    }
                    case UiStyleField::BorderColor:
                    {
                        style._borderColor = value;
                        break;
                    }
                    case UiStyleField::ShadowColor:
                    {
                        style._shadowColor = value;
                        break;
                    }
                    case UiStyleField::Padding:
                    {
                        style._padding = value;
                        break;
                    }
                    case UiStyleField::TextColor:
                    {
                        style._textColor = value;
                        break;
                    }
                    case UiStyleField::TextOutlineColor:
                    {
                        style._textOutlineColor = value;
                        break;
                    }
                    case UiStyleField::FocusRingColor:
                    {
                        style._focusRingColor = value;
                        break;
                    }
                    case UiStyleField::ShadowOffset:
                    {
                        style._shadowOffset = float2{ arr[0], arr[1] };
                        break;
                    }
                    case UiStyleField::BorderWidth:
                    {
                        style._borderWidth = arr[0];
                        break;
                    }
                    case UiStyleField::ShadowBlur:
                    {
                        style._shadowBlur = arr[0];
                        break;
                    }
                    case UiStyleField::FontSize:
                    {
                        style._fontSize = arr[0];
                        break;
                    }
                    case UiStyleField::TextOutlineWidth:
                    {
                        style._textOutlineWidth = arr[0];
                        break;
                    }
                    case UiStyleField::Opacity:
                    {
                        style._opacity = arr[0];
                        break;
                    }
                    case UiStyleField::Font:
                    case UiStyleField::Transition:
                    case UiStyleField::Count:
                    {
                        break;
                    }
                }
            }

            /** @brief 채널의 지금 값입니다. */
            static void evaluateChannel( const UiStyleTransitionState::Channel& channel, float32 ( &outArr )[4] )
            {
                const float32 weight = channel._duration > 0.0f ? evaluateUiCurve( channel._curve, channel._elapsed / channel._duration ) : 1.0f;
                for ( uint32 index = 0; index < 4; ++index )
                {
                    outArr[index] = MathUtil::lerp( channel._arrFrom[index], channel._arrTo[index], weight );
                }
            }

            /** @brief 항목 하나(`_field 0.2 EaseOut`)를 @p inoutSpec 에 더합니다. */
            [[nodiscard]] static bool parseEntry( string_view item, UiStyleTransitionSpec& inoutSpec, string& outError )
            {
                // 빈 칸으로 나눈 낱말 셋까지 — 칸 · 길이 · 곡선.
                string_view arrToken[4]{};
                uint32      tokenCount = 0;
                size_t      position   = 0;
                while ( position < item.size() )
                {
                    while ( position < item.size() && ( item[position] == ' ' || item[position] == '\t' ) )
                    {
                        ++position;
                    }
                    const size_t begin = position;
                    while ( position < item.size() && item[position] != ' ' && item[position] != '\t' )
                    {
                        ++position;
                    }
                    if ( position == begin )
                        break;
                    if ( tokenCount == 4 )
                    {
                        outError = "transition item '" + string( item ) + "' has more than three words";
                        return false;
                    }
                    arrToken[tokenCount++] = item.substr( begin, position - begin );
                }
                if ( tokenCount < 2 || tokenCount > 3 )
                {
                    outError = "transition item '" + string( item ) + "' must be 'field seconds [curve]'";
                    return false;
                }
                UiStyleTransitionSpec::Entry entry{};
                if ( StringUtil::parseFloat( arrToken[1], entry._duration ) == false || entry._duration < 0.0f )
                {
                    outError = "transition item '" + string( item ) + "' has an unreadable duration";
                    return false;
                }
                if ( tokenCount == 3 )
                {
                    BlendCurveSpec      scratch{};
                    const PropertyInfo* pCurve = BlendCurveSpec::StaticType()->findProperty( hashed_string( "_curve" ) );
                    if ( pCurve == nullptr || SerializerUtil::applyPropertyText( *pCurve, &scratch, arrToken[2], SerializeContext::getDefault() ) == false )
                    {
                        outError = "transition item '" + string( item ) + "' has an unknown curve '" + string( arrToken[2] ) + "'";
                        return false;
                    }
                    entry._curve = scratch._curve;
                }
                const bool bAll = StringUtil::equals( arrToken[0], string_view( kAllName ), true );
                if ( bAll == false )
                {
                    if ( UiStyleFieldTable::tryFindField( arrToken[0], entry._field ) == false )
                    {
                        outError = "transition names unknown style property '" + string( arrToken[0] ) + "'";
                        return false;
                    }
                    if ( UiStyleTransitionSpec::isInterpolable( entry._field ) == false )
                    {
                        outError = "style property '" + string( arrToken[0] ) + "' cannot transition (not a number, size or color)";
                        return false;
                    }
                }
                for ( uint32 index = 0; index < static_cast<uint32>( UiStyleField::Count ); ++index )
                {
                    const UiStyleField field = static_cast<UiStyleField>( index );
                    if ( ( bAll && UiStyleTransitionSpec::isInterpolable( field ) ) || ( bAll == false && field == entry._field ) )
                        setEntry( inoutSpec, field, entry );
                }
                return true;
            }

            static void setEntry( UiStyleTransitionSpec& inoutSpec, UiStyleField field, const UiStyleTransitionSpec::Entry& source )
            {
                UiStyleTransitionSpec::Entry entry = source;
                entry._field                       = field;
                for ( UiStyleTransitionSpec::Entry& existing : inoutSpec._listEntry )
                {
                    if ( existing._field == field )
                    {
                        existing = entry;
                        return;
                    }
                }
                inoutSpec._listEntry.push_back( entry );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const UiStyleTransitionSpec::Entry* UiStyleTransitionSpec::findEntry( UiStyleField field ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._field == field )
                return &entry;
        }
        return nullptr;
    }

    bool UiStyleTransitionSpec::parse( string_view text, UiStyleTransitionSpec& outSpec, string& outError )
    {
        outSpec       = UiStyleTransitionSpec{};
        size_t begin  = 0;
        bool   bFirst = true;
        while ( begin <= text.size() )
        {
            const size_t      comma = text.find( ',', begin );
            const string_view item  = StringUtil::trim( text.substr( begin, comma == string_view::npos ? string_view::npos : comma - begin ) );
            if ( item.empty() == false )
            {
                if ( UiStyleTransitionInternal::parseEntry( item, outSpec, outError ) == false )
                    return false;
            }
            else if ( bFirst == false || comma != string_view::npos )
            {
                outError = "transition '" + string( text ) + "' has an empty item";
                return false;
            }
            bFirst = false;
            if ( comma == string_view::npos )
                break;
            begin = comma + 1;
        }
        return true;
    }

    bool UiStyleTransitionSpec::isInterpolable( UiStyleField field )
    {
        return UiStyleTransitionInternal::findComponentCount( field ) > 0;
    }

    void UiStyleTransition::onStyleChanged( Widget& widget, const UiComputedStyle* pOld, const UiComputedStyle* pNew, uint32 changedFields )
    {
        using Internal = UiStyleTransitionInternal;
        // 처음 맞추는 위젯 · 스타일이 사라짐 · 움직임 줄이기 — 전환 없이 바로.
        if ( pOld == nullptr || pNew == nullptr || gv_uiReduceMotion )
        {
            widget._styleTransition.reset();
            return;
        }
        UiStyleTransitionSpec spec{};
        if ( pNew->has( UiStyleField::Transition ) && pNew->_value._transition.empty() == false )
        {
            string error;
            if ( UiStyleTransitionSpec::parse( pNew->_value._transition, spec, error ) == false )
                spec = UiStyleTransitionSpec{}; // 시트 로드가 이미 걸렀다 — 여기서는 조용히 전환 없음
        }

        unique_ptr<UiStyleTransitionState>& state = widget._styleTransition;
        for ( uint32 index = 0; index < static_cast<uint32>( UiStyleField::Count ); ++index )
        {
            const UiStyleField field = static_cast<UiStyleField>( index );
            if ( ( changedFields & UiStyleFieldTable::makeBit( field ) ) == 0 )
                continue;
            // 바뀐 칸 — 옛 채널은 지금 값을 넘겨주고 빠진다.
            float32 arrFrom[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            bool    bHaveFrom = false;
            if ( state != nullptr )
            {
                vector<UiStyleTransitionState::Channel>& listChannel = state->_listChannel;
                for ( size_t channelIndex = 0; channelIndex < listChannel.size(); ++channelIndex )
                {
                    if ( listChannel[channelIndex]._field != field )
                        continue;
                    Internal::evaluateChannel( listChannel[channelIndex], arrFrom );
                    bHaveFrom = true;
                    listChannel.erase( listChannel.begin() + static_cast<ptrdiff_t>( channelIndex ) );
                    break;
                }
            }
            const UiStyleTransitionSpec::Entry* pEntry = spec.findEntry( field );
            if ( pEntry == nullptr || pEntry->_duration <= 0.0f || pOld->has( field ) == false || pNew->has( field ) == false )
                continue; // 적지 않은 칸 · 한쪽만 정한 칸 — 바로
            if ( bHaveFrom == false )
                Internal::readField( pOld->_value, field, arrFrom );
            UiStyleTransitionState::Channel channel{};
            Internal::readField( pNew->_value, field, channel._arrTo );
            bool bSame = true;
            for ( uint32 component = 0; component < 4; ++component )
            {
                channel._arrFrom[component] = arrFrom[component];
                bSame                       = bSame && arrFrom[component] == channel._arrTo[component];
            }
            if ( bSame )
                continue;
            channel._field    = field;
            channel._duration = pEntry->_duration;
            channel._curve    = pEntry->_curve;
            if ( state == nullptr )
                state = make_unique<UiStyleTransitionState>();
            state->_listChannel.push_back( channel );
        }

        if ( state == nullptr )
            return;
        if ( state->_listChannel.empty() )
        {
            state.reset();
            return;
        }
        // 보이는 값 = 새 목표 + 도는 칸의 지금 값. 새로 든 위젯이면 트리의 전환 목록에 올린다.
        state->_shown = *pNew;
        for ( const UiStyleTransitionState::Channel& channel : state->_listChannel )
        {
            float32 arrValue[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            Internal::evaluateChannel( channel, arrValue );
            Internal::writeField( state->_shown._value, channel._field, arrValue );
        }
        WidgetTree* pTree = widget.getTree();
        if ( pTree != nullptr && std::find( pTree->_listStyleTransition.begin(), pTree->_listStyleTransition.end(), widget.getId() ) == pTree->_listStyleTransition.end() )
            pTree->_listStyleTransition.push_back( widget.getId() );
    }

    uint32 UiStyleTransition::update( WidgetTree& tree, float32 deltaSeconds )
    {
        using Internal = UiStyleTransitionInternal;
        if ( tree._listStyleTransition.empty() )
            return 0;
        const bool    bReduceMotion = gv_uiReduceMotion;
        const float32 step          = MathUtil::max( 0.0f, deltaSeconds );
        uint32        updatedCount  = 0;
        for ( size_t index = 0; index < tree._listStyleTransition.size(); )
        {
            Widget* pWidget = tree.findWidgetById( tree._listStyleTransition[index] );
            if ( pWidget == nullptr || pWidget->_styleTransition == nullptr )
            {
                tree._listStyleTransition.erase( tree._listStyleTransition.begin() + static_cast<ptrdiff_t>( index ) );
                continue;
            }
            UiStyleTransitionState& state         = *pWidget->_styleTransition;
            uint32                  changedFields = 0;
            for ( size_t channelIndex = 0; channelIndex < state._listChannel.size(); )
            {
                UiStyleTransitionState::Channel& channel = state._listChannel[channelIndex];
                channel._elapsed                         = bReduceMotion ? channel._duration : channel._elapsed + step;
                const bool bFinished                     = channel._elapsed >= channel._duration;
                float32    arrValue[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                if ( bFinished )
                    std::copy( std::begin( channel._arrTo ), std::end( channel._arrTo ), std::begin( arrValue ) );
                else
                    Internal::evaluateChannel( channel, arrValue );
                Internal::writeField( state._shown._value, channel._field, arrValue );
                changedFields |= UiStyleFieldTable::makeBit( channel._field );
                if ( bFinished )
                    state._listChannel.erase( state._listChannel.begin() + static_cast<ptrdiff_t>( channelIndex ) );
                else
                    ++channelIndex;
            }
            ++updatedCount;
            pWidget->invalidate( UiStylePass::makeDirtyReason( changedFields ) );
            if ( state._listChannel.empty() )
            {
                // 끝 — 보이는 값이 목표와 같으니 위젯은 다시 계산된 스타일(나눠 쓰는 것)을 읽는다.
                pWidget->_styleTransition.reset();
                tree._listStyleTransition.erase( tree._listStyleTransition.begin() + static_cast<ptrdiff_t>( index ) );
                continue;
            }
            ++index;
        }
        return updatedCount;
    }
} // namespace sw
