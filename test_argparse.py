import argparse

try:
    print("Creating ArgumentParser...")
    parser = argparse.ArgumentParser(description='Test Argument Parser')
    print("ArgumentParser created successfully:", parser)
    
    print("Adding argument...")
    parser.add_argument('--foo', help='foo help')
    print("Argument added successfully.")
    
    print("Parsing args...")
    args = parser.parse_args(['--foo', 'BAR'])
    print("Parsed args:", args)
except Exception as e:
    import traceback
    traceback.print_exc()
