
# to find the KeyRingTest receiver
INCLUDE_DIRECTORIES( ${LIBZYPP_SOURCE_DIR}/tests/zypp )

ADD_TESTS(Solvable SolvParsing WhatProvides WhatObsoletes LookupAttr AttrMatcher IdString)


