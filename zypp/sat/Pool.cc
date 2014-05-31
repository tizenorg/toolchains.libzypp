/*---------------------------------------------------------------------\
|                          ____ _   __ __ ___                          |
|                         |__  / \ / / . \ . \                         |
|                           / / \ V /|  _/  _/                         |
|                          / /__ | | | | | |                           |
|                         /_____||_| |_| |_|                           |
|                                                                      |
\---------------------------------------------------------------------*/
/** \file	zypp/sat/Pool.cc
 *
*/

#include <iostream>

#include "zypp/base/Easy.h"
#include "zypp/base/Logger.h"
#include "zypp/base/Gettext.h"
#include "zypp/base/Exception.h"

#include "zypp/AutoDispose.h"

#include "zypp/sat/detail/PoolImpl.h"
#include "zypp/sat/Pool.h"
#include "zypp/sat/LookupAttr.h"

///////////////////////////////////////////////////////////////////
namespace zypp
{ /////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////
  namespace sat
  { /////////////////////////////////////////////////////////////////

    const std::string & Pool::systemRepoAlias()
    { return detail::PoolImpl::systemRepoAlias(); }

    ::_Pool * Pool::get() const
    { return myPool().getPool(); }

    Pool::size_type Pool::capacity() const
    { return myPool()->nsolvables; }

    const SerialNumber & Pool::serial() const
    { return myPool().serial(); }

    void Pool::prepare() const
    { return myPool().prepare(); }

    bool Pool::reposEmpty() const
    { return ! myPool()->nrepos; }

    Pool::size_type Pool::reposSize() const
    { return myPool()->nrepos; }

    Pool::RepositoryIterator Pool::reposBegin() const
    { return RepositoryIterator( myPool()->repos ); }

    Pool::RepositoryIterator Pool::reposEnd() const
    { return RepositoryIterator( myPool()->repos+myPool()->nrepos ); }

    bool Pool::solvablesEmpty() const
    {
      // return myPool()->nsolvables;
      // nsolvables is the array size including
      // invalid Solvables.
      for_( it, reposBegin(), reposEnd() )
      {
        if ( ! it->solvablesEmpty() )
          return false;
      }
      return true;
    }

    Pool::size_type Pool::solvablesSize() const
    {
      // Do not return myPool()->nsolvables;
      // nsolvables is the array size including
      // invalid Solvables.
      size_type ret = 0;
      for_( it, reposBegin(), reposEnd() )
      {
        ret += it->solvablesSize();
      }
      return ret;
    }

    Pool::SolvableIterator Pool::solvablesBegin() const
    { return SolvableIterator( myPool().getFirstId() ); }

    Pool::SolvableIterator Pool::solvablesEnd() const
    { return SolvableIterator(); }

    Repository Pool::reposInsert( const std::string & alias_r )
    {
      Repository ret( reposFind( alias_r ) );
      if ( ret )
        return ret;

      ret = Repository( myPool()._createRepo( alias_r ) );
      if ( ret.isSystemRepo() )
      {
        // autoprovide (dummy) RepoInfo
        RepoInfo info;
        info.setAlias( alias_r );
        info.setName( alias_r );
        info.setAutorefresh( true );
        info.setEnabled( true );
        ret.setInfo( info );
      }
      return ret;
    }

    Repository Pool::reposFind( const std::string & alias_r ) const
    {
      for_( it, reposBegin(), reposEnd() )
      {
        if ( alias_r == it->alias() )
          return *it;
      }
      return Repository();
    }

    Repository Pool::findSystemRepo() const
    {
      return Repository( myPool().systemRepo() );
    }

    Repository Pool::systemRepo()
    {
      if ( myPool().systemRepo() )
        return Repository( myPool().systemRepo() );
      return reposInsert( systemRepoAlias() );
    }

    Repository Pool::addRepoSolv( const Pathname & file_r, const std::string & alias_r )
    {
      // Using a temporay repo! (The additional parenthesis are required.)
      AutoDispose<Repository> tmprepo( (Repository::EraseFromPool()) );
      *tmprepo = reposInsert( alias_r );
      tmprepo->addSolv( file_r );

      // no exceptions so we keep it:
      tmprepo.resetDispose();
      return tmprepo;
    }

    Repository Pool::addRepoSolv( const Pathname & file_r )
    { return addRepoSolv( file_r, file_r.basename() ); }

    Repository Pool::addRepoSolv( const Pathname & file_r, const RepoInfo & info_r )
    {
      Repository ret( addRepoSolv( file_r, info_r.alias() ) );
      ret.setInfo( info_r );
      return ret;
    }

    /////////////////////////////////////////////////////////////////

    Repository Pool::addRepoHelix( const Pathname & file_r, const std::string & alias_r )
    {
      // Using a temporay repo! (The additional parenthesis are required.)
      AutoDispose<Repository> tmprepo( (Repository::EraseFromPool()) );
      *tmprepo = reposInsert( alias_r );
      tmprepo->addHelix( file_r );

      // no exceptions so we keep it:
      tmprepo.resetDispose();
      return tmprepo;
    }

    Repository Pool::addRepoHelix( const Pathname & file_r )
    { return addRepoHelix( file_r, file_r.basename() ); }

    Repository Pool::addRepoHelix( const Pathname & file_r, const RepoInfo & info_r )
    {
      Repository ret( addRepoHelix( file_r, info_r.alias() ) );
      ret.setInfo( info_r );
      return ret;
    }

   /////////////////////////////////////////////////////////////////

    void Pool::setTextLocale( const Locale & locale_r )
    { myPool().setTextLocale( locale_r ); }

    void Pool::setRequestedLocales( const LocaleSet & locales_r )
    { myPool().setRequestedLocales( locales_r ); }

    bool Pool::addRequestedLocale( const Locale & locale_r )
    { return myPool().addRequestedLocale( locale_r ); }

    bool Pool::eraseRequestedLocale( const Locale & locale_r )
    { return myPool().eraseRequestedLocale( locale_r ); }

    const LocaleSet & Pool::getRequestedLocales() const
    { return myPool().getRequestedLocales(); }

    bool Pool::isRequestedLocale( const Locale & locale_r ) const
    { return myPool().isRequestedLocale( locale_r ); }

    const LocaleSet & Pool::getAvailableLocales() const
    {  return myPool().getAvailableLocales(); }

    bool Pool::isAvailableLocale( const Locale & locale_r ) const
    { return myPool().isAvailableLocale( locale_r ); }

    bool Pool::multiversionEmpty() const			{ return myPool().multiversionList().empty(); }
    size_t Pool::multiversionSize() const			{ return myPool().multiversionList().size(); }
    Pool::MultiversionIterator Pool::multiversionBegin() const	{ return myPool().multiversionList().begin(); }
    Pool::MultiversionIterator Pool::multiversionEnd() const	{ return myPool().multiversionList().end(); }
    bool Pool::isMultiversion( IdString ident_r ) const		{ return myPool().isMultiversion( ident_r ); }

    bool Pool::onSystemByUserEmpty() const			{ return myPool().onSystemByUserList().empty(); }
    size_t Pool::onSystemByUserSize() const			{ return myPool().onSystemByUserList().size(); }
    Pool::OnSystemByUserIterator Pool::onSystemByUserBegin() const	{ return myPool().onSystemByUserList().begin(); }
    Pool::OnSystemByUserIterator Pool::onSystemByUserEnd() const	{ return myPool().onSystemByUserList().end(); }
    bool Pool::isOnSystemByUser( IdString ident_r ) const	{ return myPool().isOnSystemByUser( ident_r ); }

   /******************************************************************
    **
    **	FUNCTION NAME : operator<<
    **	FUNCTION TYPE : std::ostream &
    */
    std::ostream & operator<<( std::ostream & str, const Pool & obj )
    {
      return str << "sat::pool(" << obj.serial() << ")["
          << obj.capacity() << "]{"
          << obj.reposSize() << "repos|"
	  << obj.solvablesSize() << "slov}";
    }

    /////////////////////////////////////////////////////////////////
  } // namespace sat
  ///////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////
} // namespace zypp
///////////////////////////////////////////////////////////////////
