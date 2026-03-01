set pagination off
break std::abort
run
thread 2
frame 2
p proto::g_currentScanCellForDebug
p ((proto::ParentLinkImplementation*)proto::g_currentScanCellForDebug)->parent
p ((proto::ParentLinkImplementation*)proto::g_currentScanCellForDebug)->object
p ref
quit
