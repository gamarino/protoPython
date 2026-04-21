import os
import dis

def dis_repr():
    print("Disassembling os._Environ.__repr__:")
    try:
        # We need to reach os._Environ
        # In this environment, we might need to import it
        import os
        dis.dis(os._Environ.__repr__)
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    dis_repr()
