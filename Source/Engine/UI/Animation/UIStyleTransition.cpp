#include "pch.h"

#include "Engine/UI/Animation/UIStyleTransition.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/UI/Animation/UIAnimatedProperty.h"
#include "Engine/UI/Base/PanelWidget.h"
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

            /** @brief 필드의 성분 수입니다(보간되지 않는 필드는 0). */
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

            /** @brief 필드의 값을 성분으로 읽습니다. */
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

            /** @brief 성분을 필드에 씁니다. */
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
                const float32 progress = channel._duration > 0.0f ? MathUtil::clamp( ( channel._elapsed - channel._delay ) / channel._duration, 0.0f, 1.0f ) : 1.0f;
                const float32 weight   = evaluateUICurve( channel._curve, progress );
                for ( uint32 index = 0; index < 4; ++index )
                {
                    outArr[index] = MathUtil::lerp( channel._arrFrom[index], channel._arrTo[index], weight );
                }
            }

            /** @brief 채널이 지연과 길이를 모두 지났는지 봅니다(`update` 가 채널을 쓰고, 지우는 판정도 같은 식으로 합니다). */
            [[nodiscard]] static bool isFinished( const UIStyleTransitionState::Channel& channel )
            {
                return channel._elapsed >= channel._delay + channel._duration;
            }

            /** @brief 항목 하나(`_field 0.2 EaseOut`)를 @p inoutSpec 에 더합니다. */
            [[nodiscard]] static bool parseEntry( string_view item, UIStyleTransitionSpec& inoutSpec, string& outError )
            {
                // 공백으로 나눈 단어를 셋까지 읽는다. 필드, 길이, 곡선 순서다.
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
                    while ( true )
                    {
                        const bool bInsideToken = position < item.size() && item[position] != ' ' && item[position] != '\t';
                        if ( bInsideToken == false )
                            break;
                        ++position;
                    }
                    if ( position == begin )
                        break;
                    if ( tokenCount == 4 )
                    {
                        outError = "transition item '" + string( item ) + "' has more than four words";
                        return false;
                    }
                    arrToken[tokenCount++] = item.substr( begin, position - begin );
                }
                if ( tokenCount < 2 )
                {
                    outError = "transition item '" + string( item ) + "' must be 'field seconds [curve] [delay]'";
                    return false;
                }
                UIStyleTransitionSpec::Entry entry{};
                if ( StringUtil::parseFloat( arrToken[1], entry._duration ) == false || entry._duration < 0.0f )
                {
                    outError = "transition item '" + string( item ) + "' has an unreadable duration";
                    return false;
                }
                // 길이 다음 단어: 숫자면 지연(곡선 없음), 아니면 곡선이다. 곡선 다음 숫자는 지연이다.
                uint32  nextToken = 2;
                float32 delay     = 0.0f;
                if ( nextToken < tokenCount && StringUtil::parseFloat( arrToken[nextToken], delay ) )
                {
                    entry._delay = delay;
                    ++nextToken;
                }
                else if ( nextToken < tokenCount )
                {
                    BlendCurveDef       scratch{};
                    const PropertyInfo* pCurve = BlendCurveDef::StaticType()->findProperty( hashed_string( "_curve" ) );
                    if ( pCurve == nullptr || SerializerUtil::applyPropertyText( *pCurve, MutableObjectAddress{ &scratch }, arrToken[nextToken], SerializeContext::getDefault( SerializePurpose::Authoring ) ) == false )
                    {
                        outError = "transition item '" + string( item ) + "' has an unknown curve '" + string( arrToken[nextToken] ) + "'";
                        return false;
                    }
                    entry._curve = scratch._curve;
                    ++nextToken;
                    if ( nextToken < tokenCount )
                    {
                        if ( StringUtil::parseFloat( arrToken[nextToken], delay ) == false )
                        {
                            outError = "transition item '" + string( item ) + "' has an unreadable delay '" + string( arrToken[nextToken] ) + "'";
                            return false;
                        }
                        entry._delay = delay;
                        ++nextToken;
                    }
                }
                if ( nextToken < tokenCount || entry._delay < 0.0f )
                {
                    outError = "transition item '" + string( item ) + "' must be 'field seconds [curve] [delay]' with a delay of zero or more";
                    return false;
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

            /** @brief 필드 @p field 의 진행 중인 채널이 있으면 그 지금 값을 @p outFrom 에 담고 채널을 뺍니다(새 채널이 그 값에서 이어 간다). */
            [[nodiscard]] static bool takeRunningValue( UIStyleTransitionState* pState, UIStyleField field, float32 ( &outFrom )[4] )
            {
                if ( pState == nullptr )
                    return false;
                vector<UIStyleTransitionState::Channel>& listChannel = pState->_listChannel;
                for ( size_t channelIndex = 0; channelIndex < listChannel.size(); ++channelIndex )
                {
                    if ( listChannel[channelIndex]._field != field )
                        continue;
                    evaluateChannel( listChannel[channelIndex], outFrom );
                    listChannel.erase( listChannel.begin() + static_cast<ptrdiff_t>( channelIndex ) );
                    return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const UIStyleTransitionSpec::Entry* UIStyleTransitionSpec::findEntry( UIStyleField field ) const
    {
        return VectorUtil::findBy<&Entry::_field>( _listEntry, field );
    }

    bool UIStyleTransitionSpec::parse( string_view text, UIStyleTransitionSpec& outSpec, string& outError )
    {
        outSpec                            = UIStyleTransitionSpec{};
        const vector<string_view> listItem = StringUtil::split( text, ",", { ._bTrim = true } );
        // 빈 글 하나(항목 없음)는 받고, 항목이 여럿인데 빈 것이 끼면 오류다.
        if ( listItem.size() == 1 && listItem[0].empty() )
            return true;
        for ( const string_view item : listItem )
        {
            if ( item.empty() )
            {
                outError = "transition '" + string( text ) + "' has an empty item";
                return false;
            }
            if ( UIStyleTransitionInternal::parseEntry( item, outSpec, outError ) == false )
                return false;
        }
        return true;
    }

    bool UIStyleTransitionSpec::isInterpolable( UIStyleField field )
    {
        return UIStyleTransitionInternal::findComponentCount( field ) > 0;
    }

    bool UIStyleTransition::findParentChannel( const Widget& widget, UIStyleField field, const float32 ( &newArr )[4], UIStyleTransitionState::Channel& outChannel )
    {
        const PanelWidget* pParent = widget.getParent();
        if ( pParent == nullptr || pParent->_styleTransition == nullptr )
            return false;
        for ( const UIStyleTransitionState::Channel& channel : pParent->_styleTransition->_listChannel )
        {
            if ( channel._field != field )
                continue;
            const uint32 componentCount = UIStyleTransitionInternal::findComponentCount( field );
            for ( uint32 component = 0; component < componentCount; ++component )
            {
                if ( channel._arrTo[component] != newArr[component] )
                    return false;
            }
            outChannel = channel;
            return true;
        }
        return false;
    }

    void UIStyleTransition::refreshShownStyle( Widget& widget, UIStyleTransitionState& state, const UIComputedStyle& newStyle )
    {
        using Internal = UIStyleTransitionInternal;
        // 보이는 값은 새 목표에 도는 필드의 지금 값을 반영한 것이다. 새로 들어온 위젯이면 트리의 전환 목록에 올린다.
        state._shown = newStyle;
        for ( const UIStyleTransitionState::Channel& channel : state._listChannel )
        {
            float32 arrValue[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            Internal::evaluateChannel( channel, arrValue );
            Internal::writeField( state._shown._value, channel._field, arrValue );
            state._shown._setMask |= UIStyleFieldTable::makeBit( channel._field ); // 새 스타일이 정하지 않은 필드도 옮기는 동안은 정한 것으로 보인다
        }
        WidgetTree* pTree = widget.getTree();
        if ( pTree != nullptr )
            pTree->_uniqueStyleTransition.insert( widget.getID() ); // 이미 있으면 넣지 않는다 — 같은 위젯이 두 번 들어오지 않는다
    }

    bool UIStyleTransition::makeChannel( Widget& widget, const UIComputedStyle& oldStyle, const UIComputedStyle& newStyle, UIStyleField field, const UIStyleTransitionSpec& spec,
                                         uint32 oldStates, uint32 newStates, bool bHaveFrom, float32 ( &inoutFrom )[4], UIStyleTransitionState::Channel& outChannel )
    {
        using Internal                               = UIStyleTransitionInternal;
        const UIStyleTransitionSpec::Entry* pEntry   = spec.findEntry( field );
        const bool                          bOwnSpec = pEntry != nullptr && pEntry->_duration > 0.0f;
        // 새 값: 시트가 정했으면 그 값, 아니면 위젯이 자기 필드로 그리는 값이다(읽지 못하는 위젯은 바로 바뀐다).
        UIStyleTransitionState::Channel channel{};
        if ( newStyle.has( field ) )
            Internal::readField( newStyle._value, field, channel._arrTo );
        else if ( widget.tryGetOwnStyleField( field, newStates, channel._arrTo ) == false )
            return false;
        UIStyleTransitionState::Channel mirrored{};
        const bool                      bMirror = bOwnSpec == false && newStyle.has( field ) && ( UIStyleFieldTable::getFlags( field ) & UIStyleFieldTable::kInherited ) != 0 &&
                             findParentChannel( widget, field, channel._arrTo, mirrored );
        if ( bOwnSpec == false && bMirror == false )
            return false; // 적지 않은 필드는 바로 바꾼다
        if ( bMirror )
        {
            channel = mirrored;
            for ( uint32 component = 0; component < 4; ++component )
            {
                inoutFrom[component] = channel._arrFrom[component];
            }
        }
        else
        {
            // 옛 값: 옛 스타일이 정했으면 그 값, 아니면 위젯이 자기 필드로 그리던 값이다. 진행 중이던 채널의 지금 값이 있으면 그것이 우선이다.
            if ( bHaveFrom == false && oldStyle.has( field ) )
                Internal::readField( oldStyle._value, field, inoutFrom );
            else if ( bHaveFrom == false && widget.tryGetOwnStyleField( field, oldStates, inoutFrom ) == false )
                return false;
            channel._field    = field;
            channel._duration = pEntry->_duration;
            channel._curve    = pEntry->_curve;
            channel._delay    = pEntry->_delay;
        }
        bool bSame = true;
        for ( uint32 component = 0; component < 4; ++component )
        {
            channel._arrFrom[component] = inoutFrom[component];
            bSame                       = bSame && inoutFrom[component] == channel._arrTo[component];
        }
        if ( bSame )
            return false;
        outChannel = channel;
        return true;
    }

    void UIStyleTransition::onStyleChanged( Widget& widget, const UIComputedStyle* pOld, const UIComputedStyle* pNew, uint32 changedFields, uint32 oldStates, uint32 newStates )
    {
        using Internal = UIStyleTransitionInternal;
        // 처음 맞추는 위젯, 스타일이 사라진 경우, 움직임 줄이기는 전환 없이 바로 바꾼다.
        const bool bSnapWithoutTransition = pOld == nullptr || pNew == nullptr || gv_uiReduceMotion;
        if ( bSnapWithoutTransition )
        {
            widget._styleTransition.reset();
            return;
        }
        UIStyleTransitionSpec spec{};
        if ( pNew->has( UIStyleField::Transition ) && pNew->_value._transition.empty() == false )
        {
            string error;
            if ( UIStyleTransitionSpec::parse( pNew->_value._transition, spec, error ) == false )
                spec = UIStyleTransitionSpec{}; // 시트 로드가 이미 걸렀으므로 여기서는 조용히 전환 없음으로 처리한다
        }

        unique_ptr<UIStyleTransitionState>& state = widget._styleTransition;
        for ( uint32 index = 0; index < static_cast<uint32>( UIStyleField::Count ); ++index )
        {
            const UIStyleField field = static_cast<UIStyleField>( index );
            if ( ( changedFields & UIStyleFieldTable::makeBit( field ) ) == 0 )
                continue;
            // 바뀐 필드: 기존 채널은 지금 값을 넘겨주고 빠진다.
            float32                         arrFrom[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            const bool                      bHaveFrom = Internal::takeRunningValue( state.get(), field, arrFrom );
            UIStyleTransitionState::Channel channel{};
            if ( makeChannel( widget, *pOld, *pNew, field, spec, oldStates, newStates, bHaveFrom, arrFrom, channel ) == false )
                continue;
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
        refreshShownStyle( widget, *state, *pNew );
    }

    uint32 UIStyleTransition::update( WidgetTree& tree, float32 deltaSeconds )
    {
        using Internal = UIStyleTransitionInternal;
        if ( tree._uniqueStyleTransition.empty() )
            return 0;
        const bool    bReduceMotion = gv_uiReduceMotion;
        const float32 step          = MathUtil::max( 0.0f, deltaSeconds );
        uint32        updatedCount  = 0;
        // 집합은 지우면 맨 뒤 원소가 그 자리로 와서(순서는 상관없다) 지운 뒤에는 이터레이터를 나아가지 않고 같은 자리를 다시 본다.
        for ( auto iter = tree._uniqueStyleTransition.begin(); iter != tree._uniqueStyleTransition.end(); )
        {
            Widget* pWidget = tree.findWidgetByID( *iter );
            if ( pWidget == nullptr || pWidget->_styleTransition == nullptr )
            {
                iter = tree._uniqueStyleTransition.erase( iter );
                continue;
            }
            UIStyleTransitionState& state         = *pWidget->_styleTransition;
            uint32                  changedFields = 0;
            for ( UIStyleTransitionState::Channel& channel : state._listChannel )
            {
                channel._elapsed = bReduceMotion ? channel._delay + channel._duration : channel._elapsed + step;
                float32 arrValue[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                if ( Internal::isFinished( channel ) )
                    std::copy( std::begin( channel._arrTo ), std::end( channel._arrTo ), std::begin( arrValue ) );
                else
                    Internal::evaluateChannel( channel, arrValue );
                Internal::writeField( state._shown._value, channel._field, arrValue );
                changedFields |= UIStyleFieldTable::makeBit( channel._field );
            }
            VectorUtil::removeIf( state._listChannel, Internal::isFinished ); // 끝난 채널은 한 번에 지운다(남은 순서는 그대로)
            ++updatedCount;
            pWidget->invalidate( UIStylePass::makeDirtyReason( changedFields ) );
            if ( state._listChannel.empty() )
            {
                // 끝: 보이는 값이 목표와 같으므로 위젯은 다시 (공유하는) 계산된 스타일을 읽는다.
                pWidget->_styleTransition.reset();
                iter = tree._uniqueStyleTransition.erase( iter );
                continue;
            }
            ++iter;
        }
        return updatedCount;
    }
} // namespace sw
