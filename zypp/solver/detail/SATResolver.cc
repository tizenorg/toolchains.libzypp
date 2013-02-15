/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* SATResolver.cc
 *
 * Copyright (C) 2000-2002 Ximian, Inc.
 * Copyright (C) 2005 SUSE Linux Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA.
 */
extern "C"
{
#include <satsolver/repo_solv.h>
#include <satsolver/poolarch.h>
#include <satsolver/evr.h>
#include <satsolver/poolvendor.h>
#include <satsolver/policy.h>
#include <satsolver/bitmap.h>
#include <satsolver/queue.h>
}

#include "zypp/solver/detail/Helper.h"
#include "zypp/base/String.h"
#include "zypp/Product.h"
#include "zypp/Capability.h"
#include "zypp/ResStatus.h"
#include "zypp/VendorAttr.h"
#include "zypp/base/LogTools.h"
#include "zypp/base/String.h"
#include "zypp/base/Gettext.h"
#include "zypp/base/Algorithm.h"
#include "zypp/ResPool.h"
#include "zypp/ResFilters.h"
#include "zypp/ZConfig.h"
#include "zypp/sat/Pool.h"
#include "zypp/sat/WhatProvides.h"
#include "zypp/sat/WhatObsoletes.h"
#include "zypp/solver/detail/SATResolver.h"
#include "zypp/solver/detail/ProblemSolutionCombi.h"
#include "zypp/solver/detail/ProblemSolutionIgnore.h"
#include "zypp/solver/detail/SolverQueueItemInstall.h"
#include "zypp/solver/detail/SolverQueueItemDelete.h"
#include "zypp/solver/detail/SystemCheck.h"
#include "zypp/sat/Transaction.h"

/////////////////////////////////////////////////////////////////////////
namespace zypp
{ ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  namespace solver
  { /////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////
    namespace detail
    { ///////////////////////////////////////////////////////////////////

using namespace std;

IMPL_PTR_TYPE(SATResolver);

#define MAYBE_CLEANDEPS (cleandepsOnRemove()?SOLVER_CLEANDEPS:0)

//---------------------------------------------------------------------------
// Callbacks for SAT policies
//---------------------------------------------------------------------------

int vendorCheck( Pool *pool, Solvable *solvable1, Solvable *solvable2 )
{
  return VendorAttr::instance().equivalent( IdString(solvable1->vendor),
                                            IdString(solvable2->vendor) ) ? 0 : 1;
}


inline std::string itemToString( const PoolItem & item )
{
  if ( !item )
    return std::string();

  sat::Solvable slv( item.satSolvable() );
  std::string ret( slv.asString() ); // n-v-r.a
  if ( ! slv.isSystem() )
  {
    ret += "[";
    ret += slv.repository().alias();
    ret += "]";
  }
  return ret;
}

inline PoolItem getPoolItem( Id id_r )
{
  PoolItem ret( (sat::Solvable( id_r )) );
  if ( !ret && id_r )
    INT << "id " << id_r << " not found in ZYPP pool." << endl;
  return ret;
}

//---------------------------------------------------------------------------

std::ostream &
SATResolver::dumpOn( std::ostream & os ) const
{
    os << "<resolver>" << endl;
    if (_solv) {
	os << "  fixsystem = " << _solv->fixsystem << endl;
	os << "  allowdowngrade = " << _solv->allowdowngrade << endl;
	os << "  allowarchchange = " << _solv->allowarchchange << endl;
	os << "  allowvendorchange = " <<  _solv->allowvendorchange << endl;
	os << "  allowuninstall = " << _solv->allowuninstall << endl;
	os << "  updatesystem = " << _solv->updatesystem << endl;
	os << "  noupdateprovide = " << _solv->noupdateprovide << endl;
	os << "  dosplitprovides = " << _solv->dosplitprovides << endl;
	os << "  onlyRequires = " << _solv->dontinstallrecommended << endl;
	os << "  ignorealreadyrecommended = " << _solv->ignorealreadyrecommended << endl;
	os << "  distupgrade = " << _distupgrade << endl;
        os << "  distupgrade_removeunsupported = " << _distupgrade_removeunsupported << endl;
	os << "  solveSrcPackages = " << _solveSrcPackages << endl;
	os << "  cleandepsOnRemove = " << _cleandepsOnRemove << endl;
    } else {
	os << "<NULL>";
    }
    return os << "<resolver/>" << endl;
}

//---------------------------------------------------------------------------

SATResolver::SATResolver (const ResPool & pool, Pool *SATPool)
    : _pool (pool)
    , _SATPool (SATPool)
    , _solv(NULL)
    , _fixsystem(false)
    , _allowdowngrade(false)
    , _allowarchchange(false)
    , _allowvendorchange(ZConfig::instance().solver_allowVendorChange())
    , _allowuninstall(false)
    , _updatesystem(false)
    , _noupdateprovide(false)
    , _dosplitprovides(false)
    , _onlyRequires(ZConfig::instance().solver_onlyRequires())
    , _ignorealreadyrecommended(false)
    , _distupgrade(false)
    , _distupgrade_removeunsupported(false)
    , _solveSrcPackages(false)
    , _cleandepsOnRemove(ZConfig::instance().solver_cleandepsOnRemove())
{
}


SATResolver::~SATResolver()
{
  solverEnd();
}

//---------------------------------------------------------------------------

sat::Transaction SATResolver::getTransaction()
{
  if ( !_solv )
    return sat::Transaction();
  return sat::Transaction( _solv->trans );
}

ResPool
SATResolver::pool (void) const
{
    return _pool;
}

void
SATResolver::resetItemTransaction (PoolItem item)
{
    bool found = false;
    for (PoolItemList::const_iterator iter = _items_to_remove.begin();
	 iter != _items_to_remove.end(); ++iter) {
	if (*iter == item) {
	    _items_to_remove.remove(*iter);
	    found = true;
	    break;
	}
    }
    if (!found) {
	for (PoolItemList::const_iterator iter = _items_to_install.begin();
	     iter != _items_to_install.end(); ++iter) {
	    if (*iter == item) {
		_items_to_install.remove(*iter);
		found = true;
		break;
	    }
	}
    }
    if (!found) {
	for (PoolItemList::const_iterator iter = _items_to_keep.begin();
	     iter != _items_to_keep.end(); ++iter) {
	    if (*iter == item) {
		_items_to_keep.remove(*iter);
		found = true;
		break;
	    }
	}
    }
    if (!found) {
	for (PoolItemList::const_iterator iter = _items_to_lock.begin();
	     iter != _items_to_lock.end(); ++iter) {
	    if (*iter == item) {
		_items_to_lock.remove(*iter);
		found = true;
		break;
	    }
	}
    }
}


void
SATResolver::addPoolItemToInstall (PoolItem item)
{
    resetItemTransaction (item);
    _items_to_install.push_back (item);
    _items_to_install.unique ();
}


void
SATResolver::addPoolItemsToInstallFromList (PoolItemList & rl)
{
    for (PoolItemList::const_iterator iter = rl.begin(); iter != rl.end(); iter++) {
	addPoolItemToInstall (*iter);
    }
}


void
SATResolver::addPoolItemToRemove (PoolItem item)
{
    resetItemTransaction (item);
    _items_to_remove.push_back (item);
    _items_to_remove.unique ();
}


void
SATResolver::addPoolItemsToRemoveFromList (PoolItemList & rl)
{
    for (PoolItemList::const_iterator iter = rl.begin(); iter != rl.end(); iter++) {
	addPoolItemToRemove (*iter);
    }
}

void
SATResolver::addPoolItemToLock (PoolItem item)
{
    resetItemTransaction (item);
    _items_to_lock.push_back (item);
    _items_to_lock.unique ();
}

void
SATResolver::addPoolItemToKeep (PoolItem item)
{
    resetItemTransaction (item);
    _items_to_keep.push_back (item);
    _items_to_keep.unique ();
}

//---------------------------------------------------------------------------

// copy marked item from solution back to pool
// if data != NULL, set as APPL_LOW (from establishPool())

static void
SATSolutionToPool (PoolItem item, const ResStatus & status, const ResStatus::TransactByValue causer)
{
    // resetting
    item.status().resetTransact (causer);
    item.status().resetWeak ();

    bool r;

    // installation/deletion
    if (status.isToBeInstalled()) {
	r = item.status().setToBeInstalled (causer);
	_XDEBUG("SATSolutionToPool install returns " << item << ", " << r);
    }
    else if (status.isToBeUninstalledDueToUpgrade()) {
	r = item.status().setToBeUninstalledDueToUpgrade (causer);
	_XDEBUG("SATSolutionToPool upgrade returns " << item << ", " <<  r);
    }
    else if (status.isToBeUninstalled()) {
	r = item.status().setToBeUninstalled (causer);
	_XDEBUG("SATSolutionToPool remove returns " << item << ", " <<  r);
    }

    return;
}

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// resolvePool
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Helper functions for the ZYPP-Pool
//----------------------------------------------------------------------------


//------------------------------------------------------------------------------------------------------------
//  This function loops over the pool and grabs all items
//  It clears all previous bySolver() states also
//
//  Every toBeInstalled is passed to zypp::solver:detail::Resolver.addPoolItemToInstall()
//  Every toBeUninstalled is passed to zypp::solver:detail::Resolver.addPoolItemToRemove()
//
//  Solver results must be written back to the pool.
//------------------------------------------------------------------------------------------------------------


struct SATCollectTransact : public resfilter::PoolItemFilterFunctor
{
    SATResolver & resolver;

    SATCollectTransact (SATResolver & r)
	: resolver (r)
    { }

    bool operator()( PoolItem item )		// only transacts() items go here
    {
	ResStatus status = item.status();
	bool by_solver = (status.isBySolver() || status.isByApplLow());

	if (by_solver) {
	    item.status().resetTransact( ResStatus::APPL_LOW );// clear any solver/establish transactions
	    return true;				// back out here, dont re-queue former solver result
	}

	if ( item.satSolvable().isKind<SrcPackage>() && ! resolver.solveSrcPackages() )
	{
	  // Later we may continue on a per source package base.
	  return true; // dont process this source package.
	}

	if (status.isToBeInstalled()) {
	    resolver.addPoolItemToInstall(item);	// -> install!
	}
	else if (status.isToBeUninstalled()) {
	    resolver.addPoolItemToRemove(item);		// -> remove !
	}
        else if (status.isLocked()
		 && !by_solver) {
	    resolver.addPoolItemToLock (item);
        }
        else if (status.isKept()
		 && !by_solver) {
	    resolver.addPoolItemToKeep (item);
        }

	return true;
    }
};


//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// solving.....
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------


class CheckIfUpdate : public resfilter::PoolItemFilterFunctor
{
  public:
    bool is_updated;
    bool multiversion;
    sat::Solvable _installed;

    CheckIfUpdate( sat::Solvable installed_r )
	: is_updated( false )
        , multiversion( installed_r.multiversionInstall() )
        , _installed( installed_r )
    {}

    // check this item will be updated

    bool operator()( PoolItem item )
    {
	if ( item.status().isToBeInstalled() )
        {
          if ( ! multiversion || sameNVRA( _installed, item ) )
          {
            is_updated = true;
            return false;
          }
	}
	return true;
    }
};


class CollectPseudoInstalled : public resfilter::PoolItemFilterFunctor
{
  public:
    Queue *solvableQueue;

    CollectPseudoInstalled( Queue *queue )
	:solvableQueue (queue)
    {}

    // collecting PseudoInstalled items
    bool operator()( PoolItem item )
    {
      if ( traits::isPseudoInstalled( item.satSolvable().kind() ) )
        queue_push( solvableQueue, item.satSolvable().id() );
      return true;
    }
};

bool
SATResolver::solving(const CapabilitySet & requires_caps,
		     const CapabilitySet & conflict_caps)
{
    _solv = solver_create( _SATPool );
    _solv->vendorCheckCb = &vendorCheck;
    _solv->fixsystem = _fixsystem;
    _solv->ignorealreadyrecommended = _ignorealreadyrecommended;
    _solv->updatesystem = _updatesystem;
    _solv->allowdowngrade = _allowdowngrade;
    _solv->allowuninstall = _allowuninstall;
    _solv->allowarchchange = _allowarchchange;
    _solv->allowvendorchange = _allowvendorchange;
    _solv->dosplitprovides = _dosplitprovides;
    _solv->noupdateprovide = _noupdateprovide;
    _solv->dontinstallrecommended = _onlyRequires;
    _solv->distupgrade = _distupgrade;
    _solv->distupgrade_removeunsupported = _distupgrade_removeunsupported;

    sat::Pool::instance().prepare();

    // Solve !
    MIL << "Starting solving...." << endl;
    MIL << *this;
    solver_solve( _solv, &(_jobQueue) );
    MIL << "....Solver end" << endl;

    // copying solution back to zypp pool
    //-----------------------------------------
    _result_items_to_install.clear();
    _result_items_to_remove.clear();

    /*  solvables to be installed */
    for ( int i = 0; i < _solv->decisionq.count; ++i )
    {
      sat::Solvable slv( _solv->decisionq.elements[i] );
      if ( !slv || slv.isSystem() )
	continue;

      PoolItem poolItem( slv );
      SATSolutionToPool (poolItem, ResStatus::toBeInstalled, ResStatus::SOLVER);
      _result_items_to_install.push_back (poolItem);
    }

    /* solvables to be erased */
    Repository systemRepo( sat::Pool::instance().findSystemRepo() ); // don't create if it does not exist
    if ( systemRepo && ! systemRepo.solvablesEmpty() )
    {
      bool mustCheckObsoletes = false;
      for_( it, systemRepo.solvablesBegin(), systemRepo.solvablesEnd() )
      {
	if (_solv->decisionmap[it->id()] > 0)
	  continue;

	// Check if this is an update
	CheckIfUpdate info( *it );
	PoolItem poolItem( *it );
	invokeOnEach( _pool.byIdentBegin( poolItem ),
		      _pool.byIdentEnd( poolItem ),
		      resfilter::ByUninstalled(),			// ByUninstalled
		      functor::functorRef<bool,PoolItem> (info) );

	if (info.is_updated) {
	  SATSolutionToPool( poolItem, ResStatus::toBeUninstalledDueToUpgrade, ResStatus::SOLVER );
	} else {
	  SATSolutionToPool( poolItem, ResStatus::toBeUninstalled, ResStatus::SOLVER );
	  if ( ! mustCheckObsoletes )
	    mustCheckObsoletes = true; // lazy check for UninstalledDueToObsolete
	}
	_result_items_to_remove.push_back (poolItem);
      }
      if ( mustCheckObsoletes )
      {
	sat::WhatObsoletes obsoleted( _result_items_to_install.begin(), _result_items_to_install.end() );
	for_( it, obsoleted.poolItemBegin(), obsoleted.poolItemEnd() )
	{
	  ResStatus & status( it->status() );
	  // WhatObsoletes contains installed items only!
	  if ( status.transacts() && ! status.isToBeUninstalledDueToUpgrade() )
	    status.setToBeUninstalledDueToObsolete();
	}
      }
    }

    /*  solvables which are recommended */
    for ( int i = 0; i < _solv->recommendations.count; ++i )
    {
      PoolItem poolItem( getPoolItem( _solv->recommendations.elements[i] ) );
      poolItem.status().setRecommended( true );
    }

    /*  solvables which are suggested */
    for ( int i = 0; i < _solv->suggestions.count; ++i )
    {
      PoolItem poolItem( getPoolItem( _solv->suggestions.elements[i] ) );
      poolItem.status().setSuggested( true );
    }

    _problem_items.clear();
    /*  solvables which are orphaned */
    for ( int i = 0; i < _solv->orphaned.count; ++i )
    {
      PoolItem poolItem( getPoolItem( _solv->orphaned.elements[i] ) );
      poolItem.status().setOrphaned( true );
      _problem_items.push_back( poolItem );
    }

    /* Write validation state back to pool */
    Queue flags, solvableQueue;

    queue_init(&flags);
    queue_init(&solvableQueue);

    CollectPseudoInstalled collectPseudoInstalled(&solvableQueue);
    invokeOnEach( _pool.begin(),
		  _pool.end(),
		  functor::functorRef<bool,PoolItem> (collectPseudoInstalled) );
    solver_trivial_installable(_solv, &solvableQueue, &flags );
    for (int i = 0; i < solvableQueue.count; i++) {
	PoolItem item = _pool.find (sat::Solvable(solvableQueue.elements[i]));
	item.status().setUndetermined();

	if (flags.elements[i] == -1) {
	    item.status().setNonRelevant();
	    _XDEBUG("SATSolutionToPool(" << item << " ) nonRelevant !");
	} else if (flags.elements[i] == 1) {
	    item.status().setSatisfied();
	    _XDEBUG("SATSolutionToPool(" << item << " ) satisfied !");
	} else if (flags.elements[i] == 0) {
	    item.status().setBroken();
	    _XDEBUG("SATSolutionToPool(" << item << " ) broken !");
	}
    }

    // Solvables which were selected due requirements which have been made by the user will
    // be selected by APPL_LOW. We can't use any higher level, because this setting must
    // not serve as a request for the next solver run. APPL_LOW is reset before solving.
    for (CapabilitySet::const_iterator iter = requires_caps.begin(); iter != requires_caps.end(); iter++) {
	sat::WhatProvides rpmProviders(*iter);
	for_( iter2, rpmProviders.begin(), rpmProviders.end() ) {
	    PoolItem poolItem(*iter2);
	    if (poolItem.status().isToBeInstalled()) {
		MIL << "User requirement " << *iter << " sets " << poolItem << endl;
		poolItem.status().setTransactByValue (ResStatus::APPL_LOW);
	    }
	}
    }
    for (CapabilitySet::const_iterator iter = conflict_caps.begin(); iter != conflict_caps.end(); iter++) {
	sat::WhatProvides rpmProviders(*iter);
	for_( iter2, rpmProviders.begin(), rpmProviders.end() ) {
	    PoolItem poolItem(*iter2);
	    if (poolItem.status().isToBeUninstalled()) {
		MIL << "User conflict " << *iter << " sets " << poolItem << endl;
		poolItem.status().setTransactByValue (ResStatus::APPL_LOW);
	    }
	}
    }

    if (_solv->problems.count > 0 )
    {
	ERR << "Solverrun finished with an ERROR" << endl;
	return false;
    }

    queue_free(&(solvableQueue));
    queue_free(&flags);

    return true;
}


void
SATResolver::solverInit(const PoolItemList & weakItems)
{
    SATCollectTransact info (*this);

    MIL << "SATResolver::solverInit()" << endl;

    // remove old stuff
    solverEnd();

    queue_init( &_jobQueue );
    _items_to_install.clear();
    _items_to_remove.clear();
    _items_to_lock.clear();
    _items_to_keep.clear();

    invokeOnEach ( _pool.begin(), _pool.end(),
		   functor::functorRef<bool,PoolItem>(info) );

    for (PoolItemList::const_iterator iter = weakItems.begin(); iter != weakItems.end(); iter++) {
	Id id = (*iter)->satSolvable().id();
	if (id == ID_NULL) {
	    ERR << "Weaken: " << *iter << " not found" << endl;
	}
	MIL << "Weaken dependencies of " << *iter << endl;
	queue_push( &(_jobQueue), SOLVER_WEAKENDEPS | SOLVER_SOLVABLE );
        queue_push( &(_jobQueue), id );
    }

    // Add rules for parallel installable resolvables with different versions
    for_( it, sat::Pool::instance().multiversionBegin(), sat::Pool::instance().multiversionEnd() )
    {
      queue_push( &(_jobQueue), SOLVER_NOOBSOLETES | SOLVABLE_NAME );
      queue_push( &(_jobQueue), it->id() );
    }

    if ( cleandepsOnRemove() )
    {
      // Add all items known to be installed by user request (not solver selected).
      for_( it, sat::Pool::instance().onSystemByUserBegin(), sat::Pool::instance().onSystemByUserEnd() )
      {
	queue_push( &(_jobQueue), SOLVER_USERINSTALLED | SOLVER_SOLVABLE_NAME );
	queue_push( &(_jobQueue), it->id() );
      }
    }

    if ( _distupgrade )
    {
      if ( ZConfig::instance().solverUpgradeRemoveDroppedPackages() )
      {
        MIL << "Checking droplists ..." << endl;
        // Dropped packages: look for 'weakremover()' provides
        // in dup candidates of installed products.
        ResPoolProxy proxy( ResPool::instance().proxy() );
        for_( it, proxy.byKindBegin<Product>(), proxy.byKindEnd<Product>() )
        {
          if ( (*it)->onSystem() ) // (to install) or (not to delete)
          {
            Product::constPtr prodCand( (*it)->candidateAsKind<Product>() );
            if ( ! prodCand || (*it)->identicalInstalledCandidate() )
              continue; // product no longer available or unchanged

            CapabilitySet droplist( prodCand->droplist() );
            dumpRangeLine( MIL << "Droplist for " << (*it)->candidateObj() << ": " << droplist.size() << " ", droplist.begin(), droplist.end() ) << endl;
            for_( cap, droplist.begin(), droplist.end() )
            {
              queue_push( &_jobQueue, SOLVER_DROP_ORPHANED | SOLVER_SOLVABLE_NAME );
              queue_push( &_jobQueue, cap->id() );
            }
          }
        }
      }
      else
      {
        MIL << "Droplist processing is disabled." << endl;
      }
    }
}

void
SATResolver::solverEnd()
{
  // cleanup
  if ( _solv )
  {
    solver_free(_solv);
    _solv = NULL;
    queue_free( &(_jobQueue) );
  }
}


bool
SATResolver::resolvePool(const CapabilitySet & requires_caps,
			 const CapabilitySet & conflict_caps,
			 const PoolItemList & weakItems,
                         const std::set<Repository> & upgradeRepos)
{
    MIL << "SATResolver::resolvePool()" << endl;

    // initialize
    solverInit(weakItems);

    for (PoolItemList::const_iterator iter = _items_to_install.begin(); iter != _items_to_install.end(); iter++) {
	Id id = (*iter)->satSolvable().id();
	if (id == ID_NULL) {
	    ERR << "Install: " << *iter << " not found" << endl;
	} else {
	    MIL << "Install " << *iter << endl;
	    queue_push( &(_jobQueue), SOLVER_INSTALL_SOLVABLE );
	    queue_push( &(_jobQueue), id );
	}
    }

    for (PoolItemList::const_iterator iter = _items_to_remove.begin(); iter != _items_to_remove.end(); iter++) {
	Id id = (*iter)->satSolvable().id();
	if (id == ID_NULL) {
	    ERR << "Delete: " << *iter << " not found" << endl;
	} else {
	    MIL << "Delete " << *iter << endl;
	    queue_push( &(_jobQueue), SOLVER_ERASE_SOLVABLE | MAYBE_CLEANDEPS );
	    queue_push( &(_jobQueue), id);
	}
    }

    for_( iter, upgradeRepos.begin(), upgradeRepos.end() )
    {
	queue_push( &(_jobQueue), SOLVER_DISTUPGRADE | SOLVER_SOLVABLE_REPO );
	queue_push( &(_jobQueue), iter->get()->repoid );
        MIL << "Upgrade repo " << *iter << endl;
    }

    for (CapabilitySet::const_iterator iter = requires_caps.begin(); iter != requires_caps.end(); iter++) {
	queue_push( &(_jobQueue), SOLVER_INSTALL | SOLVER_SOLVABLE_PROVIDES );
	queue_push( &(_jobQueue), iter->id() );
	MIL << "Requires " << *iter << endl;
    }

    for (CapabilitySet::const_iterator iter = conflict_caps.begin(); iter != conflict_caps.end(); iter++) {
	queue_push( &(_jobQueue), SOLVER_ERASE | SOLVER_SOLVABLE_PROVIDES | MAYBE_CLEANDEPS );
	queue_push( &(_jobQueue), iter->id() );
	MIL << "Conflicts " << *iter << endl;
    }

    // set requirements for a running system
    setSystemRequirements();

    // set locks for the solver
    setLocks();

    // solving
    bool ret = solving(requires_caps, conflict_caps);

    (ret?MIL:WAR) << "SATResolver::resolvePool() done. Ret:" << ret <<  endl;
    return ret;
}


bool
SATResolver::resolveQueue(const SolverQueueItemList &requestQueue,
			  const PoolItemList & weakItems)
{
    MIL << "SATResolver::resolvQueue()" << endl;

    // initialize
    solverInit(weakItems);

    // generate solver queue
    for (SolverQueueItemList::const_iterator iter = requestQueue.begin(); iter != requestQueue.end(); iter++) {
	(*iter)->addRule(_jobQueue);
    }

    // Add addition item status to the resolve-queue cause these can be set by problem resolutions
    for (PoolItemList::const_iterator iter = _items_to_install.begin(); iter != _items_to_install.end(); iter++) {
	Id id = (*iter)->satSolvable().id();
	if (id == ID_NULL) {
	    ERR << "Install: " << *iter << " not found" << endl;
	} else {
	    MIL << "Install " << *iter << endl;
	    queue_push( &(_jobQueue), SOLVER_INSTALL_SOLVABLE );
	    queue_push( &(_jobQueue), id );
	}
    }
    for (PoolItemList::const_iterator iter = _items_to_remove.begin(); iter != _items_to_remove.end(); iter++) {
        sat::detail::IdType ident( (*iter)->satSolvable().ident().id() );
	MIL << "Delete " << *iter << ident << endl;
	queue_push( &(_jobQueue), SOLVER_ERASE | SOLVER_SOLVABLE_NAME | MAYBE_CLEANDEPS );
	queue_push( &(_jobQueue), ident);
    }

    // set requirements for a running system
    setSystemRequirements();

    // set locks for the solver
    setLocks();

    // solving
    bool ret = solving();

    MIL << "SATResolver::resolveQueue() done. Ret:" << ret <<  endl;
    return ret;
}

/** \todo duplicate code to be joined with \ref solving. */
void SATResolver::doUpdate()
{
    MIL << "SATResolver::doUpdate()" << endl;

    // initialize
    solverInit(PoolItemList());

    // set requirements for a running system
    setSystemRequirements();

    // set locks for the solver
    setLocks();

    _solv = solver_create( _SATPool );
    _solv->vendorCheckCb = &vendorCheck;
    _solv->fixsystem = _fixsystem;
    _solv->ignorealreadyrecommended = _ignorealreadyrecommended;
    _solv->updatesystem = true;
    _solv->allowdowngrade = _allowdowngrade;
    _solv->allowuninstall = _allowuninstall;
    _solv->allowarchchange = _allowarchchange;
    _solv->allowvendorchange = _allowvendorchange;
    _solv->dosplitprovides = true;
    _solv->noupdateprovide = _noupdateprovide;
    _solv->dontinstallrecommended = _onlyRequires;
    _solv->distupgrade = _distupgrade;
    _solv->distupgrade_removeunsupported = _distupgrade_removeunsupported;

    sat::Pool::instance().prepare();

    // Solve !
    MIL << "Starting solving for update...." << endl;
    MIL << *this;
    solver_solve( _solv, &(_jobQueue) );
    MIL << "....Solver end" << endl;

    // copying solution back to zypp pool
    //-----------------------------------------

    /*  solvables to be installed */
    for (int i = 0; i < _solv->decisionq.count; i++)
    {
      Id p;
      p = _solv->decisionq.elements[i];
      if (p < 0 || !sat::Solvable(p))
	continue;
      if (sat::Solvable(p).repository().get() == _solv->installed)
	continue;

      PoolItem poolItem = _pool.find (sat::Solvable(p));
      if (poolItem) {
	  SATSolutionToPool (poolItem, ResStatus::toBeInstalled, ResStatus::SOLVER);
      } else {
	  ERR << "id " << p << " not found in ZYPP pool." << endl;
      }
    }

    /* solvables to be erased */
    for (int i = _solv->installed->start; i < _solv->installed->start + _solv->installed->nsolvables; i++)
    {
      if (_solv->decisionmap[i] > 0)
	continue;

      PoolItem poolItem( _pool.find( sat::Solvable(i) ) );
      if (poolItem) {
	  // Check if this is an update
	  CheckIfUpdate info( (sat::Solvable(i)) );
	  invokeOnEach( _pool.byIdentBegin( poolItem ),
			_pool.byIdentEnd( poolItem ),
			resfilter::ByUninstalled(),			// ByUninstalled
			functor::functorRef<bool,PoolItem> (info) );

	  if (info.is_updated) {
	      SATSolutionToPool (poolItem, ResStatus::toBeUninstalledDueToUpgrade , ResStatus::SOLVER);
	  } else {
	      SATSolutionToPool (poolItem, ResStatus::toBeUninstalled, ResStatus::SOLVER);
	  }
      } else {
	  ERR << "id " << i << " not found in ZYPP pool." << endl;
      }
    }
    MIL << "SATResolver::doUpdate() done" << endl;
}



//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// error handling
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// helper function
//----------------------------------------------------------------------------

struct FindPackage : public resfilter::ResObjectFilterFunctor
{
    ProblemSolutionCombi *problemSolution;
    TransactionKind action;
    FindPackage (ProblemSolutionCombi *p, const TransactionKind act)
       : problemSolution (p)
       , action (act)
	{
	}

    bool operator()( PoolItem p)
   {
       problemSolution->addSingleAction (p, action);
       return true;
   }
};


//----------------------------------------------------------------------------
// Checking if this solvable/item has a buddy which reflect the real
// user visible description of an item
// e.g. The release package has a buddy to the concerning product item.
// This user want's the message "Product foo conflicts with product bar" and
// NOT "package release-foo conflicts with package release-bar"
//----------------------------------------------------------------------------


PoolItem SATResolver::mapItem (const PoolItem &item)
{
    sat::Solvable buddy = item.buddy();
    if (buddy != sat::Solvable())
    {
	return _pool.find (buddy);
    }
    else
    {
	return item;
    }
}

sat::Solvable SATResolver::mapSolvable (const Id &id)
{
    PoolItem item = _pool.find (sat::Solvable(id));
    return mapItem(item).satSolvable();
}

string SATResolver::SATprobleminfoString(Id problem, string &detail, Id &ignoreId)
{
  string ret;
  Pool *pool = _solv->pool;
  Id probr;
  Id dep, source, target;
  sat::Solvable s, s2;

  ignoreId = 0;
  probr = solver_findproblemrule(_solv, problem);
  switch (solver_ruleinfo(_solv, probr, &source, &target, &dep))
  {
      case SOLVER_RULE_DISTUPGRADE:
	  s = mapSolvable (source);
	  ret = str::form (_("%s does not belong to a distupgrade repository"), s.asString().c_str());
  	  break;
      case SOLVER_RULE_INFARCH:
	  s = mapSolvable (source);
	  ret = str::form (_("%s has inferior architecture"), s.asString().c_str());
	  break;
      case SOLVER_RULE_UPDATE:
	  s = mapSolvable (source);
	  ret = str::form (_("problem with installed package %s"), s.asString().c_str());
	  break;
      case SOLVER_RULE_JOB:
	  ret = _("conflicting requests");
	  break;
      case SOLVER_RULE_RPM:
	  ret = _("some dependency problem");
	  break;
      case SOLVER_RULE_JOB_NOTHING_PROVIDES_DEP:
	  ret = str::form (_("nothing provides requested %s"), dep2str(pool, dep));
	  detail += _("Have you enabled all requested repositories?");
	  break;
      case SOLVER_RULE_RPM_NOT_INSTALLABLE:
	  s = mapSolvable (source);
	  ret = str::form (_("%s is not installable"), s.asString().c_str());
	  break;
      case SOLVER_RULE_RPM_NOTHING_PROVIDES_DEP:
	  ignoreId = source; // for setting weak dependencies
	  s = mapSolvable (source);
	  ret = str::form (_("nothing provides %s needed by %s"), dep2str(pool, dep), s.asString().c_str());
	  break;
      case SOLVER_RULE_RPM_SAME_NAME:
	  s = mapSolvable (source);
	  s2 = mapSolvable (target);
	  ret = str::form (_("cannot install both %s and %s"), s.asString().c_str(), s2.asString().c_str());
	  break;
      case SOLVER_RULE_RPM_PACKAGE_CONFLICT:
	  s = mapSolvable (source);
	  s2 = mapSolvable (target);
	  ret = str::form (_("%s conflicts with %s provided by %s"), s.asString().c_str(), dep2str(pool, dep), s2.asString().c_str());
	  break;
      case SOLVER_RULE_RPM_PACKAGE_OBSOLETES:
	  s = mapSolvable (source);
	  s2 = mapSolvable (target);
	  ret = str::form (_("%s obsoletes %s provided by %s"), s.asString().c_str(), dep2str(pool, dep), s2.asString().c_str());
	  break;
      case SOLVER_RULE_RPM_INSTALLEDPKG_OBSOLETES:
	  s = mapSolvable (source);
	  s2 = mapSolvable (target);
	  ret = str::form (_("installed %s obsoletes %s provided by %s"), s.asString().c_str(), dep2str(pool, dep), s2.asString().c_str());
	  break;
      case SOLVER_RULE_RPM_SELF_CONFLICT:
	  s = mapSolvable (source);
	  ret = str::form (_("solvable %s conflicts with %s provided by itself"), s.asString().c_str(), dep2str(pool, dep));
          break;
      case SOLVER_RULE_RPM_PACKAGE_REQUIRES:
	  ignoreId = source; // for setting weak dependencies
	  s = mapSolvable (source);
	  Capability cap(dep);
	  sat::WhatProvides possibleProviders(cap);

	  // check, if a provider will be deleted
	  typedef list<PoolItem> ProviderList;
	  ProviderList providerlistInstalled, providerlistUninstalled;
	  for_( iter1, possibleProviders.begin(), possibleProviders.end() ) {
	      PoolItem provider1 = ResPool::instance().find( *iter1 );
	      // find pair of an installed/uninstalled item with the same NVR
	      bool found = false;
	      for_( iter2, possibleProviders.begin(), possibleProviders.end() ) {
		  PoolItem provider2 = ResPool::instance().find( *iter2 );
		  if (compareByNVR (provider1.resolvable(),provider2.resolvable()) == 0
		      && ( (provider1.status().isInstalled() && provider2.status().isUninstalled())
			  || (provider2.status().isInstalled() && provider1.status().isUninstalled()) ))  {
		      found = true;
		      break;
		  }
	      }
	      if (!found) {
		  if (provider1.status().isInstalled())
		      providerlistInstalled.push_back(provider1);
		  else
		      providerlistUninstalled.push_back(provider1);
	      }
	  }

	  ret = str::form (_("%s requires %s, but this requirement cannot be provided"), s.asString().c_str(), dep2str(pool, dep));
	  if (providerlistInstalled.size() > 0) {
	      detail += _("deleted providers: ");
	      for (ProviderList::const_iterator iter = providerlistInstalled.begin(); iter != providerlistInstalled.end(); iter++) {
		  if (iter == providerlistInstalled.begin())
		      detail += itemToString( *iter );
		  else
		      detail += "\n                   " + itemToString( mapItem(*iter) );
	      }
	  }
	  if (providerlistUninstalled.size() > 0) {
	      if (detail.size() > 0)
		  detail += _("\nuninstallable providers: ");
	      else
		  detail = _("uninstallable providers: ");
	      for (ProviderList::const_iterator iter = providerlistUninstalled.begin(); iter != providerlistUninstalled.end(); iter++) {
		  if (iter == providerlistUninstalled.begin())
		      detail += itemToString( *iter );
		  else
		      detail += "\n                   " + itemToString( mapItem(*iter) );
	      }
	  }
	  break;
  }

  return ret;
}

ResolverProblemList
SATResolver::problems ()
{
    ResolverProblemList resolverProblems;
    if (_solv && _solv->problems.count) {
	Pool *pool = _solv->pool;
	int pcnt;
	Id p, rp, what;
	Id problem, solution, element;
	sat::Solvable s, sd;

	CapabilitySet system_requires = SystemCheck::instance().requiredSystemCap();
	CapabilitySet system_conflicts = SystemCheck::instance().conflictSystemCap();

	MIL << "Encountered problems! Here are the solutions:\n" << endl;
	pcnt = 1;
	problem = 0;
	while ((problem = solver_next_problem(_solv, problem)) != 0) {
	    MIL << "Problem " <<  pcnt++ << ":" << endl;
	    MIL << "====================================" << endl;
	    string detail;
	    Id ignoreId;
	    string whatString = SATprobleminfoString (problem,detail,ignoreId);
	    MIL << whatString << endl;
	    MIL << "------------------------------------" << endl;
	    ResolverProblem_Ptr resolverProblem = new ResolverProblem (whatString, detail);

	    solution = 0;
	    while ((solution = solver_next_solution(_solv, problem, solution)) != 0) {
		element = 0;
		ProblemSolutionCombi *problemSolution = new ProblemSolutionCombi(resolverProblem);
		while ((element = solver_next_solutionelement(_solv, problem, solution, element, &p, &rp)) != 0) {
		    if (p == SOLVER_SOLUTION_JOB) {
			/* job, rp is index into job queue */
			what = _jobQueue.elements[rp];
			switch (_jobQueue.elements[rp-1]&(SOLVER_SELECTMASK|SOLVER_JOBMASK))
			{
			    case SOLVER_INSTALL | SOLVER_SOLVABLE: {
				s = mapSolvable (what);
				PoolItem poolItem = _pool.find (s);
				if (poolItem) {
				    if (_solv->installed && s.get()->repo == _solv->installed) {
					problemSolution->addSingleAction (poolItem, REMOVE);
					string description = str::form (_("do not keep %s installed"),  s.asString().c_str() );
					MIL << description << endl;
					problemSolution->addDescription (description);
				    } else {
					problemSolution->addSingleAction (poolItem, KEEP);
					string description = str::form (_("do not install %s"), s.asString().c_str());
					MIL << description << endl;
					problemSolution->addDescription (description);
				    }
				} else {
				    ERR << "SOLVER_INSTALL_SOLVABLE: No item found for " << s.asString() << endl;
				}
			    }
				break;
			    case SOLVER_ERASE | SOLVER_SOLVABLE: {
				s = mapSolvable (what);
				PoolItem poolItem = _pool.find (s);
				if (poolItem) {
				    if (_solv->installed && s.get()->repo == _solv->installed) {
					problemSolution->addSingleAction (poolItem, KEEP);
					string description = str::form (_("keep %s"), s.asString().c_str());
					MIL << description << endl;
					problemSolution->addDescription (description);
				    } else {
					problemSolution->addSingleAction (poolItem, UNLOCK);
					string description = str::form (_("do not forbid installation of %s"), itemToString( poolItem ).c_str());
					MIL << description << endl;
					problemSolution->addDescription (description);
				    }
				} else {
				    ERR << "SOLVER_ERASE_SOLVABLE: No item found for " << s.asString() << endl;
				}
			    }
				break;
			    case SOLVER_INSTALL | SOLVER_SOLVABLE_NAME:
				{
				IdString ident( what );
				SolverQueueItemInstall_Ptr install =
				    new SolverQueueItemInstall(_pool, ident.asString(), false );
				problemSolution->addSingleAction (install, REMOVE_SOLVE_QUEUE_ITEM);

				string description = str::form (_("do not install %s"), ident.c_str() );
				MIL << description << endl;
				problemSolution->addDescription (description);
				}
				break;
			    case SOLVER_ERASE | SOLVER_SOLVABLE_NAME:
				{
				// As we do not know, if this request has come from resolvePool or
				// resolveQueue we will have to take care for both cases.
                                IdString ident( what );
				FindPackage info (problemSolution, KEEP);
				invokeOnEach( _pool.byIdentBegin( ident ),
					      _pool.byIdentEnd( ident ),
					      functor::chain (resfilter::ByInstalled (),			// ByInstalled
							      resfilter::ByTransact ()),			// will be deinstalled
					      functor::functorRef<bool,PoolItem> (info) );

				SolverQueueItemDelete_Ptr del =
				    new SolverQueueItemDelete(_pool, ident.asString(), false );
				problemSolution->addSingleAction (del, REMOVE_SOLVE_QUEUE_ITEM);

				string description = str::form (_("keep %s"), ident.c_str());
				MIL << description << endl;
				problemSolution->addDescription (description);
				}
				break;
			    case SOLVER_INSTALL | SOLVER_SOLVABLE_PROVIDES:
				{
				problemSolution->addSingleAction (Capability(what), REMOVE_EXTRA_REQUIRE);
				string description = "";

				// Checking if this problem solution would break your system
				if (system_requires.find(Capability(what)) != system_requires.end()) {
				    // Show a better warning
				    resolverProblem->setDetails( resolverProblem->description() + "\n" + resolverProblem->details() );
				    resolverProblem->setDescription(_("This request will break your system!"));
				    description = _("ignore the warning of a broken system");
                                    description += string(" (requires:")+dep2str(pool, what)+")";
                                    MIL << description << endl;
                                    problemSolution->addFrontDescription (description);
				} else {
				    description = str::form (_("do not ask to install a solvable providing %s"), dep2str(pool, what));
                                    MIL << description << endl;
                                    problemSolution->addDescription (description);
				}
				}
				break;
			    case SOLVER_ERASE | SOLVER_SOLVABLE_PROVIDES:
				{
				problemSolution->addSingleAction (Capability(what), REMOVE_EXTRA_CONFLICT);
				string description = "";

				// Checking if this problem solution would break your system
				if (system_conflicts.find(Capability(what)) != system_conflicts.end()) {
				    // Show a better warning
				    resolverProblem->setDetails( resolverProblem->description() + "\n" + resolverProblem->details() );
				    resolverProblem->setDescription(_("This request will break your system!"));
				    description = _("ignore the warning of a broken system");
                                    description += string(" (conflicts:")+dep2str(pool, what)+")";
                                    MIL << description << endl;
                                    problemSolution->addFrontDescription (description);

				} else {
				    description = str::form (_("do not ask to delete all solvables providing %s"), dep2str(pool, what));
                                    MIL << description << endl;
                                    problemSolution->addDescription (description);
				}
				}
				break;
			    case SOLVER_UPDATE | SOLVER_SOLVABLE:
				{
				s = mapSolvable (what);
				PoolItem poolItem = _pool.find (s);
				if (poolItem) {
				    if (_solv->installed && s.get()->repo == _solv->installed) {
					problemSolution->addSingleAction (poolItem, KEEP);
					string description = str::form (_("do not install most recent version of %s"), s.asString().c_str());
					MIL << description << endl;
					problemSolution->addDescription (description);
				    } else {
					ERR << "SOLVER_INSTALL_SOLVABLE_UPDATE " << poolItem << " is not selected for installation" << endl;
				    }
				} else {
				    ERR << "SOLVER_INSTALL_SOLVABLE_UPDATE: No item found for " << s.asString() << endl;
				}
				}
				break;
			    default:
				MIL << "- do something different" << endl;
				ERR << "No valid solution available" << endl;
				break;
			}
		    } else if (p == SOLVER_SOLUTION_INFARCH) {
			s = mapSolvable (rp);
			PoolItem poolItem = _pool.find (s);
			if (_solv->installed && s.get()->repo == _solv->installed) {
			    problemSolution->addSingleAction (poolItem, LOCK);
			    string description = str::form (_("keep %s despite the inferior architecture"), s.asString().c_str());
			    MIL << description << endl;
			    problemSolution->addDescription (description);
			} else {
			    problemSolution->addSingleAction (poolItem, INSTALL);
			    string description = str::form (_("install %s despite the inferior architecture"), s.asString().c_str());
			    MIL << description << endl;
			    problemSolution->addDescription (description);
			}
		    } else if (p == SOLVER_SOLUTION_DISTUPGRADE) {
			s = mapSolvable (rp);
			PoolItem poolItem = _pool.find (s);
			if (_solv->installed && s.get()->repo == _solv->installed) {
			    problemSolution->addSingleAction (poolItem, LOCK);
			    string description = str::form (_("keep obsolete %s"), s.asString().c_str());
			    MIL << description << endl;
			    problemSolution->addDescription (description);
			} else {
			    problemSolution->addSingleAction (poolItem, INSTALL);
			    string description = str::form (_("install %s from excluded repository"), s.asString().c_str());
			    MIL << description << endl;
			    problemSolution->addDescription (description);
			}
		    } else {
			/* policy, replace p with rp */
			s = mapSolvable (p);
			if (rp)
			    sd = mapSolvable (rp);

			PoolItem itemFrom = _pool.find (s);
			if (s == sd && _solv->distupgrade)
			{
			    PoolItem poolItem = _pool.find (s);
			    if (poolItem) {
				problemSolution->addSingleAction (poolItem, LOCK); // for solver reason: NOT weak lock.
				string description = str::form (_("keep obsolete %s"), s.asString().c_str());
				MIL << description << endl;
				problemSolution->addDescription (description);
			    } else {
				ERR << "SOLVER_INSTALL_SOLVABLE: No item found for " << s.asString() << endl;
			    }
			}
			else if (rp)
			{
			    int gotone = 0;

			    PoolItem itemTo = _pool.find (sd);
			    if (itemFrom && itemTo) {
				problemSolution->addSingleAction (itemTo, INSTALL);

				if (evrcmp(pool, s.get()->evr, sd.get()->evr, EVRCMP_COMPARE ) > 0)
				{
				    string description = str::form (_("downgrade of %s to %s"), s.asString().c_str(), sd.asString().c_str());
				    MIL << description << endl;
				    problemSolution->addDescription (description);
				    gotone = 1;
				}
				if (!_solv->allowarchchange && s.get()->name == sd.get()->name && s.get()->arch != sd.get()->arch
				    && policy_illegal_archchange(_solv, s.get(), sd.get()))
				{
				    string description = str::form (_("architecture change of %s to %s"), s.asString().c_str(), sd.asString().c_str());
				    MIL << description << endl;
				    problemSolution->addDescription (description);
				    gotone = 1;
				}
				if (!_solv->allowvendorchange && s.get()->name == sd.get()->name && s.get()->vendor != sd.get()->vendor
				    && policy_illegal_vendorchange(_solv, s.get(), sd.get()))
				{
                                    IdString s_vendor( s.vendor() );
                                    IdString sd_vendor( sd.vendor() );
				    string description = str::form (_("install %s (with vendor change)\n  %s  -->  %s") ,
								    sd.asString().c_str(),
                                                                    ( s_vendor ? s_vendor.c_str() : " (no vendor) " ),
                                                                    ( sd_vendor ? sd_vendor.c_str() : " (no vendor) " ) );
				    MIL << description << endl;
				    problemSolution->addDescription (description);
				    gotone = 1;
				}
				if (!gotone) {
				    string description = str::form (_("replacement of %s with %s"), s.asString().c_str(), sd.asString().c_str());
				    MIL << description << endl;
				    problemSolution->addDescription (description);
				}
			    } else {
				ERR << s.asString() << " or "  << sd.asString() << " not found" << endl;
			    }
			}
			else
			{
			    if (itemFrom) {
				string description = str::form (_("deinstallation of %s"), s.asString().c_str());
				MIL << description << endl;
				problemSolution->addDescription (description);
				problemSolution->addSingleAction (itemFrom, REMOVE);
			    }
			}
		    }
		}
		resolverProblem->addSolution (problemSolution,
					      problemSolution->actionCount() > 1 ? true : false); // Solutions with more than 1 action will be shown first.
		MIL << "------------------------------------" << endl;
	    }

	    if (ignoreId > 0) {
		// There is a possibility to ignore this error by setting weak dependencies
		PoolItem item = _pool.find (sat::Solvable(ignoreId));
		ProblemSolutionIgnore *problemSolution = new ProblemSolutionIgnore(resolverProblem, item);
		resolverProblem->addSolution (problemSolution,
					      false); // Solutions will be shown at the end
		MIL << "ignore some dependencies of " << item << endl;
		MIL << "------------------------------------" << endl;
	    }

	    // save problem
	    resolverProblems.push_back (resolverProblem);
	}
    }
    return resolverProblems;
}

void
SATResolver::applySolutions (const ProblemSolutionList & solutions)
{
    for (ProblemSolutionList::const_iterator iter = solutions.begin();
	 iter != solutions.end(); ++iter) {
	ProblemSolution_Ptr solution = *iter;
	Resolver dummyResolver(_pool);
	if (!solution->apply (dummyResolver))
	    break;
    }
}

void SATResolver::setLocks()
{
    for (PoolItemList::const_iterator iter = _items_to_lock.begin(); iter != _items_to_lock.end(); iter++) {
        sat::detail::SolvableIdType ident( (*iter)->satSolvable().id() );
	if (iter->status().isInstalled()) {
	    MIL << "Lock installed item " << *iter << endl;
	    queue_push( &(_jobQueue), SOLVER_INSTALL_SOLVABLE );
	    queue_push( &(_jobQueue), ident );
	} else {
	    MIL << "Lock NOT installed item " << *iter << endl;
	    queue_push( &(_jobQueue), SOLVER_ERASE_SOLVABLE | MAYBE_CLEANDEPS );
	    queue_push( &(_jobQueue), ident );
	}
    }

    for (PoolItemList::const_iterator iter = _items_to_keep.begin(); iter != _items_to_keep.end(); iter++) {
        sat::detail::SolvableIdType ident( (*iter)->satSolvable().id() );
	if (iter->status().isInstalled()) {
	    MIL << "Keep installed item " << *iter << endl;
	    queue_push( &(_jobQueue), SOLVER_INSTALL_SOLVABLE | SOLVER_WEAK);
	    queue_push( &(_jobQueue), ident );
	} else {
	    MIL << "Keep NOT installed item " << *iter << ident << endl;
	    queue_push( &(_jobQueue), SOLVER_ERASE_SOLVABLE | SOLVER_WEAK | MAYBE_CLEANDEPS );
	    queue_push( &(_jobQueue), ident );
	}
    }
}

void SATResolver::setSystemRequirements()
{
    CapabilitySet system_requires = SystemCheck::instance().requiredSystemCap();
    CapabilitySet system_conflicts = SystemCheck::instance().conflictSystemCap();

    for (CapabilitySet::const_iterator iter = system_requires.begin(); iter != system_requires.end(); iter++) {
	queue_push( &(_jobQueue), SOLVER_INSTALL | SOLVER_SOLVABLE_PROVIDES );
	queue_push( &(_jobQueue), iter->id() );
	MIL << "SYSTEM Requires " << *iter << endl;
    }

    for (CapabilitySet::const_iterator iter = system_conflicts.begin(); iter != system_conflicts.end(); iter++) {
	queue_push( &(_jobQueue), SOLVER_ERASE | SOLVER_SOLVABLE_PROVIDES | MAYBE_CLEANDEPS );
	queue_push( &(_jobQueue), iter->id() );
	MIL << "SYSTEM Conflicts " << *iter << endl;
    }

    // Lock the architecture of the running systems rpm
    // package on distupgrade.
    if ( _distupgrade && ZConfig::instance().systemRoot() == "/" )
    {
      ResPool pool( ResPool::instance() );
      IdString rpm( "rpm" );
      for_( it, pool.byIdentBegin(rpm), pool.byIdentEnd(rpm) )
      {
        if ( (*it)->isSystem() )
        {
          Capability archrule( (*it)->arch(), rpm.c_str(), Capability::PARSED );
          queue_push( &(_jobQueue), SOLVER_INSTALL | SOLVABLE_NAME | SOLVER_ESSENTIAL );
          queue_push( &(_jobQueue), archrule.id() );

        }
      }
    }
}


///////////////////////////////////////////////////////////////////
};// namespace detail
    /////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////
  };// namespace solver
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
};// namespace zypp
/////////////////////////////////////////////////////////////////////////

