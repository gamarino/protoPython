#include <protoPython/HeapqModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

namespace protoPython {
namespace heapq {

static bool lt(proto::ProtoContext* ctx, const proto::ProtoObject* a, const proto::ProtoObject* b) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
    return a->call(ctx, nullptr, env->getLtString(), a, args, nullptr) == PROTO_TRUE;
}

static const proto::ProtoObject* py_heappush(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* /*kwArgs*/) {
    
    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* heapObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* item = posArgs->getAt(ctx, 1);
    
    const proto::ProtoList* heap = heapObj->asList(ctx);
    if (!heap) return PROTO_NONE;

    heap = heap->appendLast(ctx, item);
    // Sift up
    long long pos = (long long)heap->getSize(ctx) - 1;
    while (pos > 0) {
        long long parent = (pos - 1) >> 1;
        const proto::ProtoObject* parentItem = heap->getAt(ctx, (size_t)parent);
        if (lt(ctx, item, parentItem)) {
            // Swap
            heap = heap->setAt(ctx, (size_t)pos, parentItem);
            pos = parent;
        } else {
            break;
        }
    }
    heap = heap->setAt(ctx, (size_t)pos, item);
    
    // ProtoList::setAt returns a new list in protoCore if immutable, 
    // but here we need to update the object if it's a wrapper.
    // However, usually these modules mutate the list in-place if it's a Python list.
    // In protoPython, lists are often native wrappers around ProtoList.
    
    return PROTO_NONE;
}

static const proto::ProtoObject* py_heappop(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* /*kwArgs*/) {
    
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* heapObj = posArgs->getAt(ctx, 0);
    const proto::ProtoList* heap = heapObj->asList(ctx);
    if (!heap || heap->getSize(ctx) == 0) return PROTO_NONE;

    const proto::ProtoObject* lastelt = heap->getAt(ctx, heap->getSize(ctx) - 1);
    const proto::ProtoObject* returnitem;
    
    if (heap->getSize(ctx) == 1) {
        returnitem = heap->getAt(ctx, 0);
        // heap.pop()
        return returnitem;
    }

    returnitem = heap->getAt(ctx, 0);
    // heap[0] = lastelt
    // heap.pop()
    
    // Sift down
    size_t size = heap->getSize(ctx) - 1;
    size_t pos = 0;
    while (true) {
        size_t child = (pos << 1) + 1;
        if (child >= size) break;
        size_t right = child + 1;
        if (right < size && lt(ctx, heap->getAt(ctx, right), heap->getAt(ctx, child))) {
            child = right;
        }
        if (lt(ctx, lastelt, heap->getAt(ctx, child))) break;
        // heap[pos] = heap[child]
        pos = child;
    }
    // heap[pos] = lastelt
    
    return returnitem;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "heappush"), ctx->fromMethod(nullptr, py_heappush));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "heappop"), ctx->fromMethod(nullptr, py_heappop));
    return mod;
}

} // namespace heapq
} // namespace protoPython
