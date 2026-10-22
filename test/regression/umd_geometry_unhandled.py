# An error of a C++ UMD module that no Python handler catches reaches the top
# level, which reports it by the name and the message the value carries
# ("NegativeRadius: circleArea: the radius -3.000000 is negative") and exits
# with status 1.  Run by protopy_umd_host (test/umd/).
import example.geometry as geometry


def outer():
    return geometry.circleArea(-3)


outer()
print("FAIL: the error did not reach the top level")
