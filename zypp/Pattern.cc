/*---------------------------------------------------------------------\
|                          ____ _   __ __ ___                          |
|                         |__  / \ / / . \ . \                         |
|                           / / \ V /|  _/  _/                         |
|                          / /__ | | | | | |                           |
|                         /_____||_| |_| |_|                           |
|                                                                      |
\---------------------------------------------------------------------*/
/** \file	zypp/Pattern.cc
 *
*/
#include <iostream>
#include "zypp/base/LogTools.h"

#include "zypp/ResPool.h"
#include "zypp/Pattern.h"
#include "zypp/Filter.h"

using std::endl;

///////////////////////////////////////////////////////////////////
namespace zypp
{ /////////////////////////////////////////////////////////////////

  ///////////////////////////////////////////////////////////////////
  namespace
  { /////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////
    //
    //	CLASS NAME : PatternExpander
    //
    /** Recursively expand a Pattern.
     *
     * This means recursively expanding Patterns included by this or
     * extending this. The result is a \c set of <tt>Pattern::constPtr</tt>
     * accessible via iterator.
     */
    class PatternExpander
    {
      public:
        typedef std::map<Pattern::constPtr, DefaultIntegral<bool, false> > PatternMap;
        typedef PatternMap::size_type size_type;
        typedef PatternMap::key_type  value_type;
        typedef MapKVIteratorTraits<PatternMap>::Key_const_iterator const_iterator;

      public:
        PatternExpander()
        {}

        /** Recursively expand Pattern. */
        size_type doExpand( Pattern::constPtr pat_r )
        {
          // INT << "+++ " << pat_r << " ++++++++++++++++++++++++++++++++++" << endl;
          _patternMap.clear();
          if ( pat_r )
          {
            _patternMap[pat_r];
            Pattern::constPtr unprocessed( pat_r );
            // MIL << _patternMap << endl;
            do {
              expandIncludes( unprocessed );
              expandExtending( unprocessed );
              _patternMap[unprocessed] = true;
              // MIL << _patternMap << endl;
            } while( (unprocessed = nextUnprocessed()) );
          }
          // SEC << "--- " << _patternMap.size() << " ----------------------------------" << endl;
          return _patternMap.size();
        }

        const_iterator begin() const
        { return make_map_key_begin( _patternMap ); }

        const_iterator end() const
        { return make_map_key_end( _patternMap ); }

      private:
        /** Get the next unprocessed Pattern in \c _patternMap. */
        Pattern::constPtr nextUnprocessed() const
        {
          for_( it, _patternMap.begin(), _patternMap.end() )
          {
            if ( ! it->second )
              return it->first;
          }
          return NULL;
        }

      private:
        /** Store all included patterns in \c _patternMap. */
        void expandIncludes( const Pattern::constPtr & pat_r )
        {
          Pattern::NameList c( pat_r->includes() );
          for_( it, c.begin(), c.end() )
          {
            expandInclude( Capability( it->c_str()/*, *ResKind::pattern*/ ) );
          }
        }

        /** Store Patterns matching an \c Includes capability in \c _patternMap. */
        void expandInclude( const Capability & include_r )
        {
          sat::WhatProvides w( include_r );
          for_( it, w.begin(), w.end() )
          {
            _patternMap[asKind<Pattern>(PoolItem(*it))];
          }
        }

      private:
        /** Store all patterns extending \c pat_r in \c _patternMap. */
        void expandExtending( const Pattern::constPtr & pat_r )
        {
          ResPool pool( ResPool::instance() );
          for_( it, pool.byKindBegin<Pattern>(), pool.byKindEnd<Pattern>() )
          {
            expandIfExtends( pat_r, *it );
          }
        }

        /** Store \c extending_r if it extends \c pat_r. */
        void expandIfExtends( const Pattern::constPtr & pat_r, const PoolItem & extending_r )
        {
          Pattern::constPtr extending( asKind<Pattern>(extending_r) );
          Pattern::NameList c( extending->extends() );
          for_( it, c.begin(), c.end() )
          {
            if ( providedBy( pat_r, Capability( it->c_str()/*, *ResKind::pattern*/ ) ) )
            {
              // an extends matches the Pattern
              _patternMap[extending];
              break;
            }
          }
        }

        /** Return true if Capability \c extends_r is provided by Pattern. */
        bool providedBy( const Pattern::constPtr & pat_r, const Capability & extends_r )
        {
          if ( !pat_r )
            return false;

          sat::Solvable pat( pat_r->satSolvable() );
          sat::WhatProvides w( extends_r );
          for_( it, w.begin(), w.end() )
          {
            if ( pat == *it )
              return true;
          }
          return false;
        }

      private:
        PatternMap _patternMap;
    };
    /////////////////////////////////////////////////////////////////
  } // namespace
  ///////////////////////////////////////////////////////////////////
  IMPL_PTR_TYPE(Pattern);

  ///////////////////////////////////////////////////////////////////
  //
  //	METHOD NAME : Pattern::Pattern
  //	METHOD TYPE : Ctor
  //
  Pattern::Pattern( const sat::Solvable & solvable_r )
  : ResObject( solvable_r )
  {}

  ///////////////////////////////////////////////////////////////////
  //
  //	METHOD NAME : Pattern::~Pattern
  //	METHOD TYPE : Dtor
  //
  Pattern::~Pattern()
  {}

  ///////////////////////////////////////////////////////////////////
  //
  //	Pattern interface forwarded to implementation
  //
  ///////////////////////////////////////////////////////////////////
  /** */
  bool Pattern::isDefault() const
  { return lookupBoolAttribute( sat::SolvAttr::isdefault ); }
  /** */
  bool Pattern::userVisible() const
  { return lookupBoolAttribute( sat::SolvAttr::isvisible ); }
  /** */
  std::string Pattern::category( const Locale & lang_r ) const
  { return lookupStrAttribute( sat::SolvAttr::category, lang_r ); }
  /** */
  Pathname Pattern::icon() const
  { return lookupStrAttribute( sat::SolvAttr::icon ); }
  /** */
  Pathname Pattern::script() const
  { return lookupStrAttribute( sat::SolvAttr::script ); }

  std::string Pattern::order() const
  { return lookupStrAttribute( sat::SolvAttr::order ); }

  Pattern::NameList Pattern::includes() const
  { return NameList( sat::SolvAttr::includes, satSolvable() ); }

  Pattern::NameList Pattern::extends() const
  { return NameList( sat::SolvAttr::extends, satSolvable() ); }

  Pattern::Contents Pattern::core() const
  {
    // get items providing the requirements
    sat::WhatProvides prv( requires() );
    // return packages only.
    return Pattern::Contents( make_filter_begin( filter::byKind<Package>(), prv ),
                              make_filter_end( filter::byKind<Package>(), prv ) );
  }

  Pattern::Contents Pattern::depends() const
  {
    // load requires, recommends, suggests
    CapabilitySet caps;
    {
      Capabilities c( requires() );
      caps.insert( c.begin(),c.end() );
      c = recommends();
      caps.insert( c.begin(),c.end() );
      c = suggests();
      caps.insert( c.begin(),c.end() );
    }
    // get items providing the above
    sat::WhatProvides prv( caps );
    // return packages only.
    return Pattern::Contents( make_filter_begin( filter::byKind<Package>(), prv ),
                              make_filter_end( filter::byKind<Package>(), prv ) );
  }

  Pattern::Contents Pattern::contents() const
  {
    PatternExpander expander;
    if ( ! expander.doExpand( this ) )
      return Contents(); // empty pattern set

    Contents result;
    for_( it, expander.begin(), expander.end() )
    {
      Contents c( (*it)->depends() );
      result.get().insert( c.begin(), c.end() );
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////
} // namespace zypp
///////////////////////////////////////////////////////////////////
