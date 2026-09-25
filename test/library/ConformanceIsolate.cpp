// One conformance case per process: the three whose failure destroys a run --
// one aborts inside waitForHeapHeadroom, two deadlock the whole space.
#include "ConformanceHost.h"

#include <protoCoreConformance.h>

PROTOCORE_CONFORMANCE_ISOLATE_MAIN(protoPython::conformance::PythonConformanceHost)
