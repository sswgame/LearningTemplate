#include "pch.h"

#include "Engine/UI/Animation/UIStyleTransition.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/UI/Animation/UIAnimatedProperty.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Style/UIStylePass.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    namespace
    {
        struct UIStyleTransitionInternal
        {
            static constexpr utf8 kAllName[] = "all";

            /** @brief 칸의 성분 수입니다(보간되지 않는 칸은 0). */
            static uint32 findComponentCount( UIStyleField field )
            {
                switch ( field )
                {
                    case UIStyleField::BackgroundColor:
                    case UIStyleField::CornerRadius:
                    case UIStyleField::BorderColor:
                    case UIStyleField::ShadowColor:
                    case UIStyleField::Padding:
                    case UIStyleField::TextColor:
                    case UIStyleField::TextOutlineColor:
                    case UIStyleField::FocusRingColor:
                        return 4;
                    case UIStyleField::ShadowOffset:
                        return 2;
                    case UIStyleField::BorderWidth:
                    case UIStyleField::ShadowBlur:
                    case UIStyleField::FontSize:
                    case UIStyleField::TextOutlineWidth:
                    case UIStyleField::Opacity:
                        return 1;
                    case UIStyleField::Font:
                    case UIStyleField::Transition:
                    case UIStyleField::Count:
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
            static void readField( const WidgetStyle& style, UIStyleField field, float32 ( &outArr )[4] )
            {
                switch ( field )
                {
                    case UIStyleField::BackgroundColor:
                    {
                        readFloat4( style._backgroundColor, outArr );
                        break;
                    }
                    case UIStyleField::CornerRadius:
                    {
                        readFloat4( style._cornerRadius, outArr );
                        break;
                    }
                    case UIStyleField::BorderColor:
                    {
                        readFloat4( style._borderColor, outArr );
                        break;
                    }
                    case UIStyleField::ShadowColor:
                    {
                        readFloat4( style._shadowColor, outArr );
                        break;
                    }
                    case UIStyleField::Padding:
                    {
                        readFloat4( style._padding, outArr );
                        break;
                    }
                    case UIStyleField::TextColor:
                    {
                        readFloat4( style._textColor, outArr );
                        break;
                    }
                    case UIStyleField::TextOutlineColor:
                    {
                        readFloat4( style._textOutlineColor, outArr );
                        break;
                    }
                    case UIStyleField::FocusRingColor:
                    {
                        readFloat4( style._focusRingColor, outArr );
                        break;
                    }
                    case UIStyleField::ShadowOffset:
                    {
                        outArr[0] = style._shadowOffset._x;
                        outArr[1] = style._shadowOffset._y;
                        break;
                    }
                    case UIStyleField::BorderWidth:
                    {
                        outArr[0] = style._borderWidth;
                        break;
                    }
                    case UIStyleField::ShadowBlur:
                    {
                        outArr[0] = style._shadowBlur;
                        break;
                    }
                    case UIStyleField::FontSize:
                    {
                        outArr[0] = style._fontSize;
                        break;
                    }
                    case UIStyleField::TextOutlineWidth:
                    {
                        outArr[0] = style._textOutlineWidth;
                        break;
                    }
                    case UIStyleField::Opacity:
                    {
                        outArr[0] = style._opacity;
                        break;
                    }
                    case UIStyleField::Font:
                    case UIStyleField::Transition:
                    case UIStyleField::Count:
                    {
                        break;
                    }
                }
            }

            /** @brief 성분을 칸에 씁니다. */
            static void writeField( WidgetStyle& style, UIStyleField field, const float32 ( &arr )[4] )
            {
                const float4 value{ arr[0], arr[1], arr[2], arr[3] };
                switch ( field )
                {
                    case UIStyleField::BackgroundColor:
                    {
                        style._backgroundColor = value;
                        break;
                    }
                    case UIStyleField::CornerRadius:
                    {
                        style._cornerRadius = value;
                        break;
                    }
                    case UIStyleField::BorderColor:
                    {
                        style._borderColor = value;
                        break;
                    }
                    case UIStyleField::ShadowColor:
                    {
                        style._shadowColor = value;
                        break;
                    }
                    case UIStyleField::Padding:
                    {
                        style._padding = value;
                        break;
                    }
                    case UIStyleField::TextColor:
                    {
                        style._textColor = value;
                        break;
                    }
                    case UIStyleField::TextOutlineColor:
                    {
                        style._textOutlineColor = value;
                        break;
                    }
                    case UIStyleField::FocusRingColor:
                    {
                        style._focusRingColor = value;
                        break;
                    }
                    case UIStyleField::ShadowOffset:
                    {
                        style._shadowOffset = float2{ arr[0], arr[1] };
                        break;
                    }
                    case UIStyleField::BorderWidth:
                    {
                        style._borderWidth = arr[0];
                        break;
                    }
                    case UIStyleField::ShadowBlur:
                    {
                        style._shadowBlur = arr[0];
                        break;
                    }
                    case UIStyleField::FontSize:
                    {
                        style._fontSize = arr[0];
                        break;
                    }
                    case UIStyleField::TextOutlineWidth:
                    {
                        style._textOutlineWidth = arr[0];
                        break;
                    }
                    case UIStyleField::Opacity:
                    {
                        style._opacity = arr[0];
                        break;
                    }
                    case UIStyleField::Font:
                    case UIStyleField::Transition:
                    case UIStyleField::Count:
                    {
                        break;
                    }
                }
            }

            /** @brief 채널의 지금 값입니다. */
            static void evaluateChannel( const UIStyleTransitionState::Channel& channel, float32 ( &outArr )[4] )
            {
                const float32 weight = channel._duration > 0.0f ? evaluateUICurve( channel._curve, channel._elapsed / channel._duration ) : 1.0f;
                for ( uint32 index = 0; index < 4; ++index )
                {
                    outArr[index] = MathUtil::lerp( channel._arrFrom[index], channel._arrTo[index], weight );
                }
            }

            /** @brief 항목 하나(`_field 0.2 EaseOut`)를 @p inoutSpec 에 더합니다. */
            [[nodiscard]] static bool parseEntry( string_view item, UIStyleTransitionSpec& inoutSpec, string& outError )
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
                UIStyleTransitionSpec::Entry entry{};
                if ( StringUtil::parseFloat( arrToken[1], entry._duration ) == false || entry._duration < 0.0f )
                {
                    outError = "transition item '" + string( item ) + "' has an unreadable duration";
                    return false;
                }
                if ( tokenCount == 3 )
                {
                    BlendCurveDef       scratch{};
                    const PropertyInfo* pCurve = BlendCurveDef::StaticType()->findProperty( hashed_string( "_curve" ) );
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
                    if ( UIStyleFieldTable::tryFindField( arrToken[0], entry._field ) == false )
                    {
                        outError = "transition names unknown style property '" + string( arrToken[0] ) + "'";
                        return false;
                    }
                    if ( UIStyleTransitionSpec::isInterpolable( entry._field ) == false )
                    {
                        outError = "style property '" + string( arrToken[0] ) + "' cannot transition (not a number, size or color)";
                        return false;
                    }
                }
                for ( uint32 index = 0; index < static_cast<uint32>( UIStyleField::Count ); ++index )
                {
                    const UIStyleField field = static_cast<UIStyleField>( index );
                    if ( ( bAll && UIStyleTransitionSpec::isInterpolable( field ) ) || ( bAll == false && field == entry._field ) )
                        setEntry( inoutSpec, field, entry );
                }
                return true;
            }

            static void setEntry( UIStyleTransitionSpec& inoutSpec, UIStyleField field, const UIStyleTransitionSpec::Entry& source )
            {
                UIStyleTransitionSpec::Entry entry = source;
                entry._field                       = field;
                for ( UIStyleTransitionSpec::Entry& existing : inoutSpec._listEntry )
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
    const UIStyleTransitionSpec::Entry* UIStyleTransitionSpec::findEntry( UIStyleField field ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._field == field )
                return &entry;
        }
        return nullptr;
    }

    bool UIStyleTransitionSpec::parse( string_view text, UIStyleTransitionSpec& outSpec, string& outError )
    {
        outSpec       = UIStyleTransitionSpec{};
        size_t begin  = 0;
        bool   bFirst = true;
        while ( begin <= text.size() )
        {
            const size_t      comma = text.find( ',', begin );
            const string_view item  = StringUtil::trim( text.substr( begin, comma == string_view::npos ? string_view::npos : comma - begin ) );
            if ( item.empty() == false )
            {
                if ( UIStyleTransitionInternal::parseEntry( item, outSpec, outError ) == false )
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

    bool UIStyleTransitionSpec::isInterpolable( UIStyleField field )
    {
        return UIStyleTransitionInternal::findComponentCount( field ) > 0;
    }

    void UIStyleTransition::onStyleChanged( Widget& widget, const UIComputedStyle* pOld, const UIComputedStyle* pNew, uint32 changedFields )
    {
        using Internal = UIStyleTransitionInternal;
        // 처음 맞추는 위젯 · 스타일이 사라짐 · 움직임 줄이기 — 전환 없이 바로.
        if ( pOld == nullptr || pNew == nullptr || gv_uiReduceMotion )
        {
            widget._styleTransition.reset();
            return;
        }
        UIStyleTransitionSpec spec{};
        if ( pNew->has( UIStyleField::Transition ) && pNew->_value._transition.empty() == false )
        {
            string error;
            if ( UIStyleTransitionSpec::parse( pNew->_value._transition, spec, error ) == false )
                spec = UIStyleTransitionSpec{}; // 시트 로드가 이미 걸렀다 — 여기서는 조용히 전환 없음
        }

        unique_ptr<UIStyleTransitionState>& state = widget._styleTransition;
        for ( uint32 index = 0; index < static_cast<uint32>( UIStyleField::Count ); ++index )
        {
            const UIStyleField field = static_cast<UIStyleField>( index );
            if ( ( changedFields & UIStyleFieldTable::makeBit( field ) ) == 0 )
                continue;
            // 바뀐 칸 — 옛 채널은 지금 값을 넘겨주고 빠진다.
            float32 arrFrom[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            bool    bHaveFrom = false;
            if ( state != nullptr )
            {
                vector<UIStyleTransitionState::Channel>& listChannel = state->_listChannel;
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
            const UIStyleTransitionSpec::Entry* pEntry = spec.findEntry( field );
            if ( pEntry == nullptr || pEntry->_duration <= 0.0f || pOld->has( field ) == false || pNew->has( field ) == false )
                continue; // 적지 않은 칸 · 한쪽만 정한 칸 — 바로
            if ( bHaveFrom == false )
                Internal::readField( pOld->_value, field, arrFrom );
            UIStyleTransitionState::Channel channel{};
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
                state = make_unique<UIStyleTransitionState>();
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
        for ( const UIStyleTransitionState::Channel& channel : state->_listChannel )
        {
            float32 arrValue[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            Internal::evaluateChannel( channel, arrValue );
            Internal::writeField( state->_shown._value, channel._field, arrValue );
        }
        WidgetTree* pTree = widget.getTree();
        if ( pTree != nullptr && std::find( pTree->_listStyleTransition.begin(), pTree->_listStyleTransition.end(), widget.getID() ) == pTree->_listStyleTransition.end() )
            pTree->_listStyleTransition.push_back( widget.getID() );
    }

    uint32 UIStyleTransition::update( WidgetTree& tree, float32 deltaSeconds )
    {
        using Internal = UIStyleTransitionInternal;
        if ( tree._listStyleTransition.empty() )
            return 0;
        const bool    bReduceMotion = gv_uiReduceMotion;
        const float32 step          = MathUtil::max( 0.0f, deltaSeconds );
        uint32        updatedCount  = 0;
        for ( size_t index = 0; index < tree._listStyleTransition.size(); )
        {
            Widget* pWidget = tree.findWidgetByID( tree._listStyleTransition[index] );
            if ( pWidget == nullptr || pWidget->_styleTransition == nullptr )
            {
                tree._listStyleTransition.erase( tree._listStyleTransition.begin() + static_cast<ptrdiff_t>( index ) );
                continue;
            }
            UIStyleTransitionState& state         = *pWidget->_styleTransition;
            uint32                  changedFields = 0;
            for ( size_t channelIndex = 0; channelIndex < state._listChannel.size(); )
            {
                UIStyleTransitionState::Channel& channel = state._listChannel[channelIndex];
                channel._elapsed                         = bReduceMotion ? channel._duration : channel._elapsed + step;
                const bool bFinished                     = channel._elapsed >= channel._duration;
                float32    arrValue[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                if ( bFinished )
                    std::copy( std::begin( channel._arrTo ), std::end( channel._arrTo ), std::begin( arrValue ) );
                else
                    Internal::evaluateChannel( channel, arrValue );
                Internal::writeField( state._shown._value, channel._field, arrValue );
                changedFields |= UIStyleFieldTable::makeBit( channel._field );
                if ( bFinished )
                    state._listChannel.erase( state._listChannel.begin() + static_cast<ptrdiff_t>( channelIndex ) );
                else
                    ++channelIndex;
            }
            ++updatedCount;
            pWidget->invalidate( UIStylePass::makeDirtyReason( changedFields ) );
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
