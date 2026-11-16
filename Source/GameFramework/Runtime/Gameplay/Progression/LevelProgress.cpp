#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Progression/LevelProgress.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    ExperienceCurve::ExperienceCurve()
        : _listXPToNext{}
        , _base{ 100.0f }
        , _exponent{ 1.5f }
        , _linear{ 0.0f }
        , _maxLevel{ 99 }
    {
    }

    bool ExperienceCurve::setFormula( float32 base, float32 exponent, float32 linear, int32 maxLevel )
    {
        if ( base < 0.0f || maxLevel < 1 )
        {
            SW_LOG_ERROR_ID( "LevelProgress.ExperienceCurveRefusedFormulaBase", "ExperienceCurve: refused formula base %# maxLevel %# - base must not be negative and maxLevel must be at least 1", base, maxLevel );
            return false;
        }
        _listXPToNext.clear();
        _base     = base;
        _exponent = exponent;
        _linear   = linear;
        _maxLevel = maxLevel;
        return true;
    }

    void ExperienceCurve::setTable( const vector<int64>& listXPToNext )
    {
        _listXPToNext = listXPToNext;
        _maxLevel     = static_cast<int32>( listXPToNext.size() ) + 1;
    }

    bool ExperienceCurve::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, "ExperienceCurve", root ) == false )
            return false;
        return loadFromXML( root, sourceName );
    }

    bool ExperienceCurve::loadFromXML( const XMLNode& node, string_view sourceName )
    {
        // 표(`<Level xp>`)가 있으면 표, 없으면 공식이다. 표의 xp 는 필수이고 1 이상, 공식의 빠진 칸은 생성자의 초기값이다.
        // 깨진 값 · 범위 밖 값은 오류이고 곡선을 바꾸지 않는다(자르지 않는다).
        bool          bValid = true;
        vector<int64> listXPToNext;
        for ( XMLNode child = node.findChild( "Level" ); child; child = child.findNextSibling( "Level" ) )
        {
            ExperienceLevelParams level; // xp 필수 · >= 1 은 PROPERTY 가 본다
            if ( XMLFields::readProperties( child, level, sourceName ) )
                listXPToNext.push_back( level._xp );
            else
                bValid = false;
        }
        if ( bValid == false )
            return false;
        if ( listXPToNext.empty() == false )
        {
            setTable( listXPToNext );
            return true;
        }
        ExperienceCurveParams params{ _base, _exponent, _linear, _maxLevel };
        return XMLFields::readProperties( node, params, sourceName ) && setFormula( params._base, params._exponent, params._linear, params._maxLevel );
    }

    int64 ExperienceCurve::getXPToNext( int32 level ) const
    {
        if ( level < 1 || level >= _maxLevel )
            return 0;
        if ( _listXPToNext.empty() == false )
            return _listXPToNext[static_cast<size_t>( level - 1 )];
        const float32 value = _base * MathUtil::pow( static_cast<float32>( level ), _exponent ) + _linear * static_cast<float32>( level );
        return MathUtil::max<int64>( 1, static_cast<int64>( value + 0.5f ) );
    }

    int64 ExperienceCurve::computeTotalXP( int32 level ) const
    {
        int64 total = 0;
        for ( int32 current = 1; current < MathUtil::min( level, _maxLevel ); ++current )
        {
            total += getXPToNext( current );
        }
        return total;
    }

    int32 LevelProgress::addXP( const ExperienceCurve& curve, int64 amount )
    {
        if ( amount <= 0 || _level >= curve.getMaxLevel() )
            return 0;
        _totalXP += amount;
        _xp += amount;
        int32 gained = 0;
        while ( _level < curve.getMaxLevel() && _xp >= curve.getXPToNext( _level ) )
        {
            _xp -= curve.getXPToNext( _level );
            ++_level;
            ++gained;
        }
        if ( _level >= curve.getMaxLevel() )
        {
            _totalXP -= _xp; // 넘친 것은 버린다
            _xp = 0;
        }
        return gained;
    }

    void LevelProgress::setLevel( const ExperienceCurve& curve, int32 level )
    {
        _level   = MathUtil::clamp( level, 1, curve.getMaxLevel() );
        _xp      = 0;
        _totalXP = curve.computeTotalXP( _level );
    }

    float32 LevelProgress::computeRatio( const ExperienceCurve& curve ) const
    {
        const int64 toNext = curve.getXPToNext( _level );
        return toNext <= 0 ? 1.0f : static_cast<float32>( _xp ) / static_cast<float32>( toNext );
    }

    void LevelProgress::serializeState( StateIO& io )
    {
        io.value( _xp );
        io.value( _totalXP );
        io.value( _level );
        io.check( 0 <= _xp && _xp <= _totalXP && 1 <= _level );
    }

} // namespace sw
