#include "pch.h"

#include "GameFramework/Base/Gameplay/Progression/LevelProgress.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "LevelProgress" );

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
            SW_LOG_ERROR( "ExperienceCurve: refused formula base %# maxLevel %# - base must not be negative and maxLevel must be at least 1", base, maxLevel );
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
        return loadFromNode( root, sourceName );
    }

    bool ExperienceCurve::loadFromNode( const XMLNode& node, string_view sourceName )
    {
        // 표(`<Level xp>`)가 있으면 표, 없으면 공식이다. 표의 xp 는 필수이고 1 이상, 공식의 빠진 칸은 생성자의 초기값이다.
        // 깨진 값 · 범위 밖 값은 오류이고 곡선을 바꾸지 않는다(자르지 않는다).
        bool          bValid = true;
        vector<int64> listXPToNext;
        for ( XMLNode child = node.findChild( "Level" ); child; child = child.findNextSibling( "Level" ) )
        {
            const std::optional<int32> xp = child.readRequiredAttribute<int32>( "xp" );
            if ( xp.has_value() && *xp >= 1 )
            {
                listXPToNext.push_back( *xp );
                continue;
            }
            if ( xp.has_value() )
                SW_LOG_ERROR( "%#: <Level xp=\"%#\"> must be at least 1", sourceName, *xp );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        if ( listXPToNext.empty() == false )
        {
            setTable( listXPToNext );
            return true;
        }
        float32 base     = _base;
        float32 exponent = _exponent;
        float32 linear   = _linear;
        int32   maxLevel = _maxLevel;
        bValid           = XMLReadUtil::readOptionalNumber( node, "base", sourceName, 0.0f, MathUtil::kMaxFloat, base );
        bValid           = XMLReadUtil::readOptionalNumber( node, "exponent", sourceName, -MathUtil::kMaxFloat, MathUtil::kMaxFloat, exponent ) && bValid;
        bValid           = XMLReadUtil::readOptionalNumber( node, "linear", sourceName, -MathUtil::kMaxFloat, MathUtil::kMaxFloat, linear ) && bValid;
        bValid           = XMLReadUtil::readOptionalNumber( node, "maxLevel", sourceName, 1, MathUtil::kMaxInt32, maxLevel ) && bValid;
        return bValid && setFormula( base, exponent, linear, maxLevel );
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

    void LevelProgress::writeState( Archive& outArchive ) const
    {
        outArchive << _xp;
        outArchive << _totalXP;
        outArchive << _level;
    }

    bool LevelProgress::readState( Archive& archive )
    {
        int64 xp      = 0;
        int64 totalXP = 0;
        int32 level   = 1;
        archive >> xp;
        archive >> totalXP;
        archive >> level;
        const bool bValid = archive.isOk() && 0 <= xp && xp <= totalXP && 1 <= level;
        if ( bValid == false )
            return false;
        _xp      = xp;
        _totalXP = totalXP;
        _level   = level;
        return true;
    }
} // namespace sw
