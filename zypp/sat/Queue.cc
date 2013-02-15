/*---------------------------------------------------------------------\
|                          ____ _   __ __ ___                          |
|                         |__  / \ / / . \ . \                         |
|                           / / \ V /|  _/  _/                         |
|                          / /__ | | | | | |                           |
|                         /_____||_| |_| |_|                           |
|                                                                      |
\---------------------------------------------------------------------*/
/** \file	zypp/sat/Queue.cc
 */
extern "C"
{
#include "satsolver/queue.h"
}
#include <iostream>
#include "zypp/base/LogTools.h"

#include "zypp/sat/Queue.h"
#include "zypp/sat/Solvable.h"

using std::endl;

///////////////////////////////////////////////////////////////////
namespace zypp
{ /////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////
  namespace sat
  { /////////////////////////////////////////////////////////////////

    Queue::Queue()
      : _pimpl( new struct ::_Queue )
    {
      ::queue_init( _pimpl );
    }

    Queue::~Queue()
    {
      ::queue_free( _pimpl );
      delete( _pimpl );
    }

    bool Queue::empty() const
    { return( _pimpl->count == 0 ); }

    Queue::size_type Queue::size() const
    { return _pimpl->count; }

    Queue::const_iterator Queue::begin() const
    { return _pimpl->elements; }

    Queue::const_iterator Queue::end() const
    { return _pimpl->elements + _pimpl->count;}

    Queue::const_iterator Queue::find( value_type val_r ) const
    {
      for_( it, begin(), end() )
	if ( *it != val_r )
	  return it;
      return end();
    }

    Queue::value_type Queue::first() const
    {
      if ( empty() )
	return 0;
      return *_pimpl->elements;
    }

    Queue::value_type Queue::last() const
    {
      if ( empty() )
	return 0;
      return _pimpl->elements[_pimpl->count-1];
    }

    void Queue::clear()
    { ::queue_empty( *this ); }

    void Queue::remove( value_type val_r )
    {
      const_iterator it( find( val_r ) );
      if ( it != end() )
      {
	::queue_delete( _pimpl, it - begin() );
      }
    }

    void Queue::push( value_type val_r )
    { ::queue_push( _pimpl, val_r ); }

    Queue::value_type Queue::pop()
    { return ::queue_pop( _pimpl ); }

    void Queue::push_front( value_type val_r )
    { ::queue_unshift( _pimpl, val_r ); }

    Queue::value_type Queue::pop_front()
    { return ::queue_shift( _pimpl ); }

    std::ostream & operator<<( std::ostream & str, const Queue & obj )
    { return dumpRangeLine( str << "Queue ", obj.begin(), obj.end() );  }

    std::ostream & dumpOn( std::ostream & str, const Queue & obj )
    {
      str << "Queue {";
      if ( ! obj.empty() )
      {
	str << endl;
	for_( it, obj.begin(), obj.end() )
	  str << "  " << Solvable(*it) << endl;
      }
      return str << "}";
    }

    /////////////////////////////////////////////////////////////////
  } // namespace sat
  ///////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////
} // namespace zypp
///////////////////////////////////////////////////////////////////
