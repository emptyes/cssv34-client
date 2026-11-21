#include "tier1/utlpriorityqueue.h"
#include <stdio.h>

#ifndef UTLPRIORITYQUEUE_STANDALONE_TEST
#include "unitlib/unitlib.h"
DEFINE_TESTSUITE( UtlPriorityQueueTestSuite )
#endif

static bool LessPriority( const int &a, const int &b )
{
	return a < b;
}

static bool GreaterPriority( const int &a, const int &b )
{
	return a > b;
}

static bool CheckRemoval( const int *values, int count, int removeIndex,
	const int *expected, bool reverse = false )
{
	CUtlPriorityQueue<int> queue( 0, 0, reverse ? GreaterPriority : LessPriority );
	for ( int i = 0; i < count; ++i )
		queue.Insert( reverse ? -values[i] : values[i] );

	queue.RemoveAt( removeIndex );
	if ( queue.Count() != count - 1 )
		return false;

	for ( int i = 0; i < count - 1; ++i )
	{
		int wanted = reverse ? -expected[i] : expected[i];
		if ( queue.ElementAtHead() != wanted )
		{
			fprintf( stderr, "RemoveAt(%d), output %d: expected %d, got %d\n",
				removeIndex, i, wanted, queue.ElementAtHead() );
			return false;
		}
		queue.RemoveAtHead();
	}
	return queue.Count() == 0;
}

static bool RemoveAtTests()
{
	const int values[] = { 100, 50, 90, 40, 45, 80, 85 };
	const int upward[] = { 100, 90, 85, 80, 50, 45 };
	const int root[] = { 90, 85, 80, 50, 45, 40 };
	const int tail[] = { 100, 90, 80, 50, 45, 40 };
	const int downwardValues[] = { 100, 90, 80, 70, 60, 50, 40, 30, 20, 10 };
	const int downward[] = { 100, 80, 70, 60, 50, 40, 30, 20, 10 };
	const int single[] = { 100 };

	return CheckRemoval( values, 7, 3, upward ) &&
		CheckRemoval( values, 7, 3, upward, true ) &&
		CheckRemoval( values, 7, 0, root ) &&
		CheckRemoval( values, 7, 6, tail ) &&
		CheckRemoval( downwardValues, 10, 1, downward ) &&
		CheckRemoval( single, 1, 0, NULL );
}

#ifdef UTLPRIORITYQUEUE_STANDALONE_TEST
int main()
{
	return RemoveAtTests() ? 0 : 1;
}
#else
DEFINE_TESTCASE( RemoveAtTest, UtlPriorityQueueTestSuite )
{
	Shipping_Assert( RemoveAtTests() );
}
#endif
