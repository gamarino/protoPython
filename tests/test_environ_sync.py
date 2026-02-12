print("--- test_environ_sync START ---")
print("import os...")
import os
print("import os... DONE")
import sys

def test_sync():
    print("Testing os.environ sync with native getenv...")
    
    # Set in os.environ
    os.environ['PROTO_SYNC_TEST'] = 'sync_val_1'
    print("Added to os.environ: PROTO_SYNC_TEST=sync_val_1")
    
    # Check if native getenv sees it
    try:
        import _os
        native_val = _os.getenv('PROTO_SYNC_TEST')
        print("Native _os.getenv('PROTO_SYNC_TEST') result: %s" % native_val)
        if native_val == 'sync_val_1':
            print("SUCCESS: Native getenv reflects os.environ change.")
        else:
            print("FAILURE: Native getenv does NOT reflect os.environ change.")
    except ImportError:
        print("SKIP: _os module not available for native check.")

    # Remove from os.environ
    del os.environ['PROTO_SYNC_TEST']
    print("Deleted from os.environ: PROTO_SYNC_TEST")
    
    try:
        native_val = _os.getenv('PROTO_SYNC_TEST')
        print("Native _os.getenv('PROTO_SYNC_TEST') after del: %s" % native_val)
        if native_val is None:
            print("SUCCESS: Native getenv reflects deletion.")
        else:
            print("FAILURE: Native getenv still sees deleted key.")
    except NameError: # _os might not be defined if import failed above
        pass

if __name__ == "__main__":
    test_sync()
